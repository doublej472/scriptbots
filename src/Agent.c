#include "Agent.h"

#include "AVXBrain.h"
#include "helpers.h"
#include "settings.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void agent_init(struct Agent *agent) {
  vector2f_init(&agent->pos, randf(0, WIDTH), randf(0, HEIGHT));
  agent->angle = randf(-M_PI, M_PI);
  agent->health = 1.0f + randf(0, 0.1f);
  agent->touch = 0;
  agent->age = 0;
  agent->spikeLength = 0;
  agent->red = 0;
  agent->gre = 0;
  agent->blu = 0;
  agent->w1 = 0;
  agent->w2 = 0;
  agent->soundmul = 1;
  agent->give = 0;
  agent->clockf1 = randf(5, 100);
  agent->clockf2 = randf(5, 100);
  agent->boost = 0;
  agent->indicator = 0;
  agent->gencount = 0;
  agent->selectflag = 0;
  agent->ir = 0;
  agent->ig = 0;
  agent->ib = 0;
  agent->temperature_preference = randf(0, 1);
  agent->hybrid = 0;
  agent->herbivore = randf(0, 1);
  agent->rep = 0;
  agent->repcounter =
      agent->herbivore * randf(REPRATEH - 0.1f, REPRATEH + 0.1f) +
      (1.0f - agent->herbivore) * randf(REPRATEC - 0.1f, REPRATEC + 0.1f);
  agent->numchildren = 0;

  agent->MUTRATE1 = METAMUTRATE1;
  agent->MUTRATE2 = METAMUTRATE2;

  agent->spiked = 0;

  memset(&agent->in, '\0', sizeof(float) * INPUTSIZE);
  memset(&agent->out, '\0', sizeof(float) * OUTPUTSIZE);

  agent->brain = alloc_aligned(32, sizeof(struct AVXBrain));

  avxbrain_init(agent->brain);
}

void agent_print(struct Agent *agent) {
  printf("Agent age=%i\n", agent->age);
  printf("\ttemp pref=%f\n\n", agent->temperature_preference);
}

void agent_initevent(struct Agent *agent, float size, float r, float g,
                     float b) {
  // Don't overwrite more important events
  if (size > agent->indicator) {
    agent->indicator = size;
    agent->ir = r;
    agent->ig = g;
    agent->ib = b;
  }
}

void agent_tick(struct Agent *agent) {
  avxbrain_tick(agent->brain, &agent->in, &agent->out);
}

void agent_reproduce(struct Agent *child, struct Agent *parent) {

  // spawn the baby somewhere closeby behind agent
  // we want to spawn behind so that agents dont accidentally eat their young
  // right away
  struct Vector2f fb;
  vector2f_init(&fb, randf(BOTRADIUS, BOTRADIUS * 3.0f), 0.0f);

  vector2f_rotate(&fb, -child->angle);
  vector2f_add(&child->pos, &parent->pos, &fb);

  parent->numchildren++;

  if (child->pos.x < 0)
    child->pos.x = WIDTH + child->pos.x;
  if (child->pos.x >= WIDTH)
    child->pos.x = child->pos.x - WIDTH;
  if (child->pos.y < 0)
    child->pos.y = HEIGHT + child->pos.y;
  if (child->pos.y >= HEIGHT)
    child->pos.y = child->pos.y - HEIGHT;

  child->gencount = parent->gencount + 1;
  child->repcounter =
      child->herbivore * randf(REPRATEH - 0.2f, REPRATEH + 0.2f) +
      (1.0f - child->herbivore) * randf(REPRATEC - 0.2f, REPRATEC + 0.2f);

  // noisy attribute passing
  child->MUTRATE1 = parent->MUTRATE1;
  child->MUTRATE2 = parent->MUTRATE2;
  if (randf(0, 1) < 0.2f)
    child->MUTRATE1 = cap(randn(parent->MUTRATE1, METAMUTRATE1));
  if (randf(0, 1) < 0.2f)
    child->MUTRATE2 = randn(parent->MUTRATE2, METAMUTRATE2);
  if (child->MUTRATE1 < 0.02f) {
    child->MUTRATE1 = 0.02f;
  }
  if (child->MUTRATE2 < 0.02f) {
    child->MUTRATE2 = 0.02f;
  }
  child->herbivore = cap(randn(parent->herbivore, 0.03f));
  if (randf(0, 1) < child->MUTRATE1 * 5.0f)
    child->clockf1 = randn(child->clockf1, child->MUTRATE2);
  if (child->clockf1 < 2.0f)
    child->clockf1 = 2.0f;
  if (randf(0, 1) < child->MUTRATE1 * 5.0f)
    child->clockf2 = randn(child->clockf2, child->MUTRATE2);
  if (child->clockf2 < 2.0f)
    child->clockf2 = 2.0f;

  child->temperature_preference =
      cap(randn(parent->temperature_preference, 0.005f));
  //    child->temperature_preference= parent->temperature_preference;

  // mutate brain here
  memcpy(child->brain, parent->brain, sizeof(struct AVXBrain));
  avxbrain_mutate(child->brain, child->MUTRATE1, child->MUTRATE2);
  avxbrain_reset_offsets(child->brain);
}

