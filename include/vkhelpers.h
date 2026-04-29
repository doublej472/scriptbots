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

// Forward declare GLFWwindow to avoid pulling in GLFW headers in C compilation units
typedef struct GLFWwindow GLFWwindow;

// ---- Maximum constants ----
#define VK_MAX_AGENTS   30000
#define VK_MAX_FOOD_VERTS (FOOD_SQUARES_WIDTH * FOOD_SQUARES_HEIGHT * 6)

// ---- Per-agent instance data uploaded to GPU each frame (SSBO) ----
// Must match the Agent struct in shaders byte-for-byte (all floats, no vec3).
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

// ---- Callback for ImGui rendering (called from vkdraw_frame before ending render pass) ----
typedef void (*vkdraw_imgui_cb)(VkCommandBuffer cmd, void *user_data);

// ---- Camera uniform buffer ----
typedef struct CameraUBO {
    float mvp[16];  // 4x4 column-major orthographic projection
} CameraUBO;

// ---- Food grid vertex ----
typedef struct FoodVertex {
    float x, y;
    float r, g, b;
} FoodVertex;

// ---- Push constant layouts (must match shaders) ----
typedef struct PushConstCircle {
    float botRadius;
    uint32_t agentOffset;
} PushConstCircle;

typedef struct PushConstLines {
    float coneLength;
    float spikeScale;
    uint32_t agentOffset;
} PushConstLines;

typedef struct PushConstHud {
    uint32_t agentOffset;
} PushConstHud;

// ---- Vulkan initialization ----
typedef struct VKState VKState;  // opaque, defined in vkinit.c

// Initialize all Vulkan objects. Returns NULL on failure.
VKState *vkinit_create(GLFWwindow *window);

// Destroy all Vulkan objects.
void vkinit_destroy(VKState *vk);

// Called when the swapchain needs recreation (resize, out-of-date).
// Returns 1 on success, 0 if the window is minimized (try again later).
int vkinit_recreate_swapchain(VKState *vk);

// Get render pass handle (for ImGui initialization).
VkRenderPass vkinit_get_render_pass(VKState *vk);

// Get device handle.
VkDevice vkinit_get_device(VKState *vk);

// Get physical device handle.
VkPhysicalDevice vkinit_get_phys_device(VKState *vk);

// Get queue family index.
uint32_t vkinit_get_queue_family(VKState *vk);

// Get graphics queue.
VkQueue vkinit_get_queue(VKState *vk);

// Get command buffer (primary, reset each frame then re-recorded).
VkCommandBuffer vkinit_get_command_buffer(VKState *vk);

// Get number of command buffers (always 1 in our simple setup).
uint32_t vkinit_get_command_buffer_count(VKState *vk);

// Get Vulkan instance.
VkInstance vkinit_get_instance(VKState *vk);

// Get swapchain.
VkSwapchainKHR vkinit_get_swapchain(VKState *vk);

// Get swapchain extent.
void vkinit_get_extent(VKState *vk, uint32_t *width, uint32_t *height);

// Check if swapchain needs recreation (set by vkdraw_frame on OUT_OF_DATE).
int  vkinit_needs_recreation(VKState *vk);
void vkinit_set_needs_recreation(VKState *vk);

#ifdef __cplusplus
}
#endif
