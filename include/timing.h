// timing.h — unified per-frame timing for simulation + GPU + draw
#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct FrameTiming {
  // ── SIMULATION PIPELINE (sequential CPU phases, ms) ──
  float food_update;      // world_update_food
  float spatial_sort;     // world_sortGrid (counting sort)
  float compute_submit;   // world_submit_compute (vkQueueSubmit for prev dispatch)
  float input_staging;    // agent_set_inputs + GPU upload + spike drain + health apply
  float gpu_wait;         // CPU waiting for vkWaitSemaphores (compute)
  float output_processing;// world_processOutputs + world_apply_food_requests
  float death_repro;      // death distribution + movie + reproduction + random bots
  float flush_staging;    // free dead agents, add newborns, grow arrays, stage brains
  float record_compute;   // vkBegin/vkEnd/vkSubmit for next compute dispatch

  // ── DRAW PIPELINE (sequential CPU phases, ms) ──
  float draw_upload; // update_agents + update_food (memcpy → SSBOs)
  float draw_record; // vkBegin/vkEnd command buffer + vkQueueSubmit

  // ── GPU MEASUREMENTS (previous-frame timestamps, 0 if unavailable) ──
  float gpu_compute_ms; // compute shader wall time on GPU
  float gpu_draw_ms;    // draw command wall time on GPU

  // ── Frame total (measured from main loop, excluding FPS limiter) ──
  float frame_total_ms;

  // ── Active agent count (updated each frame) ──
  uint32_t agent_count;
} FrameTiming;

#ifdef __cplusplus
}
#endif
