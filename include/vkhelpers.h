// vkhelpers.h — Vulkan bootstrap utilities and shared types
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <vulkan/vulkan.h>
#include "settings.h"
#include "Food.h"

#ifdef __cplusplus
extern "C" {
#endif

// Forward declare GLFWwindow and World
typedef struct GLFWwindow GLFWwindow;
struct World;

// ---- Constants ----
#define VK_MAX_AGENTS   200000
#define VK_MAX_FOOD_VERTS (FOOD_SQUARES_WIDTH * FOOD_SQUARES_HEIGHT * 6)
#define VK_MAX_FRAMES_IN_FLIGHT 1  // single FIF: shared agent/food/camera buffers can't be double-buffered

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
    int   boost;
    int   select_flag;
    float indicator_r, indicator_g, indicator_b;
    float indicator_size;
} AgentInstance;

// ---- Camera uniform buffer ----
typedef struct CameraUBO {
    float mvp[16];
} CameraUBO;

// ---- Food grid vertex ----
typedef struct FoodVertex {
    float x, y;
    float r, g, b;
} FoodVertex;

// ---- Push constant layouts (must match shaders) ----
typedef struct PushConstCircle {
    float    botRadius;
    uint32_t agentOffset;
    uint32_t type;       // 0 = body, 1 = selection, 2 = indicator event
} PushConstCircle;

typedef struct PushConstLines {
    float    coneLength;
    float    spikeScale;
    uint32_t agentOffset;
} PushConstLines;

typedef struct PushConstHud {
    uint32_t agentOffset;
} PushConstHud;

// ---- Callback for ImGui rendering (called from vkdraw_frame before ending render pass) ----
typedef void (*vkdraw_imgui_cb)(VkCommandBuffer cmd, void *user_data);

// ---- View state passed to vkdraw_frame (avoids global coupling) ----
typedef struct VKViewState {
    int   wwidth, wheight;
    float scalemult;
    float xtranslate, ytranslate;
    int   drawfood;
    struct Base *base;
} VKViewState;

// ---- VKState ----
//
// Concurrency model: all submission happens on a single VkQueue, serialised by
// FIFO ordering.  Graphics and compute share no GPU buffers — descriptor sets,
// pipelines, and data buffers are fully partitioned:
//
//   Graphics (vkdraw.c)  ←→  queue cmd_buf[0], fences in_flight / render_done
//   Compute  (vkbrain.c)  ←→  queue staging_cmd / cmd_compute[0..1],
//                              fences compute_fence / staging_fence
//
// CPU-side, world_update and vkdraw_frame run sequentially on the main thread.
//
typedef struct VKState {
    // === Device core (shared, immutable after init) ===
    VkInstance               instance;
    VkDebugUtilsMessengerEXT debug_messenger;
    VkPhysicalDevice         phys_device;
    VkDevice                 device;
    VkQueue                  queue;          // single queue: graphics + compute
    uint32_t                 queue_family;
    VkSurfaceKHR             surface;
    VkRenderPass             render_pass;
    GLFWwindow              *window;

    // === Swapchain (vkswap.c) ===
    VkSwapchainKHR    swapchain;
    VkImage          *sc_images;
    VkImageView      *sc_views;
    VkFramebuffer    *sc_framebufs;
    uint32_t          sc_count;
    VkFormat          sc_format;
    VkExtent2D        sc_extent;
    int               needs_recreation;
    int               framebuffer_resized;   // set by GLFW framebuffer size callback

    // === Graphics pipeline (vkdraw.c) ===
    VkCommandPool   cmd_pool;
    VkCommandBuffer cmd_buf[VK_MAX_FRAMES_IN_FLIGHT];

    uint32_t       current_frame;
    VkSemaphore    image_avail[VK_MAX_FRAMES_IN_FLIGHT];
    VkSemaphore   *render_done;            // one per swapchain image
    uint32_t       render_done_count;
    VkFence        in_flight[VK_MAX_FRAMES_IN_FLIGHT];

    VkDescriptorSetLayout desc_layout;     // binding 0: cam UBO, 1: agent SSBO
    VkDescriptorPool      desc_pool;
    VkDescriptorSet       desc_set;
    VkPipelineLayout      pipeline_layout;

    VkPipeline pipe_circle, pipe_lines, pipe_hud, pipe_food;

    VkBuffer       cam_ubo_buf;  VkDeviceMemory cam_ubo_mem;  float         *mapped_cam;
    VkBuffer       agent_buf;    VkDeviceMemory agent_mem;    AgentInstance *mapped_agents;
    VkBuffer       food_vbuf;    VkDeviceMemory food_vmem;    FoodVertex    *mapped_food;

    // Static meshes (uploaded once, device-local)
    VkBuffer       mesh_circle_vb;  VkDeviceMemory mesh_circle_mem;  uint32_t mesh_circle_verts;
    VkBuffer       mesh_lines_vb;   VkDeviceMemory mesh_lines_mem;   uint32_t mesh_lines_verts;
    VkBuffer       mesh_hud_vb;     VkDeviceMemory mesh_hud_mem;

    // === Brain compute pipeline (vkbrain.c) ===
    VkPipeline             brain_pipeline;
    VkPipelineLayout       brain_layout;
    VkDescriptorSetLayout  brain_desc_layout;
    VkDescriptorPool       brain_pool;
    VkDescriptorSet        brain_set[2];   // double-buffered I/O slots

    VkCommandBuffer staging_cmd;            // dedicated: staging→weights copies
    VkCommandBuffer cmd_compute[2];         // dispatch recording, one per slot
    VkFence         compute_fence;          // dispatch completion
    VkFence         staging_fence;          // staging copy completion
    uint32_t        brain_frame;

    VkBuffer       brain_weights_buf;  VkDeviceMemory brain_weights_mem;
    VkBuffer       brain_inputs_buf[2];   VkDeviceMemory brain_inputs_mem[2];
    VkBuffer       brain_outputs_buf[2];  VkDeviceMemory brain_outputs_mem[2];
    VkBuffer       brain_staging_buf;     VkDeviceMemory brain_staging_mem;

    float *mapped_inputs[2];
    float *mapped_outputs[2];
    float *mapped_staging;

    uint32_t brain_capacity;
    uint32_t brain_max_capacity;
    uint32_t staging_count;
    uint32_t *staging_indices;
    uint32_t staging_capacity;
} VKState;

