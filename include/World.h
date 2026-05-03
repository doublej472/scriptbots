#ifndef WORLD_H
#define WORLD_H

#include <stdint.h>
#include <time.h>

#include "queue.h"
#include "settings.h"
#include "vec.h"
#include "Food.h"

// Forward: VKState for GPU brain
typedef struct VKState VKState;

#ifdef __cplusplus
extern "C" {
#endif

#define AGENT_BUCKETS (1024 * 64)  // 65536 — must exceed total occupied grid cells (~16K) to minimize hash collisions

struct AgentQueueItem {
  struct World *world;
  size_t start;
  size_t end;
};

struct World {
  int32_t modcounter; // temp not private
  int32_t current_epoch;
  int32_t stopSim;
  int32_t movieMode;
  int32_t numAgentsAdded; // counts how many agents have been artifically added
                          // per reporting iteration

  // food
  struct FoodGrid foodGrid;

  // if environment is closed, then no random bots are added per time interval
  int32_t closed;

  int32_t touch;
  int32_t brain_slot;  // 0 or 1 — which double-buffer slot current GPU compute reads/writes

  VKState *brain_gpu;   // GPU brain state (null if no GPU)

  struct Queue *queue;

  struct AVec agents;
  // When agents get added to the world, they go to this AVec first, then
  // they get pushed to the agents array
  struct AVec agents_staging;

  // Stable agent array: agents[] is NEVER reordered (GPU brain indexed by position).
  // sorted_agents[i] = pointer to agent at sorted position i (for spatial iteration).
  struct Agent **sorted_agents;
  size_t sorted_capacity;
  size_t sorted_size;

  // End-index into sorted_agents per spatial bucket [0, bucket_size)
  size_t agent_grid[AGENT_BUCKETS];

  // Per-frame timing (ms) — reset each frame
  double time_sort, time_inputs, time_compute, time_outputs;
  double time_staging, time_flush, time_record, time_total_frame;
};

void world_init(struct World *world, int initFood, size_t numbots);
void world_flush_staging(struct World *world);
void world_update(struct World *world);
void world_setInputsRunBrain(struct World *world);
void world_submit_compute(struct World *world);
void world_record_compute(struct World *world);
void world_processOutputs(struct World *world);
void world_addRandomBots(struct World *world, int32_t num);
void world_addCarnivore(struct World *world);
void world_reproduce(struct World *world, struct Agent *a);
void world_writeReport(struct World *world);
void world_free_agents(struct World *world);
void world_reset(struct World *world);
void world_processMouse(struct World *world, int32_t button, int32_t state, int32_t x, int32_t y);
void world_sortGrid(struct World *world);
int32_t world_numCarnivores(struct World *world);
int32_t world_numHerbivores(struct World *world);
int32_t world_numAgents(struct World *world);
float world_getTotalFood(struct World *world);

void agent_input_processor(void *arg);
void agent_output_processor(void *arg);

#ifdef __cplusplus
}
#endif

#endif // WORLD_H
