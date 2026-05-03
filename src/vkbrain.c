// vkbrain.c — GPU brain compute pipeline, buffers, dispatch (dynamic capacity)
//
// All submissions use the single VkQueue (shared with graphics, FIFO ordered).
// No GPU buffers overlap with graphics — descriptor sets, pipelines, and data
// buffers are fully independent.  CPU-side, world_update runs before vkdraw_frame
// on the main thread; no concurrent access to any VKState field.
//
// Synchronisation overview:
//   Per-frame (all on same queue, submitted in order):
//     1. vkbrain_flush_staging     — waits for previous fence, resets, submits copy+barrier
//        (signals staging_fence)
//     2. vkQueueSubmit(dispatch cb) — HOST+TRANSFER→COMPUTE barrier → dispatch
//        → COMPUTE→HOST output barrier (signals compute_fence)
//     3. CPU waits compute_fence → reads outputs from host-coherent buffer
//     4. vkbrain_flush_staging     — next frame's staging
//
//   staging_cmd is dedicated exclusively to staging/growth copies.
//   cmd_compute[0..1] are double-buffered for dispatch recording.
//
#include "vkhelpers.h"
#include "World.h"
#include "Agent.h"
#include "settings.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STAGING_FLOATS (512 * 1024 * 1024 / 4)  // 512 MB

static void alloc_host_buffer(VKState *vk, VkDeviceSize size, VkBufferUsageFlags usage,
                               VkBuffer *buf, VkDeviceMemory *mem, float **mapped) {
    VkBufferCreateInfo bci = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = size, .usage = usage, .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    if (vkCreateBuffer(vk->device, &bci, NULL, buf) != VK_SUCCESS)
        { fprintf(stderr, "FATAL: brain host buffer creation failed\n"); exit(1); }
    VkMemoryRequirements mr;
    vkGetBufferMemoryRequirements(vk->device, *buf, &mr);
    VkMemoryAllocateInfo ai = { .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = mr.size,
        .memoryTypeIndex = vk_find_memory_type(vk->phys_device, mr.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT),
    };
    if (vkAllocateMemory(vk->device, &ai, NULL, mem) != VK_SUCCESS)
        { fprintf(stderr, "FATAL: brain host memory alloc failed\n"); exit(1); }
    vkBindBufferMemory(vk->device, *buf, *mem, 0);
    if (mapped)
        vkMapMemory(vk->device, *mem, 0, VK_WHOLE_SIZE, 0, (void**)mapped);
}

// Write descriptor sets for a given slot
static void write_brain_descriptor(VKState *vk, uint32_t slot) {
    VkDeviceSize wsize  = (VkDeviceSize)vk->brain_capacity * BRAIN_WEIGHT_FLOATS * sizeof(float);
    VkDeviceSize iosize = (VkDeviceSize)vk->brain_capacity * BRAIN_INPUT_SIZE * sizeof(float);
    VkDescriptorBufferInfo wInfo = { .buffer = vk->brain_weights_buf, .offset = 0, .range = wsize };
    VkDescriptorBufferInfo iInfo = { .buffer = vk->brain_inputs_buf[slot],  .offset = 0, .range = iosize };
    VkDescriptorBufferInfo oInfo = { .buffer = vk->brain_outputs_buf[slot], .offset = 0, .range = iosize };
    VkWriteDescriptorSet writes[] = {
        { .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = vk->brain_set[slot],
          .dstBinding = 0, .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
          .pBufferInfo = &wInfo },
        { .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = vk->brain_set[slot],
          .dstBinding = 1, .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
          .pBufferInfo = &iInfo },
        { .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = vk->brain_set[slot],
          .dstBinding = 2, .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
          .pBufferInfo = &oInfo },
    };
    vkUpdateDescriptorSets(vk->device, 3, writes, 0, NULL);
}

uint32_t vkbrain_capacity(VKState *vk) { return vk->brain_capacity; }

