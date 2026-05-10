// vk_mgmt.c — Vulkan resource and lifecycle management wrappers (implementation)
#include "vk_mgmt.h"
#include "vkhelpers.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ============================================================================
// VKM_Buffer
// ============================================================================

VKM_Buffer vkm_buffer_create(const VKState *vk, VkDeviceSize size,
                             VkBufferUsageFlags usage,
                             VkMemoryPropertyFlags props) {
  VKM_Buffer buf = {0};
  buf.size = size;

  VkBufferCreateInfo bci = {
      .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
      .size  = size,
      .usage = usage,
      .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
  };
  if (vkCreateBuffer(vk->device, &bci, NULL, &buf.buffer) != VK_SUCCESS) {
    fprintf(stderr, "FATAL: vkm_buffer_create: buffer creation failed\n");
    exit(1);
  }

  VkMemoryRequirements mr;
  vkGetBufferMemoryRequirements(vk->device, buf.buffer, &mr);
  VkMemoryAllocateInfo ai = {
      .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
      .allocationSize = mr.size,
      .memoryTypeIndex = vkm_find_memory_type(vk->phys_device,
                                              mr.memoryTypeBits, props),
  };
  if (vkAllocateMemory(vk->device, &ai, NULL, &buf.memory) != VK_SUCCESS) {
    fprintf(stderr, "FATAL: vkm_buffer_create: memory allocation failed\n");
    exit(1);
  }
  VkResult bindRes = vkBindBufferMemory(vk->device, buf.buffer, buf.memory, 0);
  if (bindRes != VK_SUCCESS) {
    fprintf(stderr, "FATAL: vkm_buffer_create: buffer bind failed (VkResult=%d)\n", bindRes);
    exit(1);
  }

  if (props & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) {
    if (vkMapMemory(vk->device, buf.memory, 0, VK_WHOLE_SIZE, 0,
                    &buf.mapped) != VK_SUCCESS) {
      fprintf(stderr, "FATAL: vkm_buffer_create: map failed\n");
      exit(1);
    }
  }

  return buf;
}

VKM_Buffer vkm_buffer_create_staged(const VKState *vk, const void *data,
                                    VkDeviceSize size,
                                    VkBufferUsageFlags usage,
                                    uint32_t src_family,
                                    uint32_t dst_family) {
  // Staging buffer (host-visible, transfer-src)
  VKM_Buffer staging = vkm_buffer_create(
      vk, size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
  memcpy(staging.mapped, data, size);

  // Target buffer (device-local, transfer-dst)
  VKM_Buffer buf = vkm_buffer_create(
      vk, size, usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

  // One-time transfer command
  VkCommandBuffer tmpCmd;
  {
    VkCommandBufferAllocateInfo cbai = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = vk->cmd_pool_transfer.pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };
    vkAllocateCommandBuffers(vk->device, &cbai, &tmpCmd);
  }
  VkCommandBufferBeginInfo bi = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
      .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
  };
  vkBeginCommandBuffer(tmpCmd, &bi);

  VkBufferCopy bc = {.size = size};
  vkCmdCopyBuffer(tmpCmd, staging.buffer, buf.buffer, 1, &bc);

  // Queue family ownership release
  uint32_t sf = src_family, df = dst_family;
  if (sf == df)
    sf = df = VK_QUEUE_FAMILY_IGNORED;
  VkBufferMemoryBarrier bmb = {
      .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
      .srcAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT,
      .dstAccessMask       = 0,
      .srcQueueFamilyIndex = sf,
      .dstQueueFamilyIndex = df,
      .buffer              = buf.buffer,
      .size                = VK_WHOLE_SIZE,
  };
  vkCmdPipelineBarrier(tmpCmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                       VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                       0, 0, NULL, 1, &bmb, 0, NULL);
  vkEndCommandBuffer(tmpCmd);

  VkSubmitInfo si = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                     .commandBufferCount = 1,
                     .pCommandBuffers = &tmpCmd};
  vkQueueSubmit(vk->transfer_queue, 1, &si, VK_NULL_HANDLE);
  vkQueueWaitIdle(vk->transfer_queue);

  vkFreeCommandBuffers(vk->device, vk->cmd_pool_transfer.pool, 1, &tmpCmd);
  vkm_buffer_destroy(vk->device, &staging);
  return buf;
}

