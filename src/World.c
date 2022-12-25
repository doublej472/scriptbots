#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "World.h"
#include "helpers.h"
#include "queue.h"
#include "settings.h"
#include "vec.h"
#include "vec2f.h"

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

// Grow food around square
static void world_growFood(struct World *world, int32_t x, int32_t y) {
  // check if food square is inside the world
  if (x >= 0 && x < world->FW && y >= 0 && y < world->FH &&
      world->food[x][y] < FOODMAX) {
    world->food[x][y] += FOODGROWTH;
  }
}

void world_flush_staging(struct World *world) {
  // Check for and delete dead agents
  for (size_t i = 0; i < world->agents.size; i++) {
    struct Agent *a = &world->agents.agents[i];

    // Cull any dead agents
    if (a->health <= 0) {
      // The i-- is very important here, since we need to retry the current
      // iteration because it was replaced with a different agent
      free(a->brain);
      avec_delete(&world->agents, i--);
      continue;
    }
  }
  // Add agents from staging vector
  for (size_t i = 0; i < world->agents_staging.size; i++) {
    avec_push_back(&world->agents, *avec_get(&world->agents_staging, i));
  }
  world->agents_staging.size = 0;
}

void world_init(struct World *world) {
  memset(&world->agent_grid, '\0', sizeof(size_t) * WORLD_GRID_LENGTH);

  queue_init(&world->queue);

  world->stopSim = 0;
  world->movieMode = 0;
  world->modcounter = 0;
  world->current_epoch = 0;
  world->numAgentsAdded = 0;
  world->FW = WIDTH / CZ;
  world->FH = HEIGHT / CZ;

  clock_gettime(CLOCK_MONOTONIC_RAW, &world->startTime);
  // Track total running time:
  clock_gettime(CLOCK_MONOTONIC_RAW, &world->totalStartTime);

  avec_init(&world->agents, 16);
  avec_init(&world->agents_staging, 16);

  // create the bots but with 20% more carnivores, to give them head start
  world_addRandomBots(world, (int32_t)NUMBOTS * .8);
  for (int32_t i = 0; i < (int32_t)NUMBOTS * .2; ++i)
    world_addCarnivore(world);

  // inititalize food layer
  for (int32_t x = 0; x < world->FW; x++) {
    for (int32_t y = 0; y < world->FH; y++) {

      float rand1 = randf(0, 1);
      if (rand1 > .5) {
        world->food[x][y] = rand1 * FOODMAX;
      } else {
        world->food[x][y] = 0;
      }
    }
  }

  // Decide if world if closed based on settings.h
  world->closed = CLOSED;

  // Delete the old report to start fresh
  remove("report.csv");
}

void world_printState(struct World *world) {
  printf("World State Info -----------\n");
  printf("Epoch:\t\t%i\n", world->current_epoch);
  printf("Tick:\t\t%i\n", world->modcounter);
  printf("Num Agents:%li\n", world->agents.size);
  printf("Agents Added:%i\n", world->numAgentsAdded);
  printf("----------------------------\n");
}

static void world_update_food(struct World *world) {
  // What kind of food method are we using?
  if (FOOD_MODEL == FOOD_MODEL_GROW) {
    // GROW food enviroment model
    if (world->modcounter % FOODADDFREQ == 0) {
      for (int32_t x = 0; x < world->FW; ++x) {
        for (int32_t y = 0; y < world->FH; ++y) {
          // only grow if not dead
          if (world->food[x][y] > 0) {

            // Grow current square
            world_growFood(world, x, y);

            // Grow surrounding squares sometimes and only if well grown
            if (randf(0, world->food[x][y]) > .1) {
              // Spread to surrounding squares
              world_growFood(world, x + 1, y - 1);
              world_growFood(world, x + 1, y);
              world_growFood(world, x + 1, y + 1);
              world_growFood(world, x - 1, y - 1);
              world_growFood(world, x - 1, y);
              world_growFood(world, x - 1, y + 1);
              world_growFood(world, x, y - 1);
              world_growFood(world, x, y + 1);
            }
          }
        }
      }
    }
  } else {
    // Add Random food model - default
    if (world->modcounter % FOODADDFREQ == 0) {
      world->fx = randi(0, world->FW);
      world->fy = randi(0, world->FH);
      world->food[world->fx][world->fy] = FOODMAX;
    }
  }
}