void vkbrain_ensure_capacity(VKState *vk, uint32_t needed) {
    if (needed <= vk->brain_capacity) return;
    if (needed > vk->brain_max_capacity) needed = vk->brain_max_capacity;
    if (needed <= vk->brain_capacity) return;

    uint32_t new_cap = needed * 2;
    if (new_cap > vk->brain_max_capacity) new_cap = vk->brain_max_capacity;

    VkDeviceSize new_wsize = (VkDeviceSize)new_cap * BRAIN_WEIGHT_FLOATS * sizeof(float);
    VkDeviceSize new_iosize= (VkDeviceSize)new_cap * BRAIN_INPUT_SIZE * sizeof(float);
    VkDeviceSize old_wsize = (VkDeviceSize)vk->brain_capacity * BRAIN_WEIGHT_FLOATS * sizeof(float);

    printf("[VKBrain] Growing from %u to %u agents (weights: %.1f GB)\n",
           vk->brain_capacity, new_cap, (double)new_wsize / (1024*1024*1024));

    VkBuffer new_wbuf; VkDeviceMemory new_wmem;
    {
        VkBufferCreateInfo bci = {
            .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .size = new_wsize,
            .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        };
        if (vkCreateBuffer(vk->device, &bci, NULL, &new_wbuf) != VK_SUCCESS) {
            fprintf(stderr, "[VKBrain] Failed to grow weights buffer — staying at %u\n", vk->brain_capacity);
            return;
        }
        VkMemoryRequirements mr;
        vkGetBufferMemoryRequirements(vk->device, new_wbuf, &mr);
        VkMemoryAllocateInfo ai = { .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
            .allocationSize = mr.size,
            .memoryTypeIndex = vk_find_memory_type(vk->phys_device, mr.memoryTypeBits,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT),
        };
        if (vkAllocateMemory(vk->device, &ai, NULL, &new_wmem) != VK_SUCCESS) {
            vkDestroyBuffer(vk->device, new_wbuf, NULL);
            fprintf(stderr, "[VKBrain] Failed to allocate weights memory — staying at %u\n", vk->brain_capacity);
            return;
        }
        vkBindBufferMemory(vk->device, new_wbuf, new_wmem, 0);
    }

    VkBuffer new_ibuf[2], new_obuf[2];
    VkDeviceMemory new_imem[2], new_omem[2];
    float *new_min[2], *new_mout[2];
    for (int s = 0; s < 2; s++) {
        alloc_host_buffer(vk, new_iosize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                          &new_ibuf[s], &new_imem[s], &new_min[s]);
        alloc_host_buffer(vk, new_iosize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                          &new_obuf[s], &new_omem[s], &new_mout[s]);
    }

    VkCommandBuffer cmd = vk->staging_cmd;
    vkResetCommandBuffer(cmd, 0);
    VkCommandBufferBeginInfo bi = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    vkBeginCommandBuffer(cmd, &bi);
    VkBufferCopy bc = { .size = old_wsize };
    vkCmdCopyBuffer(cmd, vk->brain_weights_buf, new_wbuf, 1, &bc);
    VkBufferMemoryBarrier bmb = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
        .buffer = new_wbuf, .size = VK_WHOLE_SIZE,
    };
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, NULL, 1, &bmb, 0, NULL);
    vkEndCommandBuffer(cmd);
    VkSubmitInfo si = { .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                        .commandBufferCount = 1, .pCommandBuffers = &cmd };
    vkResetFences(vk->device, 1, &vk->staging_fence);
    vkQueueSubmit(vk->queue, 1, &si, vk->staging_fence);
    vkbrain_drain_staging(vk);

    for (int s = 0; s < 2; s++) {
        memcpy(new_min[s], vk->mapped_inputs[s],
               vk->brain_capacity * BRAIN_INPUT_SIZE * sizeof(float));
        memcpy(new_mout[s], vk->mapped_outputs[s],
               vk->brain_capacity * BRAIN_OUTPUT_SIZE * sizeof(float));
    }

    vkDestroyBuffer(vk->device, vk->brain_weights_buf, NULL);
    vkFreeMemory(vk->device, vk->brain_weights_mem, NULL);
    for (int s = 0; s < 2; s++) {
        vkUnmapMemory(vk->device, vk->brain_inputs_mem[s]);
        vkUnmapMemory(vk->device, vk->brain_outputs_mem[s]);
        vkDestroyBuffer(vk->device, vk->brain_inputs_buf[s], NULL);
        vkDestroyBuffer(vk->device, vk->brain_outputs_buf[s], NULL);
        vkFreeMemory(vk->device, vk->brain_inputs_mem[s], NULL);
        vkFreeMemory(vk->device, vk->brain_outputs_mem[s], NULL);
    }

    vk->brain_weights_buf = new_wbuf;
    vk->brain_weights_mem = new_wmem;
    for (int s = 0; s < 2; s++) {
        vk->brain_inputs_buf[s]  = new_ibuf[s];
        vk->brain_inputs_mem[s]  = new_imem[s];
        vk->mapped_inputs[s]     = new_min[s];
        vk->brain_outputs_buf[s] = new_obuf[s];
        vk->brain_outputs_mem[s] = new_omem[s];
        vk->mapped_outputs[s]    = new_mout[s];
    }
    vk->brain_capacity = new_cap;

    for (int s = 0; s < 2; s++)
        write_brain_descriptor(vk, s);

    printf("[VKBrain] Growth complete.\n");
}