void vkm_buffer_destroy(VkDevice device, VKM_Buffer *buf) {
  if (!buf->buffer)
    return;
  if (buf->mapped)
    vkUnmapMemory(device, buf->memory);
  vkDestroyBuffer(device, buf->buffer, NULL);
  vkFreeMemory(device, buf->memory, NULL);
  memset(buf, 0, sizeof(*buf));
}

VKM_Buffer vkm_buffer_resize(const VKState *vk, VKM_Buffer *old,
                             VkDeviceSize new_size,
                             VkBufferUsageFlags usage) {
  if (!old->buffer)
    return *old;

  VkMemoryPropertyFlags props = old->mapped
      ? (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
         VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)
      : VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

  vkm_buffer_destroy(vk->device, old);
  VKM_Buffer buf = vkm_buffer_create(vk, new_size, usage, props);
  *old = buf;
  return buf;
}

// ============================================================================
// VKM_Fence
// ============================================================================

VKM_Fence vkm_fence_create(VkDevice device, bool signaled) {
  VkFenceCreateInfo fci = {
      .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
      .flags = signaled ? VK_FENCE_CREATE_SIGNALED_BIT : 0u,
  };
  VKM_Fence f = {0};
  VkResult res = vkCreateFence(device, &fci, NULL, &f.fence);
  if (res != VK_SUCCESS) {
    fprintf(stderr, "FATAL: fence creation failed (VkResult=%d)\n", res);
    exit(1);
  }
  return f;
}

void vkm_fence_destroy(VkDevice device, VKM_Fence *f) {
  if (f->fence) {
    vkDestroyFence(device, f->fence, NULL);
    f->fence = VK_NULL_HANDLE;
  }
}

// ============================================================================
// VKM_Semaphore
// ============================================================================

VKM_Semaphore vkm_semaphore_create(VkDevice device) {
  VkSemaphoreCreateInfo sci = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
  VKM_Semaphore s = {0};
  VkResult res = vkCreateSemaphore(device, &sci, NULL, &s.semaphore);
  if (res != VK_SUCCESS) {
    fprintf(stderr, "FATAL: semaphore creation failed (VkResult=%d)\n", res);
    exit(1);
  }
  return s;
}

void vkm_semaphore_destroy(VkDevice device, VKM_Semaphore *s) {
  if (s->semaphore) {
    vkDestroySemaphore(device, s->semaphore, NULL);
    s->semaphore = VK_NULL_HANDLE;
  }
}

// ============================================================================
// VKM_CmdPool
// ============================================================================

VKM_CmdPool vkm_cmdpool_create(VkDevice device, uint32_t queue_family) {
  VkCommandPoolCreateInfo cpci = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
      .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
      .queueFamilyIndex = queue_family,
  };
  VKM_CmdPool cp = {0};
  VkResult res = vkCreateCommandPool(device, &cpci, NULL, &cp.pool);
  if (res != VK_SUCCESS) {
    fprintf(stderr, "FATAL: command pool creation failed (VkResult=%d)\n", res);
    exit(1);
  }
  return cp;
}

void vkm_cmdpool_destroy(VkDevice device, VKM_CmdPool *cp) {
  if (cp->pool) {
    vkDestroyCommandPool(device, cp->pool, NULL);
    cp->pool = VK_NULL_HANDLE;
  }
}

