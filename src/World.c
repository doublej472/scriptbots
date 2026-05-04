
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "World.h"
#include "helpers.h"
#include "queue.h"
#include "vkhelpers.h"
#include "settings.h"
#include "vec.h"
#include "vec2f.h"
#include "Food.h"

#define BATCH_SIZE 64

// ============================================================================
// Food & GUI
// ============================================================================

static void world_update_food(struct World *world) {
  // if (world->modcounter % 20 == 0) {
  //   printf("food_pivot: %d\n", world->foodGrid.food_pivot);
  // }
  
  float food_to_add = FOODMAX * 0.5f;

  if (world->foodGrid.food_pivot == 0) {
    // World is empty, add random food
    while (food_to_add > 0.0f) {
      uint32_t food_idx = randi(0, TOTAL_FOOD_SQUARES);
      size_t fx = food_idx % FOOD_SQUARES_WIDTH;
      size_t fy = food_idx / FOOD_SQUARES_WIDTH;
      food_to_add -= foodGrid_growFood(&world->foodGrid, fx, fy, FOODMAX * 0.02f);
    }
    return;
  }

  while (food_to_add > 0.0f) {
    uint32_t food_grid_idx = randi(0, world->foodGrid.food_pivot);
    uint32_t food_idx = world->foodGrid.food_sorted[food_grid_idx];
    size_t fx = food_idx % FOOD_SQUARES_WIDTH;
    size_t fy = food_idx / FOOD_SQUARES_WIDTH;
    // Grow current square
    food_to_add -= foodGrid_growFood(&world->foodGrid, fx, fy, fminf(FOODGROWTH,food_to_add));
    // Grow surrounding squares only if well grown
    if (world->foodGrid.food[fy][fx].amt > FOODMAX * 0.7f) {
      // Spread to random square nearby
      size_t fxx = randi(fx - 1, fx + 2);
      size_t fyy = randi(fy - 1, fy + 2);

      food_to_add -= foodGrid_growFood(&world->foodGrid, fxx, fyy, fminf(FOODGROWTH,food_to_add));
    }
  }
}

// ============================================================================
// Spatial Grid & Agent Lifecycle
// ============================================================================

struct BucketList {
  size_t buckets[9];
};

static size_t get_bucket_from_pos(int64_t x, int64_t y) {
  // ~48 bit primes
  const uint64_t PRIME_1 = 214058479909259;
  const uint64_t PRIME_2 = 242433689293403;

  // Reinterpret the value as uint64_t
  uint64_t this_grid_xu = *(uint64_t *)&x;
  uint64_t this_grid_yu = *(uint64_t *)&y;

  uint64_t hash = (this_grid_xu * PRIME_1) + (this_grid_yu * PRIME_2);
  size_t agent_bucket = hash % AGENT_BUCKETS;

  return agent_bucket;
}

static struct BucketList get_buckets_from_pos(float x, float y) {
  // Integer truncation means this cuts off at the whole number boundary
  int64_t this_grid_x = (int64_t)(x / DIST);
  int64_t this_grid_y = (int64_t)(y / DIST);

  size_t bucket_idx = 0;
  struct BucketList blist;
  // Check a 3x3 grid around our position
  for (int64_t xoff = -1; xoff <= 1; xoff++) {
    for (int64_t yoff = -1; yoff <= 1; yoff++) {
      blist.buckets[bucket_idx] = get_bucket_from_pos(this_grid_x + xoff, this_grid_y + yoff);
      bucket_idx++;
    }
  }

  return blist;
}

struct AgentRange {
  size_t start;
  size_t end;
};

static struct AgentRange get_agent_range(struct World *world, size_t bucket_idx) {
  struct AgentRange ret;

  if (bucket_idx == 0) {
    ret.start = 0;
  } else {
    ret.start = world->agent_grid[bucket_idx - 1];
  }

  ret.end = world->agent_grid[bucket_idx];

  return ret;
}