void vkbrain_init(VKState *vk, uint32_t initial_cap) {
    VkPhysicalDeviceProperties devProps;
    vkGetPhysicalDeviceProperties(vk->phys_device, &devProps);
    VkDeviceSize maxStorageRange = devProps.limits.maxStorageBufferRange;
    VkDeviceSize oneBrain = (VkDeviceSize)BRAIN_WEIGHT_FLOATS * sizeof(float);
    uint32_t limitByRange = (uint32_t)(maxStorageRange / oneBrain);
    vk->brain_max_capacity = limitByRange;
    if (vk->brain_max_capacity == 0) vk->brain_max_capacity = 1;

    if (initial_cap > vk->brain_max_capacity) {
        fprintf(stderr, "[VKBrain] Requested %u agents but GPU limit is %u — clamping.\n",
                initial_cap, vk->brain_max_capacity);
        initial_cap = vk->brain_max_capacity;
    }
    vk->brain_capacity = initial_cap;

    VkDeviceSize wsize  = (VkDeviceSize)initial_cap * BRAIN_WEIGHT_FLOATS * sizeof(float);
    VkDeviceSize iosize = (VkDeviceSize)initial_cap * BRAIN_INPUT_SIZE * sizeof(float);

    printf("[VKBrain] Initial capacity: %u agents, weights %.1f MB, max %u\n",
           initial_cap, (double)wsize / (1024*1024), vk->brain_max_capacity);

    // Pipeline
    VkShaderModule comp = vk_load_shader(vk->device, "shaders/brain.comp.spv");

    VkDescriptorSetLayoutBinding bindings[] = {
        { .binding = 0, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
          .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT },
        { .binding = 1, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
          .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT },
        { .binding = 2, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
          .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT },
    };
    VkDescriptorSetLayoutCreateInfo dslci = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 3, .pBindings = bindings,
    };
    vkCreateDescriptorSetLayout(vk->device, &dslci, NULL, &vk->brain_desc_layout);

    VkPushConstantRange pcr = {
        .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT, .offset = 0,
        .size = 2 * sizeof(uint32_t),
    };
    VkPipelineLayoutCreateInfo plci = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1, .pSetLayouts = &vk->brain_desc_layout,
        .pushConstantRangeCount = 1, .pPushConstantRanges = &pcr,
    };
    vkCreatePipelineLayout(vk->device, &plci, NULL, &vk->brain_layout);

    VkPipelineShaderStageCreateInfo stage = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .stage = VK_SHADER_STAGE_COMPUTE_BIT, .module = comp, .pName = "main",
    };
    VkComputePipelineCreateInfo cpci = {
        .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
        .stage = stage, .layout = vk->brain_layout,
    };
    if (vkCreateComputePipelines(vk->device, VK_NULL_HANDLE, 1, &cpci, NULL, &vk->brain_pipeline) != VK_SUCCESS)
        { fprintf(stderr, "FATAL: brain pipeline creation failed\n"); exit(1); }
    vkDestroyShaderModule(vk->device, comp, NULL);

    // Weights buffer
    {
        VkBufferCreateInfo bci = {
            .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .size = wsize,
            .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        };
        if (vkCreateBuffer(vk->device, &bci, NULL, &vk->brain_weights_buf) != VK_SUCCESS)
            { fprintf(stderr, "FATAL: brain weights buffer allocation failed\n"); exit(1); }
        VkMemoryRequirements mr;
        vkGetBufferMemoryRequirements(vk->device, vk->brain_weights_buf, &mr);
        VkMemoryAllocateInfo ai = { .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
            .allocationSize = mr.size,
            .memoryTypeIndex = vk_find_memory_type(vk->phys_device, mr.memoryTypeBits,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT),
        };
        if (vkAllocateMemory(vk->device, &ai, NULL, &vk->brain_weights_mem) != VK_SUCCESS)
            { fprintf(stderr, "FATAL: brain weights memory allocation failed\n"); exit(1); }
        vkBindBufferMemory(vk->device, vk->brain_weights_buf, vk->brain_weights_mem, 0);
    }

    // Double-buffer I/O
    for (int slot = 0; slot < 2; slot++) {
        alloc_host_buffer(vk, iosize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                          &vk->brain_inputs_buf[slot], &vk->brain_inputs_mem[slot],
                          &vk->mapped_inputs[slot]);
        alloc_host_buffer(vk, iosize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                          &vk->brain_outputs_buf[slot], &vk->brain_outputs_mem[slot],
                          &vk->mapped_outputs[slot]);
    }

    // Staging buffer
    {
        VkDeviceSize staging_size = (VkDeviceSize)STAGING_FLOATS * sizeof(float);
        alloc_host_buffer(vk, staging_size,
                          VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                          &vk->brain_staging_buf, &vk->brain_staging_mem, &vk->mapped_staging);
    }
    vk->staging_capacity = STAGING_FLOATS / BRAIN_WEIGHT_FLOATS;
    vk->staging_indices = malloc(sizeof(uint32_t) * vk->staging_capacity);

    // Descriptor pool + 2 sets
    {
        VkDescriptorPoolSize poolSize = {
            .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount = 6,
        };
        VkDescriptorPoolCreateInfo dpci = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
            .maxSets = 2, .poolSizeCount = 1, .pPoolSizes = &poolSize,
        };
        vkCreateDescriptorPool(vk->device, &dpci, NULL, &vk->brain_pool);

        VkDescriptorSetLayout layouts[] = { vk->brain_desc_layout, vk->brain_desc_layout };
        VkDescriptorSetAllocateInfo dsai = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
            .descriptorPool = vk->brain_pool, .descriptorSetCount = 2, .pSetLayouts = layouts,
        };
        vkAllocateDescriptorSets(vk->device, &dsai, vk->brain_set);
        for (int s = 0; s < 2; s++)
            write_brain_descriptor(vk, s);
    }

    // Command buffers: 1 staging + 2 compute (double-buffered)
    {
        VkCommandBufferAllocateInfo cbai = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .commandPool = vk->cmd_pool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            .commandBufferCount = 1,
        };
        vkAllocateCommandBuffers(vk->device, &cbai, &vk->staging_cmd);
        cbai.commandBufferCount = 2;
        vkAllocateCommandBuffers(vk->device, &cbai, vk->cmd_compute);
        VkFenceCreateInfo fci = { .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
        vkCreateFence(vk->device, &fci, NULL, &vk->compute_fence);
        // Staging fence: start SIGNALED so first flush's internal wait is a no-op
        VkFenceCreateInfo fci2 = { .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
                                    .flags = VK_FENCE_CREATE_SIGNALED_BIT };
        vkCreateFence(vk->device, &fci2, NULL, &vk->staging_fence);
    }

    vk->staging_count = 0;
    vk->brain_frame   = 0;
}

