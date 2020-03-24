#ifndef WORLD_H
#define WORLD_H

#include <time.h>

#include "settings.h"
#include "vec.h"

struct World {
  int modcounter; // temp not private
  int current_epoch;
  int idcounter;
  int stopSim;
  int numAgentsAdded; // counts how many agents have been artifically added per
                      // reporting iteration

  // food
  int FW;
  int FH;
  int fx;
  int fy;
  float food[WIDTH / CZ][HEIGHT / CZ];
  // if environment is closed, then no random bots are added per time interval
  int closed; 

  int touch;

  struct timespec startTime;      // used for tracking fps
  struct timespec totalStartTime; // used for deciding when to quit the simulation

  struct AVec agents;
};

void world_init(struct World *world);
void world_printState(struct World *world);
void world_update(struct World *world);
void world_growFood(struct World *world, int x, int y);
void world_setInputsRunBrain(struct World *world);
void world_processOutputs(struct World *world);
void world_addRandomBots(struct World *world, int num);
void world_addCarnivore(struct World *world);
void world_addNewByCrossover(struct World *world);
void world_reproduce(struct World *world, int ai, float MR, float MR2);
void world_writeReport(struct World *world);
void world_reset(struct World *world);
void world_processMouse(struct World *world, int button, int state, int x, int y);
int world_numCarnivores(struct World *world);
int world_numHerbivores(struct World *world);
int world_numAgents(struct World *world);

#endif // WORLD_H