void world_flush_staging(struct World *world) {
  VKState *vk_st = world->brain_gpu;

  // Delete dead agents with global brain-slot compaction.
  // When a dead agent's brain slot is freed, the globally last alive
  // agent's brain is moved into it so only the highest-index chunk
  // may have unused slots.
  for (size_t i = 0; i < world->agents.size; i++) {
    struct Agent *a = world->agents.agents[i];
    if (a->health <= 0) {
      if (vk_st && a->brain_chunk != ~0u) {
        // Find globally last alive agent
        uint32_t last_c = vk_st->chunk_count;
        while (last_c > 0) {
          last_c--;
          if (vk_st->chunks[last_c].alive_count > 0) break;
        }
        if (last_c < vk_st->chunk_count) {
          uint32_t last_s = vk_st->chunks[last_c].alive_count - 1;
          struct Agent *last_a = vk_st->chunks[last_c].slot_owner[last_s];
          // Move last agent's brain into the freed slot (unless it IS the freed slot)
          if (!(a->brain_chunk == last_c && a->brain_index == last_s)) {
            vkbrain_move_slot(vk_st, last_c, last_s,
                              a->brain_chunk, a->brain_index, last_a);
            last_a->brain_chunk = a->brain_chunk;
            last_a->brain_index = a->brain_index;
            vk_st->chunks[a->brain_chunk].slot_owner[a->brain_index] = last_a;
          }
          vk_st->chunks[last_c].alive_count--;
          vk_st->chunks[last_c].slot_owner[last_s] = NULL;
        }
      }
      free(a);
      avec_delete(&world->agents, i);
      i--;  // re-check the swapped-in agent
    }
  }

  // Add agents from staging vector — assign GPU chunk+slot on demand
  for (size_t i = 0; i < world->agents_staging.size; i++) {
    struct Agent *a = world->agents_staging.agents[i];
    uint32_t chunk, slot;
    if (vk_st && !vkbrain_assign_slot(vk_st, a, &chunk, &slot)) {
      fprintf(stderr, "[World] GPU memory exhausted, discarding %zu excess agents\n",
              world->agents_staging.size - i);
      for (; i < world->agents_staging.size; i++) {
        struct Agent *xa = world->agents_staging.agents[i];
        free(xa);
      }
      break;
    }
    if (vk_st) {
      a->brain_chunk = chunk;
      a->brain_index = slot;
      vkbrain_stage_brain(vk_st, chunk, slot, a->brain);
    }
    avec_push_back(&world->agents, a);
  }
  world->agents_staging.size = 0;

  // Submit staging copies now (separate from compute dispatch)
  if (vk_st) vkbrain_flush_staging(vk_st);

  // Reclaim empty chunks (with hysteresis)
  if (vk_st) vkbrain_try_reclaim_last(vk_st);
}

void world_alloc(struct World *world) {
  memset(world, 0, sizeof(struct World));
  for (size_t i = 0; i < AGENT_BUCKETS; i++)
    world->agent_grid[i] = 0;
  world->queue = malloc(sizeof(struct Queue));
  queue_init(world->queue);
  world->closed = CLOSED;
}

void world_populate(struct World *world, int initFood, size_t numbots) {
  avec_init(&world->agents, numbots);
  avec_init(&world->agents_staging, numbots);

  if (numbots > 100)
    printf("Adding bots, this may take a while...\n");
  world_addRandomBots(world, (int32_t)numbots * .8);
  for (int32_t i = 0; i < (int32_t)numbots * .2; ++i)
    world_addCarnivore(world);

  foodGrid_init(&world->foodGrid);
  if (initFood) {
    printf("Initializing food.."); fflush(stdout);
    for (int i = 0; i < FOOD_INIT_ITER; i++)
      world_update_food(world);
    printf("\n");
  }

  remove("report.csv");
  world_flush_staging(world);
  world_sortGrid(world);
}

void world_init(struct World *world, int initFood, size_t numbots) {
  world_alloc(world);
  world_populate(world, initFood, numbots);
}

void world_dist_dead_agent(struct World *world, size_t i) {
  // distribute its food. It will be erased soon
  // since the close_agents array is sorted, just get the index where we
  // should stop distributing the body, then the number of carnivores around

  struct Agent *a = world->agents.agents[i];

  struct BucketList buckets_to_check = get_buckets_from_pos(a->pos.x, a->pos.y);

  struct Agent *dist_agents[FOOD_DISTRIBUTION_MAX];
  int num_to_dist_body = 0;

  // For each bucket
  for (size_t j = 0; j < 9; j++) {
    size_t bucket = buckets_to_check.buckets[j];
    struct AgentRange agent_range = get_agent_range(world, bucket);

    // For each agent
    for (size_t agent_idx = agent_range.start; agent_idx < agent_range.end; agent_idx++) {
      struct Agent *a2 = world->sorted_agents[agent_idx];

      // Ignore ourselves
      if (a == a2) {
        continue;
      }

      float dist2 = vector2f_dist2(&a->pos, &a2->pos);
      if (dist2 <= FOOD_DISTRIBUTION_RADIUS * FOOD_DISTRIBUTION_RADIUS) {
        // Only distribute to alive agents that are > 90% carnivores
        if (a2->herbivore < 0.1f && a2->health > 0.0f) {
          dist_agents[num_to_dist_body] = a2;
          num_to_dist_body++;

          if (num_to_dist_body >= FOOD_DISTRIBUTION_MAX) {
            break;
          }
        }
      }
    }

    // TODO Double break, not a fan but whatever
    if (num_to_dist_body >= FOOD_DISTRIBUTION_MAX) {
      break;
    }
  }

  // Agents give 1.0f health base
  float base_health_add = 1.0f;

  // bonus for hunting in groups
  base_health_add += fminf(0.4f * (num_to_dist_body / 8.0f), 0.4f);

  // young killed agents should give very little resources
  // at age 10, they mature and give full. This can also help prevent
  // agents eating their young right away
  if (a->age < 10) {
    base_health_add *= 0.1f;
  }

  // Divide for each agent
  base_health_add /= (float)num_to_dist_body;

  for (size_t j = 0; j < num_to_dist_body; j++) {
    struct Agent *a2 = dist_agents[j];

    // Base health add
    float health_add = base_health_add;

    // Factor in herbivore percentage
    health_add *= (1.0f - a2->herbivore);

    // Also reduce repcounter
    float rep_sub = 4.0f * health_add;

    // printf("n: %d, carn: %f, h+: %f, r-: %f\n", num_to_dist_body, 1.0f -
    // a2->herbivore, health_add, rep_sub);

    a2->health += health_add;
    a2->repcounter -= rep_sub;

    if (a2->health > 2.0f)
      a2->health = 2.0f; // cap it!

    agent_initevent(a2, health_add * 50.0f, 1.0f, 0.0f,
                    0.0f); // red means they ate! nice
  }
}

