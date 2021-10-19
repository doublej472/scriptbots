#ifndef AGENT_H
#define AGENT_H
#include <stdint.h>

#include "MLPBrain.h"
#include "vec2f.h"

struct Agent {
  int32_t touch; // is bot close to wall?
  int32_t boost; // is this agent boosting
  int32_t spiked;
  int32_t hybrid; // is this agent result of crossover?

  struct Vector2f pos;

  float health; // in [0,2]. I cant remember why.
  float angle;  // of the bot

  float red;
  float gre;
  float blu;

  float w1; // wheel speeds
  float w2;

  float spikeLength;
  int32_t age;


  float in[INPUTSIZE]; // input: 2 eyes, sensors for R,G,B,proximity each,
                         // then Sound, Smell, Health
  float out[OUTPUTSIZE]; // output: Left, Right, R, G, B, SPIKE

  float repcounter;       // when repcounter gets to 0, this bot reproduces
  int rep;      // If this agent will reproduce the next world update
  int32_t gencount;           // generation counter
  float clockf1, clockf2; // the frequencies of the two clocks of this bot
  float soundmul; // sound multiplier of this bot. It can scream, or be very
                  // sneaky. This is actually always set to output 8

  // variables for drawing purposes
  float indicator;
  float ir, ig, ib; // indicator colors
  int32_t selectflag;   // is this agent selected?
  float dfood;      // what is change in health of this agent due to
               // giving/receiving?

  float give; // is this agent attempting to give food to other agent?

  int32_t id;

  // inhereted stuff
  float herbivore; // is this agent a herbivore? between 0 and 1
  float MUTRATE1;  // how often do mutations occur?
  float MUTRATE2;  // how significant are they?
  float
      temperature_preference; // what temperature does this agent like? [0 to 1]

  struct MLPBrain brain;
  // Used as a cache to find close agents
  struct Agent_d *close_agents;
  int32_t num_close_agents;
};

struct Agent_d {
  struct Agent* agent;
  float dist2;
};

void agent_init(struct Agent *agent);
void agent_print(struct Agent *agent);
void agent_initevent(struct Agent *agent, float size, float r, float g, float b);
void agent_tick(struct Agent *agent);
void agent_reproduce(struct Agent *child, struct Agent *parent, float MR, float MR2);
void agent_crossover(struct Agent *target, const struct Agent *agent1, const struct Agent *agent2);
void agent_process_health(struct Agent *agent);

#endif // AGENT_H
