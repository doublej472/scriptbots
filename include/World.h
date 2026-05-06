#ifndef WORLD_H
#define WORLD_H

#include <stdint.h>
#include <time.h>

#include "Food.h"
#include "queue.h"
#include "settings.h"
#include "timing.h"
#include "vec.h"

// Forward: VKState for GPU brain
typedef struct VKState VKState;

#ifdef __cplusplus
extern "C" {
#endif

#define AGENT_BUCKETS (1024 * 128) // 131072 — must exceed occupied grid cells (~81K at DIST=225)

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
  int32_t brain_slot; // 0 or 1 — which double-buffer slot current GPU compute reads/writes

  VKState *brain_gpu; // GPU brain state (null if no GPU)

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

  // Unified per-frame timing (consumed by imgui_hud)
  struct FrameTiming timing;

  // Cached population counts (updated in world_flush_staging, read by diagnostics)
  int32_t cached_herbivores;
  int32_t cached_carnivores;

  // Selection & movie-mode tracking (only one of each at a time)
  struct Agent *selected_agent; // user clicked agent (NULL if none)
  struct Agent *movie_agent;    // movie-mode tracked agent (NULL if none or off)
  size_t selected_index;        // index into agents[] (valid iff selected_agent != NULL)
  size_t movie_index;           // index into agents[] (valid iff movie_agent != NULL)

  // Contiguous input staging — recurrence (indices 18..47) resides here between
  // frames; sensory inputs (0..17) are added by the input dispatch each frame.
  // The combined 48-float vector is copied to the GPU mapped input buffer.
  float *agent_inputs; // [agents.allocated * AGENT_INPUT_FLOATS]

  // Contiguous render staging — mirrors agents[i] in GPU-ready AgentInstance layout.
  // Updated incrementally (output processing, agent creation) so vkdraw's
  // update_agents can do a single memcpy instead of scatter-reading 14KB mallocs.
  void *agent_render_data;
  uint32_t agent_render_capacity;
};

void world_alloc(struct World *world);
void world_populate(struct World *world, int initFood, size_t numbots);
void world_init(struct World *world, int initFood, size_t numbots); // calls both
void world_flush_staging(struct World *world);
void world_render_populate_all(struct World *world); // rebuild render cache after load
void world_update(struct World *world);
void world_setInputsRunBrain(struct World *world);
void world_submit_compute(struct World *world);
void world_record_compute(struct World *world);
void world_processOutputs(struct World *world);
void world_addRandomBots(struct World *world, int32_t num);
void world_addCarnivore(struct World *world);
void world_addHerbivore(struct World *world);
void world_reproduce(struct World *world, struct Agent *a);
void world_writeReport(struct World *world);

// Lock-free dispatch range helpers (called by queue.c)
void agent_input_processor_range(struct World *world, uint32_t start, uint32_t end, uint32_t write_slot);
void agent_output_processor_range(struct World *world, uint32_t start, uint32_t end, uint32_t read_slot);
void world_free_agents(struct World *world);
void world_seed_inputs(struct World *world);
void world_reset(struct World *world);
void world_processMouse(struct World *world, int32_t button, int32_t state, int32_t x, int32_t y);
void world_sortGrid(struct World *world);
int32_t world_numCarnivores(struct World *world);
int32_t world_numHerbivores(struct World *world);
int32_t world_numAgents(struct World *world);
float world_getTotalFood(struct World *world);

#ifdef __cplusplus
}
#endif

#endif // WORLD_H