// ============================================================================
// Main Simulation Step
// ============================================================================

static void world_wait_compute(struct World *world) {
  VKState *vk = world->brain_gpu;
  if (!vk) return;
  vkWaitForFences(vk->device, 1, &vk->compute_fence, VK_TRUE, UINT64_MAX);
  vkResetFences(vk->device, 1, &vk->compute_fence);
}

void world_update(struct World *world) {
  struct timespec t;
  timer_reset(&t);

  world->modcounter++;

  // Increment Epoch
  if (world->modcounter >= 10000) {
    world->modcounter = 0;
    world->current_epoch++;
  }

  // Write report every REPORTS_PER_EPOCH
  if (REPORTS_PER_EPOCH > 0 && (world->modcounter % (int32_t)reportInterval == 0)) {
    world_writeReport(world);
  }

  world_update_food(world);
  world->time_food  = timer_elapsed_ms(&t);

  // Sort sorted_agents[] pointers (stable — agents[] never reordered, GPU brain safe)
  world_sortGrid(world);
  world->time_sort  = timer_elapsed_ms(&t);

  world_submit_compute(world);
  world->time_submit = timer_elapsed_ms(&t);

  // Gather inputs for NEXT frame (overlaps with GPU compute)
  world_setInputsRunBrain(world);
  world->time_inputs = timer_elapsed_ms(&t);

  world_wait_compute(world);
  world->time_compute = timer_elapsed_ms(&t);

  // read output and process consequences of bots on environment. requires out[]
  world_processOutputs(world);
  world->time_outputs = timer_elapsed_ms(&t);

  // Phase 2: Apply accumulated cross-agent interactions (single-threaded, no races)
  for (size_t i = 0; i < world->agents.size; i++) {
    struct Agent *a = world->agents.agents[i];
    a->health += a->pending_damage;
    if (a->pending_spiked) a->spiked = 1;
    a->pending_damage = 0.0f;
    a->pending_spiked = 0;
    if (a->health > 2.0f) a->health = 2.0f;
    if (a->health < 0.0f) a->health = 0.0f;
  }

  struct Agent *newMovieAgent = NULL;
  struct Agent *prevMovieAgent = NULL;
  int32_t newMostChildren = -1;
  int32_t prevMostChildren = -1;

  // Some things need to be done single threaded
  for (int i = 0; i < world->agents.size; i++) {
    struct Agent *a = world->agents.agents[i];
    if (a->health <= 0 && a->spiked == 1) {
      // Distribute dead agents to nearby carnivores
      world_dist_dead_agent(world, i);
    }

    if (a->rep) {
      world_reproduce(world, a);
    }

    if (world->movieMode) {
      if (a->selectflag) {
        prevMostChildren = a->numchildren;
        prevMovieAgent = a;
      } else {
        if (a->numchildren > newMostChildren) {
          newMostChildren = a->numchildren;
          newMovieAgent = a;
        }
      }
      a->selectflag = 0;
    }
  }

  if (newMovieAgent != NULL) {
    if (prevMovieAgent != NULL) {
      if (prevMostChildren >= newMostChildren) {
        prevMovieAgent->selectflag = 1;
      } else {
        newMovieAgent->selectflag = 1;
      }
    } else {
      newMovieAgent->selectflag = 1;
    }
  }

  // add new agents, if environment isn't closed
  if (!world->closed) {
    // make sure environment is always populated with at least NUMBOTS_MIN bots
    if (world->modcounter % 1000 == 0) {
      if (world->agents.size < NUMBOTS_MIN) {
        world_addRandomBots(world, 50);
      }
      if (world_numCarnivores(world) == 0) {
        for (int i = 0; i < 500; i++) {
          world_addCarnivore(world);
        }
      }
    }
  }
  world->time_post_out = timer_elapsed_ms(&t);

  // Flush staging: delete dead + stage new brains + submit GPU copy
  world_flush_staging(world);
  world->time_staging = timer_elapsed_ms(&t);

  world_record_compute(world);
  world->time_record = timer_elapsed_ms(&t);
  world->time_total_frame = timer_elapsed_ms(&t);
}

// ============================================================================
// GPU Brain Phases
// ============================================================================

