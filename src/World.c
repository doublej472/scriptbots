
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

static void timespec_diff(struct timespec *result, struct timespec *start, struct timespec *stop) {
  if ((stop->tv_nsec - start->tv_nsec) < 0) {
    result->tv_sec = stop->tv_sec - start->tv_sec - 1;
    result->tv_nsec = stop->tv_nsec - start->tv_nsec + 1000000000;
  } else {
    result->tv_sec = stop->tv_sec - start->tv_sec;
    result->tv_nsec = stop->tv_nsec - start->tv_nsec;
  }
}

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
    if (world->foodGrid.food[fx][fy].amt > FOODMAX * 0.7f) {
      // Spread to random square nearby
      size_t fxx = randi(fx - 1, fx + 2);
      size_t fyy = randi(fy - 1, fy + 2);

      food_to_add -= foodGrid_growFood(&world->foodGrid, fxx, fyy, fminf(FOODGROWTH,food_to_add));
    }
  }
}

static void world_update_gui(struct World *world) {
  world_writeReport(world);

  // Update GUI
  struct timespec endTime;
  clock_gettime(CLOCK_MONOTONIC, &endTime);
  struct timespec ts_delta;
  struct timespec ts_totaldelta;

  timespec_diff(&ts_delta, &world->startTime, &endTime);
  timespec_diff(&ts_totaldelta, &world->totalStartTime, &endTime);

  float deltat = (float)ts_delta.tv_sec + ((float)ts_delta.tv_nsec / 1000000000.0f);
  float totaldeltat = (float)ts_totaldelta.tv_sec + ((float)ts_totaldelta.tv_nsec / 1000000000.0f);

  int32_t carnivores = world_numCarnivores(world);
  int32_t herbivores = world_numHerbivores(world);
  float total_food = 0.0f;
  for (size_t i = 0; i < world->foodGrid.food_pivot; i++) {
    uint32_t x = world->foodGrid.food_sorted[i] % FOOD_SQUARES_WIDTH;
    uint32_t y = world->foodGrid.food_sorted[i] / FOOD_SQUARES_WIDTH;
    total_food += world->foodGrid.food[x][y].amt;
  }

  printf("\rEpoch: %d | Next: %d%% | Agents: %i (C: %i H: %i) | Food: %.2f | FPS: %.1f | Time: %.2f sec       ",
         world->current_epoch, world->modcounter / 100, (int32_t)world->agents.size, carnivores, herbivores,
          total_food,
         (float)reportInterval / deltat, totaldeltat);
  fflush(stdout);

  world->startTime = endTime;
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

  // Delete dead agents by swap-with-last (O(1) per death).
  // Only the swapped-in agent changes GPU index — one brain restage per death.
  for (size_t i = 0; i < world->agents.size; i++) {
    struct Agent *a = world->agents.agents[i];
    if (a->health <= 0) {
      free(a->brain);
      free(a);
      avec_delete(&world->agents, i);
      // avec_delete swapped the last element into position i; restage it
      if (vk_st && i < world->agents.size)
        vkbrain_stage_brain(vk_st, (uint32_t)i, world->agents.agents[i]->brain);
      i--;  // re-check the swapped-in agent
    }
  }

  // Add agents from staging vector
  size_t old_size = world->agents.size;
  for (size_t i = 0; i < world->agents_staging.size; i++) {
    avec_push_back(&world->agents, world->agents_staging.agents[i]);
    if (vk_st)
      vkbrain_stage_brain(vk_st, (uint32_t)(old_size + i),
                          world->agents_staging.agents[i]->brain);
  }
  world->agents_staging.size = 0;

  // Trigger GPU buffer growth if agent count approaches capacity
  if (vk_st && world->agents.size > vkbrain_capacity(vk_st) * 3 / 4)
    vkbrain_ensure_capacity(vk_st, world->agents.size * 2);

  // Submit staging copies now (separate from compute dispatch)
  if (vk_st) vkbrain_flush_staging(vk_st);
}

