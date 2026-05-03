#ifndef AGENT_H
#define AGENT_H
#include <stdint.h>

#include "brain_config.h"
#include "sim_config.h"
#include "vec2f.h"

struct Agent {
  // --- Position & movement ---
  struct Vector2f pos;
  float angle;           // facing direction (radians)
  float w1, w2;          // wheel speeds [-1,1]

  // --- Vital signs ---
  float health;          // [0, 2]
  float pending_damage;  // accumulated cross-agent damage (Phase 1 threads write, Phase 2 single-threaded apply)
  int   pending_spiked;  // OR of all spike hits this frame
  int32_t spiked;        // was hit by a spike this turn
  int32_t boost;         // is this agent boosting
  int32_t age;

  // --- Combat ---
  float spikeLength;
  float soundmul;        // shouting volume; always set to output[7]

  // --- Appearance (for rendering) ---
  float red, gre, blu;
  float indicator;       // event indicator size [0,∞), decays each frame
  float ir, ig, ib;      // indicator colours
  int32_t selectflag;    // is this agent selected?

  // --- Reproduction & inheritance ---
  float repcounter;      // when repcounter reaches 0, this bot reproduces
  int   rep;             // if this agent will reproduce next world update
  int64_t gencount;      // generation counter
  int32_t numchildren;
  float herbivore;       // [0,1] — 0 = pure carnivore, 1 = pure herbivore
  float MUTRATE1;        // how often do mutations occur?
  float MUTRATE2;        // how significant are they?
  float clockf1, clockf2;// frequencies of the two internal clocks
  float give;            // is this agent attempting to give food?

  // --- Environment sensing ---
  int32_t touch;         // is bot close to wall?
  int32_t hybrid;        // result of crossover?

  // --- Brain ---
  float in[BRAIN_INPUT_SIZE];    // sensory inputs (padded to 48)
  float out[BRAIN_OUTPUT_SIZE];  // motor outputs (padded to 48)
  float *brain;                  // malloc'd float array, BRAIN_WEIGHT_FLOATS elements

  // --- Internal ---
  uint64_t sort_alive;   // frame marker used by world_sortGrid for compaction
};

struct Agent_d {
  struct Agent *agent;
  float dist2;
};

void agent_init(struct Agent *agent);
void agent_print(struct Agent *agent);
void agent_initevent(struct Agent *agent, float size, float r, float g, float b);
void agent_tick(struct Agent *agent);
void agent_reproduce(struct Agent *child, struct Agent *parent);
void agent_process_health(struct Agent *agent);

#endif // AGENT_H
