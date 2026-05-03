// vkswap.c — swapchain creation, resource build, recreation, and teardown
#include "vkhelpers.h"
#include "vkswap.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

// ---- Internal: create the VkSwapchainKHR handle ----
static void create_swapchain(VKState *vk) {
    VkSurfaceCapabilitiesKHR caps;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(vk->phys_device, vk->surface, &caps);

    // Handle minimized window (0 extent) — try again later
    if (caps.currentExtent.width == 0 || caps.currentExtent.height == 0) {
        vk->needs_recreation = 1;
        return;
    }

    uint32_t fmtCount;
    vkGetPhysicalDeviceSurfaceFormatsKHR(vk->phys_device, vk->surface, &fmtCount, NULL);
    VkSurfaceFormatKHR *fmts = malloc(sizeof(VkSurfaceFormatKHR) * fmtCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(vk->phys_device, vk->surface, &fmtCount, fmts);

    VkSurfaceFormatKHR chosen = fmts[0];
    for (uint32_t i = 0; i < fmtCount; i++) {
        if (fmts[i].format == VK_FORMAT_B8G8R8A8_SRGB && fmts[i].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
            { chosen = fmts[i]; break; }
    }
    free(fmts);
    vk->sc_format = chosen.format;

    uint32_t pmCount;
    vkGetPhysicalDeviceSurfacePresentModesKHR(vk->phys_device, vk->surface, &pmCount, NULL);
    VkPresentModeKHR *pms = malloc(sizeof(VkPresentModeKHR) * pmCount);
    vkGetPhysicalDeviceSurfacePresentModesKHR(vk->phys_device, vk->surface, &pmCount, pms);
    VkPresentModeKHR pmode = VK_PRESENT_MODE_FIFO_KHR;
    for (uint32_t i = 0; i < pmCount; i++)
        if (pms[i] == VK_PRESENT_MODE_MAILBOX_KHR) { pmode = VK_PRESENT_MODE_MAILBOX_KHR; break; }
    free(pms);

    VkExtent2D extent = caps.currentExtent;
    if (extent.width == UINT32_MAX) {
        int w, h; glfwGetFramebufferSize(vk->window, &w, &h);
        extent = (VkExtent2D){ (uint32_t)w, (uint32_t)h };
        if (extent.width < caps.minImageExtent.width) extent.width = caps.minImageExtent.width;
        if (extent.height < caps.minImageExtent.height) extent.height = caps.minImageExtent.height;
    }
    vk->sc_extent = extent;

    // Use minImageCount directly for MAILBOX (frames are replaced not queued)
    // For FIFO, add one to avoid stalls
    uint32_t imgCount = caps.minImageCount;
    if (pmode == VK_PRESENT_MODE_FIFO_KHR && caps.maxImageCount == 0)
        imgCount = caps.minImageCount + 1;
    if (caps.maxImageCount > 0 && imgCount > caps.maxImageCount)
        imgCount = caps.maxImageCount;

    VkSwapchainCreateInfoKHR sci = {
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface = vk->surface, .minImageCount = imgCount,
        .imageFormat = vk->sc_format, .imageColorSpace = chosen.colorSpace,
        .imageExtent = extent, .imageArrayLayers = 1,
        .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        .preTransform = caps.currentTransform,
        .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .presentMode = pmode, .clipped = VK_TRUE,
        .oldSwapchain = vk->swapchain,
    };

    VkSwapchainKHR oldSwapchain = vk->swapchain;
    if (vkCreateSwapchainKHR(vk->device, &sci, NULL, &vk->swapchain) != VK_SUCCESS) {
        fprintf(stderr, "FATAL: swapchain creation failed\n"); exit(1);
    }

    // oldSwapchain handles deferred image cleanup; destroy the handle now
    if (oldSwapchain) {
        vkDestroySwapchainKHR(vk->device, oldSwapchain, NULL);
    }
}

// ---- Public: create swapchain (wrap static helper) ----
void vkswap_create(VKState *vk) {
    create_swapchain(vk);
}

// ---- Public: build image views and framebuffers from swapchain images ----
void vkswap_build_resources(VKState *vk) {
    vkGetSwapchainImagesKHR(vk->device, vk->swapchain, &vk->sc_count, NULL);
    vk->sc_images = malloc(sizeof(VkImage) * vk->sc_count);
    vk->sc_views  = malloc(sizeof(VkImageView) * vk->sc_count);
    vk->sc_framebufs = malloc(sizeof(VkFramebuffer) * vk->sc_count);
    vkGetSwapchainImagesKHR(vk->device, vk->swapchain, &vk->sc_count, vk->sc_images);

    for (uint32_t i = 0; i < vk->sc_count; i++) {
        VkImageViewCreateInfo ivci = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = vk->sc_images[i], .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = vk->sc_format,
            .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 },
        };
        vkCreateImageView(vk->device, &ivci, NULL, &vk->sc_views[i]);

        VkFramebufferCreateInfo fci = {
            .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
            .renderPass = vk->render_pass, .attachmentCount = 1,
            .pAttachments = &vk->sc_views[i],
            .width = vk->sc_extent.width, .height = vk->sc_extent.height, .layers = 1,
        };
        vkCreateFramebuffer(vk->device, &fci, NULL, &vk->sc_framebufs[i]);
    }
}