static void world_update_gui(struct World *world) {
  world_writeReport(world);

  // Update GUI
  struct timespec endTime;
  clock_gettime(CLOCK_MONOTONIC_RAW, &endTime);
  struct timespec ts_delta;
  struct timespec ts_totaldelta;

  timespec_diff(&ts_delta, &world->startTime, &endTime);
  timespec_diff(&ts_totaldelta, &world->totalStartTime, &endTime);

  float deltat = (float)ts_delta.tv_sec + ((float) ts_delta.tv_nsec / 1000000000.0f);
  float totaldeltat =
      (float)ts_totaldelta.tv_sec + ((float) ts_totaldelta.tv_nsec / 1000000000.0f);

  printf("Simulation Running... Epoch: %d - Next: %d%% - Agents: %i - FPS: "
         "%.1f - Time: %.2f sec     \r",
         world->current_epoch, world->modcounter / 100,
         (int32_t) world->agents.size, (float) reportInterval / deltat,
         totaldeltat);

  world->startTime = endTime;

  // Check if simulation needs to end

  if (world->current_epoch >= MAX_EPOCHS ||
      (endTime.tv_sec - world->totalStartTime.tv_sec) >= MAX_SECONDS) {
    world->stopSim = 1;
  }
}

static int32_t check_grid_position(struct World *world, struct Agent *a,
                                   struct Agent_d *close_agents, size_t x,
                                   size_t y, int32_t num_close_agents) {
  // If this is outside the grid then ignore
  if (x < 0 || x >= WORLD_GRID_WIDTH || y < 0 || y >= WORLD_GRID_HEIGHT) {
    return 0;
  }

  int32_t init_close_agents = num_close_agents;
  size_t this_grid_index = x + y * WORLD_GRID_WIDTH;

  size_t start = 0;
  if (this_grid_index != 0) {
    start = world->agent_grid[this_grid_index - 1];
  }

  for (size_t i = start; i < world->agent_grid[this_grid_index]; i++) {
    if (num_close_agents >= NUMBOTS_CLOSE) {
      break;
    }
    struct Agent *a2 = &world->agents.agents[i];
    if (a == a2) {
      continue;
    }

    // printf("Checking agent at index: %li\n", i);

    float d = vector2f_dist2(&a->pos, &a2->pos);

    // If we are too far away, don't even consider this agent
    if (d > DIST * DIST) {
      continue;
    }

    close_agents[num_close_agents].agent = a2;
    close_agents[num_close_agents].dist2 = d;
    num_close_agents++;
  }

  return num_close_agents - init_close_agents;
}

int32_t world_get_close_agents(struct World *world, size_t index,
                               struct Agent_d *close_agents) {
  struct Agent *a = &world->agents.agents[index];
  int32_t num_close_agents = 0;

  size_t this_grid_x = floor(a->pos.x / WORLD_GRID_SIZE);
  size_t this_grid_y = floor(a->pos.y / WORLD_GRID_SIZE);

  // Check 3x3 grid position around our current grid position
  // Check current grid position first, so we can guarantee we will find close
  // agents

  num_close_agents += check_grid_position(world, a, close_agents, this_grid_x,
                                          this_grid_y, num_close_agents);

  // Then check direct borders
  num_close_agents += check_grid_position(world, a, close_agents, this_grid_x,
                                          this_grid_y - 1, num_close_agents);
  num_close_agents += check_grid_position(world, a, close_agents, this_grid_x,
                                          this_grid_y + 1, num_close_agents);
  num_close_agents += check_grid_position(
      world, a, close_agents, this_grid_x - 1, this_grid_y, num_close_agents);
  num_close_agents += check_grid_position(
      world, a, close_agents, this_grid_x + 1, this_grid_y, num_close_agents);

  // Then check corners
  num_close_agents +=
      check_grid_position(world, a, close_agents, this_grid_x - 1,
                          this_grid_y - 1, num_close_agents);
  num_close_agents +=
      check_grid_position(world, a, close_agents, this_grid_x - 1,
                          this_grid_y + 1, num_close_agents);
  num_close_agents +=
      check_grid_position(world, a, close_agents, this_grid_x + 1,
                          this_grid_y - 1, num_close_agents);
  num_close_agents +=
      check_grid_position(world, a, close_agents, this_grid_x + 1,
                          this_grid_y + 1, num_close_agents);

  return num_close_agents;
}