void world_setInputsRunBrain(struct World *world) {
  struct AgentQueueItem agentQueueItems[((world->agents.size / BATCH_SIZE) + 1)];
  for (size_t i = 0; i * BATCH_SIZE < world->agents.size; i++) {
    size_t start = (i * BATCH_SIZE);
    size_t end = (i * BATCH_SIZE) + BATCH_SIZE;
    if (end > world->agents.size) {
      end = world->agents.size;
    }
    struct AgentQueueItem *agentQueueItem = &agentQueueItems[i];
    agentQueueItem->world = world;
    agentQueueItem->start = start;
    agentQueueItem->end = end;

    struct QueueItem queueItem = {agent_input_processor, agentQueueItem};
    queue_enqueue(world->queue, queueItem);
  }

  queue_wait_until_done(world->queue);
  // Note: GPU dispatch is submitted separately (world_submit_compute)
}

void world_submit_compute(struct World *world) {
  VKState *vk = world->brain_gpu;
  if (!vk) return;
  uint32_t read_slot = (uint32_t)world->brain_slot;
  VkSubmitInfo si = { .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                      .commandBufferCount = 1,
                      .pCommandBuffers = &vk->cmd_compute[read_slot] };
  vkQueueSubmit(vk->compute_queue, 1, &si, vk->compute_fence);
}

void world_record_compute(struct World *world) {
  VKState *vk = world->brain_gpu;
  if (!vk) return;
  // Record for NEXT frame's read slot (the slot we just wrote inputs to)
  uint32_t next_slot = 1u - (uint32_t)world->brain_slot;
  vkbrain_record_dispatch(vk, next_slot);
  world->brain_slot = (int32_t)next_slot;
}

void world_processOutputs(struct World *world) {
  struct AgentQueueItem agentQueueItems[((world->agents.size / BATCH_SIZE) + 1)];
  for (size_t i = 0; i * BATCH_SIZE < world->agents.size; i++) {
    size_t start = (i * BATCH_SIZE);
    size_t end = (i * BATCH_SIZE) + BATCH_SIZE;
    if (end > world->agents.size) {
      end = world->agents.size;
    }
    struct AgentQueueItem *agentQueueItem = &agentQueueItems[i];
    agentQueueItem->world = world;
    agentQueueItem->start = start;
    agentQueueItem->end = end;

    struct QueueItem queueItem = {agent_output_processor, agentQueueItem};
    queue_enqueue(world->queue, queueItem);
  }

  queue_wait_until_done(world->queue);
  // printf("output done, %zu size, %zu work items\n", world->queue->size,
  // world->queue->num_work_items);
  // printf("nagets: %zu\n", world->agents.size);
}

// ============================================================================
// Agent Creation, Reproduction, & Reporting
// ============================================================================

void world_addRandomBots(struct World *world, int32_t num) {
  world->numAgentsAdded += num; // record in report

  for (int32_t i = 0; i < num; i++) {
    struct Agent *a = malloc(sizeof(struct Agent));
    agent_init(a);

    // printf("%f\n", a.brain->inputs[0][0]);

    avec_push_back(&world->agents_staging, a);
  }
}

void world_addCarnivore(struct World *world) {
  struct Agent *a = malloc(sizeof(struct Agent));
  agent_init(a);
  a->herbivore = randf(0, 0.1f);

  avec_push_back(&world->agents_staging, a);

  world->numAgentsAdded++;
}

void world_reproduce(struct World *world, struct Agent *a) {
  agent_initevent(a, 30, 0.0f, 0.8f, 0.0f);
  for (int32_t i = 0; i < BABIES; i++) {
    struct Agent *a2 = malloc(sizeof(struct Agent));
    agent_init(a2);
    agent_reproduce(a2, a);
    avec_push_back(&world->agents_staging, a2);
  }
  // Children's brains are staged in world_flush_staging when added to agents.
  // Parent's brain is unchanged (only copied, not mutated).
}

void world_writeReport(struct World *world) {
  // save all kinds of nice data stuff
  int32_t numherb = 0;
  int32_t numcarn = 0;
  int32_t topherb = 0;
  int32_t topcarn = 0;
  int32_t total_age = 0;
  int32_t avg_age;
  float epoch_decimal = (float)world->modcounter / 10000.0f + (float)world->current_epoch;

  // Count number of herb, carn and top of each
  for (size_t i = 0; i < world->agents.size; i++) {
    if (world->agents.agents[i]->herbivore > 0.5f)
      numherb++;
    else
      numcarn++;

    if (world->agents.agents[i]->herbivore > 0.5f && world->agents.agents[i]->gencount > topherb)
      topherb = world->agents.agents[i]->gencount;
    if (world->agents.agents[i]->herbivore < 0.5f && world->agents.agents[i]->gencount > topcarn)
      topcarn = world->agents.agents[i]->gencount;

    // Average Age:
    total_age += world->agents.agents[i]->age;
  }
  if (world->agents.size > 0) {
    avg_age = total_age / world->agents.size;
  } else {
    avg_age = 0;
  }

  FILE *fp = fopen("report.csv", "a");

  fprintf(fp, "%f,%i,%i,%i,%i,%i,%i\n", epoch_decimal, numherb, numcarn, topherb, topcarn,
          avg_age, world->numAgentsAdded);

  fclose(fp);

  // Reset number agents added to zero
  world->numAgentsAdded = 0;
}