// void agent_crossover(struct Agent *target, const struct Agent *agent1,
//                      const struct Agent *agent2) {
//   target->hybrid = 1; // set this non-default flag
//   target->gencount = agent1->gencount;
//   if (agent2->gencount < target->gencount)
//     target->gencount = agent2->gencount;
//
//   // agent heredity attributes
//   target->clockf1 = randf(0, 1) < 0.5 ? agent1->clockf1 : agent2->clockf1;
//   target->clockf2 = randf(0, 1) < 0.5 ? agent1->clockf2 : agent2->clockf2;
//   target->herbivore = randf(0, 1) < 0.5 ? agent1->herbivore :
//   agent2->herbivore; target->MUTRATE1 = randf(0, 1) < 0.5 ? agent1->MUTRATE1
//   : agent2->MUTRATE1; target->MUTRATE2 = randf(0, 1) < 0.5 ? agent1->MUTRATE2
//   : agent2->MUTRATE2; target->temperature_preference = randf(0, 1) < 0.5
//                                        ? agent1->temperature_preference
//                                        : agent2->temperature_preference;
//
//   target->brain = malloc(sizeof(struct AVXBrain));
//   mlpbrain_crossover(target->brain, agent1->brain, agent2->brain);
// }

void agent_process_health(struct Agent *agent) {
  // process bots health
  float healthloss = LOSS_BASE; // base amount of health lost every turn for
                                // being alive
  if (agent->age > 500.0f) {
    healthloss += ((float)LOSS_AGE * (((float)agent->age - 500.0f) / 250.0f));
  }

  // remove health based on wheel speed
  if (agent->boost) { // is using boost
    healthloss += (float)LOSS_SPEED * (float)BOTSPEED *
                  ((fabsf(agent->w1) + fabsf(agent->w2)) / 2.0f) + (float)LOSS_BOOST *
                  agent->boost;
  } else { // no boost
    healthloss += LOSS_SPEED * BOTSPEED * (fabsf(agent->w1) + fabsf(agent->w2));
  }

  // shouting costs energy.
  healthloss += LOSS_SHOUTING * agent->soundmul;

  // process temperature preferences
  // calculate temperature at the agents spot. (based on distance from
  // equator)
  float dd = 2.0f * fabsf(agent->pos.x / (float)WIDTH - 0.5f);
  float discomfort = fabsf(dd - agent->temperature_preference);
  discomfort = discomfort * discomfort;
  if (discomfort < 0.08f)
    discomfort = 0.0f;
  healthloss += LOSS_TEMP * discomfort;

  // apply the health changes
  agent->health -= healthloss;
}
