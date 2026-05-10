// vk_mgmt.h — Vulkan resource and lifecycle management wrappers
//
// Provides zero-overhead C-compatible wrappers for common Vulkan resource
// patterns. Each wrapper bundles related Vulkan handles and enforces correct
// create/destroy ordering.
//
// Design principles:
// - Transparent structs (no heap allocation, compatible with calloc)
// - destroy() on a zero/NULL handle is a safe no-op
// - mapped pointers are unmap'd automatically on destroy
// - _staged helpers handle staging-buffer copy for device-local resources
//
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <vulkan/vulkan.h>

#ifdef __cplusplus
extern "C" {
#endif

// Forward declaration — VKState fully defined in vkhelpers.h
typedef struct VKState VKState;

// ============================================================================
// VKM_Buffer — VkBuffer + VkDeviceMemory + optional mapped pointer
// ============================================================================

typedef struct {
  VkBuffer       buffer;
  VkDeviceMemory memory;
  VkDeviceSize   size;
  void          *mapped;  // NULL if not host-visible
} VKM_Buffer;

// Create a buffer with its own dedicated device memory allocation.
// If (props & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT), the buffer is mapped
// and buf.mapped points to the host-visible region.
// On failure, prints an error and exits (FATAL).
VKM_Buffer vkm_buffer_create(const VKState *vk, VkDeviceSize size,
                             VkBufferUsageFlags usage,
                             VkMemoryPropertyFlags props);

// Create a device-local buffer and upload initial data via the transfer queue.
// Uses a temporary staging buffer + one-time command buffer.
VKM_Buffer vkm_buffer_create_staged(const VKState *vk, const void *data,
                                    VkDeviceSize size,
                                    VkBufferUsageFlags usage,
                                    uint32_t src_family,
                                    uint32_t dst_family);

// Destroy a buffer: unmap (if mapped), destroy buffer handle, free memory.
// Safe to call with a zeroed VKM_Buffer (no-op).
void vkm_buffer_destroy(VkDevice device, VKM_Buffer *buf);

// Resize a buffer by destroying it and creating a new one of the given size.
// Preserves usage/memory-property flags determined by whether buf->mapped was set.
// The caller is responsible for re-binding the buffer into any descriptor sets.
// Requires the device to be idle before calling.
VKM_Buffer vkm_buffer_resize(const VKState *vk, VKM_Buffer *old,
                             VkDeviceSize new_size,
                             VkBufferUsageFlags usage);

// ============================================================================
// VKM_Fence — VkFence with signalled-state init option
// ============================================================================

typedef struct {
  VkFence fence;
} VKM_Fence;

VKM_Fence vkm_fence_create(VkDevice device, bool signaled);
void      vkm_fence_destroy(VkDevice device, VKM_Fence *f);

// ============================================================================
// VKM_Semaphore
// ============================================================================

typedef struct {
  VkSemaphore semaphore;
} VKM_Semaphore;

VKM_Semaphore vkm_semaphore_create(VkDevice device);
void          vkm_semaphore_destroy(VkDevice device, VKM_Semaphore *s);

// ============================================================================
// VKM_CmdPool — VkCommandPool (owns its command buffers indirectly)
// ============================================================================

typedef struct {
  VkCommandPool pool;
} VKM_CmdPool;

VKM_CmdPool vkm_cmdpool_create(VkDevice device, uint32_t queue_family);
void        vkm_cmdpool_destroy(VkDevice device, VKM_CmdPool *cp);

// Allocate command buffers from a pool
void vkm_cmdpool_alloc_buffers(VkDevice device, VKM_CmdPool *cp,
                               uint32_t count, VkCommandBuffer *out);

// ============================================================================
// VKM_Shader — VkShaderModule
// ============================================================================

VkShaderModule vkm_shader_load(VkDevice device, const char *path);
void           vkm_shader_destroy(VkDevice device, VkShaderModule mod);

// ============================================================================
// VKM_Pipeline — VkPipeline with optional VkPipelineLayout ownership
// ============================================================================

typedef struct {
  VkPipeline       pipeline;
  VkPipelineLayout layout;   // VK_NULL_HANDLE if not owned by this pipeline
} VKM_Pipeline;

// Graphics pipeline (single call that bundles layout + pipeline creation).
// The caller is responsible for destroying shader_modules after pipeline creation.
// The caller provides the pipeline layout (with push constants already configured).
// On failure, prints error and exits (FATAL).
VKM_Pipeline vkm_pipeline_graphics_create(
    VkDevice device,
    VkPipelineLayout layout,
    VkRenderPass render_pass,
    VkShaderModule vert, VkShaderModule frag,
    const VkPipelineVertexInputStateCreateInfo *vertex_input,
    VkPrimitiveTopology topology);

// Compute pipeline (owns its layout).
VKM_Pipeline vkm_pipeline_compute_create(
    VkDevice device,
    VkDescriptorSetLayout desc_layout,
    VkShaderModule comp,
    uint32_t push_const_size);

// Create pipeline layout from descriptor set layout(s) + push constants.
// Returns VK_NULL_HANDLE on failure → exits (FATAL).
VkPipelineLayout vkm_pipeline_layout_create(
    VkDevice device,
    uint32_t set_count, const VkDescriptorSetLayout *sets,
    uint32_t push_const_size, VkShaderStageFlags push_const_stages);

// Destroy pipeline + layout (if owned).
void vkm_pipeline_destroy(VkDevice device, VKM_Pipeline *p);

// ============================================================================
// VKM_DescPool — VkDescriptorPool
// ============================================================================

typedef struct {
  VkDescriptorPool pool;
} VKM_DescPool;

VKM_DescPool vkm_descpool_create(VkDevice device, uint32_t max_sets,
                                 const VkDescriptorPoolSize *pool_sizes,
                                 uint32_t pool_size_count,
                                 bool free_sets);
void vkm_descpool_destroy(VkDevice device, VKM_DescPool *dp);

// Allocate descriptor sets from a pool.
void vkm_descset_alloc(VkDevice device, VKM_DescPool *dp,
                       uint32_t count,
                       const VkDescriptorSetLayout *layouts,
                       VkDescriptorSet *out_sets);

// Write a buffer to a descriptor set binding (single call convenience).
void vkm_descset_write_buffer(VkDevice device, VkDescriptorSet set,
                              uint32_t binding, VkDescriptorType type,
                              const VKM_Buffer *buf,
                              VkDeviceSize range);

// ============================================================================
// VKM_DescLayout — VkDescriptorSetLayout
// ============================================================================

VkDescriptorSetLayout vkm_desclayout_create(VkDevice device,
    uint32_t binding_count,
    const VkDescriptorSetLayoutBinding *bindings);
void vkm_desclayout_destroy(VkDevice device, VkDescriptorSetLayout layout);

// ============================================================================
// VKM_QueryPool
// ============================================================================

VkQueryPool vkm_querypool_create(VkDevice device, VkQueryType type,
                                 uint32_t count);
void        vkm_querypool_destroy(VkDevice device, VkQueryPool pool);

// ============================================================================
// Miscellaneous utilities
// ============================================================================

// Find a suitable memory type index.
uint32_t vkm_find_memory_type(VkPhysicalDevice phys_device,
                              uint32_t type_filter,
                              VkMemoryPropertyFlags props);

#ifdef __cplusplus
}
#endif
