#include "include/Agent.h"

#include "include/MLPBrain.h"
#include "include/helpers.h"
#include "include/settings.h"
#include <cstring>
#include <iostream>
#include <stdio.h>
#include <string>

void agent_init(Agent& agent) {
  vector2f_init(agent.pos, randf(0, WIDTH), randf(0, HEIGHT));
  agent.angle = randf(-M_PI, M_PI);
  agent.health = 1.0 + randf(0, 0.1);
  agent.touch = 0;
  agent.age = 0;
  agent.spikeLength = 0;
  agent.red = 0;
  agent.gre = 0;
  agent.blu = 0;
  agent.w1 = 0;
  agent.w2 = 0;
  agent.soundmul = 1;
  agent.give = 0;
  agent.clockf1 = randf(5, 100);
  agent.clockf2 = randf(5, 100);
  agent.boost = false;
  agent.indicator = 0;
  agent.gencount = 0;
  agent.selectflag = 0;
  agent.ir = 0;
  agent.ig = 0;
  agent.ib = 0;
  agent.temperature_preference = randf(0, 1);
  agent.hybrid = false;
  agent.herbivore = randf(0, 1);
  agent.repcounter =
      agent.herbivore * randf(REPRATEH - 0.1, REPRATEH + 0.1) +
      (1 - agent.herbivore) * randf(REPRATEC - 0.1, REPRATEC + 0.1);

  agent.id = 0;

  agent.MUTRATE1 = 0.003;
  agent.MUTRATE2 = 0.05;

  agent.spiked = false;
  memset(&agent.in, '\0', sizeof(float) * INPUTSIZE);
  memset(&agent.out, '\0', sizeof(float) * OUTPUTSIZE);
  mlpbrain_init(agent.brain);
}

void agent_print(Agent& agent) {
  printf("Agent age=%i\n", agent.age);
  printf("\ttemp pref=%f\n\n", agent.temperature_preference);
}

void agent_initevent(Agent& agent, float size, float r, float g, float b) {
  agent.indicator = size;
  agent.ir = r;
  agent.ig = g;
  agent.ib = b;
}

void agent_tick(Agent& agent) {
  mlpbrain_tick(agent.brain, agent.in, agent.out);
}

void agent_reproduce(Agent& child, Agent& parent, float MR, float MR2) {

  // spawn the baby somewhere closeby behind agent
  // we want to spawn behind so that agents dont accidentally eat their young
  // right away
  Vector2f fb;
  vector2f_init(fb, randf(BOTRADIUS, BOTRADIUS*3), 0);

  vector2f_rotate(fb, -child.angle);
  vector2f_add(child.pos, parent.pos, fb);

  if (child.pos.x < 0)
    child.pos.x = WIDTH + child.pos.x;
  if (child.pos.x >= WIDTH)
    child.pos.x = child.pos.x - WIDTH;
  if (child.pos.y < 0)
    child.pos.y = HEIGHT + child.pos.y;
  if (child.pos.y >= HEIGHT)
    child.pos.y = child.pos.y - HEIGHT;

  child.gencount = parent.gencount + 1;
  child.repcounter =
      child.herbivore * randf(REPRATEH - 0.1, REPRATEH + 0.1) +
      (1 - child.herbivore) * randf(REPRATEC - 0.1, REPRATEC + 0.1);

  // noisy attribute passing
  child.MUTRATE1 = parent.MUTRATE1;
  child.MUTRATE2 = parent.MUTRATE2;
  if (randf(0, 1) < 0.2)
    child.MUTRATE1 = randn(parent.MUTRATE1, METAMUTRATE1);
  if (randf(0, 1) < 0.2)
    child.MUTRATE2 = randn(parent.MUTRATE2, METAMUTRATE2);
  if (parent.MUTRATE1 < 0.001)
    parent.MUTRATE1 = 0.001;
  if (parent.MUTRATE2 < 0.02)
    parent.MUTRATE2 = 0.02;
  child.herbivore = cap(randn(parent.herbivore, 0.03));
  if (randf(0, 1) < MR * 5)
    child.clockf1 = randn(child.clockf1, MR2);
  if (child.clockf1 < 2)
    child.clockf1 = 2;
  if (randf(0, 1) < MR * 5)
    child.clockf2 = randn(child.clockf2, MR2);
  if (child.clockf2 < 2)
    child.clockf2 = 2;

  child.temperature_preference = cap(randn(parent.temperature_preference, 0.005));
  //    child.temperature_preference= parent.temperature_preference;

  // mutate brain here
  child.brain = parent.brain;
  mlpbrain_mutate(child.brain, MR, MR2);
}

void agent_crossover(Agent& target, const Agent& agent1, const Agent& agent2) {
  target.hybrid = true; // set this non-default flag
  target.gencount = agent1.gencount;
  if (agent2.gencount < target.gencount)
    target.gencount = agent2.gencount;

  // agent heredity attributes
  target.clockf1 = randf(0, 1) < 0.5 ? agent1.clockf1 : agent2.clockf1;
  target.clockf2 = randf(0, 1) < 0.5 ? agent1.clockf2 : agent2.clockf2;
  target.herbivore = randf(0, 1) < 0.5 ? agent1.herbivore : agent2.herbivore;
  target.MUTRATE1 = randf(0, 1) < 0.5 ? agent1.MUTRATE1 : agent2.MUTRATE1;
  target.MUTRATE2 = randf(0, 1) < 0.5 ? agent1.MUTRATE2 : agent2.MUTRATE2;
  target.temperature_preference = randf(0, 1) < 0.5
                                    ? agent1.temperature_preference
                                    : agent2.temperature_preference;

  mlpbrain_crossover(target.brain, agent1.brain, agent2.brain);
}