// ---- Internal: tear down framebuffers, image views, and images (not the swapchain handle) ----
static void destroy_swapchain_resources(VKState *vk) {
    for (uint32_t i = 0; i < vk->sc_count; i++) {
        vkDestroyFramebuffer(vk->device, vk->sc_framebufs[i], NULL);
        vkDestroyImageView(vk->device, vk->sc_views[i], NULL);
    }
    free(vk->sc_framebufs); free(vk->sc_views); free(vk->sc_images);
    vk->sc_framebufs = NULL; vk->sc_views = NULL; vk->sc_images = NULL;
    vk->sc_count = 0;
}

// ---- Public: full swapchain recreation (resize, out-of-date) ----
int vkswap_recreate(VKState *vk) {
    // Drain the entire GPU before touching swapchain resources.
    // vkDeviceWaitIdle is safer than vkWaitForFences on a single fence
    // because compute (vkbrain) may have work referencing old semaphores.
    vkDeviceWaitIdle(vk->device);

    // Handle minimized window: block until the framebuffer has a real size.
    // Per the Vulkan Tutorial, this avoids busy-looping each frame.
    {
        int fbw = 0, fbh = 0;
        glfwGetFramebufferSize(vk->window, &fbw, &fbh);
        while (fbw == 0 || fbh == 0) {
            glfwWaitEvents();
            glfwGetFramebufferSize(vk->window, &fbw, &fbh);
        }
    }

    // Destroy old framebuffers and image views (swapchain destroyed in create_swapchain)
    destroy_swapchain_resources(vk);
    create_swapchain(vk);

    // create_swapchain may still bail if the surface reports zero extent
    // (defensive check — shouldn't happen after the wait loop above)
    if (vk->needs_recreation)
        return 0;

    // Recreate swapchain resources (framebuffers with new extent)
    vkswap_build_resources(vk);

    // Grow render_done array if needed (never shrink — old semaphores in use by present)
    if (vk->sc_count > vk->render_done_count) {
        VkSemaphoreCreateInfo sci = { .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
        VkSemaphore *old = vk->render_done;
        uint32_t old_count = vk->render_done_count;
        vk->render_done = malloc(sizeof(VkSemaphore) * vk->sc_count);
        vk->render_done_count = vk->sc_count;
        for (uint32_t i = 0; i < old_count; i++)
            vk->render_done[i] = old[i];
        for (uint32_t i = old_count; i < vk->sc_count; i++)
            vkCreateSemaphore(vk->device, &sci, NULL, &vk->render_done[i]);
        free(old);
    }

    vk->needs_recreation = 0;
    return 1;
}

// ---- Public: full swapchain teardown ----
void vkswap_destroy(VKState *vk) {
    // Destroy render_done semaphores
    if (vk->render_done) {
        for (uint32_t i = 0; i < vk->render_done_count; i++)
            vkDestroySemaphore(vk->device, vk->render_done[i], NULL);
        free(vk->render_done);
        vk->render_done = NULL;
    }

    destroy_swapchain_resources(vk);

    if (vk->swapchain) {
        vkDestroySwapchainKHR(vk->device, vk->swapchain, NULL);
        vk->swapchain = VK_NULL_HANDLE;
    }
}

// ---- Accessors ----
VkSwapchainKHR vkswap_get_swapchain(VKState *vk)     { return vk->swapchain; }
void vkswap_get_extent(VKState *vk, uint32_t *w, uint32_t *h) { *w = vk->sc_extent.width; *h = vk->sc_extent.height; }
int  vkswap_needs_recreation(VKState *vk)             { return vk->needs_recreation; }
void vkswap_set_needs_recreation(VKState *vk)         { vk->needs_recreation = 1; }