void world_reset(struct World *world) {
  avec_free(&world->agents_staging);
  avec_free(&world->agents);
  avec_init(&world->agents_staging, NUMBOTS);
  avec_init(&world->agents, NUMBOTS);
  world_addRandomBots(world, NUMBOTS);
  world_flush_staging(world);
}

void world_free_agents(struct World *world) {
  for (size_t i = 0; i < world->agents.size; i++)
    free(world->agents.agents[i]);
  for (size_t i = 0; i < world->agents_staging.size; i++)
    free(world->agents_staging.agents[i]);
  avec_free(&world->agents);
  avec_free(&world->agents_staging);
}

void world_processMouse(struct World *world, int32_t button, int32_t state, int32_t x, int32_t y) {
  if (state == 0) {
    float mind = 1e10;
    struct Agent *nearest = NULL;

    // Use spatial grid to only check agents in nearby buckets
    struct BucketList buckets = get_buckets_from_pos((float)x, (float)y);
    for (size_t b = 0; b < 9; b++) {
      struct AgentRange range = get_agent_range(world, buckets.buckets[b]);
      for (size_t i = range.start; i < range.end; i++) {
        struct Agent *a = world->sorted_agents[i];
        float dx = (float)x - a->pos.x, dy = (float)y - a->pos.y;
        float d = dx * dx + dy * dy;
        if (d < mind) { mind = d; nearest = a; }
      }
    }
    // Toggle selection by pointer identity (avoids O(n) index lookup)
    if (nearest) {
      for (size_t i = 0; i < world->agents.size; i++) {
        world->agents.agents[i]->selectflag =
            (world->agents.agents[i] == nearest) ? !nearest->selectflag : 0;
      }
    }
  }
}

int32_t world_numHerbivores(struct World *world) {
  int32_t numherb = 0;
  for (size_t i = 0; i < world->agents.size; i++) {
    if (world->agents.agents[i]->herbivore > 0.5f)
      numherb++;
  }

  return numherb;
}

int32_t world_numCarnivores(struct World *world) {
  int32_t numcarn = 0;
  for (size_t i = 0; i < world->agents.size; i++) {
    if (world->agents.agents[i]->herbivore <= 0.5f)
      numcarn++;
  }

  return numcarn;
}

int32_t world_numAgents(struct World *world) {
  if (world->closed && world->agents.size == 0) {
    printf("Population is extinct at epoch %i\n", world->current_epoch);
    exit(1);
  }
  return world->agents.size;
}

float world_getTotalFood(struct World *world) {
  return foodGrid_getTotalFood(&world->foodGrid);
}

// ============================================================================
// Spatial Grid Sorting
// ============================================================================

void world_sortGrid(struct World *world) {
  size_t n = world->agents.size;
  if (n > world->sorted_capacity) {
    world->sorted_capacity = n * 2;
    world->sorted_agents = realloc(world->sorted_agents,
                                    world->sorted_capacity * sizeof(struct Agent *));
  }

  // Counting sort by spatial bucket — O(N), groups agents into contiguous
  // bucket ranges so the 9-bucket scan in agent_set_inputs is cache-friendly.
  uint32_t *counts = calloc(AGENT_BUCKETS, sizeof(uint32_t));

  // 1. Count agents per bucket
  for (size_t i = 0; i < n; i++) {
    size_t b = get_bucket_from_pos((int64_t)(world->agents.agents[i]->pos.x / DIST),
                                    (int64_t)(world->agents.agents[i]->pos.y / DIST));
    counts[b]++;
  }

  // 2. Prefix sum → start offsets per bucket
  uint32_t total = 0;
  for (size_t b = 0; b < AGENT_BUCKETS; b++) {
    uint32_t c = counts[b];
    counts[b] = total;
    total += c;
  }

  // 3. Scatter agents into sorted_agents (counts now holds start offsets)
  for (size_t i = 0; i < n; i++) {
    size_t b = get_bucket_from_pos((int64_t)(world->agents.agents[i]->pos.x / DIST),
                                    (int64_t)(world->agents.agents[i]->pos.y / DIST));
    world->sorted_agents[counts[b]++] = world->agents.agents[i];
  }

  // 4. Build agent_grid end-indices from the final scatter offsets
  //    (counts[b] now holds end of bucket b = start of bucket b+1)
  for (size_t b = 0; b < AGENT_BUCKETS; b++)
    world->agent_grid[b] = counts[b];

  free(counts);
  world->sorted_size = n;
}

// ============================================================================
// Agent Processing (thread workers)
// ============================================================================

