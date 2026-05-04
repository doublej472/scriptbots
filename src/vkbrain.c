// vkbrain.c — GPU brain compute: multi-chunk buffers, staging, dispatch
//
// Each BrainChunk holds one complete buffer group: weights (device-local),
// inputs[2] and outputs[2] (host-visible).  Chunks are allocated on demand
// up to available GPU memory, supporting arbitrary agent counts.
//
// Agent mapping: each agent stores (brain_chunk, brain_index).  Within a
// chunk, alive agents occupy indices [0..alive_count-1].  Global compaction
// ensures only the highest-index chunk may have free slots; all lower chunks
// are at capacity.
//
// Staging: host→staging buffer via memcpy, then GPU copy to per-chunk
// weight buffers.  staging_chunks[] / staging_indices[] encode the
// destination (chunk, slot) for each staged brain.
//
#include "vkhelpers.h"
#include "World.h"
#include "Agent.h"
#include "settings.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STAGING_UINTS (512 * 1024 * 1024 / 4)   // 512 MB staging buffer (uint32_t units)

// ---- Host-visible buffer helper ----
static void alloc_buffer(VKState *vk, VkDeviceSize size, VkBufferUsageFlags usage,
                          VkMemoryPropertyFlags props,
                          VkBuffer *buf, VkDeviceMemory *mem, float **mapped) {
    VkBufferCreateInfo bci = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = size, .usage = usage, .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    if (vkCreateBuffer(vk->device, &bci, NULL, buf) != VK_SUCCESS)
        { fprintf(stderr, "FATAL: brain buffer creation failed\n"); exit(1); }
    VkMemoryRequirements mr;
    vkGetBufferMemoryRequirements(vk->device, *buf, &mr);
    VkMemoryAllocateInfo ai = { .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = mr.size,
        .memoryTypeIndex = vk_find_memory_type(vk->phys_device, mr.memoryTypeBits, props),
    };
    if (vkAllocateMemory(vk->device, &ai, NULL, mem) != VK_SUCCESS)
        { fprintf(stderr, "FATAL: brain memory alloc failed\n"); exit(1); }
    vkBindBufferMemory(vk->device, *buf, *mem, 0);
    if (mapped)
        vkMapMemory(vk->device, *mem, 0, VK_WHOLE_SIZE, 0, (void**)mapped);
}

// ---- Write descriptor sets for one chunk + I/O slot ----
static void write_chunk_descriptor(VKState *vk, BrainChunk *c, uint32_t slot) {
    VkDescriptorBufferInfo wInfo = { .buffer = c->weights_buf,      .offset = 0, .range = VK_WHOLE_SIZE };
    VkDescriptorBufferInfo iInfo = { .buffer = c->inputs_buf[slot],  .offset = 0, .range = VK_WHOLE_SIZE };
    VkDescriptorBufferInfo oInfo = { .buffer = c->outputs_buf[slot], .offset = 0, .range = VK_WHOLE_SIZE };
    VkWriteDescriptorSet writes[] = {
        { .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = c->desc_set[slot],
          .dstBinding = 0, .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
          .pBufferInfo = &wInfo },
        { .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = c->desc_set[slot],
          .dstBinding = 1, .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
          .pBufferInfo = &iInfo },
        { .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = c->desc_set[slot],
          .dstBinding = 2, .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
          .pBufferInfo = &oInfo },
    };
    vkUpdateDescriptorSets(vk->device, 3, writes, 0, NULL);
}

