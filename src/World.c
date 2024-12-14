
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <assert.h>

#include "World.h"
#include "helpers.h"
#include "queue.h"
#include "settings.h"
#include "vec.h"
#include "vec2f.h"

#define BATCH_SIZE 64

static void timespec_diff(struct timespec *result, struct timespec *start,
                          struct timespec *stop) {
  if ((stop->tv_nsec - start->tv_nsec) < 0) {
    result->tv_sec = stop->tv_sec - start->tv_sec - 1;
    result->tv_nsec = stop->tv_nsec - start->tv_nsec + 1000000000;
  } else {
    result->tv_sec = stop->tv_sec - start->tv_sec;
    result->tv_nsec = stop->tv_nsec - start->tv_nsec;
  }
}

/* Grow food in one square
 *
 * Returns the amount the food square has grown.
 */
static float world_growOneFoodSafe(struct World *world, float amt, int32_t x, int32_t y) {
  // check if food square is inside the world
  if (x < 0 || x >= world->FW || y < 0 || y >= world->FH) {
    return 0.0f;
  }

  float to_grow = fminf(fmaxf(FOODMAX - world->food[x][y], 0.0f), amt);

  world->food[x][y] += to_grow;
  return to_grow;
}

// Grow food around square
static void world_growFood(struct World *world, int32_t x, int32_t y) {
  if (x < 0 || x >= world->FW || y < 0 || y >= world->FH) {
    return;
  }

  // Keep track of how much we are growing.
  float to_grow = FOODGROWTH;
  // Grow ourselves unconditionally
  to_grow -= world_growOneFoodSafe(world, to_grow, x, y);

  // Otherwise try to grow out randomly
  int tries = 16;
  while (tries-- > 0 && to_grow > 0.0f) {
    int xx = randi(x - 1, x + 2);
    int yy = randi(y - 1, y + 2);
    to_grow -= world_growOneFoodSafe(world, to_grow, xx, yy);
  }
}

static void world_update_food(struct World *world) {
  if (world->modcounter % FOODADDFREQ == 0) {
    size_t fx = randi(0, world->FW);
    size_t fy = randi(0, world->FH);
    world->food[fx][fy] = FOODMAX;
  }

  int to_grow = FOODSQUARES;
  int attempts = FOODSQUARES * 10;

  while (attempts-- > 0 && to_grow > 0) {
    size_t fx = randi(0, world->FW);
    size_t fy = randi(0, world->FH);
    // only grow if not dead
    if (world->food[fx][fy] >= 0.001f) {
      to_grow--;
      world_growFood(world, fx, fy);
    } else {
      world->food[fx][fy] = 0.0f;
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

  float deltat =
      (float)ts_delta.tv_sec + ((float)ts_delta.tv_nsec / 1000000000.0f);
  float totaldeltat = (float)ts_totaldelta.tv_sec +
                      ((float)ts_totaldelta.tv_nsec / 1000000000.0f);

  int32_t carnivores = world_numCarnivores(world);
  int32_t herbivores = world_numHerbivores(world);

  printf("Simulation Running... Epoch: %d - Next: %d%% - Agents: %i (C: %i H: %i) - FPS: "
         "%.1f - Time: %.2f sec     \r",
         world->current_epoch, world->modcounter / 100,
         (int32_t)world->agents.size,
         carnivores,
         herbivores,
         (float)reportInterval / deltat,
         totaldeltat);
  fflush(stdout);

  world->startTime = endTime;

  // Check if simulation needs to end

  if (world->current_epoch >= MAX_EPOCHS ||
      (endTime.tv_sec - world->totalStartTime.tv_sec) >= MAX_SECONDS) {
    world->stopSim = 1;
  }
}

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
  int64_t this_grid_x = (int64_t) (x / DIST);
  int64_t this_grid_y = (int64_t) (y / DIST);

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
    ret.start = world->agent_grid[bucket_idx-1];
  }

  ret.end = world->agent_grid[bucket_idx];

  return ret;
}