void agent_output_processor(void *arg) {
  struct AgentQueueItem *aqi = (struct AgentQueueItem *)arg;
  struct World *world = aqi->world;
  VKState *vk = world->brain_gpu;
  uint32_t read_slot = (uint32_t)world->brain_slot;

  for (size_t i = aqi->start; i < aqi->end; i++) {
    struct Agent *a = world->agents.agents[i];

    // Read GPU outputs + recurrence (memcpy is SIMD-optimized, faster than scalar loop)
    if (vk && a->brain_chunk != ~0u) {
      BrainChunk *c = &vk->chunks[a->brain_chunk];
      memcpy(a->out,
             c->mapped_outputs[read_slot] + a->brain_index * BRAIN_OUTPUT_SIZE,
             BRAIN_OUTPUT_SIZE * sizeof(float));
      memcpy(a->in + 18, a->out + 18, (BRAIN_INPUT_SIZE - 18) * sizeof(float));
    }

    a->w1 = a->out[0];
    a->w2 = a->out[1];
    a->red = fmaxf(a->out[2], 0.15f);
    a->gre = fmaxf(a->out[3], 0.15f);
    a->blu = fmaxf(a->out[4], 0.15f);
    a->boost = a->out[6] > 0.5f;
    a->soundmul = a->out[7];
    a->give = a->out[8];

    float greadj = fmaxf(0.0f, a->herbivore - 0.5f);
    float redadj = fmaxf(0.0f, 0.5f - a->herbivore);
    a->gre = fminf(a->gre + greadj, 1.0f);
    a->red = fminf(a->red + redadj, 1.0f);

    float g = a->out[5];
    if (a->spikeLength < g)
      a->spikeLength += SPIKESPEED;
    else if (a->spikeLength > g)
      a->spikeLength = g;

    // Wheel orientation: cos(a+π/2)=−sin(a), sin(a+π/2)=cos(a)
    // Compiler fuses adjacent sinf/cosf on same arg into a single x86 sincos.
    float halfR = BOTRADIUS * 0.5f;
    float sina = cosf(a->angle), cosa = -sinf(a->angle);
    float vx = halfR * cosa, vy = halfR * sina;
    float w1px = a->pos.x + vx, w1py = a->pos.y + vy;
    float w2px = a->pos.x - vx, w2py = a->pos.y - vy;

    float BW1 = BOTSPEED * a->w1;
    float BW2 = BOTSPEED * a->w2;
    if (a->boost) { BW1 *= BOOSTSIZEMULT; BW2 *= BOOSTSIZEMULT; }

    // Rotate around w2p by -BW1 (small-angle: sin≈x, cos≈1-x²/2)
    float vvx = w2px - a->pos.x, vvy = w2py - a->pos.y;
    float bw1s = -BW1, bw1c = 1.0f - BW1*BW1*0.5f;  // sin≈-BW1, cos≈1-BW1²/2
    float nvx = vvx * bw1c - vvy * bw1s, nvy = vvx * bw1s + vvy * bw1c;
    a->pos.x = w2px - nvx;
    a->pos.y = w2py - nvy;
    a->angle -= BW1;
    if (a->angle < (float)-M_PI) a->angle = (float)M_PI - ((float)-M_PI - a->angle);

    // Rotate around w1p by +BW2 (small-angle approximation)
    vvx = a->pos.x - w1px; vvy = a->pos.y - w1py;
    float bw2s = BW2, bw2c = 1.0f - BW2*BW2*0.5f;  // sin≈BW2, cos≈1-BW2²/2
    nvx = vvx * bw2c - vvy * bw2s; nvy = vvx * bw2s + vvy * bw2c;
    a->pos.x = w1px + nvx;
    a->pos.y = w1py + nvy;
    a->angle += BW2;
    if (a->angle > (float)M_PI) a->angle = (float)-M_PI + (a->angle - (float)M_PI);

    // Food intake (inlined bounds check)
    int32_t cx = (int32_t)a->pos.x / CZ;
    int32_t cy = (int32_t)a->pos.y / CZ;
    if ((uint32_t)cx < (uint32_t)FOOD_SQUARES_WIDTH &&
        (uint32_t)cy < (uint32_t)FOOD_SQUARES_HEIGHT &&
        world->foodGrid.food[cy][cx].amt > 0.0f &&
        a->health < 2.0f && a->herbivore > 0.1f) {
      float to_take = FOODINTAKE;
      float speedmul = ((1.0f - fabsf(a->w1)) + (1.0f - fabsf(a->w2))) * 0.25f + 0.5f;
      to_take *= speedmul * a->herbivore * a->herbivore;
      float itk = foodGrid_takeFood(&world->foodGrid, cx, cy, to_take);
      a->health += itk;
      a->repcounter -= 3.0f * itk;
    }

    a->rep = 0;

    if (a->repcounter < 0.0f && a->health > REP_MIN_HEALTH && randf(0, 1) < 0.05333f) {  // ~80% ÷ 15
      a->health -= a->health / ((float)BABIES + 1.0f);
      a->rep = 1;
      a->repcounter = a->herbivore * randf(REPRATEH - 0.1f, REPRATEH + 0.1f) +
                      (1.0f - a->herbivore) * randf(REPRATEC - 0.1f, REPRATEC + 0.1f);
    }

    // Inlined agent_process_health
    float healthloss = LOSS_BASE;
    if (a->age > 500.0f)
      healthloss += LOSS_AGE * ((a->age - 500.0f) * 0.004f);  // /250 = *0.004
    float wavg = (fabsf(a->w1) + fabsf(a->w2)) * BOTSPEED;
    healthloss += LOSS_SPEED * (a->boost ? wavg * 0.5f : wavg);
    if (a->boost) healthloss += LOSS_BOOST;
    healthloss += LOSS_SHOUTING * a->soundmul;
    a->health -= healthloss;
  }
}

