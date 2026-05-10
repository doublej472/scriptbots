
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "Food.h"
#include "World.h"
#include "helpers.h"
#include "queue.h"
#include "settings.h"
#include "vec.h"
#include "vec2f.h"
#include "vkhelpers.h"

// Forward declarations — these two are defined after world_update
static void world_drain_spike_outboxes(struct World *world);
static void world_apply_food_requests(struct World *world);

// ============================================================================
// Food & GUI
// ============================================================================

static void world_update_food(struct World *world) {
  // if (world->modcounter % 20 == 0) {
  //   printf("food_pivot: %d\n", world->foodGrid.food_pivot);
  // }

  float food_to_add = FOOD_ADD_PER_FRAME;

  // When food density is very low, seed random cells to kick-start growth.
  // Once enough cells are alive, switch to preferential growth on existing
  // food squares so patches spread and thicken naturally.
  if (world->foodGrid.food_pivot < (uint32_t)(TOTAL_FOOD_SQUARES * FOOD_SPARSE_THRESHOLD)) {
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
    food_to_add -= foodGrid_growFood(&world->foodGrid, fx, fy, fminf(FOODGROWTH, food_to_add));
    // Grow surrounding squares only if well grown
    if (world->foodGrid.food[fy][fx].amt > FOODMAX * 0.7f) {
      // Spread to random square nearby
      size_t fxx = randi(fx - 1, fx + 2);
      size_t fyy = randi(fy - 1, fy + 2);

      food_to_add -= foodGrid_growFood(&world->foodGrid, fxx, fyy, fminf(FOODGROWTH, food_to_add));
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

// Forward: defined below in "Agent Processing" section
static void world_update_render_entry(struct World *world, size_t idx);

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
          if (vk_st->chunks[last_c].alive_count > 0)
            break;
        }
        if (last_c < vk_st->chunk_count && vk_st->chunks[last_c].alive_count > 0) {
          uint32_t last_s = vk_st->chunks[last_c].alive_count - 1;
          struct Agent *last_a = vk_st->chunks[last_c].slot_owner[last_s];
          // Move last agent's brain into the freed slot (unless it IS the freed slot)
          if (!(a->brain_chunk == last_c && a->brain_index == last_s)) {
            vkbrain_move_slot(vk_st, last_c, last_s, a->brain_chunk, a->brain_index, last_a);
            last_a->brain_chunk = a->brain_chunk;
            last_a->brain_index = a->brain_index;
            vk_st->chunks[a->brain_chunk].slot_owner[a->brain_index] = last_a;
          }
          vk_st->chunks[last_c].alive_count--;
          vk_st->chunks[last_c].slot_owner[last_s] = NULL;
        }
      }
      if (world->selected_agent == a) {
        world->selected_agent = NULL;
        world->selected_index = 0;
      }
      if (world->movie_agent == a) {
        world->movie_agent = NULL;
        world->movie_index = 0;
      }
      world->agents.agents[i] = NULL; // poison for ASAN before avec_delete overwrites

      // Move flat I/O + render entries from tail to this slot (matching avec_delete swap)
      size_t last = world->agents.size - 1;
      if (i < last) {
        if (world->agent_inputs)
          memcpy(world->agent_inputs + i * AGENT_INPUT_FLOATS, world->agent_inputs + last * AGENT_INPUT_FLOATS,
                 AGENT_INPUT_FLOATS * sizeof(float));
        if (world->agent_render_data) {
          AgentInstance *r = (AgentInstance *)world->agent_render_data;
          r[i] = r[last];
        }
        // Fix up selected/movie index if the swapped agent was tracked
        if (world->selected_agent && world->selected_index == last)
          world->selected_index = i;
        if (world->movie_agent && world->movie_index == last)
          world->movie_index = i;
      }

      free(a);
      avec_delete(&world->agents, i);
      i--; // re-check the swapped-in agent
    }
  }

  // Add agents from staging vector — assign GPU chunk+slot on demand
  size_t old_size = world->agents.size;
  for (size_t i = 0; i < world->agents_staging.size; i++) {
    struct Agent *a = world->agents_staging.agents[i];
    uint32_t chunk, slot;
    if (vk_st && !vkbrain_assign_slot(vk_st, a, &chunk, &slot)) {
      fprintf(stderr, "[World] GPU memory exhausted, discarding %zu excess agents\n", world->agents_staging.size - i);
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
  // Grow flat I/O + render arrays to match final allocated capacity
  if (world->agents.allocated > 0) {
    size_t needed_inputs = world->agents.allocated * AGENT_INPUT_FLOATS;
    world->agent_inputs = realloc(world->agent_inputs, needed_inputs * sizeof(float));
    // Zero the new tail portion (realloc preserves old data, new bytes are uninit)
    if (old_size < world->agents.size) {
      size_t new_start = old_size * AGENT_INPUT_FLOATS;
      memset(world->agent_inputs + new_start, 0, (needed_inputs - new_start) * sizeof(float));
    }
    if (world->agent_render_capacity < world->agents.allocated) {
      world->agent_render_capacity = world->agents.allocated;
      world->agent_render_data =
          realloc(world->agent_render_data, world->agent_render_capacity * sizeof(AgentInstance));
    }
  }
  // Populate render entries for newborn agents
  if (world->agent_render_data) {
    for (size_t i = old_size; i < world->agents.size; i++)
      world_update_render_entry(world, i);
  }
  world->agents_staging.size = 0;

  // Submit staging copies now (separate from compute dispatch)
  if (vk_st)
    vkbrain_flush_staging(vk_st);

  // Reclaim empty chunks (with hysteresis)
  if (vk_st)
    vkbrain_try_reclaim_last(vk_st);

  // Update cached population counts (used by diagnostics)
  int32_t h = 0, c = 0;
  for (size_t i = 0; i < world->agents.size; i++) {
    if (world->agents.agents[i]->herbivore > 0.5f)
      h++;
    else
      c++;
  }
  world->cached_herbivores = h;
  world->cached_carnivores = c;
}

void world_alloc(struct World *world) {
  memset(world, 0, sizeof(struct World));
  world->selected_agent = NULL;
  world->movie_agent = NULL;
  world->selected_index = 0;
  world->movie_index = 0;
  world->agent_inputs = NULL;
  world->agent_render_data = NULL;
  world->agent_render_capacity = 0;
  for (size_t i = 0; i < AGENT_BUCKETS; i++)
    world->agent_grid[i] = 0;
  world->queue = malloc(sizeof(struct Queue));
  queue_init(world->queue);
  world->queue->world = world;
  world->closed = CLOSED;
}

void world_populate(struct World *world, int initFood, size_t numbots) {
  world->numbots = (uint32_t)numbots;
  avec_init(&world->agents, numbots);
  avec_init(&world->agents_staging, numbots);

  if (numbots > 100)
    printf("Adding bots, this may take a while...\n");
  world_addRandomBots(world, (size_t)(numbots * .8));
  for (size_t i = 0; i < (size_t)(numbots * .2); ++i)
    world_addCarnivore(world);

  foodGrid_init(&world->foodGrid);
  if (initFood) {
    printf("Initializing food..");
    fflush(stdout);
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
      if (!a2)
        continue;

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
  if (!vk)
    return;
  uint64_t wait_value = vk->compute_timeline_value - 1;
  VkSemaphoreWaitInfo swi = {
      .sType          = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO,
      .semaphoreCount = 1,
      .pSemaphores    = &vk->compute_timeline,
      .pValues        = &wait_value,
  };
  vkWaitSemaphores(vk->device, &swi, UINT64_MAX);
}

void world_update(struct World *world) {
  struct timespec t0;
  double prev = 0.0, now;
  timer_reset(&t0);

  world->modcounter++;

  if (world->modcounter >= 10000) {
    world->modcounter = 0;
    world->current_epoch++;
  }

  if (REPORTS_PER_EPOCH > 0 && (world->modcounter % reportInterval == 0)) {
    world_writeReport(world);
  }

  world_update_food(world);
  now = timer_since_ms(&t0);
  world->timing.food_update = (float)(now - prev);
  prev = now;

  world_sortGrid(world);
  now = timer_since_ms(&t0);
  world->timing.spatial_sort = (float)(now - prev);
  prev = now;

  // Gather inputs and deploy to GPU (uses current write_slot)
  world_setInputsRunBrain(world);
  world_drain_spike_outboxes(world);
  // Commit deferred health deltas from input dispatch (parallel-safe)
  for (size_t i = 0; i < world->agents.size; i++) {
    struct Agent *a = world->agents.agents[i];
    a->health += a->pending_health_delta;
    if (a->health > 2.0f)
      a->health = 2.0f;
    a->pending_health_delta = 0.0f;
  }
  now = timer_since_ms(&t0);
  world->timing.input_staging = (float)(now - prev);
  prev = now;

  world_wait_compute(world);
  now = timer_since_ms(&t0);
  world->timing.gpu_wait = (float)(now - prev);
  prev = now;

  // read output and process consequences of bots on environment.
  world_processOutputs(world);
  world_apply_food_requests(world);
  now = timer_since_ms(&t0);
  world->timing.output_processing = (float)(now - prev);
  prev = now;

  // Death distribution, movie mode — single-threaded
  struct Agent *newMovieAgent = NULL;
  struct Agent *prevMovieAgent = NULL;
  size_t newMovieIndex = 0;
  size_t prevMovieIndex = 0;
  int32_t newMostChildren = -1;
  int32_t prevMostChildren = -1;

  for (size_t i = 0; i < world->agents.size; i++) {
    struct Agent *a = world->agents.agents[i];
    if (a->health <= 0 && a->spiked == 1) {
      world_dist_dead_agent(world, i);
    }
    if (world->movieMode) {
      if (a == world->movie_agent) {
        prevMostChildren = a->numchildren;
        prevMovieAgent = a;
        prevMovieIndex = i;
      } else if (a->numchildren > newMostChildren) {
        newMostChildren = a->numchildren;
        newMovieAgent = a;
        newMovieIndex = i;
      }
    }
  }

  if (world->movieMode) {
    // If the previous movie agent died or wasn't found, switch to new
    if (newMovieAgent != NULL) {
      if (prevMovieAgent != NULL && prevMostChildren >= newMostChildren) {
        world->movie_agent = prevMovieAgent;
        world->movie_index = prevMovieIndex;
      } else {
        world->movie_agent = newMovieAgent;
        world->movie_index = newMovieIndex;
      }
    } else if (prevMovieAgent == NULL) {
      world->movie_agent = NULL;
      world->movie_index = 0;
    }
  }

  // Reproduction (single-threaded: world_reproduce calls malloc + brain init;
  // parallel dispatch added no real concurrency since staging_lock serialized it)
  for (size_t i = 0; i < world->agents.size; i++) {
    if (world->agents.agents[i]->rep)
      world_reproduce(world, world->agents.agents[i]);
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
  world->timing.death_repro = (float)(timer_since_ms(&t0) - prev);
  prev = timer_since_ms(&t0);

  // Flush staging: delete dead + stage new brains + submit GPU copy
  world_flush_staging(world);
  world->timing.flush_staging = (float)(timer_since_ms(&t0) - prev);
  prev = timer_since_ms(&t0);

  world_record_compute(world);
  world->timing.record_compute = (float)(timer_since_ms(&t0) - prev);
  prev = timer_since_ms(&t0);

  world_submit_compute(world);
  world->timing.compute_submit = (float)(timer_since_ms(&t0) - prev);
}

// ============================================================================
// GPU Brain Phases
// ============================================================================

void world_setInputsRunBrain(struct World *world) {
  VKState *vk = world->brain_gpu;
  uint32_t write_slot = vk ? 1u - world->brain_slot : 0;
  world_dispatch(world->queue, QUEUE_PHASE_INPUTS, write_slot);
}

// Drain every agent's spike outbox, applying damage to defenders.
// Called single-threaded after the input dispatch completes.
static void world_drain_spike_outboxes(struct World *world) {
  for (size_t i = 0; i < world->agents.size; i++) {
    struct Agent *a = world->agents.agents[i];
    for (uint32_t j = 0; j < a->spike_outbox_count; j++) {
      struct Agent *def = a->spike_outbox[j].target;
      def->pending_damage -= a->spike_outbox[j].damage;
      def->pending_spiked = 1;
    }
    a->spike_outbox_count = 0;
  }
}

void world_submit_compute(struct World *world) {
  VKState *vk = world->brain_gpu;
  if (!vk)
    return;
  uint32_t read_slot = world->brain_slot;
  uint64_t signal_value = vk->compute_timeline_value++;
  VkTimelineSemaphoreSubmitInfo tsi = {
      .sType                     = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO,
      .signalSemaphoreValueCount = 1,
      .pSignalSemaphoreValues    = &signal_value,
  };
  VkSubmitInfo si = {
      .sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO,
      .pNext              = &tsi,
      .commandBufferCount = 1,
      .pCommandBuffers    = &vk->cmd_compute[read_slot],
      .signalSemaphoreCount = 1,
      .pSignalSemaphores    = &vk->compute_timeline,
  };
  vkQueueSubmit(vk->compute_queue, 1, &si, VK_NULL_HANDLE);
}

void world_record_compute(struct World *world) {
  VKState *vk = world->brain_gpu;
  if (!vk)
    return;
  // Record for NEXT frame's read slot (the slot we just wrote inputs to)
  uint32_t next_slot = 1u - world->brain_slot;
  vkbrain_record_dispatch(vk, next_slot);
  world->brain_slot = next_slot;
}

void world_processOutputs(struct World *world) {
  uint32_t read_slot = world->brain_slot;
  world_dispatch(world->queue, QUEUE_PHASE_OUTPUTS, read_slot);
}

// Apply deferred food consumption requests.  Runs single-threaded after the
// output dispatch so foodGrid_takeFood (which does RMW on food[y][x].amt and
// mutates the food_pivot / food_sorted index) has no concurrent writers.
static void world_apply_food_requests(struct World *world) {
  for (size_t i = 0; i < world->agents.size; i++) {
    struct Agent *a = world->agents.agents[i];
    if (a->food_request <= 0.0f)
      continue;
    int32_t cx = (int32_t)a->pos.x / CZ;
    int32_t cy = (int32_t)a->pos.y / CZ;
    if ((uint32_t)cx < (uint32_t)FOOD_SQUARES_WIDTH && (uint32_t)cy < (uint32_t)FOOD_SQUARES_HEIGHT &&
        world->foodGrid.food[cy][cx].amt > 0.0f) {
      float taken = foodGrid_takeFood(&world->foodGrid, cx, cy, a->food_request);
      a->health += taken;
      a->repcounter -= 3.0f * taken;
    }
    a->food_request = 0.0f;
  }
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

void world_addHerbivore(struct World *world) {
  struct Agent *a = malloc(sizeof(struct Agent));
  agent_init(a);
  a->herbivore = randf(0.9f, 1.0f);

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

  fprintf(fp, "%f,%i,%i,%i,%i,%i,%i\n", epoch_decimal, numherb, numcarn, topherb, topcarn, avg_age,
          world->numAgentsAdded);

  fclose(fp);

  // Reset number agents added to zero
  world->numAgentsAdded = 0;
}

void world_reset(struct World *world) {
  // Free old agents and their GPU brain slots
  for (size_t i = 0; i < world->agents.size; i++)
    free(world->agents.agents[i]);
  for (size_t i = 0; i < world->agents_staging.size; i++)
    free(world->agents_staging.agents[i]);
  avec_free(&world->agents_staging);
  avec_free(&world->agents);

  // Release GPU brain slots so new agents reuse the existing chunks
  if (world->brain_gpu) {
    vkDeviceWaitIdle(vkinit_get_device(world->brain_gpu));
    vkbrain_reset_counts(world->brain_gpu);
  }

  avec_init(&world->agents_staging, world->numbots);
  avec_init(&world->agents, world->numbots);
  world_addRandomBots(world, world->numbots);
  world_flush_staging(world);
}

void world_free_agents(struct World *world) {
  for (size_t i = 0; i < world->agents.size; i++)
    free(world->agents.agents[i]);
  for (size_t i = 0; i < world->agents_staging.size; i++)
    free(world->agents_staging.agents[i]);
  avec_free(&world->agents);
  avec_free(&world->agents_staging);
  free(world->agent_inputs);
  free(world->agent_render_data);
  world->agent_inputs = NULL;
  world->agent_render_data = NULL;
  world->agent_render_capacity = 0;
}

void world_processMouse(struct World *world, int32_t button, int32_t state, int32_t x, int32_t y) {
  if (state == 0) {
    float mind = 1e10;
    struct Agent *nearest = NULL;

    // Linear scan — mouse clicks are rare, O(n) is negligible.
    // The old bucket-based search only checked a 3×3 hash neighbourhood
    // and missed agents when clicking on empty regions.
    size_t nearest_idx = 0;
    for (size_t i = 0; i < world->agents.size; i++) {
      struct Agent *a = world->agents.agents[i];
      float dx = (float)x - a->pos.x, dy = (float)y - a->pos.y;
      float d = dx * dx + dy * dy;
      if (d < mind) {
        mind = d;
        nearest = a;
        nearest_idx = i;
      }
    }

    // Toggle: clicking the already-selected agent deselects it;
    // clicking any other agent selects it.
    if (nearest) {
      if (world->selected_agent == nearest) {
        world->selected_agent = NULL;
        world->selected_index = 0;
      } else {
        world->selected_agent = nearest;
        world->selected_index = nearest_idx;
      }
    }
  }
}

int32_t world_numHerbivores(struct World *world) { return world->cached_herbivores; }

int32_t world_numCarnivores(struct World *world) { return world->cached_carnivores; }

int32_t world_numAgents(struct World *world) {
  if (world->closed && world->agents.size == 0) {
    printf("Population is extinct at epoch %i\n", world->current_epoch);
    exit(1);
  }
  return world->agents.size;
}

float world_getTotalFood(struct World *world) { return foodGrid_getTotalFood(&world->foodGrid); }

// ============================================================================
// Spatial Grid Sorting
// ============================================================================

void world_sortGrid(struct World *world) {
  size_t n = world->agents.size;
  if (n > world->sorted_capacity) {
    world->sorted_capacity = n * 2;
    world->sorted_agents = realloc(world->sorted_agents, world->sorted_capacity * sizeof(struct Agent *));
  }

  // Counting sort by spatial bucket — O(N), groups agents into contiguous
  // bucket ranges so the 9-bucket scan in agent_set_inputs is cache-friendly.
  // Zero the full array first so any counting-sort bug or hash collision
  // leaves NULL instead of a stale pointer from a previous (larger) population.
  memset(world->sorted_agents, 0, world->sorted_capacity * sizeof(struct Agent *));

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

// Copy all rendering-relevant fields from agent → contiguous render cache.
// Called from output processor (pays the cache-miss cost once per frame per
// agent) and from flush_staging (agent birth / swap-on-delete).
// vkdraw's update_agents then just memcpy's the entire cache to the SSBO.
static void world_update_render_entry(struct World *world, size_t idx) {
  struct Agent *a = world->agents.agents[idx];
  AgentInstance *r = &((AgentInstance *)world->agent_render_data)[idx];
  r->pos_x = a->pos.x;
  r->pos_y = a->pos.y;
  r->color_r = a->red;
  r->color_g = a->gre;
  r->color_b = a->blu;
  r->angle = a->angle;
  r->health = a->health;
  r->herbivore = a->herbivore;
  r->soundmul = a->soundmul;
  r->spike_length = a->spikeLength;
  r->boost = a->boost ? 1 : 0;
  r->select_flag = 0; // patched by vkdraw per select/movie agent
  r->indicator_r = a->ir;
  r->indicator_g = a->ig;
  r->indicator_b = a->ib;
  r->indicator_size = a->indicator;
}

// Full rebuild of render cache (called after world load, before first frame)
void world_render_populate_all(struct World *world) {
  // Ensure flat I/O arrays are allocated
  if (!world->agent_inputs) {
    world->agent_inputs = calloc(world->agents.allocated * AGENT_INPUT_FLOATS, sizeof(float));
  }
  if (!world->agent_render_data) {
    world->agent_render_capacity = world->agents.allocated;
    world->agent_render_data = calloc(world->agent_render_capacity, sizeof(AgentInstance));
  }
  for (size_t i = 0; i < world->agents.size; i++)
    world_update_render_entry(world, i);
}

// Range-based version — called by lock-free cursor dispatch.
void agent_output_processor_range(struct World *world, uint32_t start, uint32_t end, uint32_t read_slot) {
  VKState *vk = world->brain_gpu;
  for (uint32_t i = start; i < end; i++) {
    struct Agent *a = world->agents.agents[i];
    if (!a)
      continue;

    float *in_ptr = world->agent_inputs + i * AGENT_INPUT_FLOATS;

    // Read GPU outputs directly from mapped buffer (host-coherent, compute done)
    const float *gpu_out = NULL;
    if (vk && a->brain_chunk != ~0u) {
      BrainChunk *c = &vk->chunks[a->brain_chunk];
      gpu_out = (const float *)c->outputs[read_slot].mapped + a->brain_index * BRAIN_OUTPUT_SIZE;
      // Recurrence: copy from GPU output to input staging for next frame
      memcpy(in_ptr + 18, gpu_out + 18, (BRAIN_INPUT_SIZE - 18) * sizeof(float));
    }

    // Agent fields from GPU output (or zero if no GPU)
    a->w1 = gpu_out ? gpu_out[0] : 0.0f;
    a->w2 = gpu_out ? gpu_out[1] : 0.0f;
    a->red = gpu_out ? fmaxf(gpu_out[2], 0.15f) : 0.0f;
    a->gre = gpu_out ? fmaxf(gpu_out[3], 0.15f) : 0.0f;
    a->blu = gpu_out ? fmaxf(gpu_out[4], 0.15f) : 0.0f;
    a->boost = gpu_out ? (gpu_out[6] > 0.5f) : 0;
    a->soundmul = gpu_out ? gpu_out[7] : 1.0f;
    a->give = gpu_out ? gpu_out[8] : 0.0f;

    float greadj = fmaxf(0.0f, a->herbivore - 0.5f);
    float redadj = fmaxf(0.0f, 0.5f - a->herbivore);
    a->gre = fminf(a->gre + greadj, 1.0f);
    a->red = fminf(a->red + redadj, 1.0f);

    float g = gpu_out ? gpu_out[5] : 0.0f;
    if (a->spikeLength < g)
      a->spikeLength += SPIKESPEED;
    else if (a->spikeLength > g)
      a->spikeLength = g;

    // Wheel orientation: cos(a+π/2)=−sin(a), sin(a+π/2)=cos(a)
    float halfR = BOTRADIUS * 0.5f;
    float sina = cosf(a->angle), cosa = -sinf(a->angle);
    float vx = halfR * cosa, vy = halfR * sina;
    float w1px = a->pos.x + vx, w1py = a->pos.y + vy;
    float w2px = a->pos.x - vx, w2py = a->pos.y - vy;

    float BW1 = BOTSPEED * a->w1;
    float BW2 = BOTSPEED * a->w2;
    if (a->boost) {
      BW1 *= BOOSTSIZEMULT;
      BW2 *= BOOSTSIZEMULT;
    }

    float vvx = w2px - a->pos.x, vvy = w2py - a->pos.y;
    float bw1s = -BW1, bw1c = 1.0f - BW1 * BW1 * 0.5f;
    float nvx = vvx * bw1c - vvy * bw1s, nvy = vvx * bw1s + vvy * bw1c;
    a->pos.x = w2px - nvx;
    a->pos.y = w2py - nvy;
    a->angle -= BW1;
    if (a->angle < (float)-M_PI)
      a->angle = (float)M_PI - ((float)-M_PI - a->angle);

    vvx = a->pos.x - w1px;
    vvy = a->pos.y - w1py;
    float bw2s = BW2, bw2c = 1.0f - BW2 * BW2 * 0.5f;
    nvx = vvx * bw2c - vvy * bw2s;
    nvy = vvx * bw2s + vvy * bw2c;
    a->pos.x = w1px + nvx;
    a->pos.y = w1py + nvy;
    a->angle += BW2;
    if (a->angle > (float)M_PI)
      a->angle = (float)-M_PI + (a->angle - (float)M_PI);

    // Food intake — record request, applied single-threaded after dispatch
    // (avoids data race on foodGrid.food[y][x].amt and foodGrid_place's pivot)
    a->food_request = 0.0f;
    int32_t cx = (int32_t)a->pos.x / CZ;
    int32_t cy = (int32_t)a->pos.y / CZ;
    if ((uint32_t)cx < (uint32_t)FOOD_SQUARES_WIDTH && (uint32_t)cy < (uint32_t)FOOD_SQUARES_HEIGHT &&
        world->foodGrid.food[cy][cx].amt > 0.0f && a->health < 2.0f && a->herbivore > 0.1f) {
      float to_take = FOODINTAKE;
      float speedmul = ((1.0f - fabsf(a->w1)) + (1.0f - fabsf(a->w2))) * 0.25f + 0.5f;
      to_take *= speedmul * a->herbivore * a->herbivore;
      a->food_request = to_take;
    }

    a->rep = 0;

    if (a->repcounter < 0.0f && a->health > REP_MIN_HEALTH && randf(0, 1) < 0.05333f) {
      a->health -= a->health / ((float)BABIES + 1.0f);
      a->rep = 1;
      a->repcounter = a->herbivore * randf(REPRATEH - 0.1f, REPRATEH + 0.1f) +
                      (1.0f - a->herbivore) * randf(REPRATEC - 0.1f, REPRATEC + 0.1f);
    }

    // Phase 2: apply accumulated cross-agent interactions (was a separate loop)
    a->health += a->pending_damage;
    if (a->pending_spiked)
      a->spiked = 1;
    a->pending_damage = 0.0f;
    a->pending_spiked = 0;

    // Health decay
    float healthloss = LOSS_BASE;
    if (a->age > 500.0f)
      healthloss += LOSS_AGE * ((a->age - 500.0f) * 0.004f);
    float wavg = (fabsf(a->w1) + fabsf(a->w2)) * BOTSPEED;
    healthloss += LOSS_SPEED * (a->boost ? wavg * 0.5f : wavg);
    if (a->boost)
      healthloss += LOSS_BOOST;
    healthloss += LOSS_SHOUTING * a->soundmul;
    a->health -= healthloss;

    if (a->health > 2.0f)
      a->health = 2.0f;
    if (a->health < 0.0f)
      a->health = 0.0f;

    // Update contiguous render cache (pays cache-miss once, then
    // vkdraw just memcpy's this dense array to the GPU SSBO)
    if (world->agent_render_data)
      world_update_render_entry(world, i);
  }
}

void agent_set_inputs(struct World *world, struct Agent *a, struct BucketList buckets_to_check, size_t agent_idx) {
  float *in_ptr = world->agent_inputs + agent_idx * AGENT_INPUT_FLOATS;

  a->spiked = 0;
  a->indicator = fmaxf(a->indicator - 1.0f, 0.0f);

  if (world->modcounter % 100 == 0)
    a->age++;

  // Food sensor
  int32_t cx = (int32_t)a->pos.x / CZ;
  int32_t cy = (int32_t)a->pos.y / CZ;
  in_ptr[4] = 0.0f;
  if ((uint32_t)cx < (uint32_t)FOOD_SQUARES_WIDTH && (uint32_t)cy < (uint32_t)FOOD_SQUARES_HEIGHT)
    in_ptr[4] = world->foodGrid.food[cy][cx].amt / FOODMAX;

  // Accumulators
  float p1 = 0, r1 = 0, g1 = 0, b1 = 0;
  float p2 = 0, r2 = 0, g2 = 0, b2 = 0;
  float soaccum = 0, smaccum = 0, hearaccum = 0, blood = 0;
  int nearby_count = 0;
  float ratio_sum = 0;

  // Precompute per-agent constants (constant for this frame)
  float acos = cosf(a->angle), asin = sinf(a->angle);
  // Cone axes: cos/sin of eye directions (a ± π/16) via angle-sum identities
  float leye_cx = acos * COS_PI16 + asin * SIN_PI16; // cos(a - π/16)
  float leye_cy = asin * COS_PI16 - acos * SIN_PI16; // sin(a - π/16)
  float reye_cx = acos * COS_PI16 - asin * SIN_PI16; // cos(a + π/16)
  float reye_cy = asin * COS_PI16 + acos * SIN_PI16; // sin(a + π/16)
  float invDIST = 1.0f / DIST;
  float invDIST2 = 1.0f / (DIST * DIST);
  float invGROUP2 = 1.0f / (DIST_GROUPING * DIST_GROUPING);
  float DIST2 = DIST * DIST;
  float GROUP2 = DIST_GROUPING * DIST_GROUPING;
  float SHARE2 = FOOD_SHARING_DISTANCE * FOOD_SHARING_DISTANCE;
  float COLLISION_RADIUS = BOTRADIUS * 1.9f;
  float COLLISION2 = COLLISION_RADIUS * COLLISION_RADIUS;
  float DOT_SKIP = -0.5f;                // cos(120°) — skip eye/blood for agents behind
  float DOT_SKIP2 = DOT_SKIP * DOT_SKIP; // 0.25 = cos²(120°)
  float EYE_RANGE2 = DIST2 * 0.36f;      // (0.6*DIST)² — skip angle math for distant agents

  for (size_t j = 0; j < 9; j++) {
    size_t bucket = buckets_to_check.buckets[j];
    struct AgentRange r = get_agent_range(world, bucket);

    for (size_t idx = r.start; idx < r.end; idx++) {
      struct Agent *a2 = world->sorted_agents[idx];
      if (!a2 || a == a2)
        continue;

      float dx = a2->pos.x - a->pos.x;
      float dy = a2->pos.y - a->pos.y;
      float d2 = dx * dx + dy * dy;
      if (d2 > DIST2)
        continue;

      // Squared-distance falloffs for smell/hearing/grouping (avoid sqrt)
      float df2 = 1.0f - d2 * invDIST2; // = 1 - (d/DIST)²

      // Smell & hearing (cheap, always evaluated)
      smaccum += 0.3f * df2;
      hearaccum += a2->soundmul * df2;

      // Grouping proximity
      if (d2 < GROUP2) {
        float ratio = 1.0f - d2 * invGROUP2; // = 1 - (d/GROUP)²
        nearby_count++;
        ratio_sum += ratio;
        if (5.0f * ratio > a->indicator) {
          a->indicator = 5.0f * ratio;
          a->ir = 0.5f;
          a->ig = 0.5f;
          a->ib = 0.5f;
        }
        soaccum += 0.4f * df2 * fmaxf(fabsf(a2->w1), fabsf(a2->w2));
      }

      // Eye / blood / collision — skip if more than 120° behind
      float dot = acos * dx + asin * dy;
      if (dot < 0.0f && (dot * dot) > DOT_SKIP2 * d2)
        goto skip_vision;

      // Cone pre-tests (no atan2f) for eye-range neighbors
      if (d2 < EYE_RANGE2) {
        // Need sqrt for angle calculations in eye sensors
        float d = sqrtf(d2);
        float dist_falloff = 1.0f - d * invDIST;

        float leye_dot = leye_cx * dx + leye_cy * dy;
        float reye_dot = reye_cx * dx + reye_cy * dy;
        bool leye_pass = leye_dot > 0.0f && fabsf(leye_cx * dy - leye_cy * dx) < leye_dot * TAN_3PI16;
        bool reye_pass = reye_dot > 0.0f && fabsf(reye_cx * dy - reye_cy * dx) < reye_dot * TAN_3PI16;
        bool blood_pass = dot > 0.0f && fabsf(asin * dx - acos * dy) < dot * TAN_3PI16;

        if (leye_pass || reye_pass || blood_pass) {
          float cross = asin * dx - acos * dy;
          float ang_diff = approx_atan2(cross, dot);

          if (leye_pass) {
            float diff = ang_diff - PI8; // left eye centered at -π/16
            float mul = EYE_SENSITIVITY * ((PI38 - fabsf(diff)) / PI38) * dist_falloff;
            float p = mul * (d * invDIST);
            p1 += p;
            r1 += mul * a2->red;
            g1 += mul * a2->gre;
            b1 += mul * a2->blu;
          }
          if (reye_pass) {
            float diff = ang_diff + PI8; // right eye centered at +π/16
            float mul = EYE_SENSITIVITY * ((PI38 - fabsf(diff)) / PI38) * dist_falloff;
            float p = mul * (d * invDIST);
            p2 += p;
            r2 += mul * a2->red;
            g2 += mul * a2->gre;
            b2 += mul * a2->blu;
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
          a->pending_health_delta -= FOODTRANSFER;
        if (a2->give > 0.5f && a->health < 2.0f)
          a->pending_health_delta += FOODTRANSFER;
      }

      // Spike collision
      if (d2 < COLLISION2) {
        float diff = a->angle - approx_atan2(dy, dx);
        if (diff < -(float)M_PI)
          diff += 2.0f * (float)M_PI;
        if (diff > (float)M_PI)
          diff -= 2.0f * (float)M_PI;
        diff = fabsf(diff);

        if (diff < (float)M_PI / 4.0f) {
          float DMG =
              SPIKEMULT * a->spikeLength * (1.0f - a->herbivore) * fmaxf(fabsf(a->w1), fabsf(a->w2)) * BOOSTSIZEMULT;
          if (DMG > 1.25f) {
            if (a->spike_outbox_count < SPIKE_OUTBOX_SIZE) {
              a->spike_outbox[a->spike_outbox_count].target = a2;
              a->spike_outbox[a->spike_outbox_count].damage = DMG;
              a->spike_outbox_count++;
            }
            a->spikeLength = fmaxf(a->spikeLength - DMG, 0.0f);
            if (10.0f * DMG > a->indicator) {
              a->indicator = 10.0f * DMG;
              a->ir = 1.0f;
              a->ig = 1.0f;
              a->ib = 0.0f;
            }
          }
        }
      }
    skip_vision:;
    }
  }

  // Grouping health gain
  {
    float effective_ratio = fminf((float)ratio_sum, CROWDING_LIMIT * 0.6f);
    float gain = GAIN_GROUPING * effective_ratio;
    int excess = nearby_count - CROWDING_LIMIT;
    float penalty = (excess > 0) ? CROWDING_PENALTY * (float)(excess * excess) : 0.0f;
    a->pending_health_delta += gain - penalty;
  }

  {
    float effective_health = a->health + a->pending_health_delta;
    if (effective_health > 2.0f)
      effective_health = 2.0f;
    in_ptr[11] = cap(effective_health * 0.5f);
  }

  in_ptr[0] = cap(p1);
  in_ptr[1] = cap(r1);
  in_ptr[2] = cap(g1);
  in_ptr[3] = cap(b1);
  in_ptr[5] = cap(p2);
  in_ptr[6] = cap(r2);
  in_ptr[7] = cap(g2);
  in_ptr[8] = cap(b2);
  in_ptr[9] = cap(soaccum);
  in_ptr[10] = cap(smaccum);
  in_ptr[12] = fabsf(sinf((float)world->modcounter / a->clockf1));
  in_ptr[13] = fabsf(sinf((float)world->modcounter / a->clockf2));
  in_ptr[14] = cap(hearaccum);
  in_ptr[15] = cap(blood);
  in_ptr[16] = cap((float)a->touch);
  if (randf(0.0f, 1.0f) > 0.95f)
    in_ptr[17] = randf(0.0f, 1.0f);
}

// Range-based input processor — called by lock-free cursor dispatch.
void agent_input_processor_range(struct World *world, uint32_t start, uint32_t end, uint32_t write_slot) {
  VKState *vk = world->brain_gpu;
  for (uint32_t i = start; i < end; i++) {
    struct Agent *a = world->agents.agents[i];
    if (!a)
      continue;
    struct BucketList buckets_to_check = get_buckets_from_pos(a->pos.x, a->pos.y);
    agent_set_inputs(world, a, buckets_to_check, i);

    if (vk && a->brain_chunk != ~0u) {
      BrainChunk *c = &vk->chunks[a->brain_chunk];
      memcpy((float *)c->inputs[write_slot].mapped + a->brain_index * BRAIN_INPUT_SIZE,
             world->agent_inputs + i * AGENT_INPUT_FLOATS, BRAIN_INPUT_SIZE * sizeof(float));
    }
  }
}

// Seed both double-buffer input slots with valid data after world load/init.
// Must be called after world_sortGrid (for spatial neighbor queries) and
// after brain weights have been uploaded to GPU.
void world_seed_inputs(struct World *world) {
  VKState *vk = world->brain_gpu;
  if (!vk)
    return;
  uint32_t saved_slot = world->brain_slot;
  // Populate slot 0
  world->brain_slot = 1;
  world_setInputsRunBrain(world);
  // Populate slot 1
  world->brain_slot = 0;
  world_setInputsRunBrain(world);
  // Restore
  world->brain_slot = saved_slot;
}