// ---- Allocate one chunk (~1 GB weights, ~16 MB I/O) ----
static int vkbrain_alloc_chunk(VKState *vk) {
    if (vk->chunk_count >= vk->chunk_capacity) {
        uint32_t new_cap = vk->chunk_capacity ? vk->chunk_capacity * 2 : 4;
        BrainChunk *p = realloc(vk->chunks, new_cap * sizeof(BrainChunk));
        if (!p) { fprintf(stderr, "[VKBrain] chunk array realloc failed\n"); return -1; }
        vk->chunks = p;
        memset(vk->chunks + vk->chunk_capacity, 0,
               (new_cap - vk->chunk_capacity) * sizeof(BrainChunk));
        vk->chunk_capacity = new_cap;
    }
    BrainChunk *c = &vk->chunks[vk->chunk_count];

    uint32_t cap = BRAIN_CHUNK_AGENTS;
    VkDeviceSize wsize  = (VkDeviceSize)cap * BRAIN_WEIGHT_UINTS * sizeof(uint32_t);
    VkDeviceSize iosize = (VkDeviceSize)cap * BRAIN_INPUT_SIZE   * sizeof(float);

    printf("[VKBrain] Allocating chunk %u: %u agents, weights %.1f GB\n",
           vk->chunk_count, cap, (double)wsize / (1024*1024*1024));

    alloc_buffer(vk, wsize,
                 VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                 VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                 &c->weights_buf, &c->weights_mem, NULL);
    for (int s = 0; s < 2; s++) {
        alloc_buffer(vk, iosize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                     &c->inputs_buf[s], &c->inputs_mem[s], &c->mapped_inputs[s]);
        alloc_buffer(vk, iosize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                     &c->outputs_buf[s], &c->outputs_mem[s], &c->mapped_outputs[s]);
    }

    c->slot_owner = malloc(cap * sizeof(struct Agent *));
    memset(c->slot_owner, 0, cap * sizeof(struct Agent *));

    // Per-chunk descriptor pool (6 storage buffers = 3 bindings × 2 sets)
    {
        VkDescriptorPoolSize poolSize = {
            .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount = 6,
        };
        VkDescriptorPoolCreateInfo dpci = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
            .maxSets = 2, .poolSizeCount = 1, .pPoolSizes = &poolSize,
        };
        vkCreateDescriptorPool(vk->device, &dpci, NULL, &c->desc_pool);

        VkDescriptorSetLayout layouts[] = { vk->brain_desc_layout, vk->brain_desc_layout };
        VkDescriptorSetAllocateInfo dsai = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
            .descriptorPool = c->desc_pool, .descriptorSetCount = 2, .pSetLayouts = layouts,
        };
        vkAllocateDescriptorSets(vk->device, &dsai, c->desc_set);
        for (int s = 0; s < 2; s++)
            write_chunk_descriptor(vk, c, s);
    }

    c->capacity    = cap;
    c->alive_count = 0;
    vk->chunk_count++;
    return (int)(vk->chunk_count - 1);
}

// ---- Free a single chunk (GPU resources + slot_owner) ----
static void vkbrain_free_chunk(VKState *vk, uint32_t ci) {
    BrainChunk *c = &vk->chunks[ci];
    vkDestroyDescriptorPool(vk->device, c->desc_pool, NULL);
    for (int s = 0; s < 2; s++) {
        vkUnmapMemory(vk->device, c->inputs_mem[s]);
        vkUnmapMemory(vk->device, c->outputs_mem[s]);
        vkDestroyBuffer(vk->device, c->inputs_buf[s], NULL);
        vkDestroyBuffer(vk->device, c->outputs_buf[s], NULL);
        vkFreeMemory(vk->device, c->inputs_mem[s], NULL);
        vkFreeMemory(vk->device, c->outputs_mem[s], NULL);
    }
    vkDestroyBuffer(vk->device, c->weights_buf, NULL);
    vkFreeMemory(vk->device, c->weights_mem, NULL);
    free(c->slot_owner);

    // Swap-remove from chunks array
    if (ci < vk->chunk_count - 1) {
        // Update brain_chunk for all agents in the swapped chunk
        BrainChunk *swapped = &vk->chunks[vk->chunk_count - 1];
        for (uint32_t s = 0; s < swapped->alive_count; s++) {
            struct Agent *a = swapped->slot_owner[s];
            if (a) a->brain_chunk = ci;
        }
        vk->chunks[ci] = *swapped;
        memset(swapped, 0, sizeof(BrainChunk));
    }
    vk->chunk_count--;
    printf("[VKBrain] Freed chunk %u (%u chunks remain)\n", ci, vk->chunk_count);
}