void agent_set_inputs(struct World *world, struct Agent *a, struct BucketList buckets_to_check) {
  a->spiked = 0;
  a->indicator = fmaxf(a->indicator - 1.0f, 0.0f);

  if (world->modcounter % 100 == 0)
    a->age++;

  // Food sensor
  int32_t cx = (int32_t)a->pos.x / CZ;
  int32_t cy = (int32_t)a->pos.y / CZ;
  a->in[4] = 0.0f;
  if ((uint32_t)cx < (uint32_t)FOOD_SQUARES_WIDTH &&
      (uint32_t)cy < (uint32_t)FOOD_SQUARES_HEIGHT)
    a->in[4] = world->foodGrid.food[cy][cx].amt / FOODMAX;

  // Accumulators
  float p1 = 0, r1 = 0, g1 = 0, b1 = 0;
  float p2 = 0, r2 = 0, g2 = 0, b2 = 0;
  float soaccum = 0, smaccum = 0, hearaccum = 0, blood = 0;
  int   nearby_count = 0;
  float ratio_sum = 0;

  // Precompute per-agent constants (constant for this frame)
  float acos = cosf(a->angle), asin = sinf(a->angle);
  // Cone axes: cos/sin of eye directions (a ± π/16) via angle-sum identities
  float leye_cx = acos * COS_PI16 + asin * SIN_PI16;  // cos(a - π/16)
  float leye_cy = asin * COS_PI16 - acos * SIN_PI16;  // sin(a - π/16)
  float reye_cx = acos * COS_PI16 - asin * SIN_PI16;  // cos(a + π/16)
  float reye_cy = asin * COS_PI16 + acos * SIN_PI16;  // sin(a + π/16)
  float invDIST = 1.0f / DIST;
  float invGROUP = 1.0f / DIST_GROUPING;
  float DIST2 = DIST * DIST;
  float GROUP2 = DIST_GROUPING * DIST_GROUPING;
  float SHARE2  = FOOD_SHARING_DISTANCE * FOOD_SHARING_DISTANCE;
  float COLLISION_RADIUS = BOTRADIUS * 1.9f;
  float COLLISION2 = COLLISION_RADIUS * COLLISION_RADIUS;
  float DOT_SKIP = -0.5f;  // cos(120°) — skip eye/blood for agents behind
  float EYE_RANGE2 = DIST2 * 0.36f;  // (0.6*DIST)² — skip angle math for distant agents

  for (size_t j = 0; j < 9; j++) {
    size_t bucket = buckets_to_check.buckets[j];
    struct AgentRange r = get_agent_range(world, bucket);

    for (size_t idx = r.start; idx < r.end; idx++) {
      struct Agent *a2 = world->sorted_agents[idx];
      if (a == a2) continue;

      float dx = a2->pos.x - a->pos.x;
      float dy = a2->pos.y - a->pos.y;
      float d2 = dx*dx + dy*dy;
      if (d2 > DIST2) continue;

      float d = sqrtf(d2);
      float dist_falloff = 1.0f - d * invDIST;  // (DIST-d)/DIST

      // Smell & hearing (cheap, always evaluated)
      smaccum  += 0.3f * dist_falloff;
      hearaccum += a2->soundmul * dist_falloff;

      // Grouping proximity
      if (d2 < GROUP2) {
        float ratio = 1.0f - d * invGROUP;  // 1 at center, 0 at threshold
        nearby_count++;
        ratio_sum += ratio;
        if (5.0f * ratio > a->indicator) {
          a->indicator = 5.0f * ratio;
          a->ir = 0.5f; a->ig = 0.5f; a->ib = 0.5f;
        }
        soaccum += 0.4f * dist_falloff * fmaxf(fabsf(a2->w1), fabsf(a2->w2));
      }

      // Eye / blood / collision — skip if behind us
      float dot = acos * dx + asin * dy;
      if (dot < DOT_SKIP * d) goto skip_vision;

      // Cone pre-tests (no atan2f) for eye-range neighbors
      if (d2 < EYE_RANGE2) {
        float leye_dot   = leye_cx * dx + leye_cy * dy;
        float reye_dot   = reye_cx * dx + reye_cy * dy;
        bool  leye_pass  = leye_dot > 0.0f &&
               fabsf(leye_cx * dy - leye_cy * dx) < leye_dot * TAN_3PI16;
        bool  reye_pass  = reye_dot > 0.0f &&
               fabsf(reye_cx * dy - reye_cy * dx) < reye_dot * TAN_3PI16;
        bool  blood_pass = dot > 0.0f &&
               fabsf(asin * dx - acos * dy) < dot * TAN_3PI16;

        if (leye_pass || reye_pass || blood_pass) {
          // Only now compute atan2f for angle falloff weighting
          float cross = asin * dx - acos * dy;
          float ang_diff = atan2f(cross, dot);

          if (leye_pass) {
            float diff = ang_diff - PI8;  // left eye centered at -π/16
            float mul = EYE_SENSITIVITY * ((PI38 - fabsf(diff)) / PI38) * dist_falloff;
            float p = mul * (d * invDIST);
            p1 += p; r1 += mul * a2->red; g1 += mul * a2->gre; b1 += mul * a2->blu;
          }
          if (reye_pass) {
            float diff = ang_diff + PI8;  // right eye centered at +π/16
            float mul = EYE_SENSITIVITY * ((PI38 - fabsf(diff)) / PI38) * dist_falloff;
            float p = mul * (d * invDIST);
            p2 += p; r2 += mul * a2->red; g2 += mul * a2->gre; b2 += mul * a2->blu;
          }
          if (blood_pass) {
            float mul = BLOOD_SENSITIVITY * ((PI38 - fabsf(ang_diff)) / PI38) * dist_falloff;
            blood += mul * (1.0f - a2->health * 0.5f);
          }
        }
      }

      // Food sharing
      if (d2 < SHARE2) {
        if (a->give > 0.5f && a2->health < 2.0f)
          a->health -= FOODTRANSFER;
        if (a2->give > 0.5f && a->health < 2.0f)
          a->health += FOODTRANSFER;
      }

      // Spike collision
      if (d2 < COLLISION2) {
        float diff = a->angle - atan2f(dy, dx);
        if (diff < -(float)M_PI) diff += 2.0f * (float)M_PI;
        if (diff >  (float)M_PI) diff -= 2.0f * (float)M_PI;
        diff = fabsf(diff);

        if (diff < (float)M_PI / 4.0f) {
          float DMG = SPIKEMULT * a->spikeLength * (1.0f - a->herbivore)
                      * fmaxf(fabsf(a->w1), fabsf(a->w2)) * BOOSTSIZEMULT;
          if (DMG > 1.25f) {
            a2->pending_damage -= DMG;
            a->spikeLength = fmaxf(a->spikeLength - DMG, 0.0f);
            if (10.0f * DMG > a->indicator) {
              a->indicator = 10.0f * DMG;
              a->ir = 1.0f; a->ig = 1.0f; a->ib = 0.0f;
            }
            a2->pending_spiked = 1;
          }
        }
      }
      skip_vision:;
    }
  }

  // Grouping health gain
  {
    float effective_ratio = fminf((float)ratio_sum, CROWDING_LIMIT * 0.6f);
    float gain    = GAIN_GROUPING * effective_ratio;
    int   excess  = nearby_count - CROWDING_LIMIT;
    float penalty = (excess > 0) ? CROWDING_PENALTY * (float)(excess * excess) : 0.0f;
    a->health += gain - penalty;
  }

  if (a->health > 2.0f) a->health = 2.0f;

  a->in[0]  = cap(p1);  a->in[1]  = cap(r1);
  a->in[2]  = cap(g1);  a->in[3]  = cap(b1);
  a->in[5]  = cap(p2);  a->in[6]  = cap(r2);
  a->in[7]  = cap(g2);  a->in[8]  = cap(b2);
  a->in[9]  = cap(soaccum);
  a->in[10] = cap(smaccum);
  a->in[11] = cap(a->health * 0.5f);
  a->in[12] = fabsf(sinf((float)world->modcounter / a->clockf1));
  a->in[13] = fabsf(sinf((float)world->modcounter / a->clockf2));
  a->in[14] = cap(hearaccum);
  a->in[15] = cap(blood);
  a->in[16] = cap((float)a->touch);
  if (randf(0.0f, 1.0f) > 0.95f)
    a->in[17] = randf(0.0f, 1.0f);
}