void vkm_cmdpool_alloc_buffers(VkDevice device, VKM_CmdPool *cp,
                               uint32_t count, VkCommandBuffer *out) {
  VkCommandBufferAllocateInfo cbai = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
      .commandPool = cp->pool,
      .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
      .commandBufferCount = count,
  };
  VkResult res = vkAllocateCommandBuffers(device, &cbai, out);
  if (res != VK_SUCCESS) {
    fprintf(stderr, "FATAL: command buffer allocation failed (VkResult=%d)\n", res);
    exit(1);
  }
}

// ============================================================================
// VKM_Shader
// ============================================================================

VkShaderModule vkm_shader_load(VkDevice device, const char *path) {
  FILE *f = fopen(path, "rb");
  if (!f) {
    fprintf(stderr, "FATAL: cannot open shader %s\n", path);
    exit(1);
  }
  fseek(f, 0, SEEK_END);
  long sz = ftell(f);
  rewind(f);
  char *code = malloc(sz);
  fread(code, 1, sz, f);
  fclose(f);

  VkShaderModuleCreateInfo smci = {
      .sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
      .codeSize = sz,
      .pCode    = (uint32_t *)code,
  };
  VkShaderModule mod;
  if (vkCreateShaderModule(device, &smci, NULL, &mod) != VK_SUCCESS) {
    fprintf(stderr, "FATAL: failed to create shader module %s\n", path);
    exit(1);
  }
  free(code);
  return mod;
}

void vkm_shader_destroy(VkDevice device, VkShaderModule mod) {
  if (mod)
    vkDestroyShaderModule(device, mod, NULL);
}

// ============================================================================
// VKM_Pipeline helpers
// ============================================================================

VkPipelineLayout vkm_pipeline_layout_create(
    VkDevice device,
    uint32_t set_count, const VkDescriptorSetLayout *sets,
    uint32_t push_const_size, VkShaderStageFlags push_const_stages) {
  VkPipelineLayoutCreateInfo plci = {
      .sType          = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
      .setLayoutCount = set_count,
      .pSetLayouts    = sets,
  };
  VkPushConstantRange pcr = {0};
  if (push_const_size > 0) {
    pcr.stageFlags = push_const_stages;
    pcr.offset     = 0;
    pcr.size       = push_const_size;
    plci.pushConstantRangeCount = 1;
    plci.pPushConstantRanges    = &pcr;
  }
  VkPipelineLayout layout;
  if (vkCreatePipelineLayout(device, &plci, NULL, &layout) != VK_SUCCESS) {
    fprintf(stderr, "FATAL: pipeline layout creation failed\n");
    exit(1);
  }
  return layout;
}