// ---- Try to reclaim the last chunk if eligible ----
void vkbrain_try_reclaim_last(VKState *vk) {
    if (vk->chunk_count == 0) return;
    uint32_t last = vk->chunk_count - 1;
    if (vk->chunks[last].alive_count > 0) return;  // not empty

    // Hysteresis: keep if previous chunk is more than half full
    if (last > 0 && vk->chunks[last - 1].alive_count > vk->chunks[last - 1].capacity / 2)
        return;

    vkbrain_free_chunk(vk, last);
}

// ---- Public API ----

uint32_t vkbrain_total_capacity(VKState *vk) {
    uint32_t sum = 0;
    for (uint32_t i = 0; i < vk->chunk_count; i++)
        sum += vk->chunks[i].capacity;
    return sum;
}

bool vkbrain_assign_slot(VKState *vk, struct Agent *a,
                          uint32_t *out_chunk, uint32_t *out_slot) {
    for (uint32_t ci = 0; ci < vk->chunk_count; ci++) {
        BrainChunk *c = &vk->chunks[ci];
        if (c->alive_count < c->capacity) {
            *out_chunk = ci;
            *out_slot  = c->alive_count;
            c->slot_owner[c->alive_count] = a;
            c->alive_count++;
            return true;
        }
    }
    int ci = vkbrain_alloc_chunk(vk);
    if (ci < 0) return false;
    BrainChunk *c = &vk->chunks[ci];
    *out_chunk = (uint32_t)ci;
    *out_slot  = c->alive_count;
    c->slot_owner[c->alive_count] = a;
    c->alive_count++;
    return true;
}

void vkbrain_stage_brain(VKState *vk, uint32_t chunk, uint32_t slot, const uint32_t *brain) {
    if (vk->staging_count >= vk->staging_capacity)
        vkbrain_flush_staging(vk);
    vk->staging_chunks[vk->staging_count]  = chunk;
    vk->staging_indices[vk->staging_count] = slot;
    // Brain is already packed fp16 on CPU — direct memcpy to staging
    memcpy(vk->mapped_staging + vk->staging_count * BRAIN_WEIGHT_UINTS,
           brain, BRAIN_WEIGHT_UINTS * sizeof(uint32_t));
    vk->staging_count++;
}

// ---- Move a brain from one chunk+slot to another ----
void vkbrain_move_slot(VKState *vk, uint32_t from_c, uint32_t from_s,
                        uint32_t to_c, uint32_t to_s, struct Agent *moved) {
    // Stage brain weights for GPU copy to new location
    vkbrain_stage_brain(vk, to_c, to_s, moved->brain);

    // Move I/O buffers immediately on CPU
    BrainChunk *fc = &vk->chunks[from_c];
    BrainChunk *tc = &vk->chunks[to_c];
    for (int s = 0; s < 2; s++) {
        memcpy(tc->mapped_inputs[s]  + to_s * BRAIN_INPUT_SIZE,
               fc->mapped_inputs[s]  + from_s * BRAIN_INPUT_SIZE,
               BRAIN_INPUT_SIZE * sizeof(float));
        memcpy(tc->mapped_outputs[s] + to_s * BRAIN_OUTPUT_SIZE,
               fc->mapped_outputs[s] + from_s * BRAIN_OUTPUT_SIZE,
               BRAIN_OUTPUT_SIZE * sizeof(float));
    }

    // Update slot_owner
    tc->slot_owner[to_s]   = moved;
    fc->slot_owner[from_s] = NULL;
}

// ---- Staging flush: copy staged brains to per-chunk weight buffers ----