void world_init(struct World *world, int initFood, size_t numbots) {
  for (size_t i = 0; i < AGENT_BUCKETS; i++) {
    world->agent_grid[i] = 0;
  }

  world->queue = malloc(sizeof(struct Queue));
  queue_init(world->queue);

  world->stopSim = 0;
  world->movieMode = 0;
  world->modcounter = 0;
  world->current_epoch = 0;
  world->numAgentsAdded = 0;
  world->brain_slot = 0;
  world->brain_gpu = NULL;

  clock_gettime(CLOCK_MONOTONIC, &world->startTime);
  // Track total running time:
  clock_gettime(CLOCK_MONOTONIC, &world->totalStartTime);

  avec_init(&world->agents, numbots);
  avec_init(&world->agents_staging, numbots);

  world->sorted_agents = NULL;
  world->sorted_capacity = 0;
  world->sorted_size = 0;

  // create the bots but with 20% more carnivores, to give them head start
  if (numbots > 100) {
    printf("Adding bots, this may take a while...\n");
  }

  world_addRandomBots(world, (int32_t)numbots * .8);
  for (int32_t i = 0; i < (int32_t)numbots * .2; ++i)
    world_addCarnivore(world);

  foodGrid_init(&world->foodGrid);

  if (initFood) {
    printf("Initializing food..");
    fflush(stdout);
    for (int i = 0; i < 1000; i++) {
      if (i % 100 == 0) {
        printf(".");
        fflush(stdout);
      }
      world_update_food(world);
    }
    printf("\n");
  }

  // Decide if world if closed based on settings.h
  world->closed = CLOSED;

  // Delete the old report to start fresh
  remove("report.csv");
  world_flush_staging(world);
  world_sortGrid(world);
}