// ---- Staging: host→staging buffer via memcpy, then GPU copy to device-local weights ----
// Fence lifecycle: created SIGNALED → waited + reset in flush → signaled by vkQueueSubmit.
// Reuses cmd_compute[0] (safe: CPU-serialised, only borrowed between dispatches).

static void record_staging_copy(VKState *vk) {
    VkBufferCopy *regions = malloc(sizeof(VkBufferCopy) * vk->staging_count);
    for (uint32_t i = 0; i < vk->staging_count; i++) {
        regions[i] = (VkBufferCopy){
            .srcOffset = (VkDeviceSize)i * BRAIN_WEIGHT_FLOATS * sizeof(float),
            .dstOffset = (VkDeviceSize)vk->staging_indices[i] * BRAIN_WEIGHT_FLOATS * sizeof(float),
            .size = BRAIN_WEIGHT_FLOATS * sizeof(float),
        };
    }
    VkCommandBufferBeginInfo bi = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    vkResetCommandBuffer(vk->staging_cmd, 0);
    vkBeginCommandBuffer(vk->staging_cmd, &bi);
    vkCmdCopyBuffer(vk->staging_cmd, vk->brain_staging_buf, vk->brain_weights_buf,
                    vk->staging_count, regions);
    VkBufferMemoryBarrier bmb = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
        .buffer = vk->brain_weights_buf, .size = VK_WHOLE_SIZE,
    };
    vkCmdPipelineBarrier(vk->staging_cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, NULL, 1, &bmb, 0, NULL);
    vkEndCommandBuffer(vk->staging_cmd);
    free(regions);
}