static void record_staging_copy(VKState *vk, uint32_t idx) {
    if (vk->staging_count == 0) return;

    VkCommandBufferBeginInfo bi = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    vkResetCommandBuffer(vk->staging_cmd[idx], 0);
    vkBeginCommandBuffer(vk->staging_cmd[idx], &bi);

    // Batch copies by chunk for fewer barriers
    for (uint32_t ci = 0; ci < vk->chunk_count; ci++) {
        uint32_t first = 0;
        while (first < vk->staging_count) {
            // Find consecutive entries for this chunk
            if (vk->staging_chunks[first] != ci) { first++; continue; }
            uint32_t last = first + 1;
            while (last < vk->staging_count && vk->staging_chunks[last] == ci)
                last++;

            uint32_t n = last - first;
            VkBufferCopy *regions = malloc(n * sizeof(VkBufferCopy));
            for (uint32_t i = 0; i < n; i++) {
                uint32_t slot = vk->staging_indices[first + i];
                regions[i] = (VkBufferCopy){
                    .srcOffset = (VkDeviceSize)(first + i) * BRAIN_WEIGHT_UINTS * sizeof(uint32_t),
                    .dstOffset = (VkDeviceSize)slot * BRAIN_WEIGHT_UINTS * sizeof(uint32_t),
                    .size      = BRAIN_WEIGHT_UINTS * sizeof(uint32_t),
                };
            }
            vkCmdCopyBuffer(vk->staging_cmd[idx], vk->brain_staging_buf,
                            vk->chunks[ci].weights_buf, n, regions);
            free(regions);

            // Release weights for this chunk to compute queue family
            // (use IGNORED when the two families are the same to avoid validation noise)
            VkBufferMemoryBarrier bmb = {
                .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
                .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
                .dstAccessMask = 0,
                .srcQueueFamilyIndex = vk->transfer_family,
                .dstQueueFamilyIndex = vk->compute_family,
                .buffer = vk->chunks[ci].weights_buf, .size = VK_WHOLE_SIZE,
            };
            if (vk->transfer_family == vk->compute_family)
                bmb.srcQueueFamilyIndex = bmb.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            vkCmdPipelineBarrier(vk->staging_cmd[idx], VK_PIPELINE_STAGE_TRANSFER_BIT,
                                 VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0,
                                 0, NULL, 1, &bmb, 0, NULL);
            first = last;
        }
    }

    vkEndCommandBuffer(vk->staging_cmd[idx]);

    VkSubmitInfo si = { .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                        .commandBufferCount = 1,
                        .pCommandBuffers = &vk->staging_cmd[idx] };
    vkResetFences(vk->device, 1, &vk->staging_fence[idx]);
    vkQueueSubmit(vk->transfer_queue, 1, &si, vk->staging_fence[idx]);
    vk->staging_fence_idx = 1 - idx;

    vk->staging_count = 0;
}

void vkbrain_flush_staging(VKState *vk) {
    if (vk->staging_count == 0) return;
    // Wait for the fence we're about to reuse (submitted 2 flushes ago)
    uint32_t idx = vk->staging_fence_idx;
    vkWaitForFences(vk->device, 1, &vk->staging_fence[idx], VK_TRUE, UINT64_MAX);
    record_staging_copy(vk, idx);
}

void vkbrain_drain_staging(VKState *vk) {
    if (vk->staging_count > 0) vkbrain_flush_staging(vk);
    vkWaitForFences(vk->device, 2, vk->staging_fence, VK_TRUE, UINT64_MAX);
}

// ---- Dispatch: one bind+dispatch per non-empty chunk ----

