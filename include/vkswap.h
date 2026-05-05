// vkswap.h — swapchain creation, rebuild, teardown, and accessors
#pragma once

#include <stdint.h>
#include <vulkan/vulkan.h>

// Forward declaration — VKState is fully defined in vkhelpers.h
typedef struct VKState VKState;

#ifdef __cplusplus
extern "C" {
#endif

// Called by vkinit_create after render pass exists
void vkswap_create(VKState *vk);
void vkswap_build_resources(VKState *vk);

// Full recreation (resize, out-of-date, etc.)
int vkswap_recreate(VKState *vk);

// Teardown (framebuffers, views, render_done semas, swapchain handle)
void vkswap_destroy(VKState *vk);

// Accessors
VkSwapchainKHR vkswap_get_swapchain(VKState *vk);
void vkswap_get_extent(VKState *vk, uint32_t *w, uint32_t *h);
int vkswap_needs_recreation(VKState *vk);
void vkswap_set_needs_recreation(VKState *vk);

#ifdef __cplusplus
}
#endif