void vkbrain_flush_staging(VKState *vk) {
    if (vk->staging_count == 0) return;

    // Wait for previous staging to complete, then reset fence for re-submit
    vkWaitForFences(vk->device, 1, &vk->staging_fence, VK_TRUE, UINT64_MAX);
    vkResetFences(vk->device, 1, &vk->staging_fence);

    record_staging_copy(vk);

    VkSubmitInfo si = { .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                        .commandBufferCount = 1, .pCommandBuffers = &vk->staging_cmd };
    vkQueueSubmit(vk->queue, 1, &si, vk->staging_fence);
    vk->staging_count = 0;
}

void vkbrain_drain_staging(VKState *vk) {
    // Flush any pending staged brains, then wait for GPU to finish.
    // Does NOT reset the fence — leaves it SIGNALED for the next flush.
    if (vk->staging_count > 0) vkbrain_flush_staging(vk);
    vkWaitForFences(vk->device, 1, &vk->staging_fence, VK_TRUE, UINT64_MAX);
}

void vkbrain_stage_brain(VKState *vk, uint32_t agent_idx, const float *brain) {
    if (vk->staging_count >= vk->staging_capacity)
        vkbrain_flush_staging(vk);
    vk->staging_indices[vk->staging_count] = agent_idx;
    float *dst = vk->mapped_staging + vk->staging_count * BRAIN_WEIGHT_FLOATS;
    memcpy(dst, brain, BRAIN_WEIGHT_FLOATS * sizeof(float));
    vk->staging_count++;
}

