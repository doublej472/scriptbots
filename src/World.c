#include <time.h>
#include <stdio.h>

#include "include/settings.h"
#include "include/World.h"
#include "include/vec.h"
#include "include/helpers.h"
#include "include/vec2f.h"
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#ifdef OPENMP
#include <omp.h>
#endif

static void timespec_diff(struct timespec* result, struct timespec *start,
                          struct timespec *stop) {
    if ((stop->tv_nsec - start->tv_nsec) < 0) {
        result->tv_sec = stop->tv_sec - start->tv_sec - 1;
        result->tv_nsec = stop->tv_nsec - start->tv_nsec + 1000000000;
    } else {
        result->tv_sec = stop->tv_sec - start->tv_sec;
        result->tv_nsec = stop->tv_nsec - start->tv_nsec;
    }
}

void world_init(struct World *world) {
  world->stopSim = 0;
  world->modcounter = 0;
  world->current_epoch = 0;
  world->idcounter = 0;
  world->numAgentsAdded = 0;
  world->FW = WIDTH / CZ;
  world->FH = HEIGHT / CZ;

  clock_gettime(CLOCK_MONOTONIC_RAW, &world->startTime);
  // Track total running time:
  clock_gettime(CLOCK_MONOTONIC_RAW, &world->totalStartTime);

  avec_init(&world->agents, NUMBOTS);

  // create the bots but with 20% more carnivores, to give them head start
  world_addRandomBots(world, (int) NUMBOTS * .8);
  for (int i = 0; i < (int) NUMBOTS * .2; ++i)
    world_addCarnivore(world);

  // inititalize food layer
  srand(time(0));
  double rand1; // store temp random float to save randf() call

  for (int x = 0; x < world->FW; x++) {
    for (int y = 0; y < world->FH; y++) {

      rand1 = randf(0, 1);
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
    world_writeReport(world);

    // Update GUI
    struct timespec endTime;
    clock_gettime(CLOCK_MONOTONIC_RAW, &endTime);
    struct timespec ts_delta;
    struct timespec ts_totaldelta;

    timespec_diff(&ts_delta, &world->startTime, &endTime);
    timespec_diff(&ts_totaldelta, &world->totalStartTime, &endTime);

    float deltat = (float) ts_delta.tv_sec + (ts_delta.tv_nsec / 1000000000.0f);
    float totaldeltat = (float) ts_totaldelta.tv_sec + (ts_totaldelta.tv_nsec / 1000000000.0f);

    printf("Simulation Running... Epoch: %d - Next: %d%% - Agents: %i - FPS: "
           "%f - Time: %.2f sec     \r",
           world->current_epoch, world->modcounter / 100, (int) world->agents.size,
           (int) reportInterval / deltat,
           totaldeltat);

    world->startTime = endTime;

    // Check if simulation needs to end

    if (world->current_epoch >= MAX_EPOCHS || (endTime.tv_sec -
world->totalStartTime.tv_sec) >= MAX_SECONDS) { world->stopSim = 1; }
  }

  // What kind of food method are we using?
  if (FOOD_MODEL == FOOD_MODEL_GROW) {
    // GROW food enviroment model
    if (world->modcounter % FOODADDFREQ == 0) {
      for (int x = 0; x < world->FW; ++x) {
        for (int y = 0; y < world->FH; ++y) {
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

  // give input to every agent. Sets in[] array. Runs brain
  world_setInputsRunBrain(world);

  // read output and process consequences of bots on environment. requires out[]
  world_processOutputs(world);

  float healthloss;     // amount of health lost
  float dd, discomfort; // temperature preference vars
  float d, agemult;     // used for dead agents

  // process bots health
  for (size_t i = 0; i < world->agents.size; i++) {
    healthloss = LOSS_BASE; // base amount of health lost every turn for
                                  // being alive

    // remove health based on wheel speed
    if (world->agents.agents[i].boost) { // is using boost
      healthloss += LOSS_SPEED * BOTSPEED *
                    (abs(world->agents.agents[i].w1) + abs(world->agents.agents[i].w2)) *
                    BOOSTSIZEMULT * world->agents.agents[i].boost;
    } else { // no boost
      healthloss += LOSS_SPEED * BOTSPEED *
                    (abs(world->agents.agents[i].w1) + abs(world->agents.agents[i].w2));
    }

    // shouting costs energy.
    healthloss += LOSS_SHOUTING * world->agents.agents[i].soundmul;

    // process temperature preferences
    // calculate temperature at the agents spot. (based on distance from
    // equator)
    dd = 2.0 * abs(world->agents.agents[i].pos.x / WIDTH - 0.5);
    discomfort = abs(dd - world->agents.agents[i].temperature_preference);
    discomfort = discomfort * discomfort;
    if (discomfort < 0.08)
      discomfort = 0;
    healthloss += LOSS_TEMP * discomfort;

    // apply the health changes
    world->agents.agents[i].health -= healthloss;

    //------------------------------------------------------------------------------------------

    // remove dead agents and distribute food

    // if this agent was spiked this round as well (i.e. killed). This will make
    // it so that natural deaths can't be capitalized on. I feel I must do this
    // or otherwise agents will sit on spot and wait for things to die around
    // them. They must do work!
    if (world->agents.agents[i].health <= 0 && world->agents.agents[i].spiked) {

      // distribute its food. It will be erased soon
      // first figure out how many are around, to distribute this evenly
      int numaround = 0;
      for (size_t j = 0; j < world->agents.size; j++) {
        // only carnivores get food. not same agent as dying
        if (world->agents.agents[j].herbivore < .1 && world->agents.agents[j].health > 0) {

          d = vector2f_dist(&world->agents.agents[i].pos, &world->agents.agents[j].pos);
          if (d < FOOD_DISTRIBUTION_RADIUS) {
            numaround++;
          }
        }
      }

      // young killed agents should give very little resources
      // at age 5, they mature and give full. This can also help prevent
      // agents eating their young right away
      agemult = 1.0;
      if (world->agents.agents[i].age < 5)
        agemult = world->agents.agents[i].age * 0.2;

      if (numaround > 0) {
        // distribute its food evenly
        for (size_t j = 0; j < world->agents.size; j++) {
          // only carnivores get food. not same agent as dying
          if (world->agents.agents[j].herbivore < .1 && world->agents.agents[j].health > 0) {

            d = vector2f_dist(&world->agents.agents[i].pos, &world->agents.agents[j].pos);
            if (d < FOOD_DISTRIBUTION_RADIUS) {
              // add to agent's health
              /*  percent_carnivore = 1-agents[j].herbivore
                      coefficient = 5
                      numaround = # of other agents within vicinity
                      agemult = 1 if agent is older than 4
                      health += percent_carnivore ^ 2 * agemult * 5
                      -----------------------------------
                      numaround ^ 1.25
              */
              world->agents.agents[j].health += 5 * (1 - world->agents.agents[j].herbivore) *
                                  (1 - world->agents.agents[j].herbivore) /
                                  pow(numaround, 1.25) * agemult;

              // make this bot reproduce sooner
              world->agents.agents[j].repcounter -= 6 * (1 - world->agents.agents[j].herbivore) *
                                      (1 - world->agents.agents[j].herbivore) /
                                      pow(numaround, 1.25) * agemult;

              if (world->agents.agents[j].health > 2)
                world->agents.agents[j].health = 2; // cap it!

              agent_initevent(&world->agents.agents[j], 30, 1, 1, 1); // white means they ate! nice
            }
          }
        }
      } else {
        // if no agents are around to eat it, it becomes regular food
        world->food[(int)world->agents.agents[i].pos.x / CZ][(int)world->agents.agents[i].pos.y / CZ] =
            FOOD_DEAD *
            FOODMAX; // since it was dying it is not much food
      }
    }
  }

  // Delete dead agents
  for (size_t i = 0; i < world->agents.size; i++) {
    if (world->agents.agents[i].health <= 0) {
      avec_delete(&world->agents, i);
    }
  }

  // Handle reproduction
  for (size_t i = 0; i < world->agents.size; i++) {
    if (world->agents.agents[i].repcounter < 0 && world->agents.agents[i].health > REP_MIN_HEALTH &&
        world->modcounter % 15 == 0 && randf(0, 1) < 0.1) {
      // agent is healthy (REP_MIN_HEALTH) and is ready to reproduce.
      // Also inject a bit non-determinism

      // the parent splits it health evenly with all of its babies
      world->agents.agents[i].health -= world->agents.agents[i].health / (BABIES + 1);

      // add BABIES new agents to agents[]
      world_reproduce(world, i, world->agents.agents[i].MUTRATE1, world->agents.agents[i].MUTRATE2);

      world->agents.agents[i].repcounter =
          world->agents.agents[i].herbivore *
              randf(REPRATEH - 0.1, REPRATEH + 0.1) +
          (1 - world->agents.agents[i].herbivore) *
              randf(REPRATEC - 0.1, REPRATEC + 0.1);
    }
  }

  // add new agents, if environment isn't closed
  if (!world->closed) {
    // make sure environment is always populated with at least NUMBOTS_MIN bots
    if (world->agents.size < NUMBOTS_MIN) {
      world_addRandomBots(world, 10); // add new agent
    }
    /*
      if (world->modcounter%100==0) {
      if (randf(0,1)<0.5){
      addRandomBots(1); //every now and then add random bots in
      }else{
      addNewByCrossover(); //or by crossover
      }
      }
    */
  }
}

// Grow food around square
void world_growFood(struct World *world, int x, int y) {
  // check if food square is inside the world
  if (world->food[x][y] < FOODMAX 
    && x >= 0
    && x < world->FW
    && y >= 0
    && y < world->FH) {
      world->food[x][y] += FOODGROWTH;
    }
}

void world_setInputsRunBrain(struct World *world) {
  // See README.markdown for documentation of input ids

#pragma omp parallel for
  for (size_t i = 0; i < world->agents.size; i++) {

    struct Agent *a = &world->agents.agents[i];

    // General settings
    // says that agent was not hit this turn
    a->spiked = 0;

    // process indicator used in drawing
    if (a->indicator > 0)
      a->indicator--;

    // Update agents age
    if (!world->modcounter % 100)
      a->age++;

    // FOOD
    int cx = (int)a->pos.x / CZ;
    int cy = (int)a->pos.y / CZ;
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
    float p3 = 0;
    float r3 = 0;
    float g3 = 0;
    float b3 = 0;
    float soaccum = 0;
    float smaccum = 0;
    float hearaccum = 0;

    // BLOOD ESTIMATOR
    float blood = 0;

    // AMOUNT OF HEALTH GAINED FROM BEING IN GROUP
    float health_gain = 0;

    // SMELL SOUND EYES
    for (size_t j = 0; j < world->agents.size; j++) {
      if (i == j)
        continue; // do not process self

      struct Agent *a2 = &world->agents.agents[j];

      // Do manhattan-distance estimation
      if (a->pos.x < a2->pos.x - DIST ||
          a->pos.x > a2->pos.x + DIST ||
          a->pos.y > a2->pos.y + DIST ||
          a->pos.y < a2->pos.y - DIST)
        continue;

      // standard distance formula (more fine grain)
      float d = vector2f_dist(&a->pos, &a2->pos);

      if (d < DIST) { // two bots are within DIST of each other

        // smell
        smaccum += 0.3 * (DIST - d) / DIST;

        // hearing. (listening to other agents shouting)
        hearaccum += a2->soundmul * (DIST - d) / DIST;

        // more fine-tuned closeness
        if (d < DIST_GROUPING) {
          // grouping health bonus for each agent near by
          // health gain is most when two bots are just at threshold, is less
          // when they are ontop each other
          float ratio = (1 - (DIST_GROUPING - d) / DIST_GROUPING);
          health_gain += GAIN_GROUPING * ratio;
          agent_initevent(a, 5 * ratio, .5, .5, .5); // visualize it

          // sound (number of agents nearby)
          soaccum += 0.4 * (DIST - d) / DIST *
                     (fmax(fabs(a2->w1), fabs(a2->w2)));
        }

        // current angle between bots
        float ang = vector2f_angle_between(&a->pos, &a2->pos);

        // left and right eyes
        float leyeangle = a->angle - PI8;
        float reyeangle = a->angle + PI8;
        float backangle = a->angle + M_PI;
        float forwangle = a->angle;
        if (leyeangle < -M_PI)
          leyeangle += 2 * M_PI;
        if (reyeangle > M_PI)
          reyeangle -= 2 * M_PI;
        if (backangle > M_PI)
          backangle -= 2 * M_PI;
        float diff1 = leyeangle - ang;
        if (fabs(diff1) > M_PI)
          diff1 = 2 * M_PI - fabs(diff1);
        diff1 = fabs(diff1);
        float diff2 = reyeangle - ang;
        if (fabs(diff2) > M_PI)
          diff2 = 2 * M_PI - fabs(diff2);
        diff2 = fabs(diff2);
        float diff3 = backangle - ang;
        if (fabs(diff3) > M_PI)
          diff3 = 2 * M_PI - fabs(diff3);
        diff3 = fabs(diff3);
        float diff4 = forwangle - ang;
        if (fabs(forwangle) > M_PI)
          diff4 = 2 * M_PI - fabs(forwangle);
        diff4 = fabs(diff4);

        if (diff1 < PI38) {
          // we see this agent with left eye. Accumulate info
          float mul1 = EYE_SENSITIVITY *
                       ((PI38 - diff1) / PI38) *
                       ((DIST - d) / DIST);
          // float mul1= 100*((DIST-d)/DIST);
          p1 += mul1 * (d / DIST);
          r1 += mul1 * a2->red;
          g1 += mul1 * a2->gre;
          b1 += mul1 * a2->blu;
        }

        if (diff2 < PI38) {
          // we see this agent with left eye. Accumulate info
          float mul2 = EYE_SENSITIVITY *
                       ((PI38 - diff2) / PI38) *
                       ((DIST - d) / DIST);
          // float mul2= 100*((DIST-d)/DIST);
          p2 += mul2 * (d / DIST);
          r2 += mul2 * a2->red;
          g2 += mul2 * a2->gre;
          b2 += mul2 * a2->blu;
        }

        if (diff3 < PI38) {
          // we see this agent with back eye. Accumulate info
          float mul3 = EYE_SENSITIVITY *
                       ((PI38 - diff3) / PI38) *
                       ((DIST - d) / DIST);
          // float mul2= 100*((DIST-d)/DIST);
          p3 += mul3 * (d / DIST);
          r3 += mul3 * a2->red;
          g3 += mul3 * a2->gre;
          b3 += mul3 * a2->blu;
        }

        if (diff4 < PI38) {
          float mul4 = BLOOD_SENSITIVITY *
                       ((PI38 - diff4) / PI38) *
                       ((DIST - d) / DIST);
          // if we can see an agent close with both eyes in front of us
          blood +=
              mul4 * (1 - a2->health / 2); // remember: health is in [0 2]
          // agents with high life dont bleed. low life makes them bleed more
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
    float dd = 2.0 * abs(a->pos.x / WIDTH - 0.5);
    float discomfort = abs(dd - a->temperature_preference);

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
    a->in[12] = cap(p3);
    a->in[13] = cap(r3);
    a->in[14] = cap(g3);
    a->in[15] = cap(b3);
    a->in[16] = abs(sin(world->modcounter / a->clockf1));
    a->in[17] = abs(sin(world->modcounter / a->clockf2));
    a->in[18] = cap(hearaccum); // HEARING (other agents shouting)
    a->in[19] = cap(blood);
    a->in[20] = cap(discomfort);
    a->in[21] = cap(a->touch);
    // change the random input at random intervals (10% chance)
    if (randf(0, 1) < .1)
      a->in[22] = randf(0, 1); // random input for bot

    // Copy last ouput and last "plan" to the current inputs
    // PREV_OUT is 23-32
    // PREV_PLAN is 33-42
    for (int i = 0; i < OUTPUTSIZE; ++i) {
      a->in[i + 23] = a->out[i];
    }

    // Now process brain
    agent_tick(a);
  }

}

void world_processOutputs(struct World *world) {
  // See README.markdown for documentation of output ids

#pragma omp parallel for
  for (size_t i = 0; i < world->agents.size; i++) {
    struct Agent *a = &world->agents.agents[i];

    a->red = a->out[2];
    a->gre = a->out[3];
    a->blu = a->out[4];
    a->w1 = a->out[0]; //-(2*a->out[0]-1);
    a->w2 = a->out[1]; //-(2*a->out[1]-1);
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
    vector2f_rotate(&v, a->angle + M_PI / 2);

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
    if (a->angle < -M_PI)
      a->angle = M_PI - (-M_PI - a->angle);
    vector2f_sub(&vv, &a->pos, &w1p);
    vector2f_rotate(&vv, BW2);
    vector2f_add(&a->pos, &w1p, &vv);
    a->angle += BW2;
    if (a->angle > M_PI)
      a->angle = -M_PI + (a->angle - M_PI);

    // wrap around the map
    /*if (a->pos.x<0) a->pos.x= WIDTH+a->pos.x;
              if (a->pos.x>=WIDTH) a->pos.x= a->pos.x-WIDTH;
              if (a->pos.y<0) a->pos.y= HEIGHT+a->pos.y;
              if (a->pos.y>=HEIGHT) a->pos.y= a->pos.y-HEIGHT;*/

    // have peetree dish borders
    if (a->pos.x < 0)
      a->pos.x = 0;
    if (a->pos.x >= WIDTH)
      a->pos.x = WIDTH - 1;
    if (a->pos.y < 0)
      a->pos.y = 0;
    if (a->pos.y >= HEIGHT)
      a->pos.y = HEIGHT - 1;

    // process food intake

    int cx = (int)world->agents.agents[i].pos.x / CZ;
    int cy = (int)world->agents.agents[i].pos.y / CZ;
    float f = world->food[cx][cy];
    if (f > 0 && a->health < 2) {
      // agent eats the food
      float itk = fmin(f, FOODINTAKE);
      float speedmul =
          (1 - (abs(a->w1) + abs(a->w2)) / 2) * 0.6 + 0.4;
      itk = itk * a->herbivore *
            speedmul; // herbivores gain more from ground food
      a->health += itk;
      a->repcounter -= 3 * itk;
      world->food[cx][cy] -= fmin(f, FOODWASTE);
    }

    // process giving and receiving of food
    a->dfood = 0;

    if (a->give > 0.5) {
      for (size_t j = 0; j < world->agents.size; j++) {
        float d = vector2f_dist(&a->pos, &world->agents.agents[j].pos);
        if (d < FOOD_SHARING_DISTANCE) {
          // initiate transfer
          if (world->agents.agents[j].health < 2) {
            world->agents.agents[j].health += FOODTRANSFER;
            a->health -= FOODTRANSFER;
            world->agents.agents[j].dfood += FOODTRANSFER; // only for drawing
            a->dfood -= FOODTRANSFER;
          }
        }
      }
    }

    // NOTE: herbivore cant attack. TODO: hmmmmm
    // fot now ok: I want herbivores to run away from carnivores, not kill
    // them back
    if (a->herbivore > 0.8 || a->spikeLength < 0.2 ||
        a->w1 < 0.5 || a->w2 < 0.5)
      continue;

    for (size_t j = 0; j < world->agents.size; j++) {

      if (i == j)
        continue;
      float d = vector2f_dist(&a->pos, &world->agents.agents[j].pos);

      if (d < 2 * BOTRADIUS) {
        // these two are in collision and agent i has extended spike and is
        // going decent fast!
        struct Vector2f v;
        vector2f_init(&v, 1, 0);
        vector2f_rotate(&v, a->angle);
        struct Vector2f tmp;
        vector2f_sub(&tmp, &world->agents.agents[j].pos, &a->pos);
        float diff = vector2f_angle_between(&v, &tmp);
        if (fabs(diff) < M_PI / 8) {
          // bot i is also properly aligned!!! that's a hit
          float DMG = SPIKEMULT * a->spikeLength *
                      fmax(fabs(a->w1), fabs(a->w2)) *
                      BOOSTSIZEMULT;

          world->agents.agents[j].health -= DMG;

          if (a->health > 2)
            a->health = 2;    // cap health at 2
          a->spikeLength = 0; // retract spike back down

          agent_initevent(&world->agents.agents[i],
              40 * DMG, 1, 1,
              0); // yellow event means bot has spiked other bot. nice!

          struct Vector2f v2;
          vector2f_init(&v2, 1, 0);
          vector2f_rotate(&v2, world->agents.agents[j].angle);
          float adiff = vector2f_angle_between(&v, &v2);
          if (fabs(adiff) < M_PI / 2) {
            // this was attack from the back. Retract spike of the other agent
            // (startle!) this is done so that the other agent cant right away
            // "by accident" attack this agent
            world->agents.agents[j].spikeLength = 0;
          }

          world->agents.agents[j].spiked =
              1; // set a flag saying that this agent was hit this turn
        }
      }
    }
  }
}

void world_addRandomBots(struct World *world, int num) {
  world->numAgentsAdded += num; // record in report

  for (int i = 0; i < num; i++) {
    struct Agent a;
    agent_init(&a);
    a.id = world->idcounter;
    world->idcounter++;

    avec_push_back(&world->agents, a);
  }
}

void world_addCarnivore(struct World *world) {
  struct Agent a;
  agent_init(&a);
  a.id = world->idcounter;
  a.herbivore = randf(0, 0.1);
  avec_push_back(&world->agents, a);

  world->idcounter++;
  world->numAgentsAdded++;
}

void world_addNewByCrossover(struct World *world) {
  // find two success cases
  size_t i1 = randi(0, world->agents.size);
  size_t i2 = randi(0, world->agents.size);
  for (size_t i = 0; i < world->agents.size; i++) {
    if (world->agents.agents[i].age > world->agents.agents[i1].age && randf(0, 1) < 0.1) {
      i1 = i;
    }
    if (world->agents.agents[i].age > world->agents.agents[i2].age && randf(0, 1) < 0.1 && i != i1) {
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
  anew.id = world->idcounter;
  world->idcounter++;
  avec_push_back(&world->agents, anew);

  world->numAgentsAdded++; // record in report
}

void world_reproduce(struct World *world, int ai, float MR, float MR2) {
  if (randf(0, 1) < 0.04)
    MR = MR * randf(1, 10);
  if (randf(0, 1) < 0.04)
    MR2 = MR2 * randf(1, 10);

  agent_initevent(&world->agents.agents[ai], 30, 0, 0.8, 0); // green event means agent reproduced.
  for (int i = 0; i < BABIES; i++) {

    struct Agent a2;
    agent_init(&a2);
    agent_reproduce(&a2, &world->agents.agents[ai], MR, MR2);
    a2.id = world->idcounter;
    world->idcounter++;
    avec_push_back(&world->agents, a2);
  }
}

void world_writeReport(struct World *world) {
  // save all kinds of nice data stuff
  int numherb = 0;
  int numcarn = 0;
  int topherb = 0;
  int topcarn = 0;
  int total_age = 0;
  int avg_age;
  double epoch_decimal = world->modcounter / 10000.0 + world->current_epoch;

  // Count number of herb, carn and top of each
  for (size_t i = 0; i < world->agents.size; i++) {
    if (world->agents.agents[i].herbivore > 0.5)
      numherb++;
    else
      numcarn++;

    if (world->agents.agents[i].herbivore > 0.5 && world->agents.agents[i].gencount > topherb)
      topherb = world->agents.agents[i].gencount;
    if (world->agents.agents[i].herbivore < 0.5 && world->agents.agents[i].gencount > topcarn)
      topcarn = world->agents.agents[i].gencount;

    // Average Age:
    total_age += world->agents.agents[i].age;
  }
  avg_age = total_age / world->agents.size;

  // Compute Standard Devitation of every weight in every agents brain
  double total_std_dev = 0;
  double total_mean_std_dev;

  // loop through every box in brain (there are BRAINSIZE of these)
  for (int i = 0; i < BRAINSIZE; i++)
  {
    double box_sum = 0;
    double box_weights[world->agents.size];
    double box_mean;
    double box_square_sum = 0;

    // loop through every agent to get the weight
    for (size_t a = 0; a < world->agents.size; a++) {
      double box_weight_sum = 0;

      for (int b = 0; b < CONNS; b++) {
        box_weight_sum += world->agents.agents[a].brain.boxes[i].w[b];
      }
      // Add this sum to total stats:
      box_sum += box_weight_sum;
      box_weights[a] = box_weight_sum;
    }

    // Computer the mean of the box_weight_sum for this box
    box_mean = box_sum / BRAINSIZE;
    // cout << "BOX MEAN = " << box_mean << endl;

    // Now calculate the population standard deviation for this box:
    // Compute diff of each weight sum from mean and square the result, then add
    // it:
    for (size_t c = 0; c < world->agents.size; c++) {
      box_square_sum += pow(box_weights[c] - box_mean, 2);
    }
  }

  total_mean_std_dev =
      total_std_dev - 200; // reduce by 200 for graph readability

  FILE *fp = fopen("report.csv", "a");

  fprintf(fp, "%f,%i,%i,%i,%i,%i,%i,%i\n", epoch_decimal, numherb, numcarn,
          topherb, topcarn, (int) total_mean_std_dev, avg_age, world->numAgentsAdded);

  fclose(fp);

  // Reset number agents added to zero
  world->numAgentsAdded = 0;
}

void world_reset(struct World *world) {
  avec_free(&world->agents);
  avec_init(&world->agents, NUMBOTS + 10);
  world_addRandomBots(world, NUMBOTS);
}

void world_processMouse(struct World *world, int button, int state, int x, int y) {
  if (state == 0) {
    float mind = 1e10;
    size_t mini = -1;
    float d;

    for (size_t i = 0; i < world->agents.size; i++) {
      d = pow(x - world->agents.agents[i].pos.x, 2) + pow(y - world->agents.agents[i].pos.y, 2);
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
        world->agents.agents[i].selectflag = world->agents.agents[mini].selectflag ? 0 : 1;
      }
    }

    // agents[mini].printSelf();
  }
}

int world_numHerbivores(struct World *world) {
  int numherb = 0;
  for (size_t i = 0; i < world->agents.size; i++) {
    if (world->agents.agents[i].herbivore > 0.5)
      numherb++;
  }

  return numherb;
}

int world_numCarnivores(struct World *world) {
  int numcarn = 0;
  for (size_t i = 0; i < world->agents.size; i++) {
    if (world->agents.agents[i].herbivore <= 0.5)
      numcarn++;
  }

  return numcarn;
}

int world_numAgents(struct World *world) {
  if (world->closed && world->agents.size == 0) {
    printf("Population is extinct at epoch %i\n", world->current_epoch);
    exit(1);
  }
  return world->agents.size;
}