void vkbrain_record_dispatch(VKState *vk, uint32_t read_slot) {
    VkCommandBuffer cmd = vk->cmd_compute[read_slot];
    vkResetCommandBuffer(cmd, 0);
    VkCommandBufferBeginInfo bi = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    vkBeginCommandBuffer(cmd, &bi);

    if (vk->timestamp_supported) {
        vkCmdResetQueryPool(cmd, vk->timestamp_pool, 0, 2);
        vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                            vk->timestamp_pool, 0);
    }

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, vk->brain_pipeline);

    for (uint32_t ci = 0; ci < vk->chunk_count; ci++) {
        BrainChunk *c = &vk->chunks[ci];
        if (c->alive_count == 0) continue;

        // Acquire weights from transfer queue + make host writes visible
        VkBufferMemoryBarrier preBarriers[2] = {
            { .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
              .srcAccessMask = 0,
              .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
              .srcQueueFamilyIndex = vk->transfer_family,
              .dstQueueFamilyIndex = vk->compute_family,
              .buffer = c->weights_buf, .size = VK_WHOLE_SIZE },
            { .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
              .srcAccessMask = VK_ACCESS_HOST_WRITE_BIT,
              .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
              .buffer = c->inputs_buf[read_slot], .size = VK_WHOLE_SIZE },
        };
        if (vk->transfer_family == vk->compute_family)
            preBarriers[0].srcQueueFamilyIndex = preBarriers[0].dstQueueFamilyIndex
                = VK_QUEUE_FAMILY_IGNORED;
        vkCmdPipelineBarrier(cmd,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT | VK_PIPELINE_STAGE_HOST_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            0, 0, NULL, 2, preBarriers, 0, NULL);

        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                                vk->brain_layout, 0, 1,
                                &c->desc_set[read_slot], 0, NULL);

        uint32_t ac = c->alive_count;
        uint32_t maxX = 65535;
        uint32_t gx = ac < maxX ? ac : maxX;
        uint32_t gy = (ac + maxX - 1) / maxX;
        uint32_t pcs[] = { ac, gx };
        vkCmdPushConstants(cmd, vk->brain_layout,
                           VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pcs), pcs);
        vkCmdDispatch(cmd, gx, gy, 1);
    }

    // Output barrier: all non-empty chunks' outputs → host read
    {
        uint32_t nc = vk->chunk_count;
        VkBufferMemoryBarrier *outBarriers = malloc(nc * sizeof(VkBufferMemoryBarrier));
        uint32_t out_count = 0;
        for (uint32_t ci = 0; ci < nc; ci++) {
            BrainChunk *c = &vk->chunks[ci];
            if (c->alive_count == 0) continue;
            outBarriers[out_count++] = (VkBufferMemoryBarrier){
                .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
                .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
                .dstAccessMask = VK_ACCESS_HOST_READ_BIT,
                .buffer = c->outputs_buf[read_slot], .size = VK_WHOLE_SIZE,
            };
        }
        if (out_count > 0)
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                 VK_PIPELINE_STAGE_HOST_BIT, 0,
                                 0, NULL, out_count, outBarriers, 0, NULL);
        free(outBarriers);
    }

    if (vk->timestamp_supported)
        vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                            vk->timestamp_pool, 1);

    vkEndCommandBuffer(cmd);
}

// ---- Init / destroy ----

void vkbrain_init(VKState *vk, uint32_t initial_cap) {
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

    // Staging buffer (global, shared across all chunks)
    {
        VkDeviceSize staging_size = (VkDeviceSize)STAGING_UINTS * sizeof(uint32_t);
        alloc_buffer(vk, staging_size,
                     VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                     &vk->brain_staging_buf, &vk->brain_staging_mem,
                     (float**)&vk->mapped_staging);
    }
    vk->staging_capacity = STAGING_UINTS / BRAIN_WEIGHT_UINTS;
    vk->staging_chunks  = malloc(sizeof(uint32_t) * vk->staging_capacity);
    vk->staging_indices = malloc(sizeof(uint32_t) * vk->staging_capacity);
    vk->staging_count   = 0;

    // Command buffers (from transfer and compute pools)
    {
        VkCommandBufferAllocateInfo cbai = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .commandPool = vk->cmd_pool_transfer, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            .commandBufferCount = 2,
        };
        vkAllocateCommandBuffers(vk->device, &cbai, vk->staging_cmd);
        cbai.commandPool = vk->cmd_pool_compute;
        cbai.commandBufferCount = 2;
        vkAllocateCommandBuffers(vk->device, &cbai, vk->cmd_compute);
        VkFenceCreateInfo fci = { .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
        vkCreateFence(vk->device, &fci, NULL, &vk->compute_fence);
        // Double-buffered staging fences — both start SIGNALED so first flush is a no-wait
        VkFenceCreateInfo fci2 = { .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
                                    .flags = VK_FENCE_CREATE_SIGNALED_BIT };
        vkCreateFence(vk->device, &fci2, NULL, &vk->staging_fence[0]);
        vkCreateFence(vk->device, &fci2, NULL, &vk->staging_fence[1]);
    }

    vk->chunks = NULL;
    vk->chunk_count    = 0;
    vk->chunk_capacity = 0;
    if (initial_cap > 0) vkbrain_alloc_chunk(vk);

    vk->brain_frame = 0;

    printf("[VKBrain] Pipeline ready. %u chunks, staging %.0f MB.\n",
           vk->chunk_count, (double)(STAGING_UINTS * 4) / (1024*1024));
}