void vkbrain_destroy(VKState *vk) {
    vkDestroyFence(vk->device, vk->compute_fence, NULL);
    vkDestroyFence(vk->device, vk->staging_fence, NULL);
    vkDestroyPipeline(vk->device, vk->brain_pipeline, NULL);
    vkDestroyPipelineLayout(vk->device, vk->brain_layout, NULL);
    vkDestroyDescriptorPool(vk->device, vk->brain_pool, NULL);
    vkDestroyDescriptorSetLayout(vk->device, vk->brain_desc_layout, NULL);
    free(vk->staging_indices);

    vkUnmapMemory(vk->device, vk->brain_staging_mem);
    for (int slot = 0; slot < 2; slot++) {
        vkUnmapMemory(vk->device, vk->brain_outputs_mem[slot]);
        vkUnmapMemory(vk->device, vk->brain_inputs_mem[slot]);
    }
    vkDestroyBuffer(vk->device, vk->brain_staging_buf, NULL);
    vkFreeMemory(vk->device, vk->brain_staging_mem, NULL);
    for (int slot = 0; slot < 2; slot++) {
        vkDestroyBuffer(vk->device, vk->brain_outputs_buf[slot], NULL);
        vkDestroyBuffer(vk->device, vk->brain_inputs_buf[slot], NULL);
        vkFreeMemory(vk->device, vk->brain_outputs_mem[slot], NULL);
        vkFreeMemory(vk->device, vk->brain_inputs_mem[slot], NULL);
    }
    vkDestroyBuffer(vk->device, vk->brain_weights_buf, NULL);
    vkFreeMemory(vk->device, vk->brain_weights_mem, NULL);
}

void vkbrain_upload_all(VKState *vk, struct World *world) {
    uint32_t total = (uint32_t)world->agents.size;
    for (uint32_t i = 0; i < total; i++) {
        // Use existing staging path: stage → auto-flush when buffer full
        float *dst = vk->mapped_staging + vk->staging_count * BRAIN_WEIGHT_FLOATS;
        memcpy(dst, world->agents.agents[i]->brain, BRAIN_WEIGHT_FLOATS * sizeof(float));
        vk->staging_indices[vk->staging_count] = i;
        vk->staging_count++;
        if (vk->staging_count >= vk->staging_capacity)
            vkbrain_flush_staging(vk);
    }
    // Drain: flush any remainder, then wait for last batch on GPU
    vkbrain_drain_staging(vk);
}

void vkbrain_record_dispatch(VKState *vk, int agent_count, uint32_t read_slot) {
    VkCommandBuffer cmd = vk->cmd_compute[read_slot];
    vkResetCommandBuffer(cmd, 0);
    VkCommandBufferBeginInfo bi = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    vkBeginCommandBuffer(cmd, &bi);

    // Weights (staging copies) and inputs (CPU writes) must be visible before dispatch.
    // Both barriers share the same dst stage → merged into one call.
    VkBufferMemoryBarrier preBarriers[2] = {
        { .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
          .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
          .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
          .buffer = vk->brain_weights_buf, .offset = 0, .size = VK_WHOLE_SIZE },
        { .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
          .srcAccessMask = VK_ACCESS_HOST_WRITE_BIT,
          .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
          .buffer = vk->brain_inputs_buf[read_slot], .offset = 0, .size = VK_WHOLE_SIZE },
    };
    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_HOST_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0, 0, NULL, 2, preBarriers, 0, NULL);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, vk->brain_pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                            vk->brain_layout, 0, 1, &vk->brain_set[read_slot], 0, NULL);
    uint32_t ac = (uint32_t)agent_count;
    uint32_t maxX = 65535;
    uint32_t gx = ac < maxX ? ac : maxX;
    uint32_t gy = (ac + maxX - 1) / maxX;
    uint32_t pcs[] = { ac, gx };
    vkCmdPushConstants(cmd, vk->brain_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pcs), pcs);
    vkCmdDispatch(cmd, gx, gy, 1);

    {
        VkBufferMemoryBarrier bmb = {
            .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
            .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
            .dstAccessMask = VK_ACCESS_HOST_READ_BIT,
            .buffer = vk->brain_outputs_buf[read_slot], .size = VK_WHOLE_SIZE,
        };
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             VK_PIPELINE_STAGE_HOST_BIT, 0, 0, NULL, 1, &bmb, 0, NULL);
    }

    vkEndCommandBuffer(cmd);
}
