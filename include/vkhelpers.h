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

// Forward declare GLFWwindow
typedef struct GLFWwindow GLFWwindow;

// ---- Constants ----
#define VK_MAX_AGENTS   30000
#define VK_MAX_FOOD_VERTS (FOOD_SQUARES_WIDTH * FOOD_SQUARES_HEIGHT * 6)
#define VK_MAX_FRAMES_IN_FLIGHT 2

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

// ---- VKState: full definition, shared by vkinit.c and vkdraw.c ----
// External users (vkview.cpp) use only the opaque pointer + accessors.
typedef struct VKState {
    VkInstance               instance;
    VkDebugUtilsMessengerEXT debug_messenger;
    VkPhysicalDevice         phys_device;
    VkDevice                 device;
    VkQueue                  queue;
    uint32_t                 queue_family;
    VkSurfaceKHR             surface;

    VkSwapchainKHR    swapchain;
    VkImage          *sc_images;
    VkImageView      *sc_views;
    VkFramebuffer    *sc_framebufs;
    uint32_t          sc_count;
    VkFormat          sc_format;
    VkExtent2D        sc_extent;

    VkRenderPass  render_pass;

    GLFWwindow    *window;
    int            needs_recreation;

    VkCommandPool   cmd_pool;
    VkCommandBuffer cmd_buf[VK_MAX_FRAMES_IN_FLIGHT];

    uint32_t       current_frame;
    VkSemaphore    image_avail[VK_MAX_FRAMES_IN_FLIGHT];
    VkSemaphore   *render_done;      // one per swapchain image
    VkFence        in_flight[VK_MAX_FRAMES_IN_FLIGHT];

    VkDescriptorSetLayout desc_layout;
    VkDescriptorPool      desc_pool;
    VkDescriptorSet       desc_set;
    VkPipelineLayout      pipeline_layout;

    VkPipeline pipe_circle;
    VkPipeline pipe_lines;
    VkPipeline pipe_hud;
    VkPipeline pipe_food;

    VkBuffer       cam_ubo_buf;
    VkDeviceMemory cam_ubo_mem;
    VkBuffer       agent_buf;
    VkDeviceMemory agent_mem;
    VkBuffer       food_vbuf;
    VkDeviceMemory food_vmem;

    VkBuffer       mesh_circle_vb;
    VkDeviceMemory mesh_circle_mem;
    uint32_t       mesh_circle_verts;

    VkBuffer       mesh_lines_vb;
    VkDeviceMemory mesh_lines_mem;
    uint32_t       mesh_lines_verts;

    VkBuffer       mesh_hud_vb;
    VkDeviceMemory mesh_hud_mem;
} VKState;

// ---- VKState accessors (for external users; internals use fields directly) ----
VKState       *vkinit_create(GLFWwindow *window);
void           vkinit_destroy(VKState *vk);
int            vkinit_recreate_swapchain(VKState *vk);
int            vkinit_needs_recreation(VKState *vk);
void           vkinit_set_needs_recreation(VKState *vk);

VkRenderPass     vkinit_get_render_pass(VKState *vk);
VkDevice         vkinit_get_device(VKState *vk);
VkPhysicalDevice vkinit_get_phys_device(VKState *vk);
uint32_t         vkinit_get_queue_family(VKState *vk);
VkQueue          vkinit_get_queue(VKState *vk);
VkCommandBuffer  vkinit_get_command_buffer(VKState *vk);
uint32_t         vkinit_get_command_buffer_count(VKState *vk);
VkInstance       vkinit_get_instance(VKState *vk);
VkSwapchainKHR   vkinit_get_swapchain(VKState *vk);
void             vkinit_get_extent(VKState *vk, uint32_t *w, uint32_t *h);

#ifdef __cplusplus
}
#endif