VKM_Pipeline vkm_pipeline_graphics_create(
    VkDevice device,
    VkPipelineLayout layout,
    VkRenderPass render_pass,
    VkShaderModule vert, VkShaderModule frag,
    const VkPipelineVertexInputStateCreateInfo *vertex_input,
    VkPrimitiveTopology topology) {

  VkPipelineShaderStageCreateInfo stages[] = {
      {.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
       .stage  = VK_SHADER_STAGE_VERTEX_BIT,
       .module = vert,
       .pName  = "main"},
      {.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
       .stage  = VK_SHADER_STAGE_FRAGMENT_BIT,
       .module = frag,
       .pName  = "main"},
  };

  VkPipelineInputAssemblyStateCreateInfo asm_info = {
      .sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
      .topology = topology,
  };

  VkPipelineViewportStateCreateInfo vp_info = {
      .sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
      .viewportCount = 1,
      .scissorCount  = 1,
  };

  VkPipelineRasterizationStateCreateInfo rs_info = {
      .sType       = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
      .polygonMode = VK_POLYGON_MODE_FILL,
      .cullMode    = VK_CULL_MODE_NONE,
      .frontFace   = VK_FRONT_FACE_COUNTER_CLOCKWISE,
      .lineWidth   = 1.0f,
  };

  VkPipelineMultisampleStateCreateInfo ms_info = {
      .sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
      .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
  };

  VkPipelineColorBlendAttachmentState blend_att = {
      .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
      .blendEnable         = VK_TRUE,
      .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
      .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
      .colorBlendOp        = VK_BLEND_OP_ADD,
      .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
      .dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
      .alphaBlendOp        = VK_BLEND_OP_ADD,
  };

  VkPipelineColorBlendStateCreateInfo cb_info = {
      .sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
      .attachmentCount = 1,
      .pAttachments    = &blend_att,
  };

  VkDynamicState dyn_states[] = {VK_DYNAMIC_STATE_VIEWPORT,
                                 VK_DYNAMIC_STATE_SCISSOR};
  VkPipelineDynamicStateCreateInfo dyn_info = {
      .sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
      .dynamicStateCount = 2,
      .pDynamicStates    = dyn_states,
  };

  VkGraphicsPipelineCreateInfo pci = {
      .sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
      .stageCount          = 2,
      .pStages             = stages,
      .pVertexInputState   = vertex_input,
      .pInputAssemblyState = &asm_info,
      .pViewportState      = &vp_info,
      .pRasterizationState = &rs_info,
      .pMultisampleState   = &ms_info,
      .pColorBlendState    = &cb_info,
      .pDynamicState       = &dyn_info,
      .layout              = layout,
      .renderPass          = render_pass,
      .subpass             = 0,
  };

  VKM_Pipeline p = {.layout = VK_NULL_HANDLE};
  if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pci, NULL,
                                &p.pipeline) != VK_SUCCESS) {
    fprintf(stderr, "FATAL: graphics pipeline creation failed\n");
    exit(1);
  }
  return p;
}

VKM_Pipeline vkm_pipeline_compute_create(
    VkDevice device,
    VkDescriptorSetLayout desc_layout,
    VkShaderModule comp,
    uint32_t push_const_size) {
  VkPipelineLayout layout = vkm_pipeline_layout_create(
      device, 1, &desc_layout,
      push_const_size, VK_SHADER_STAGE_COMPUTE_BIT);

  VkPipelineShaderStageCreateInfo stage = {
      .sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
      .stage  = VK_SHADER_STAGE_COMPUTE_BIT,
      .module = comp,
      .pName  = "main",
  };

  VkComputePipelineCreateInfo cpci = {
      .sType  = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
      .stage  = stage,
      .layout = layout,
  };

  VKM_Pipeline p = {0};
  p.layout = layout;
  if (vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &cpci, NULL,
                               &p.pipeline) != VK_SUCCESS) {
    fprintf(stderr, "FATAL: compute pipeline creation failed\n");
    exit(1);
  }
  return p;
}

void vkm_pipeline_destroy(VkDevice device, VKM_Pipeline *p) {
  if (!p->pipeline)
    return;
  vkDestroyPipeline(device, p->pipeline, NULL);
  if (p->layout)
    vkDestroyPipelineLayout(device, p->layout, NULL);
  memset(p, 0, sizeof(*p));
}

// ============================================================================
// VKM_DescPool
// ============================================================================

VKM_DescPool vkm_descpool_create(VkDevice device, uint32_t max_sets,
                                 const VkDescriptorPoolSize *pool_sizes,
                                 uint32_t pool_size_count,
                                 bool free_sets) {
  VkDescriptorPoolCreateInfo dpci = {
      .sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
      .flags         = free_sets
                           ? (VkDescriptorPoolCreateFlags)
                                 VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT
                           : 0u,
      .maxSets       = max_sets,
      .poolSizeCount = pool_size_count,
      .pPoolSizes    = pool_sizes,
  };
  VKM_DescPool dp = {0};
  VkResult res = vkCreateDescriptorPool(device, &dpci, NULL, &dp.pool);
  if (res != VK_SUCCESS) {
    fprintf(stderr, "FATAL: descriptor pool creation failed (VkResult=%d)\n", res);
    exit(1);
  }
  return dp;
}