void vkbrain_reset(VKState *vk) {
    for (uint32_t ci = 0; ci < vk->chunk_count; ci++) {
        BrainChunk *c = &vk->chunks[ci];
        vkDestroyDescriptorPool(vk->device, c->desc_pool, NULL);
        for (int s = 0; s < 2; s++) {
            vkUnmapMemory(vk->device, c->inputs_mem[s]);
            vkUnmapMemory(vk->device, c->outputs_mem[s]);
            vkDestroyBuffer(vk->device, c->inputs_buf[s], NULL);
            vkDestroyBuffer(vk->device, c->outputs_buf[s], NULL);
            vkFreeMemory(vk->device, c->inputs_mem[s], NULL);
            vkFreeMemory(vk->device, c->outputs_mem[s], NULL);
        }
        vkDestroyBuffer(vk->device, c->weights_buf, NULL);
        vkFreeMemory(vk->device, c->weights_mem, NULL);
        free(c->slot_owner);
    }
    free(vk->chunks);
    vk->chunks = NULL;
    vk->chunk_count = 0;
    vk->chunk_capacity = 0;
}

void vkbrain_reset_counts(VKState *vk) {
    for (uint32_t ci = 0; ci < vk->chunk_count; ci++) {
        BrainChunk *c = &vk->chunks[ci];
        c->alive_count = 0;
        memset(c->slot_owner, 0, c->capacity * sizeof(struct Agent *));
    }
}

void vkbrain_destroy(VKState *vk) {
    vkDestroyFence(vk->device, vk->compute_fence, NULL);
    vkDestroyFence(vk->device, vk->staging_fence[0], NULL);
    vkDestroyFence(vk->device, vk->staging_fence[1], NULL);
    vkDestroyPipeline(vk->device, vk->brain_pipeline, NULL);
    vkDestroyPipelineLayout(vk->device, vk->brain_layout, NULL);
    vkDestroyDescriptorSetLayout(vk->device, vk->brain_desc_layout, NULL);
    free(vk->staging_chunks);
    free(vk->staging_indices);

    vkUnmapMemory(vk->device, vk->brain_staging_mem);
    vkDestroyBuffer(vk->device, vk->brain_staging_buf, NULL);
    vkFreeMemory(vk->device, vk->brain_staging_mem, NULL);

    vkbrain_reset(vk);
}

void vkbrain_upload_all(VKState *vk, struct World *world) {
    uint32_t total = (uint32_t)world->agents.size;
    for (uint32_t i = 0; i < total; i++) {
        struct Agent *a = world->agents.agents[i];
        // Assign a GPU slot if this agent doesn't have one yet
        if (a->brain_chunk == ~0u) {
            uint32_t chunk, slot;
            if (!vkbrain_assign_slot(vk, a, &chunk, &slot)) break;
            a->brain_chunk = chunk;
            a->brain_index = slot;
        }
        vkbrain_stage_brain(vk, a->brain_chunk, a->brain_index, a->brain);
    }
    vkbrain_drain_staging(vk);
}