void world_dist_dead_agent(struct World *world, size_t i) {
  // distribute its food. It will be erased soon
  // since the close_agents array is sorted, just get the index where we
  // should stop distributing the body, then the number of carnivores around

  struct Agent_d close_agents[NUMBOTS_CLOSE];
  int num_close_agents = world_get_close_agents(world, i, close_agents);

  struct Agent *dist_agents[FOOD_DISTRIBUTION_MAX];
  int num_to_dist_body = 0;

  struct Agent *a = &world->agents.agents[i];

  for (size_t j = 0; j < num_close_agents; j++) {
    if (num_to_dist_body >= FOOD_DISTRIBUTION_MAX) {
      break;
    }
    struct Agent *a2 = close_agents[j].agent;
    float d = close_agents[j].dist2;

    // Dont distribute to ourselves
    if (a == a2) {
      continue;
    }

    // If we are too far away, don't even consider this agent
    if (d > FOOD_DISTRIBUTION_RADIUS * FOOD_DISTRIBUTION_RADIUS) {
      continue;
    }

    // Only distribute to alive agents that are > 90% carnivores
    if (a2->herbivore < 0.1f && a2->health > 0.0f) {
      dist_agents[num_to_dist_body++] = a2;
    }
  }

  // if (num_to_dist_body > 0) {
  //   printf("start dist: age: %d\n", a->age);
  // }

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
    health_add += 0.8f * (num_to_dist_body / FOOD_DISTRIBUTION_MAX);

    // Factor in age muliplier
    health_add *= agemult;

    // Factor in herbivore percentage
    health_add *= (1.0f - a2->herbivore);

    // Divide for each agent
    health_add /= (float) num_to_dist_body;

    float rep_sub = 3.0f * health_add;

    // printf("n: %d, carn: %f, h+: %f, r-: %f\n", num_to_dist_body, 1.0f -
    // a2->herbivore, health_add, rep_sub);

    a2->health += health_add;
    a2->repcounter -= rep_sub;

    if (a2->health > 2.0f)
      a2->health = 2.0f; // cap it!

    agent_initevent(a2, health_add * 50.0f, 1.0f, 0.0f,
                    0.0f); // red means they ate! nice
  }

  if (num_to_dist_body == 0) {
    // if no agents are around to eat it, it becomes regular food
    world->food[(int32_t)a->pos.x / CZ][(int32_t)a->pos.y / CZ] =
        FOOD_DEAD * FOODMAX; // since it was dying it is not much food
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

  struct Agent* newMovieAgent = NULL;
  struct Agent* prevMovieAgent = NULL;
  int32_t newOldest = -1;
  int32_t prevOldest = -1;

  // Some things need to be done single threaded
  for (int i = 0; i < world->agents.size; i++) {
    struct Agent *a = &world->agents.agents[i];
    if (a->health <= 0 && a->spiked == 1) {
      // Distribute dead agents to nearby carnivores
      world_dist_dead_agent(world, i);
    }

    if (a->rep) {
      world_reproduce(world, a, a->MUTRATE1, a->MUTRATE2);
    }

    if (world->movieMode) {
      if (a->selectflag) {
	      prevOldest = a->age;
	      prevMovieAgent = a;
      } else {
        if (a->age > newOldest) {
          newOldest = a->age;
          newMovieAgent = a;
        }
      }
      a->selectflag = 0;
    }
  }

  if (newMovieAgent != NULL) {
    if (prevMovieAgent != NULL) {
      if (prevOldest >= newOldest) {
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
      if (world_numCarnivores(world) < 5) {
        for (int i = 0; i < 50; i++) {
          world_addCarnivore(world);
        }
      }
    }
  }

  // Flush any agents in the staging array, and clean up dead bots
  world_flush_staging(world);
}

void world_setInputsRunBrain(struct World *world) {
  for (size_t i = 0; i < world->agents.size; i += 16) {
    size_t max = i + 16;
    if (max > world->agents.size) {
      max = world->agents.size;
    }
    struct AgentQueueItem *agentQueueItem =
        malloc(sizeof(struct AgentQueueItem));
    agentQueueItem->world = world;
    agentQueueItem->start = i;
    agentQueueItem->end = max;

    struct QueueItem queueItem = {agent_input_processor, agentQueueItem};
    queue_enqueue(&world->queue, queueItem);
  }

  pthread_mutex_lock(&world->queue.mutex);
  while (world->queue.num_work_items != 0 || world->queue.size != 0) {
    pthread_cond_wait(&world->queue.cond_work_done, &world->queue.mutex);
  }

  pthread_mutex_unlock(&world->queue.mutex);
}

void world_processOutputs(struct World *world) {
  for (size_t i = 0; i < world->agents.size; i += 16) {
    size_t max = i + 16;
    if (max > world->agents.size) {
      max = world->agents.size;
    }
    struct AgentQueueItem *agentQueueItem =
        malloc(sizeof(struct AgentQueueItem));
    agentQueueItem->world = world;
    agentQueueItem->start = i;
    agentQueueItem->end = max;

    struct QueueItem queueItem = {agent_output_processor, agentQueueItem};
    queue_enqueue(&world->queue, queueItem);
  }

  pthread_mutex_lock(&world->queue.mutex);
  while (world->queue.num_work_items != 0 || world->queue.size != 0) {
    pthread_cond_wait(&world->queue.cond_work_done, &world->queue.mutex);
  }

  pthread_mutex_unlock(&world->queue.mutex);
}

void world_addRandomBots(struct World *world, int32_t num) {
  world->numAgentsAdded += num; // record in report

  for (int32_t i = 0; i < num; i++) {
    struct Agent a;
    agent_init(&a);

    avec_push_back(&world->agents_staging, a);
  }
}

void world_addCarnivore(struct World *world) {
  struct Agent a;
  agent_init(&a);
  a.herbivore = randf(0, 0.1f);

  avec_push_back(&world->agents_staging, a);

  world->numAgentsAdded++;
}

void world_addNewByCrossover(struct World *world) {
  // find two success cases
  size_t i1 = randi(0, world->agents.size);
  size_t i2 = randi(0, world->agents.size);
  for (size_t i = 0; i < world->agents.size; i++) {
    if (world->agents.agents[i].age > world->agents.agents[i1].age &&
        randf(0, 1) < 0.1f) {
      i1 = i;
    }
    if (world->agents.agents[i].age > world->agents.agents[i2].age &&
        randf(0, 1) < 0.1f && i != i1) {
      i2 = i;
    }
  }

  struct Agent *a1 = &world->agents.agents[i1];
  struct Agent *a2 = &world->agents.agents[i2];

  // cross brains
  struct Agent anew;
  agent_init(&anew);
  agent_crossover(&anew, a1, a2);

  // maybe do mutation here? I dont know. So far its only crossover
  avec_push_back(&world->agents_staging, anew);

  world->numAgentsAdded++; // record in report
}

void world_reproduce(struct World *world, struct Agent *a, float MR,
                     float MR2) {
  if (randf(0, 1) < 0.04f)
    MR = MR * randf(1, 10);
  if (randf(0, 1) < 0.04f)
    MR2 = MR2 * randf(1, 10);

  agent_initevent(a, 30, 0.0f, 0.8f, 0.0f); // green event means agent reproduced.
  for (int32_t i = 0; i < BABIES; i++) {

    struct Agent a2;
    agent_init(&a2);
    agent_reproduce(&a2, a, MR, MR2);
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
  int32_t avg_age;
  float epoch_decimal = (float) world->modcounter / 10000.0f + (float) world->current_epoch;

  // Count number of herb, carn and top of each
  for (size_t i = 0; i < world->agents.size; i++) {
    if (world->agents.agents[i].herbivore > 0.5f)
      numherb++;
    else
      numcarn++;

    if (world->agents.agents[i].herbivore > 0.5f &&
        world->agents.agents[i].gencount > topherb)
      topherb = world->agents.agents[i].gencount;
    if (world->agents.agents[i].herbivore < 0.5f &&
        world->agents.agents[i].gencount > topcarn)
      topcarn = world->agents.agents[i].gencount;

    // Average Age:
    total_age += world->agents.agents[i].age;
  }
  avg_age = total_age / world->agents.size;

  // Compute Standard Devitation of every weight in every agents brain
  double total_std_dev = 0;
  double total_mean_std_dev;

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
  avec_init(&world->agents_staging, 16);
  avec_init(&world->agents, 16);
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
      d = powf((float) x - world->agents.agents[i].pos.x, 2.0f) +
          powf((float) y - world->agents.agents[i].pos.y, 2.0f);
      if (d < mind) {
        mind = d;
        mini = i;
      }
    }
    // toggle selection of this agent
    for (size_t i = 0; i < world->agents.size; i++) {
      if (i != mini) {
        world->agents.agents[i].selectflag = 0;
      } else {
        world->agents.agents[i].selectflag =
            world->agents.agents[mini].selectflag ? 0 : 1;
      }
    }

    // agents[mini].printSelf();
  }
}