void vkm_descpool_destroy(VkDevice device, VKM_DescPool *dp) {
  if (dp->pool) {
    vkDestroyDescriptorPool(device, dp->pool, NULL);
    dp->pool = VK_NULL_HANDLE;
  }
}

void vkm_descset_alloc(VkDevice device, VKM_DescPool *dp,
                       uint32_t count,
                       const VkDescriptorSetLayout *layouts,
                       VkDescriptorSet *out_sets) {
  VkDescriptorSetAllocateInfo dsai = {
      .sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
      .descriptorPool     = dp->pool,
      .descriptorSetCount = count,
      .pSetLayouts        = layouts,
  };
  VkResult res = vkAllocateDescriptorSets(device, &dsai, out_sets);
  if (res != VK_SUCCESS) {
    fprintf(stderr, "FATAL: descriptor set allocation failed (VkResult=%d)\n", res);
    exit(1);
  }
}

void vkm_descset_write_buffer(VkDevice device, VkDescriptorSet set,
                              uint32_t binding, VkDescriptorType type,
                              const VKM_Buffer *buf,
                              VkDeviceSize range) {
  VkDescriptorBufferInfo info = {
      .buffer = buf->buffer,
      .offset = 0,
      .range  = range,
  };
  VkWriteDescriptorSet w = {
      .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
      .dstSet          = set,
      .dstBinding      = binding,
      .dstArrayElement = 0,
      .descriptorCount = 1,
      .descriptorType  = type,
      .pBufferInfo     = &info,
  };
  vkUpdateDescriptorSets(device, 1, &w, 0, NULL);
}

// ============================================================================
// VKM_DescLayout
// ============================================================================

VkDescriptorSetLayout vkm_desclayout_create(
    VkDevice device,
    uint32_t binding_count,
    const VkDescriptorSetLayoutBinding *bindings) {
  VkDescriptorSetLayoutCreateInfo dslci = {
      .sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
      .bindingCount = binding_count,
      .pBindings    = bindings,
  };
  VkDescriptorSetLayout layout;
  if (vkCreateDescriptorSetLayout(device, &dslci, NULL, &layout) !=
      VK_SUCCESS) {
    fprintf(stderr, "FATAL: descriptor set layout creation failed\n");
    exit(1);
  }
  return layout;
}

void vkm_desclayout_destroy(VkDevice device, VkDescriptorSetLayout layout) {
  if (layout)
    vkDestroyDescriptorSetLayout(device, layout, NULL);
}

// ============================================================================
// VKM_QueryPool
// ============================================================================

VkQueryPool vkm_querypool_create(VkDevice device, VkQueryType type,
                                 uint32_t count) {
  VkQueryPoolCreateInfo qpci = {
      .sType      = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
      .queryType  = type,
      .queryCount = count,
  };
  VkQueryPool pool = VK_NULL_HANDLE;
  VkResult res = vkCreateQueryPool(device, &qpci, NULL, &pool);
  if (res != VK_SUCCESS) {
    fprintf(stderr, "WARNING: query pool creation failed (VkResult=%d)\n", res);
    return VK_NULL_HANDLE;
  }
  return pool;
}

void vkm_querypool_destroy(VkDevice device, VkQueryPool pool) {
  if (pool)
    vkDestroyQueryPool(device, pool, NULL);
}

// ============================================================================
// Utilities
// ============================================================================

uint32_t vkm_find_memory_type(VkPhysicalDevice phys_device,
                              uint32_t type_filter,
                              VkMemoryPropertyFlags props) {
  VkPhysicalDeviceMemoryProperties mem_props;
  vkGetPhysicalDeviceMemoryProperties(phys_device, &mem_props);
  for (uint32_t i = 0; i < mem_props.memoryTypeCount; i++) {
    if ((type_filter & (1u << i)) &&
        (mem_props.memoryTypes[i].propertyFlags & props) == props)
      return i;
  }
  fprintf(stderr, "FATAL: no suitable memory type\n");
  exit(1);
  return 0;
}