void world_printState(struct World *world) {
  printf("World State Info -----------\n");
  printf("Epoch:\t\t%i\n", world->current_epoch);
  printf("Tick:\t\t%i\n", world->modcounter);
  printf("Num Agents:%zu\n", world->agents.size);
  printf("Agents Added:%i\n", world->numAgentsAdded);
  printf("----------------------------\n");
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

  for (size_t j = 0; j < num_to_dist_body; j++) {
    struct Agent *a2 = dist_agents[j];
    // young killed agents should give very little resources
    // at age 10, they mature and give full. This can also help prevent
    // agents eating their young right away
    float agemult = 1.0f;
    if (a->age < 10) {
      agemult = ((float)a->age + 0.5f) / 20.0f;
    }

    // Base health add
    float health_add = 1.0f;

    // bonus for hunting in groups
    health_add += fminf(0.5f * (num_to_dist_body / 8.0f), 0.5f);

    // Factor in age muliplier
    health_add *= agemult;

    // Factor in herbivore percentage
    health_add *= (1.0f - a2->herbivore);

    // Divide for each agent
    health_add /= (float)num_to_dist_body;

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

  // Update GUI every REPORTS_PER_EPOCH amount:
  if (REPORTS_PER_EPOCH > 0 && (world->modcounter % (int32_t)reportInterval == 0)) {
    world_update_gui(world);
  }

  world_update_food(world);

  // Sort sorted_agents[] pointers (stable — agents[] never reordered, GPU brain safe)
  world_sortGrid(world);
  world->time_sort = timer_elapsed_ms(&t);

  world_submit_compute(world);

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
  world->time_flush = timer_elapsed_ms(&t);

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
  vkQueueSubmit(vk->queue, 1, &si, vk->compute_fence);
}

void world_record_compute(struct World *world) {
  VKState *vk = world->brain_gpu;
  if (!vk) return;
  // Record for NEXT frame's read slot (the slot we just wrote inputs to)
  uint32_t next_slot = 1u - (uint32_t)world->brain_slot;
  vkbrain_record_dispatch(vk, (int)world->agents.size, next_slot);
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
  for (size_t i = 0; i < world->agents.size; i++) {
    free(world->agents.agents[i]->brain);
    free(world->agents.agents[i]);
  }
  for (size_t i = 0; i < world->agents_staging.size; i++) {
    free(world->agents_staging.agents[i]->brain);
    free(world->agents_staging.agents[i]);
  }
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

  // Mark all live agents with this frame's generation
  static uint64_t gen = 0;
  gen++;
  // Prevent theoretical wrap (585M years at 1000 fps)
  if (gen == 0) { gen = 1; for (size_t i = 0; i < n; i++) world->agents.agents[i]->sort_alive = 0; }
  for (size_t i = 0; i < n; i++)
    world->agents.agents[i]->sort_alive = gen;

  // Compact: copy old sorted_agents entries that are still alive (keep order),
  // and clear their marker so new agents can be identified.
  size_t out = 0, old_n = world->sorted_size;
  for (size_t i = 0; i < old_n; i++) {
    struct Agent *a = world->sorted_agents[i];
    if (a->sort_alive == gen) {
      a->sort_alive = 0;  // consumed — won't be appended as "new"
      world->sorted_agents[out++] = a;
    }
  }
  // Append new agents (in agents[] but not in old sorted_agents)
  for (size_t i = 0; i < n; i++) {
    struct Agent *a = world->agents.agents[i];
    if (a->sort_alive == gen)  // marker still set = new agent
      world->sorted_agents[out++] = a;
  }

  // Insertion sort — O(N) on mostly-sorted data (agents move slowly)
  for (size_t i = 1; i < n; i++) {
    struct Agent *key = world->sorted_agents[i];
    size_t key_b = get_bucket_from_pos((int64_t)(key->pos.x / DIST),
                                        (int64_t)(key->pos.y / DIST));
    long j = (long)i - 1;
    while (j >= 0) {
      struct Agent *a2 = world->sorted_agents[j];
      size_t ab = get_bucket_from_pos((int64_t)(a2->pos.x / DIST),
                                       (int64_t)(a2->pos.y / DIST));
      if (ab <= key_b) break;
      world->sorted_agents[j + 1] = world->sorted_agents[j];
      j--;
    }
    world->sorted_agents[j + 1] = key;
  }

  // Populate agent_grid: end-index into sorted_agents per bucket
  size_t cur = 0;
  for (size_t i = 0; i < n; i++) {
    size_t b = get_bucket_from_pos((int64_t)(world->sorted_agents[i]->pos.x / DIST),
                                    (int64_t)(world->sorted_agents[i]->pos.y / DIST));
    while (b > cur) world->agent_grid[cur++] = i;
  }
  while (cur < AGENT_BUCKETS) world->agent_grid[cur++] = n;
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
    if (vk) {
      memcpy(a->out, vk->mapped_outputs[read_slot] + i * 48, 48 * sizeof(float));
      memcpy(a->in + 18, a->out + 18, 30 * sizeof(float));
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
        world->foodGrid.food[cx][cy].amt > 0.0f &&
        a->health < 2.0f && a->herbivore > 0.1f) {
      float to_take = FOODINTAKE;
      float speedmul = ((1.0f - fabsf(a->w1)) + (1.0f - fabsf(a->w2))) * 0.25f + 0.5f;
      to_take *= speedmul * a->herbivore * a->herbivore;
      float itk = foodGrid_takeFood(&world->foodGrid, cx, cy, to_take);
      a->health += itk;
      a->repcounter -= 3.0f * itk;
    }

    a->rep = 0;

    if (world->modcounter % 15 == 0 && a->repcounter < 0.0f && a->health > REP_MIN_HEALTH && randf(0, 1) < 0.8f) {
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
  // General settings
  // says that agent was not hit this turn
  a->spiked = 0;

  // process indicator used in drawing
  a->indicator = fmaxf(a->indicator - 1.0f, 0.0f);

  // Update agents age
  if (world->modcounter % 100 == 0)
    a->age++;

  // FOOD
  int32_t cx = (int32_t)a->pos.x / CZ;
  int32_t cy = (int32_t)a->pos.y / CZ;
  a->in[4] = 0.0f;
  if (cx >= 0 && cx < FOOD_SQUARES_WIDTH && cy >= 0 && cy < FOOD_SQUARES_HEIGHT) {
    a->in[4] = world->foodGrid.food[cx][cy].amt / FOODMAX;
  }

  // SOUND SMELL EYES
  float p1 = 0;
  float r1 = 0;
  float g1 = 0;
  float b1 = 0;
  float p2 = 0;
  float r2 = 0;
  float g2 = 0;
  float b2 = 0;
  float soaccum = 0;
  float smaccum = 0;
  float hearaccum = 0;

  // BLOOD ESTIMATOR
  float blood = 0;

  // AMOUNT OF HEALTH GAINED FROM BEING IN GROUP
  float health_gain = 0;

  // SMELL SOUND EYES
  // For each bucket
  for (size_t j = 0; j < 9; j++) {
    size_t bucket = buckets_to_check.buckets[j];
    struct AgentRange agent_range = get_agent_range(world, bucket);

    // For each agent (sorted by spatial bucket)
    for (size_t agent_idx = agent_range.start; agent_idx < agent_range.end; agent_idx++) {
      struct Agent *a2 = world->sorted_agents[agent_idx];

      // Ignore ourselves
      if (a == a2) {
        continue;
      }

      float d = vector2f_dist2(&a->pos, &a2->pos);

      if (d > DIST * DIST) {
        continue;
      }

      // Get the real distance now
      d = sqrtf(d);

      // smell
      smaccum += 0.3f * (DIST - d) / DIST;

      // hearing. (listening to other agents shouting)
      hearaccum += a2->soundmul * (DIST - d) / DIST;

      // more fine-tuned closeness
      if (d < DIST_GROUPING) {
        // grouping health bonus for each agent near by
        // health gain is most when two bots are just at threshold, is less
        // when they are ontop each other
        float ratio = (1.0f - (DIST_GROUPING - d) / DIST_GROUPING);
        health_gain += GAIN_GROUPING * ratio;
        agent_initevent(a, 5.0f * ratio, 0.5f, 0.5f, 0.5f); // visualize it

        // sound (number of agents nearby)
        soaccum += 0.4f * ((DIST - d) / DIST) * (fmaxf(fabsf(a2->w1), fabsf(a2->w2)));
      }

      // current angle between bots
      float ang = vector2f_angle_between(&a->pos, &a2->pos);

      // left and right eyes
      float leyeangle = a->angle - PI8;
      float reyeangle = a->angle + PI8;
      float forwangle = a->angle;
      if (leyeangle < (float)-M_PI)
        leyeangle += 2.0f * (float)M_PI;
      if (reyeangle > (float)M_PI)
        reyeangle -= 2.0f * (float)M_PI;
      float diff1 = leyeangle - ang;
      if (fabsf(diff1) > (float)M_PI)
        diff1 = 2.0f * (float)M_PI - fabsf(diff1);
      diff1 = fabsf(diff1);
      float diff2 = reyeangle - ang;
      if (fabsf(diff2) > (float)M_PI)
        diff2 = 2.0f * (float)M_PI - fabsf(diff2);
      diff2 = fabsf(diff2);
      float diff4 = forwangle - ang;
      if (fabsf(forwangle) > (float)M_PI)
        diff4 = 2.0f * (float)M_PI - fabsf(forwangle);
      diff4 = fabsf(diff4);

      if (diff1 < PI38) {
        // we see this agent with left eye. Accumulate info
        float mul1 = EYE_SENSITIVITY * ((PI38 - diff1) / PI38) * ((DIST - d) / DIST);
        // float mul1= 100*((DIST-d)/DIST);
        p1 += mul1 * (d / DIST);
        r1 += mul1 * a2->red;
        g1 += mul1 * a2->gre;
        b1 += mul1 * a2->blu;
      }

      if (diff2 < PI38) {
        // we see this agent with left eye. Accumulate info
        float mul2 = EYE_SENSITIVITY * ((PI38 - diff2) / PI38) * ((DIST - d) / DIST);
        // float mul2= 100*((DIST-d)/DIST);
        p2 += mul2 * (d / DIST);
        r2 += mul2 * a2->red;
        g2 += mul2 * a2->gre;
        b2 += mul2 * a2->blu;
      }

      if (diff4 < PI38) {
        float mul4 = BLOOD_SENSITIVITY * ((PI38 - diff4) / PI38) * ((DIST - d) / DIST);
        // if we can see an agent close with both eyes in front of us
        blood += mul4 * (1.0f - a2->health / 2.0f); // remember: health is in [0 2]
        // agents with high life dont bleed. low life makes them bleed more
      }

      // Process health sharing
      if (d < FOOD_SHARING_DISTANCE) {
        if (a->give > 0.5f) {
          // initiate transfer
          if (a2->health < 2.0f) {
            a->health -= FOODTRANSFER;
          }
        }

        if (a2->give > 0.5f) {
          if (a->health < 2.0f) {
            a->health += FOODTRANSFER;
          }
        }
      }

      // Process collisions
      if (d < BOTRADIUS * 1.9f) {
        // these two are in collision and agent i has extended spike and is
        // going decent fast!

        struct Vector2f tmp;
        vector2f_sub(&tmp, &a2->pos, &a->pos);

        float diffangle = vector2f_angle(&tmp);
        float diff = a->angle - diffangle;

        diff = fabsf(fmodf(diff, (float)M_PI));

        if (diff < (float)M_PI / 4.0f) {
          if (0) {
            printf("Collision Detected!\n");
            printf("  Pos a1:\t%f\t%f\n", a->pos.x, a->pos.y);
            printf("  Pos a2:\t%f\t%f\n", a2->pos.x, a2->pos.y);
            printf("  Diff Vec:\t%f\t%f\n", tmp.x, tmp.y);
            printf("  Diff Angle:\t%f\n", diffangle);
            printf("  Angle a:\t%f\n", a->angle);
            printf("  Diff:\t\t%f\n", diff);
            printf("  Distance to a2:\t%f\n", d);
          }
          //  bot i is also properly aligned!!! that's a hit
          float DMG =
              SPIKEMULT * a->spikeLength * (1.0f - a->herbivore) * fmaxf(fabsf(a->w1), fabsf(a->w2)) * BOOSTSIZEMULT;

          // You have to hit hard for it to count
          if (DMG > 1.25f) {
            a2->pending_damage -= DMG;  // accumulated, applied single-threaded in Phase 2
            a->spikeLength = fmaxf(a->spikeLength - DMG, 0.0f); // retract spike back down

            agent_initevent(a, 10.0f * DMG, 1.0f, 1.0f,
                            0.0f); // yellow event means bot has spiked other bot. nice!

            // set a flag saying that this agent was hit this turn
            a2->pending_spiked = 1;
          }
        }
      }
    }
  }

  // APPLY HEALTH GAIN
  if (health_gain > GAIN_GROUPING) // cap at conf value
    a->health += GAIN_GROUPING;
  else
    a->health += health_gain;

  if (a->health > 2) // limit the amount of health
    a->health = 2;

  a->in[0] = cap(p1);
  a->in[1] = cap(r1);
  a->in[2] = cap(g1);
  a->in[3] = cap(b1);
  a->in[5] = cap(p2);
  a->in[6] = cap(r2);
  a->in[7] = cap(g2);
  a->in[8] = cap(b2);
  a->in[9] = cap(soaccum); // SOUND (amount of other agents nearby)
  a->in[10] = cap(smaccum);
  a->in[11] = cap(a->health / 2); // divide by 2 since health is in [0,2]
  a->in[12] = fabsf(sinf(world->modcounter / a->clockf1));
  a->in[13] = fabsf(sinf(world->modcounter / a->clockf2));
  a->in[14] = cap(hearaccum); // HEARING (other agents shouting)
  a->in[15] = cap(blood);
  a->in[16] = cap(a->touch);
  if (randf(0, 1) > 0.95f) {
    a->in[17] = randf(0, 1); // random input for bot
  }
  // Recurrence (in[18..47] ← out[18..47]) is handled in agent_output_processor.
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

    if (vk) {
      memcpy(vk->mapped_inputs[write_slot] + i * 48, a->in, 48 * sizeof(float));
    }
  }
}