void agent_input_processor(void *arg) {
  struct AgentQueueItem *aqi = (struct AgentQueueItem *)arg;
  struct World *world = aqi->world;
  VKState *vk = world->brain_gpu;
  // P1 writes to the NEXT frame's slot (not current frame's read slot)
  uint32_t write_slot = (vk ? 1u - (uint32_t)world->brain_slot : 0);

  for (size_t i = aqi->start; i < aqi->end; i++) {
    struct Agent *a = world->agents.agents[i];
    struct BucketList buckets_to_check = get_buckets_from_pos(a->pos.x, a->pos.y);
    agent_set_inputs(world, a, buckets_to_check);

    if (vk && a->brain_chunk != ~0u) {
      BrainChunk *c = &vk->chunks[a->brain_chunk];
      memcpy(c->mapped_inputs[write_slot] + a->brain_index * BRAIN_INPUT_SIZE,
             a->in, BRAIN_INPUT_SIZE * sizeof(float));
    }
  }
}

// Seed both double-buffer input slots with valid data after world load/init.
// Must be called after world_sortGrid (for spatial neighbor queries) and
// after brain weights have been uploaded to GPU.
void world_seed_inputs(struct World *world) {
  VKState *vk = world->brain_gpu;
  if (!vk) return;
  int32_t saved_slot = world->brain_slot;
  // Populate slot 0
  world->brain_slot = 1;
  world_setInputsRunBrain(world);
  // Populate slot 1
  world->brain_slot = 0;
  world_setInputsRunBrain(world);
  // Restore
  world->brain_slot = saved_slot;
}
