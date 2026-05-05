// timing.h — unified per-frame timing for simulation + GPU + draw
#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct FrameTiming {
  // ── SIMULATION PIPELINE (sequential CPU phases, ms) ──
  float food_update;    // world_update_food
  float spatial_sort;   // world_sortGrid (counting sort)
  float submit_compute; // vkQueueSubmit (async, ~instant)
  float input_staging;  // dispatch: agent_set_inputs + GPU upload
  float gpu_wait;       // CPU waiting for vkWaitForFences (compute)
  float output_physics; // dispatch: GPU download + physics + render populate
  float death_repro;    // death distribution + movie + reproduction + random bots
  float flush_staging;  // free dead agents, add newborns, grow arrays
  float record_compute; // vkBegin/vkEnd/vkSubmit for next compute dispatch

  // ── DRAW PIPELINE (sequential CPU phases, ms) ──
  float draw_upload; // update_agents + update_food (memcpy → SSBOs)
  float draw_record; // vkBegin/vkEnd command buffer + vkQueueSubmit

  // ── GPU MEASUREMENTS (previous-frame timestamps, 0 if unavailable) ──
  float gpu_compute_ms; // compute shader wall time on GPU
  float gpu_draw_ms;    // draw command wall time on GPU

  // ── Frame total (measured from main loop) ──
  float frame_total_ms; // wall clock of one full iteration (sim + draw + present)

  // ── DATA VOLUMES (for bandwidth display) ──
  uint32_t agent_count;
  uint32_t input_bytes;  // agent_count × 48 × 4
  uint32_t output_bytes; // agent_count × 48 × 4
  uint32_t render_bytes; // agent_count × sizeof(AgentInstance)
} FrameTiming;

#ifdef __cplusplus
}
#endif
