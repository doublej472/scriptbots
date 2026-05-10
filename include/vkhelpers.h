// vkhelpers.h — Vulkan bootstrap utilities and shared types
#pragma once

#include "Food.h"
#include "settings.h"
#include "vk_mgmt.h"
#include <stdbool.h>
#include <stdint.h>
#include <vulkan/vulkan.h>

#ifdef __cplusplus
extern "C" {
#endif

// Forward declare GLFWwindow and World
typedef struct GLFWwindow GLFWwindow;
struct World;

// ---- Constants ----
#define VK_MAX_FRAMES_IN_FLIGHT 1 // single FIF: shared agent/food/camera buffers can't be double-buffered
// Agent SSBO capacity is now dynamic (vk->agent_capacity), sized to NUMBOTS at init
// and grown on demand when population exceeds capacity.
// One chunk's weight buffer at ~1 GB — derives agent count from brain size
#define BRAIN_CHUNK_AGENTS ((uint32_t)((1024ULL * 1024 * 1024) / (BRAIN_WEIGHT_UINTS * 4)))

// ---- Per-agent instance data uploaded to GPU each frame (SSBO) ----
// Layout must match the Agent struct in shaders byte-for-byte.
typedef struct AgentInstance {
  float pos_x, pos_y;
  float color_r, color_g, color_b;
  float angle;
  float health;
  float herbivore;
  float soundmul;
  float spike_length;
  int boost;
  int select_flag;
  float indicator_r, indicator_g, indicator_b;
  float indicator_size;
} AgentInstance;

// ---- Camera uniform buffer ----
typedef struct CameraUBO {
  float mvp[16];
} CameraUBO;

// ---- Food grid (SSBO-based, no vertex buffer) ----
// Food amounts are uploaded as an SSBO; the fragment shader samples it
typedef struct PushConstFood {
  float worldW, worldH; // WIDTH, HEIGHT
  float cellSize;       // CZ
  float foodMax;        // FOODMAX
} PushConstFood;

// ---- Push constant layouts (must match shaders) ----
typedef struct PushConstCircle {
  float botRadius;
  uint32_t agentOffset;
  uint32_t type; // 0 = body, 1 = selection, 2 = indicator event
} PushConstCircle;

typedef struct PushConstLines {
  float coneLength;
  float spikeScale;
  uint32_t agentOffset;
} PushConstLines;

typedef struct PushConstHud {
  uint32_t agentOffset;
} PushConstHud;

// ---- Callback for ImGui rendering (called from vkdraw_frame before ending render pass) ----
typedef void (*vkdraw_imgui_cb)(VkCommandBuffer cmd, void *user_data);

// ---- View state passed to vkdraw_frame (avoids global coupling) ----
typedef struct VKViewState {
  int wwidth, wheight;
  float scalemult;
  float xtranslate, ytranslate;
  int drawfood;
  int draw_agents;
  struct Base *base;
} VKViewState;

// ---- Brain chunk — one self-contained GPU buffer group (~1 GB) ----
typedef struct BrainChunk {
  VKM_Buffer       weights;      // device-local, ~1 GB
  VKM_Buffer       inputs[2];    // host-visible, double-buffered
  VKM_Buffer       outputs[2];   // host-visible, double-buffered
  VKM_DescPool     desc_pool;
  VkDescriptorSet  desc_set[2];  // double-buffered I/O slots
  uint32_t         capacity;      // max agents this chunk can hold
  uint32_t         alive_count;   // indices [0..alive_count-1] are live
  struct Agent   **slot_owner;    // slot_owner[i] = agent at this slot (for move updates)
  bool             weights_staged;// true when weights were copied (release queued on xfer queue)
} BrainChunk;

// ---- VKState ----
//
// Concurrency model: compute and graphics run on dedicated VkQueues for
// overlapping execution.  Compute→graphics dependency is expressed via a
// timeline semaphore (compute_timeline) signalled by the compute queue and
// waited on by both the graphics queue and the CPU (for output readback).
// Staging copies use transfer-queue fences (staging_fence[2]).
// CPU-side, world_update and vkdraw_frame run sequentially on the main thread.
//
typedef struct VKState {
  // === Device core (shared, immutable after init) ===
  VkInstance instance;
  VkDebugUtilsMessengerEXT debug_messenger;
  VkPhysicalDevice phys_device;
  VkDevice device;
  // Dedicated queues for GPU concurrency (SDMA, async compute)
  VkQueue gfx_queue;
  uint32_t gfx_family;
  VkQueue compute_queue;
  uint32_t compute_family;
  VkQueue transfer_queue;
  uint32_t transfer_family;
  VkSurfaceKHR surface;
  VkRenderPass render_pass;
  GLFWwindow *window;

  // === Swapchain (vkswap.c) ===
  VkSwapchainKHR swapchain;
  VkImage *sc_images;
  VkImageView *sc_views;
  VkFramebuffer *sc_framebufs;
  uint32_t sc_count;
  VkFormat sc_format;
  VkExtent2D sc_extent;
  int needs_recreation;
  int framebuffer_resized; // set by GLFW framebuffer size callback

  // === Graphics pipeline (vkdraw.c) ===
  VKM_CmdPool cmd_pool_gfx;
  VKM_CmdPool cmd_pool_compute;
  VKM_CmdPool cmd_pool_transfer;
  VkCommandBuffer cmd_buf[VK_MAX_FRAMES_IN_FLIGHT];

  uint32_t current_frame;
  VKM_Semaphore image_avail[VK_MAX_FRAMES_IN_FLIGHT];
  VKM_Semaphore *render_done; // one per swapchain image
  uint32_t render_done_count;
  VKM_Fence in_flight[VK_MAX_FRAMES_IN_FLIGHT];

  VkDescriptorSetLayout desc_layout; // binding 0: cam UBO, 1: agent SSBO
  VKM_DescPool desc_pool;
  VkDescriptorSet desc_set;
  VkPipelineLayout pipeline_layout;

  VKM_Pipeline pipe_circle, pipe_lines, pipe_hud, pipe_food;

  VKM_Buffer cam_ubo;
  VKM_Buffer agent_buf;
  uint32_t agent_capacity; // SSBO size in agents (resized on demand)
  VKM_Buffer food_buf;
  VkDescriptorSet desc_set_food; // binding 0=camera, 1=food SSBO

  // Static meshes (uploaded once, device-local)
  VKM_Buffer mesh_circle;
  uint32_t mesh_circle_verts;
  VKM_Buffer mesh_lines;
  uint32_t mesh_lines_verts;
  VKM_Buffer mesh_hud;

  // === Brain compute pipeline (vkbrain.c) — multi-chunk ===
  VKM_Pipeline brain_pipeline;
  VkDescriptorSetLayout brain_desc_layout; // shared by all chunks

  BrainChunk *chunks;
  uint32_t chunk_count;
  uint32_t chunk_capacity; // allocated array size

  VKM_Buffer brain_staging;
  uint32_t *staging_chunks;  // chunk index per staged brain
  uint32_t *staging_indices; // slot index per staged brain
  uint32_t staging_count;
  uint32_t staging_capacity;

  VkCommandBuffer staging_cmd[2]; // double-buffered (synced with staging_fence)
  VkCommandBuffer cmd_compute[2]; // dispatch recording, one per slot
  // Timeline semaphore for compute→graphics/CPU synchronization.
  // Compute queue signals with incrementing value; graphics queue and CPU wait.
  VkSemaphore compute_timeline;
  uint64_t    compute_timeline_value; // next value to signal (monotonically increasing)

  VKM_Fence staging_fence[2];       // double-buffered staging fence
  uint32_t staging_fence_idx;     // next fence to use
  uint32_t brain_frame;

  // === Timestamp queries (GPU profiling) ===
  VkQueryPool timestamp_pool; // 8 slots: 2 × (compute_start, compute_end, gfx_start, gfx_end)
  float timestamp_period;     // device limits in nanoseconds
  bool timestamp_supported;
  uint32_t timestamp_frame_idx; // 0 or 1 — which slot-set we write this frame (read from 1-idx)
} VKState;

// ---- VKState accessors ----
VKState *vkinit_create(GLFWwindow *window, uint32_t numbots);
VKState *vkinit_create_headless(uint32_t numbots);
void vkinit_destroy(VKState *vk);
void vkinit_destroy_headless(VKState *vk);

VkRenderPass vkinit_get_render_pass(VKState *vk);
VkDevice vkinit_get_device(VKState *vk);
VkPhysicalDevice vkinit_get_phys_device(VKState *vk);
uint32_t vkinit_get_gfx_family(VKState *vk);
VkQueue vkinit_get_gfx_queue(VKState *vk);
VkCommandBuffer vkinit_get_command_buffer(VKState *vk);
VkInstance vkinit_get_instance(VKState *vk);

// ---- Swapchain accessors ----
#include "vkswap.h"
static inline int vkinit_recreate_swapchain(VKState *vk) { return vkswap_recreate(vk); }
static inline int vkinit_needs_recreation(VKState *vk) { return vkswap_needs_recreation(vk); }
static inline void vkinit_set_needs_recreation(VKState *vk) { vkswap_set_needs_recreation(vk); }
static inline VkSwapchainKHR vkinit_get_swapchain(VKState *vk) { return vkswap_get_swapchain(vk); }
static inline void vkinit_get_extent(VKState *vk, uint32_t *w, uint32_t *h) { vkswap_get_extent(vk, w, h); }

// ---- Shared utilities (thin wrappers over vk_mgmt) ----
static inline uint32_t vk_find_memory_type(VkPhysicalDevice phys_device, uint32_t typeFilter,
                                           VkMemoryPropertyFlags props) {
  return vkm_find_memory_type(phys_device, typeFilter, props);
}
static inline VkShaderModule vk_load_shader(VkDevice device, const char *path) {
  return vkm_shader_load(device, path);
}

// ---- GPU Brain API ----
void vkbrain_init(VKState *vk, uint32_t initial_cap);
void vkbrain_destroy(VKState *vk);
void vkbrain_reset(VKState *vk);
void vkbrain_reset_counts(VKState *vk);
void vkbrain_upload_all(VKState *vk, struct World *world);
bool vkbrain_assign_slot(VKState *vk, struct Agent *a, uint32_t *out_chunk, uint32_t *out_slot);
void vkbrain_move_slot(VKState *vk, uint32_t from_c, uint32_t from_s, uint32_t to_c, uint32_t to_s,
                       struct Agent *moved);
void vkbrain_stage_brain(VKState *vk, uint32_t chunk, uint32_t slot, const uint32_t *brain);
void vkbrain_record_dispatch(VKState *vk, uint32_t read_slot);
void vkbrain_flush_staging(VKState *vk);
void vkbrain_drain_staging(VKState *vk);

// ---- Dynamic agent SSBO resize ----
void vkdraw_resize_agents(VKState *vk, uint32_t new_capacity);
void vkdraw_frame(VKState *vk, const VKViewState *view, vkdraw_imgui_cb imgui_cb, void *imgui_user);
uint32_t vkbrain_total_capacity(VKState *vk);
void vkbrain_try_reclaim_last(VKState *vk);

#ifdef __cplusplus
}
#endif