int32_t world_numHerbivores(struct World *world) {
  int32_t numherb = 0;
  for (size_t i = 0; i < world->agents.size; i++) {
    if (world->agents.agents[i].herbivore > 0.5)
      numherb++;
  }

  return numherb;
}

int32_t world_numCarnivores(struct World *world) {
  int32_t numcarn = 0;
  for (size_t i = 0; i < world->agents.size; i++) {
    if (world->agents.agents[i].herbivore <= 0.5)
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
  // printf("\nagents_size: %li\n", world->agents.size);
  for (size_t i = 1; i < world->agents.size; i++) {
    struct Agent key_agent = world->agents.agents[i];
    // memcpy(&key_agent, &, sizeof(struct Agent));

    size_t key_grid_x = floor(key_agent.pos.x / WORLD_GRID_SIZE);
    size_t key_grid_y = floor(key_agent.pos.y / WORLD_GRID_SIZE);
    size_t key_grid_index = key_grid_y * WORLD_GRID_WIDTH + key_grid_x;

    long j = i - 1;
    while (j >= 0) {
      struct Agent *a2 = &world->agents.agents[j];

      size_t next_grid_x = floor(a2->pos.x / WORLD_GRID_SIZE);
      size_t next_grid_y = floor(a2->pos.y / WORLD_GRID_SIZE);
      size_t next_grid_index = next_grid_y * WORLD_GRID_WIDTH + next_grid_x;

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
    struct Agent *a = &world->agents.agents[i];

    size_t grid_x = floor(a->pos.x / WORLD_GRID_SIZE);
    size_t grid_y = floor(a->pos.y / WORLD_GRID_SIZE);
    size_t grid_index = grid_y * WORLD_GRID_WIDTH + grid_x;

    while (grid_index > current_grid_index) {
      world->agent_grid[current_grid_index++] = i;
    }
  }

  while (current_grid_index < WORLD_GRID_LENGTH) {
    world->agent_grid[current_grid_index] =
        world->agent_grid[current_grid_index - 1];
    current_grid_index++;
    // printf("%li: %li\n", current_grid_index, i);
  }
}

void agent_output_processor(void *arg) {
  struct AgentQueueItem *aqi = (struct AgentQueueItem *)arg;
  struct World *world = aqi->world;

  for (size_t i = aqi->start; i < aqi->end; i++) {
    struct Agent *a = &world->agents.agents[i];

    a->w1 = a->out[0]; //-(2*a->out[0]-1);
    a->w2 = a->out[1]; //-(2*a->out[1]-1);
    a->red = a->out[2];
    a->gre = a->out[3];
    a->blu = a->out[4];
    a->boost = a->out[6] > 0.5;
    a->soundmul = a->out[7];
    a->give = a->out[8];

    // spike length should slowly tend towards out[5]
    float g = a->out[5];
    if (a->spikeLength < g)
      a->spikeLength += SPIKESPEED;
    else if (a->spikeLength > g)
      a->spikeLength = g; // its easy to retract spike, just hard to put it up.

    // Move bots

    struct Vector2f v;
    vector2f_init(&v, BOTRADIUS / 2, 0);
    vector2f_rotate(&v, a->angle + (float) M_PI / 2.0f);

    struct Vector2f w1p;
    struct Vector2f w2p;
    vector2f_add(&w1p, &a->pos, &v); // wheel positions
    vector2f_sub(&w2p, &a->pos, &v); // wheel positions

    float BW1 = BOTSPEED * a->w1; // bot speed * wheel speed
    float BW2 = BOTSPEED * a->w2;

    if (a->boost) {
      BW1 = BW1 * BOOSTSIZEMULT;
      BW2 = BW2 * BOOSTSIZEMULT;
    }

    // move bots
    struct Vector2f vv;
    vector2f_sub(&vv, &w2p, &a->pos);
    vector2f_rotate(&vv, -BW1);

    vector2f_sub(&a->pos, &w2p, &vv);
    a->angle -= BW1;
    if (a->angle < (float) -M_PI)
      a->angle = (float) M_PI - ((float) -M_PI - a->angle);
    vector2f_sub(&vv, &a->pos, &w1p);
    vector2f_rotate(&vv, BW2);
    vector2f_add(&a->pos, &w1p, &vv);
    a->angle += BW2;
    if (a->angle > (float) M_PI)
      a->angle = (float) -M_PI + (a->angle - (float) M_PI);

    // wrap around the map
    /*if (a->pos.x<0) a->pos.x= WIDTH+a->pos.x;
              if (a->pos.x>=WIDTH) a->pos.x= a->pos.x-WIDTH;
              if (a->pos.y<0) a->pos.y= HEIGHT+a->pos.y;
              if (a->pos.y>=HEIGHT) a->pos.y= a->pos.y-HEIGHT;*/

    // have peetree dish borders
    if (a->pos.x - BOTRADIUS < 0)
      a->pos.x = BOTRADIUS;
    if (a->pos.x + BOTRADIUS > WIDTH)
      a->pos.x = WIDTH - BOTRADIUS;
    if (a->pos.y - BOTRADIUS < 0)
      a->pos.y = BOTRADIUS;
    if (a->pos.y + BOTRADIUS > HEIGHT)
      a->pos.y = HEIGHT - BOTRADIUS;

    // process food intake

    int32_t cx = (int32_t)a->pos.x / CZ;
    int32_t cy = (int32_t)a->pos.y / CZ;
    float f = world->food[cx][cy];

    if (f > 0 && a->health < 2 && a->herbivore > 0.1f) {
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

    a->rep = 0;

    // Handle reproduction
    if (world->modcounter % 15 == 0 && a->repcounter < 0 &&
        a->health > REP_MIN_HEALTH && randf(0, 1) < 0.1f) {
      // agent is healthy (REP_MIN_HEALTH) and is ready to reproduce.
      // Also inject a bit non-determinism

      // the parent splits it health evenly with all of its babies
      a->health -= a->health / ((float) BABIES + 1.0f);

      a->rep = 1;

      a->repcounter =
          a->herbivore * randf(REPRATEH - 0.1f, REPRATEH + 0.1f) +
          (1.0f - a->herbivore) * randf(REPRATEC - 0.1f, REPRATEC + 0.1f);
    }

    agent_process_health(a);
  }
}

void agent_input_processor(void *arg) {
  struct AgentQueueItem *aqi = (struct AgentQueueItem *)arg;
  struct World *world = aqi->world;

  for (size_t i = aqi->start; i < aqi->end; i++) {
    struct Agent *a = &world->agents.agents[i];

    // printf("&world->agents.size: %zu\n", world->agents.size);
    // for (size_t j = 0; j < world->agents.size; j++) {
    //   printf("agent %zu: %i\n", j, a->pos.x);
    // }

    // printf("thread: %zu\n", pthread_self());
    // printf("world: %zu\n", world->agents.size);
    // printf("index: %zu\n", i);
    // printf("sizeof: %zu\n", sizeof(struct Agent));
    // printf("pointer: %p\n", (void*) &world->agents.agents[i]);

    struct Agent_d close_agents[NUMBOTS_CLOSE];
    int num_close_agents = world_get_close_agents(world, i, close_agents);

    // printf("Got %d close agents\n", num_close_agents);

    // General settings
    // says that agent was not hit this turn
    a->spiked = 0;

    // process indicator used in drawing
    if (a->indicator > 0)
      a->indicator--;

    // Update agents age
    if (world->modcounter % 100 == 0)
      a->age++;

    // FOOD
    int32_t cx = (int32_t)a->pos.x / CZ;
    int32_t cy = (int32_t)a->pos.y / CZ;
    a->in[4] = world->food[cx][cy] / FOODMAX;

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
    for (int j = 0; j < num_close_agents; j++) {

      struct Agent *a2 = close_agents[j].agent;

      // standard distance formula (more fine grain)
      float d = close_agents[j].dist2;

      if (d < DIST * DIST) {
        // Get the real distance now
        d = sqrt(d);

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
        float backangle = a->angle + (float) M_PI;
        float forwangle = a->angle;
        if (leyeangle < (float) -M_PI)
          leyeangle += 2.0f * (float) M_PI;
        if (reyeangle > (float) M_PI)
          reyeangle -= 2.0f * (float) M_PI;
        if (backangle > (float) M_PI)
          backangle -= 2.0f * (float) M_PI;
        float diff1 = leyeangle - ang;
        if (fabsf(diff1) > (float) M_PI)
          diff1 = 2.0f * (float) M_PI - fabsf(diff1);
        diff1 = fabsf(diff1);
        float diff2 = reyeangle - ang;
        if (fabsf(diff2) > (float) M_PI)
          diff2 = 2.0f * (float) M_PI - fabsf(diff2);
        diff2 = fabsf(diff2);
        float diff4 = forwangle - ang;
        if (fabsf(forwangle) > (float) M_PI)
          diff4 = 2.0f * (float) M_PI - fabsf(forwangle);
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
        if (d < BOTRADIUS * 2.0f) {
          // these two are in collision and agent i has extended spike and is
          // going decent fast!
          struct Vector2f v;
          vector2f_init(&v, 1.0f, 0.0f);
          vector2f_rotate(&v, a->angle);
          struct Vector2f tmp;
          vector2f_sub(&tmp, &a2->pos, &a->pos);
          float diff = vector2f_angle_between(&v, &tmp);
          if (fabsf(diff) < (float) M_PI / 6.0f) {
            // bot i is also properly aligned!!! that's a hit
            float DMG = SPIKEMULT * a->spikeLength * (1.0f - a->herbivore) *
                        fmaxf(fabsf(a->w1), fabsf(a->w2)) * BOOSTSIZEMULT;

            a2->health -= DMG;
            a->spikeLength = 0.0f; // retract spike back down

            agent_initevent(
                a, 10.0f * DMG, 1.0f, 1.0f,
                0.0f); // yellow event means bot has spiked other bot. nice!

            struct Vector2f v2;
            vector2f_init(&v2, 1.0f, 0.0f);
            vector2f_rotate(&v2, a2->angle);
            float adiff = vector2f_angle_between(&v, &v2);
            if (fabsf(adiff) < (float) M_PI / 2.0f) {
              // this was attack from the back. Retract spike of the other
              // agent (startle!) this is done so that the other agent cant
              // right away "by accident" attack this agent
              a2->spikeLength = 0.0f;
            }

            a2->spiked =
                1; // set a flag saying that this agent was hit this turn
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

    // TOUCH (wall)
    if (a->pos.x < 2 || a->pos.x > WIDTH - 3 || a->pos.y < 2 ||
        a->pos.y > HEIGHT - 3)
      // they are very close to the wall (1 or 2 pixels)
      a->touch = 1;
    else
      a->touch = 0;

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
    a->in[11] = cap(a->health / 2); // divide by 2 since health is in [0,2]
    a->in[12] = fabsf(sinf(world->modcounter / a->clockf1));
    a->in[13] = fabsf(sinf(world->modcounter / a->clockf2));
    a->in[14] = cap(hearaccum); // HEARING (other agents shouting)
    a->in[15] = cap(blood);
    a->in[16] = cap(discomfort);
    a->in[17] = cap(a->touch);
    if (randf(0, 1) > 0.95f) {
      a->in[18] = randf(0, 1); // random input for bot
    }

    // Copy last ouput and last "plan" to the current inputs
    // PREV_OUT is 19-28
    // PREV_PLAN is 29-38
    for (int i = 0; i < OUTPUTSIZE; i++) {
      a->in[i + INPUTSIZE - OUTPUTSIZE - 1] = a->out[i];
    }

    // Now process brain
    agent_tick(a);
  }
}