// ---- VKState accessors ----
VKState       *vkinit_create(GLFWwindow *window);
void           vkinit_destroy(VKState *vk);

VkRenderPass     vkinit_get_render_pass(VKState *vk);
VkDevice         vkinit_get_device(VKState *vk);
VkPhysicalDevice vkinit_get_phys_device(VKState *vk);
uint32_t         vkinit_get_queue_family(VKState *vk);
VkQueue          vkinit_get_queue(VKState *vk);
VkCommandBuffer  vkinit_get_command_buffer(VKState *vk);
uint32_t         vkinit_get_command_buffer_count(VKState *vk);
VkInstance       vkinit_get_instance(VKState *vk);

// ---- Swapchain accessors ----
#include "vkswap.h"
static inline int            vkinit_recreate_swapchain(VKState *vk) { return vkswap_recreate(vk); }
static inline int            vkinit_needs_recreation(VKState *vk)   { return vkswap_needs_recreation(vk); }
static inline void           vkinit_set_needs_recreation(VKState *vk){ vkswap_set_needs_recreation(vk); }
static inline VkSwapchainKHR vkinit_get_swapchain(VKState *vk)       { return vkswap_get_swapchain(vk); }
static inline void           vkinit_get_extent(VKState *vk, uint32_t *w, uint32_t *h) { vkswap_get_extent(vk, w, h); }

// ---- Shared utilities ----
uint32_t       vk_find_memory_type(VkPhysicalDevice phys_device, uint32_t typeFilter, VkMemoryPropertyFlags props);
VkShaderModule vk_load_shader(VkDevice device, const char *path);

// ---- GPU Brain API ----
void vkbrain_init(VKState *vk, uint32_t initial_cap);
void vkbrain_destroy(VKState *vk);
void vkbrain_upload_all(VKState *vk, struct World *world);
void vkbrain_stage_brain(VKState *vk, uint32_t agent_idx, const float *brain);
void vkbrain_record_dispatch(VKState *vk, int agent_count, uint32_t read_slot);
uint32_t vkbrain_capacity(VKState *vk);
void vkbrain_flush_staging(VKState *vk);
void vkbrain_drain_staging(VKState *vk);
void vkbrain_ensure_capacity(VKState *vk, uint32_t needed);

#ifdef __cplusplus
}
#endif