void world_flush_staging(struct World *world) {
  // Check for and delete dead agents
  for (size_t i = 0; i < world->agents.size; i++) {
    struct Agent *a = world->agents.agents[i];

    // Cull any dead agents
    if (a->health <= 0.0f) {
      // The i-- is very important here, since we need to retry the current
      // iteration because it was replaced with a different agent
      free_brain(a->brain);
      free(a);
      avec_delete(&world->agents, i--);
      continue;
    }
  }

  // Add agents from staging vector
  for (size_t i = 0; i < world->agents_staging.size; i++) {
    avec_push_back(&world->agents, world->agents_staging.agents[i]);
  }
  world->agents_staging.size = 0;
}

void world_init(struct World *world, int loadFromFile) {
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
  world->FW = WIDTH / CZ;
  world->FH = HEIGHT / CZ;

  clock_gettime(CLOCK_MONOTONIC, &world->startTime);
  // Track total running time:
  clock_gettime(CLOCK_MONOTONIC, &world->totalStartTime);

  avec_init(&world->agents, 10);
  avec_init(&world->agents_staging, 10);
  for (size_t x = 0; x < world->FW; x++) {
    for (size_t y = 0; y < world->FH; y++) {
      world->food[x][y] = 0;
    }
  }

  if (loadFromFile == 0) {
    size_t numbots = NUMBOTS;

    // create the bots but with 20% more carnivores, to give them head start
    if (numbots > 100) {
      printf("Adding bots, this may take a while...\n");
    }

    world_addRandomBots(world, (int32_t)numbots * .8);
    for (int32_t i = 0; i < (int32_t)numbots * .2; ++i)
      world_addCarnivore(world);

    printf("Initializing food...\n");

    for (int i = 0; i < FOOD_INIT_TICKS; i++) {
      world_update_food(world);
      world->modcounter++;
    }
    world->modcounter = 0;
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
      struct Agent *a2 = world->agents.agents[agent_idx];

      // Ignore ourselves
      if (a == a2) {
        continue;
      }

      float dist2 = vector2f_dist2(&a->pos, &a2->pos);
      if (dist2 <= FOOD_DISTRIBUTION_RADIUS*FOOD_DISTRIBUTION_RADIUS) {
        // Only distribute to alive agents that are > 90% carnivores
        if (a2->herbivore < 0.5f && a2->health > 0.0f) {
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

  // Base health add, slightly slanted towards eating herbivores
  // Also cap minimum to allow for scavenging
  float health_add = fmaxf(0.4f, a->health) * ((a->herbivore + 0.05f) * 2.0f);

  // bonus for hunting in groups
  health_add *= 1.0f + fminf(0.5f * (num_to_dist_body / 8.0f), 0.5f);

  // Divide for each agent
  health_add /= (float)num_to_dist_body;

  float rep_sub = 4.0f * health_add;

  for (size_t j = 0; j < num_to_dist_body; j++) {
    struct Agent *a2 = dist_agents[j];
    // printf("n: %d, carn: %f, h+: %f, r-: %f\n", num_to_dist_body, 1.0f -
    // a2->herbivore, health_add, rep_sub);

    float finishing_blow_multiplier = 1.0f;
    if (a2->attacked_this_frame == 1) {
      finishing_blow_multiplier = 2.0f;
    }

    a2->health += health_add * (1.0f - a2->herbivore) * finishing_blow_multiplier;
    a2->repcounter -= rep_sub * (1.0f - a2->herbivore) * finishing_blow_multiplier;

    if (a2->health > 2.0f)
      a2->health = 2.0f; // cap it!

    agent_initevent(a2, health_add * 50.0f, 1.0f, 0.0f,
                    0.0f); // red means they ate! nice
  }
}

void world_update(struct World *world) {
  // Increment Tick
  world->modcounter++;

  // Increment Epoch
  if (world->modcounter >= 10000) {
    world->modcounter = 0;
    world->current_epoch++;
  }

  // Update GUI every REPORTS_PER_EPOCH amount:
  if (REPORTS_PER_EPOCH > 0 && (world->modcounter % reportInterval == 0)) {
    world_update_gui(world);
  }

  world_update_food(world);

  // sort agents into grid
  world_sortGrid(world);

  // give input to every agent. Sets in[] array. Runs brain
  world_setInputsRunBrain(world);

  // read output and process consequences of bots on environment. requires out[]
  world_processOutputs(world);

  struct Agent *newMovieAgent = NULL;
  struct Agent *prevMovieAgent = NULL;
  int32_t newMostChildren = -1;
  int32_t prevMostChildren = -1;

  // Some things need to be done single threaded
  for (int i = 0; i < world->agents.size; i++) {
    struct Agent *a = world->agents.agents[i];
    if (a->health <= 0.0f) {
      // Distribute dead agents to nearby carnivores
      world_dist_dead_agent(world, i);
    }

    if (a->rep) {
      world_reproduce(world, a);
      a->rep = 0;
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

  // Flush any agents in the staging array, and clean up dead bots
  world_flush_staging(world);
}

void world_setInputsRunBrain(struct World *world) {
  struct AgentQueueItem
      agentQueueItems[((world->agents.size / BATCH_SIZE) + 1)];
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
  // printf("input done, %zu size, %zu work items\n", world->queue->size,
  // world->queue->num_work_items);
}

void world_processOutputs(struct World *world) {
  struct AgentQueueItem
      agentQueueItems[((world->agents.size / BATCH_SIZE) + 1)];
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

// void world_addNewByCrossover(struct World *world) {
//   // find two success cases
//   size_t i1 = randi(0, world->agents.size);
//   size_t i2 = randi(0, world->agents.size);
//   for (size_t i = 0; i < world->agents.size; i++) {
//     if (world->agents.agents[i].age > world->agents.agents[i1].age &&
//         randf(0, 1) < 0.1f) {
//       i1 = i;
//     }
//     if (world->agents.agents[i].age > world->agents.agents[i2].age &&
//         randf(0, 1) < 0.1f && i != i1) {
//       i2 = i;
//     }
//   }
//
//   struct Agent *a1 = &world->agents.agents[i1];
//   struct Agent *a2 = &world->agents.agents[i2];
//
//   // cross brains
//   struct Agent anew;
//   agent_init(&anew);
//   agent_crossover(&anew, a1, a2);
//
//   // maybe do mutation here? I dont know. So far its only crossover
//   avec_push_back(&world->agents_staging, anew);
//
//   world->numAgentsAdded++; // record in report
// }

void world_reproduce(struct World *world, struct Agent *a) {
  agent_initevent(a, 30, 0.0f, 0.8f,
                  0.0f); // green event means agent reproduced.

  // the parent splits it health evenly with all of its babies
  float health_per_agent = a->health / ((float)BABIES + 1.0f);
  a->repcounter =
      a->herbivore * randf(REPRATEH - 0.1f, REPRATEH + 0.1f) +
      (1.0f - a->herbivore) * randf(REPRATEC - 0.1f, REPRATEC + 0.1f);
  a->health = health_per_agent;

  for (int32_t i = 0; i < BABIES; i++) {
    struct Agent *a2 = malloc(sizeof(struct Agent));
    agent_init(a2);
    agent_reproduce(a2, a);
    a2->health = health_per_agent;
    avec_push_back(&world->agents_staging, a2);
  }
}

void world_writeReport(struct World *world) {
  // save all kinds of nice data stuff
  int32_t numherb = 0;
  int32_t numcarn = 0;
  int32_t topherb = 0;
  int32_t topcarn = 0;
  int32_t total_age = 0;
  int32_t avg_age = 0;
  float epoch_decimal =
      (float)world->modcounter / 10000.0f + (float)world->current_epoch;

  // Count number of herb, carn and top of each
  for (size_t i = 0; i < world->agents.size; i++) {
    if (world->agents.agents[i]->herbivore > 0.5f)
      numherb++;
    else
      numcarn++;

    if (world->agents.agents[i]->herbivore > 0.5f &&
        world->agents.agents[i]->gencount > topherb)
      topherb = world->agents.agents[i]->gencount;
    if (world->agents.agents[i]->herbivore < 0.5f &&
        world->agents.agents[i]->gencount > topcarn)
      topcarn = world->agents.agents[i]->gencount;

    // Average Age:
    total_age += world->agents.agents[i]->age;
  }
  if (world->agents.size > 0) {
    avg_age = total_age / world->agents.size;
  }

  // Compute Standard Devitation of every weight in every agents brain
  float total_std_dev = 0.0f;
  float total_mean_std_dev;

  total_mean_std_dev =
      total_std_dev - 200.0f; // reduce by 200 for graph readability

  FILE *fp = fopen("report.csv", "a");

  fprintf(fp, "%f,%i,%i,%i,%i,%i,%i,%i\n", epoch_decimal, numherb, numcarn,
          topherb, topcarn, (int32_t)total_mean_std_dev, avg_age,
          world->numAgentsAdded);

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

void world_processMouse(struct World *world, int32_t button, int32_t state,
                        int32_t x, int32_t y) {
  if (state == 0) {
    float mind = 1e10;
    size_t mini = -1;
    float d;

    for (size_t i = 0; i < world->agents.size; i++) {
      d = powf((float)x - world->agents.agents[i]->pos.x, 2.0f) +
          powf((float)y - world->agents.agents[i]->pos.y, 2.0f);
      if (d < mind) {
        mind = d;
        mini = i;
      }
    }
    // toggle selection of this agent
    for (size_t i = 0; i < world->agents.size; i++) {
      if (i != mini) {
        world->agents.agents[i]->selectflag = 0;
      } else {
        world->agents.agents[i]->selectflag =
            world->agents.agents[mini]->selectflag ? 0 : 1;
      }
    }

    // agents[mini].printSelf();
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

void world_sortGrid(struct World *world) {
  for (size_t i = 1; i < world->agents.size; i++) {
    struct Agent *key_agent = world->agents.agents[i];

    // Integer truncation means this cuts off at the whole number boundary
    int64_t this_grid_x = (int64_t) (key_agent->pos.x / DIST);
    int64_t this_grid_y = (int64_t) (key_agent->pos.y / DIST);
    size_t key_grid_index = get_bucket_from_pos(this_grid_x, this_grid_y);

    long j = i - 1;
    while (j >= 0) {
      struct Agent *a2 = world->agents.agents[j];

      // Integer truncation means this cuts off at the whole number boundary
      int64_t next_grid_x = (int64_t) (a2->pos.x / DIST);
      int64_t next_grid_y = (int64_t) (a2->pos.y / DIST);
      size_t next_grid_index = get_bucket_from_pos(next_grid_x, next_grid_y);

      if (next_grid_index <= key_grid_index) {
        break;
      }

      world->agents.agents[j + 1] = world->agents.agents[j];
      j--;
    }

    world->agents.agents[j + 1] = key_agent;
  }

  // Construct grid
  size_t current_grid_index = 0;
  for (size_t i = 0; i < world->agents.size; i++) {
    struct Agent *a = world->agents.agents[i];

    // Integer truncation means this cuts off at the whole number boundary
    int64_t grid_x = (int64_t) (a->pos.x / DIST);
    int64_t grid_y = (int64_t) (a->pos.y / DIST);
    size_t grid_index = get_bucket_from_pos(grid_x, grid_y);

    while (grid_index > current_grid_index) {
      world->agent_grid[current_grid_index++] = i;
    }
  }

  while (current_grid_index < AGENT_BUCKETS) {
    world->agent_grid[current_grid_index] =
        world->agent_grid[current_grid_index - 1];
    current_grid_index++;
    // printf("%li: %li\n", current_grid_index, i);
  }
}

void agent_output_processor(void *arg) {
  struct AgentQueueItem *aqi = (struct AgentQueueItem *)arg;
  assert(aqi != NULL);
  struct World *world = aqi->world;
  assert(world != NULL);

  for (size_t i = aqi->start; i < aqi->end; i++) {
    assert(aqi->start <= i < aqi->end);
    struct Agent *a = world->agents.agents[i];
    assert(a != NULL);

    a->w1 = a->out[0]; //-(2*a->out[0]-1);
    a->w2 = a->out[1]; //-(2*a->out[1]-1);
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

    // spike length should slowly tend towards out[5]
    float g = a->out[5];
    if (a->spikeLength < g)
      a->spikeLength += SPIKESPEED;
    else if (a->spikeLength > g)
      a->spikeLength = g; // its easy to retract spike, just hard to put it up.

    a->repcounter = fmaxf(0.0f, a->repcounter - 0.0001f);

    // Move bots

    struct Vector2f v;
    vector2f_init(&v, BOTRADIUS / 2, 0);
    vector2f_rotate(&v, a->angle + (float)M_PI / 2.0f);

    struct Vector2f w1p;
    struct Vector2f w2p;
    vector2f_add(&w1p, &a->pos, &v); // wheel positions
    vector2f_sub(&w2p, &a->pos, &v); // wheel positions

    float BW1 = BOTSPEED * a->w1; // bot speed * wheel speed
    float BW2 = BOTSPEED * a->w2;

    if (a->boost) {
      BW1 *= BOOSTSIZEMULT;
      BW2 *= BOOSTSIZEMULT;
    }

    // move bots
    struct Vector2f vv;
    vector2f_sub(&vv, &w2p, &a->pos);
    vector2f_rotate(&vv, -BW1);

    vector2f_sub(&a->pos, &w2p, &vv);
    a->angle -= BW1;
    if (a->angle < (float)-M_PI)
      a->angle = (float)M_PI - ((float)-M_PI - a->angle);
    vector2f_sub(&vv, &a->pos, &w1p);
    vector2f_rotate(&vv, BW2);
    vector2f_add(&a->pos, &w1p, &vv);
    a->angle += BW2;
    if (a->angle > (float)M_PI)
      a->angle = (float)-M_PI + (a->angle - (float)M_PI);

    // wrap around the map
    /*if (a->pos.x<0) a->pos.x= WIDTH+a->pos.x;
              if (a->pos.x>=WIDTH) a->pos.x= a->pos.x-WIDTH;
              if (a->pos.y<0) a->pos.y= HEIGHT+a->pos.y;
              if (a->pos.y>=HEIGHT) a->pos.y= a->pos.y-HEIGHT;*/

    // process food intake
    if (a->health < 2.0f && a->herbivore > 0.1f) {
      int32_t cx = (int32_t)a->pos.x / CZ;
      int32_t cy = (int32_t)a->pos.y / CZ;

      if (cx >= 0 && cx < world->FW && cy >= 0 && cy < world->FH) {
        float f = world->food[cx][cy];

        // agent eats the food
        float itk = fminf(f, FOODINTAKE);
        float speedmul =
            (((1.0f - fabsf(a->w1)) + (1.0f - fabsf(a->w2))) / 2.0f) * 0.5f +
            0.5f;
        itk = itk * speedmul * a->herbivore * a->herbivore;
        a->health += itk;
        a->repcounter -= 3 * itk;
        world->food[cx][cy] -= fminf(f, itk);
      }
    }

    // Handle reproduction
    if (a->repcounter <= 0.01f && a->health > REP_MIN_HEALTH && randf(0, 1) < 0.05f) {
      // agent is healthy (REP_MIN_HEALTH) and is ready to reproduce.
      // Also inject a bit non-determinism

      a->rep = 1;
    }

    agent_process_health(a);
  }
}

void agent_set_inputs(struct World *world, struct Agent *a,
                      struct BucketList buckets_to_check) {
  // General settings
  // says that agent was not hit this turn
  a->spiked = 0;
  a->attacked_this_frame = 0;

  // process indicator used in drawing
  a->indicator = fmaxf(a->indicator - 1.0f, 0.0f);

  // Update agents age
  if (world->modcounter % 100 == 0)
    a->age++;

  // FOOD
  int32_t cx = (int32_t)a->pos.x / CZ;
  int32_t cy = (int32_t)a->pos.y / CZ;
  a->in[4] = 0.0f;
  if (cx >= 0 && cx < world->FW && cy >= 0 && cy < world->FH) {
    a->in[4] = world->food[cx][cy] / FOODMAX;
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

    // For each agent
    for (size_t agent_idx = agent_range.start; agent_idx < agent_range.end; agent_idx++) {
      struct Agent *a2 = world->agents.agents[agent_idx];

      // Ignore ourselves
      if (a == a2) {
        continue;
      }

      float d = vector2f_dist2(&a->pos, &a2->pos);

      if (d > DIST*DIST) {
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
        soaccum +=
            0.4f * ((DIST - d) / DIST) * (fmaxf(fabsf(a2->w1), fabsf(a2->w2)));
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
        float mul1 =
            EYE_SENSITIVITY * ((PI38 - diff1) / PI38) * ((DIST - d) / DIST);
        // float mul1= 100*((DIST-d)/DIST);
        p1 += mul1 * (d / DIST);
        r1 += mul1 * a2->red;
        g1 += mul1 * a2->gre;
        b1 += mul1 * a2->blu;
      }

      if (diff2 < PI38) {
        // we see this agent with left eye. Accumulate info
        float mul2 =
            EYE_SENSITIVITY * ((PI38 - diff2) / PI38) * ((DIST - d) / DIST);
        // float mul2= 100*((DIST-d)/DIST);
        p2 += mul2 * (d / DIST);
        r2 += mul2 * a2->red;
        g2 += mul2 * a2->gre;
        b2 += mul2 * a2->blu;
      }

      if (diff4 < PI38) {
        float mul4 =
            BLOOD_SENSITIVITY * ((PI38 - diff4) / PI38) * ((DIST - d) / DIST);
        // if we can see an agent close with both eyes in front of us
        blood +=
            mul4 * (1.0f - a2->health / 2.0f); // remember: health is in [0 2]
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
      if (d < BOTRADIUS*1.9f) {
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
          float DMG = SPIKEMULT * a->spikeLength * (1.0f - a->herbivore) *
                      fmaxf(fabsf(a->w1), fabsf(a->w2)) * BOOSTSIZEMULT;

          // You have to hit hard for it to count
          if (DMG > 1.25f) {
            a2->health -= DMG;
            a->spikeLength = fmaxf(a->spikeLength - DMG, 0.0f); // retract spike back down
            a->attacked_this_frame = 1;

            agent_initevent(
                a, 10.0f * DMG, 1.0f, 1.0f,
                0.0f); // yellow event means bot has spiked other bot. nice!

            // set a flag saying that this agent was hit this turn
            a2->spiked = 1;
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

  if (a->health > 2.0f) // limit the amount of health
    a->health = 2.0f;

  // temperature varies from 0 to 1 across screen.
  // it is 0 at equator (in middle), and 1 on edges. Agents can sense
  // discomfort
  float dd = 2.0f * fabsf(a->pos.x / WIDTH - 0.5f);
  float discomfort = fabsf(dd - a->temperature_preference);

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
  a->in[11] = cap(a->health / 2.0f); // divide by 2 since health is in [0,2]
  a->in[12] = fabsf(sinf(world->modcounter / a->clockf1));
  a->in[13] = fabsf(sinf(world->modcounter / a->clockf2));
  a->in[14] = cap(hearaccum); // HEARING (other agents shouting)
  a->in[15] = cap(blood);
  a->in[16] = cap(discomfort);
  if (randf(0.0f, 1.0f) > 0.95f) {
    a->in[17] = randf(0.0f, 1.0f); // random input for bot
  }

  for (int i = 18; i < INPUTSIZE; i++) {
    a->in[i] = a->out[i];
  }
}

void agent_input_processor(void *arg) {
  struct AgentQueueItem *aqi = (struct AgentQueueItem *)arg;
  struct World *world = aqi->world;

  for (size_t i = aqi->start; i < aqi->end; i++) {
    struct Agent *a = world->agents.agents[i];
    struct BucketList buckets_to_check = get_buckets_from_pos(a->pos.x, a->pos.y);

    // printf("&world->agents.size: %zu\n", world->agents.size);
    // for (size_t j = 0; j < world->agents.size; j++) {
    //   printf("agent %zu: %i\n", j, a->pos.x);
    // }

    // printf("thread: %zu\n", pthread_self());
    // printf("world: %zu\n", world->agents.size);
    // printf("index: %zu\n", i);
    // printf("sizeof: %zu\n", sizeof(struct Agent));
    // printf("pointer: %p\n", (void*) &world->agents.agents[i]);
    agent_set_inputs(world, a, buckets_to_check);

    // printf("Got %d close agents\n", num_close_agents);

    // Now process brain
    agent_tick(a);
  }
}
