#ifndef WORLD_H
#define WORLD_H

#include <stdint.h>
#include <time.h>

#include "settings.h"
#include "vec.h"

struct World {
  int32_t modcounter; // temp not private
  int32_t current_epoch;
  int32_t idcounter;
  int32_t stopSim;
  int32_t numAgentsAdded; // counts how many agents have been artifically added per
                      // reporting iteration

  // food
  int32_t FW;
  int32_t FH;
  int32_t fx;
  int32_t fy;
  float food[WIDTH / CZ][HEIGHT / CZ];
  // if environment is closed, then no random bots are added per time interval
  int32_t closed; 

  int32_t touch;

  struct timespec startTime;      // used for tracking fps
  struct timespec totalStartTime; // used for deciding when to quit the simulation

  struct AVec agents;
  // When agents get added to the world, they go the this AVec first, then
  // they get pushed to the agents array
  struct AVec agents_staging;
};

void world_init(struct World *world);
void world_printState(struct World *world);
void world_update(struct World *world);
void world_growFood(struct World *world, int32_t x, int32_t y);
void world_setInputsRunBrain(struct World *world);
void world_processOutputs(struct World *world);
void world_addRandomBots(struct World *world, int32_t num);
void world_addCarnivore(struct World *world);
void world_addNewByCrossover(struct World *world);
void world_reproduce(struct World *world, int32_t ai, float MR, float MR2);
void world_writeReport(struct World *world);
void world_reset(struct World *world);
void world_processMouse(struct World *world, int32_t button, int32_t state, int32_t x, int32_t y);
int32_t world_numCarnivores(struct World *world);
int32_t world_numHerbivores(struct World *world);
int32_t world_numAgents(struct World *world);

#endif // WORLD_H
