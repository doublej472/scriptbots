// vkinit.c — Vulkan instance, device, swapchain, pipelines, buffers (one-time setup)
#include "vkhelpers.h"
#include "settings.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

// Helper: check if extension is in the available list
static int ext_available(const VkExtensionProperties *props, uint32_t count, const char *name) {
    for (uint32_t i = 0; i < count; i++)
        if (strcmp(props[i].extensionName, name) == 0) return 1;
    return 0;
}

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

// ---- Forward decls ----
static uint32_t find_memory_type(VKState *vk, uint32_t typeFilter, VkMemoryPropertyFlags props);
static void     create_buffer(VKState *vk, VkDeviceSize size, VkBufferUsageFlags usage,
                              VkMemoryPropertyFlags props, VkBuffer *buf, VkDeviceMemory *mem);
static void     stage_to_device(VKState *vk, const void *data, VkDeviceSize size,
                                VkBufferUsageFlags usage, VkBuffer *buf, VkDeviceMemory *mem);
static VkShaderModule load_shader(VKState *vk, const char *path);

// ---- Debug messenger ----
static VKAPI_ATTR VkBool32 VKAPI_CALL debug_callback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT type,
    const VkDebugUtilsMessengerCallbackDataEXT *data, void *user)
{
    (void)severity; (void)type; (void)user;
    fprintf(stderr, "[Vulkan] %s\n", data->pMessage);
    return VK_FALSE;
}

static VkDebugUtilsMessengerCreateInfoEXT make_debug_info(void) {
    return (VkDebugUtilsMessengerCreateInfoEXT){
        .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
        .messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
        .messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                       VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                       VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
        .pfnUserCallback = debug_callback,
    };
}

// ---- Memory helpers ----
static uint32_t find_memory_type(VKState *vk, uint32_t typeFilter, VkMemoryPropertyFlags props) {
    VkPhysicalDeviceMemoryProperties memProps;
    vkGetPhysicalDeviceMemoryProperties(vk->phys_device, &memProps);
    for (uint32_t i = 0; i < memProps.memoryTypeCount; i++) {
        if ((typeFilter & (1u << i)) && (memProps.memoryTypes[i].propertyFlags & props) == props)
            return i;
    }
    fprintf(stderr, "FATAL: no suitable memory type\n");
    exit(1);
    return 0;
}

static void create_buffer(VKState *vk, VkDeviceSize size, VkBufferUsageFlags usage,
                          VkMemoryPropertyFlags props, VkBuffer *buf, VkDeviceMemory *mem) {
    VkBufferCreateInfo bci = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = size, .usage = usage, .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    if (vkCreateBuffer(vk->device, &bci, NULL, buf) != VK_SUCCESS) {
        fprintf(stderr, "FATAL: failed to create buffer\n"); exit(1);
    }
    VkMemoryRequirements mr;
    vkGetBufferMemoryRequirements(vk->device, *buf, &mr);
    VkMemoryAllocateInfo ai = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = mr.size,
        .memoryTypeIndex = find_memory_type(vk, mr.memoryTypeBits, props),
    };
    if (vkAllocateMemory(vk->device, &ai, NULL, mem) != VK_SUCCESS) {
        fprintf(stderr, "FATAL: failed to allocate buffer memory\n"); exit(1);
    }
    vkBindBufferMemory(vk->device, *buf, *mem, 0);
}

static void stage_to_device(VKState *vk, const void *data, VkDeviceSize size,
                            VkBufferUsageFlags usage, VkBuffer *buf, VkDeviceMemory *mem) {
    // Create staging buffer (host-visible)
    VkBuffer staging; VkDeviceMemory stagingMem;
    create_buffer(vk, size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                  &staging, &stagingMem);
    void *mapped;
    vkMapMemory(vk->device, stagingMem, 0, size, 0, &mapped);
    memcpy(mapped, data, size);
    vkUnmapMemory(vk->device, stagingMem);

    // Create device-local buffer
    create_buffer(vk, size, usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                  VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, buf, mem);

    // Copy staging → device via one-time command buffer
    VkCommandBufferAllocateInfo cbai = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = vk->cmd_pool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };
    VkCommandBuffer tmpCmd;
    vkAllocateCommandBuffers(vk->device, &cbai, &tmpCmd);
    VkCommandBufferBeginInfo bi = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    vkBeginCommandBuffer(tmpCmd, &bi);
    VkBufferCopy bc = { .size = size };
    vkCmdCopyBuffer(tmpCmd, staging, *buf, 1, &bc);

    // Barrier: transfer → vertex/index buffer
    VkBufferMemoryBarrier bmb = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_INDEX_READ_BIT,
        .buffer = *buf, .size = VK_WHOLE_SIZE,
    };
    vkCmdPipelineBarrier(tmpCmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_VERTEX_INPUT_BIT, 0, 0, NULL, 1, &bmb, 0, NULL);
    vkEndCommandBuffer(tmpCmd);

    VkSubmitInfo si = { .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                        .commandBufferCount = 1, .pCommandBuffers = &tmpCmd };
    vkQueueSubmit(vk->queue, 1, &si, VK_NULL_HANDLE);
    vkQueueWaitIdle(vk->queue);

    vkFreeCommandBuffers(vk->device, vk->cmd_pool, 1, &tmpCmd);
    vkDestroyBuffer(vk->device, staging, NULL);
    vkFreeMemory(vk->device, stagingMem, NULL);
}

// ---- Shader loading ----
static VkShaderModule load_shader(VKState *vk, const char *path) {
    // Read entire file
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "FATAL: cannot open shader %s\n", path); exit(1); }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    rewind(f);
    char *code = malloc(sz);
    fread(code, 1, sz, f);
    fclose(f);

    VkShaderModuleCreateInfo smci = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = sz, .pCode = (uint32_t *)code,
    };
    VkShaderModule mod;
    if (vkCreateShaderModule(vk->device, &smci, NULL, &mod) != VK_SUCCESS) {
        fprintf(stderr, "FATAL: failed to create shader module %s\n", path); exit(1);
    }
    free(code);
    return mod;
}

// ---- Swapchain creation ----
// Creates the VkSwapchainKHR only (images/views/framebuffers created later in create_swapchain_resources)
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

    // Destroy old swapchain after creating the new one
    if (oldSwapchain) {
        vkDeviceWaitIdle(vk->device);
        for (uint32_t i = 0; i < vk->sc_count; i++) {
            vkDestroyFramebuffer(vk->device, vk->sc_framebufs[i], NULL);
            vkDestroyImageView(vk->device, vk->sc_views[i], NULL);
        }
        free(vk->sc_images); free(vk->sc_views); free(vk->sc_framebufs);
        vkDestroySwapchainKHR(vk->device, oldSwapchain, NULL);
    }
}

static void create_swapchain_resources(VKState *vk) {
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

// ---- Circle mesh generation ----
static void create_circle_mesh(VKState *vk) {
    #define CIRCLE_SEGMENTS 32
    float verts[(CIRCLE_SEGMENTS + 2) * 2];  // center + ring + closing vert
    verts[0] = 0.0f; verts[1] = 0.0f;  // center
    for (int i = 0; i <= CIRCLE_SEGMENTS; i++) {
        float a = 2.0f * (float)M_PI * i / CIRCLE_SEGMENTS;
        verts[2 + i*2]     = cosf(a);
        verts[2 + i*2 + 1] = sinf(a);
    }
    vk->mesh_circle_verts = CIRCLE_SEGMENTS + 2;
    stage_to_device(vk, verts, sizeof(verts), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                    &vk->mesh_circle_vb, &vk->mesh_circle_mem);
}

// ---- Line mesh generation (view cone + spike) ----
static void create_lines_mesh(VKState *vk) {
    float p8 = (float)M_PI / 8.0f;
    float c2 = cosf(2.0f * p8), s2 = sinf(2.0f * p8);
    float c1 = cosf(1.0f * p8), s1 = sinf(1.0f * p8);
    // Each vertex: x, y, type (0=cone, 1=spike)
    float verts[] = {
        // Spike: z=1, from origin to (1,0)
        0.0f, 0.0f, 1.0f,   1.0f, 0.0f, 1.0f,
        // View cone +2PI8: z=0
        0.0f, 0.0f, 0.0f,   c2, s2, 0.0f,
        // View cone +1PI8
        0.0f, 0.0f, 0.0f,   c1, s1, 0.0f,
        // View cone -1PI8
        0.0f, 0.0f, 0.0f,   c1, -s1, 0.0f,
        // View cone -2PI8
        0.0f, 0.0f, 0.0f,   c2, -s2, 0.0f,
    };
    vk->mesh_lines_verts = 10;
    stage_to_device(vk, verts, sizeof(verts), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                    &vk->mesh_lines_vb, &vk->mesh_lines_mem);
}

// ---- HUD quad mesh ----
static void create_hud_mesh(VKState *vk) {
    // 4 types of HUD element, each is a unit quad [0,1]² with type attribute
    // We'll use 4 separate draws with type encoded in attribute, but for now
    // we just need one quad mesh. We'll draw it 4 times with type via push constant.
    // Actually the HUD shader uses vType as a vertex attribute, so we need 4 quads
    // with different types encoded. But we can also just draw the same mesh 4 times
    // and encode type in the vertex buffer differently. Let's create 4 separate
    // vertex buffers or just 4 copies.
    //
    // Simpler: create a single quad slot in the vertex buffer for each type.
    // The draw will use firstVertex to pick the right one.
    float quads[4 * 4 * 3];  // 4 quads × 4 verts × (x, y, type)
    for (int t = 0; t < 4; t++) {
        int base = t * 4 * 3;
        // Triangle strip: (0,0), (1,0), (0,1), (1,1)
        quads[base + 0] = 0.0f; quads[base + 1] = 0.0f; quads[base + 2] = (float)t;
        quads[base + 3] = 1.0f; quads[base + 4] = 0.0f; quads[base + 5] = (float)t;
        quads[base + 6] = 0.0f; quads[base + 7] = 1.0f; quads[base + 8] = (float)t;
        quads[base + 9] = 1.0f; quads[base+10] = 1.0f; quads[base+11] = (float)t;
    }
    stage_to_device(vk, quads, sizeof(quads), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                    &vk->mesh_hud_vb, &vk->mesh_hud_mem);
}

// ---- Create pipelines ----
static void create_pipelines(VKState *vk) {
    VkShaderModule circleVert = load_shader(vk, "shaders/agent_circle.vert.spv");
    VkShaderModule circleFrag = load_shader(vk, "shaders/agent_circle.frag.spv");
    VkShaderModule linesVert  = load_shader(vk, "shaders/agent_lines.vert.spv");
    VkShaderModule linesFrag  = load_shader(vk, "shaders/agent_lines.frag.spv");
    VkShaderModule hudVert    = load_shader(vk, "shaders/agent_hud.vert.spv");
    VkShaderModule hudFrag    = load_shader(vk, "shaders/agent_hud.frag.spv");
    VkShaderModule foodVert   = load_shader(vk, "shaders/food.vert.spv");
    VkShaderModule foodFrag   = load_shader(vk, "shaders/food.frag.spv");

    // Common vertex input state for circle mesh (vec2 position only)
    VkVertexInputBindingDescription circleBindings[] = {
        { .binding = 0, .stride = 2 * sizeof(float), .inputRate = VK_VERTEX_INPUT_RATE_VERTEX },
    };
    VkVertexInputAttributeDescription circleAttrs[] = {
        { .location = 0, .binding = 0, .format = VK_FORMAT_R32G32_SFLOAT, .offset = 0 },
    };

    // Vertex input for lines mesh (vec3: x,y,type)
    VkVertexInputBindingDescription lineBindings[] = {
        { .binding = 0, .stride = 3 * sizeof(float), .inputRate = VK_VERTEX_INPUT_RATE_VERTEX },
    };
    VkVertexInputAttributeDescription lineAttrs[] = {
        { .location = 0, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = 0 },
    };

    // Vertex input for HUD (vec2 + uint type)
    VkVertexInputBindingDescription hudBindings[] = {
        { .binding = 0, .stride = 3 * sizeof(float), .inputRate = VK_VERTEX_INPUT_RATE_VERTEX },
    };
    VkVertexInputAttributeDescription hudAttrs[] = {
        { .location = 0, .binding = 0, .format = VK_FORMAT_R32G32_SFLOAT, .offset = 0 },
        { .location = 1, .binding = 0, .format = VK_FORMAT_R32_UINT, .offset = 2 * sizeof(float) },
    };

    // Vertex input for food (vec2 + vec3 color)
    VkVertexInputBindingDescription foodBindings[] = {
        { .binding = 0, .stride = 5 * sizeof(float), .inputRate = VK_VERTEX_INPUT_RATE_VERTEX },
    };
    VkVertexInputAttributeDescription foodAttrs[] = {
        { .location = 0, .binding = 0, .format = VK_FORMAT_R32G32_SFLOAT, .offset = 0 },
        { .location = 1, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = 2 * sizeof(float) },
    };

    // Common assembly / viewport / rasterization / multisampling / blending
    VkPipelineInputAssemblyStateCreateInfo asmInfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN,
    };
    VkPipelineInputAssemblyStateCreateInfo asmList = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
    };
    VkPipelineInputAssemblyStateCreateInfo asmLine = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST,
    };
    VkPipelineInputAssemblyStateCreateInfo asmStrip = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP,
    };

    VkPipelineViewportStateCreateInfo vpInfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1, .scissorCount = 1,
    };
    VkPipelineRasterizationStateCreateInfo rsInfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode = VK_POLYGON_MODE_FILL, .cullMode = VK_CULL_MODE_NONE,
        .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE, .lineWidth = 1.0f,
    };
    VkPipelineMultisampleStateCreateInfo msInfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
    };
    VkPipelineColorBlendAttachmentState blendAtt = {
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
        .blendEnable = VK_TRUE,
        .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
        .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .colorBlendOp = VK_BLEND_OP_ADD,
        .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
        .dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
        .alphaBlendOp = VK_BLEND_OP_ADD,
    };
    VkPipelineColorBlendStateCreateInfo cbInfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = 1, .pAttachments = &blendAtt,
    };

    VkDynamicState dynStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dynInfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = 2, .pDynamicStates = dynStates,
    };

    // Shader stages helper
    #define MAKE_STAGE(flag, mod) \
        (VkPipelineShaderStageCreateInfo){ \
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, \
            .stage = flag, .module = mod, .pName = "main" }

    // Helper to create pipeline
    #define CREATE_PIPE(name, vertMod, fragMod, asm, bind, battr, bcount, acount) do { \
        VkPipelineShaderStageCreateInfo stages[] = { \
            MAKE_STAGE(VK_SHADER_STAGE_VERTEX_BIT, vertMod), \
            MAKE_STAGE(VK_SHADER_STAGE_FRAGMENT_BIT, fragMod), \
        }; \
        VkPipelineVertexInputStateCreateInfo viInfo = { \
            .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO, \
            .vertexBindingDescriptionCount = bcount, \
            .pVertexBindingDescriptions = bind, \
            .vertexAttributeDescriptionCount = acount, \
            .pVertexAttributeDescriptions = battr, \
        }; \
        VkGraphicsPipelineCreateInfo pci = { \
            .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO, \
            .stageCount = 2, .pStages = stages, \
            .pVertexInputState = &viInfo, .pInputAssemblyState = &asm, \
            .pViewportState = &vpInfo, .pRasterizationState = &rsInfo, \
            .pMultisampleState = &msInfo, .pColorBlendState = &cbInfo, \
            .pDynamicState = &dynInfo, \
            .layout = vk->pipeline_layout, .renderPass = vk->render_pass, .subpass = 0, \
        }; \
        if (vkCreateGraphicsPipelines(vk->device, VK_NULL_HANDLE, 1, &pci, NULL, &name) != VK_SUCCESS) \
            { fprintf(stderr, "FATAL: failed to create " #name " pipeline\n"); exit(1); } \
    } while(0)

    CREATE_PIPE(vk->pipe_circle, circleVert, circleFrag, asmInfo,
                circleBindings, circleAttrs, 1, 1);
    CREATE_PIPE(vk->pipe_lines, linesVert, linesFrag, asmLine,
                lineBindings, lineAttrs, 1, 1);
    CREATE_PIPE(vk->pipe_hud, hudVert, hudFrag, asmStrip,
                hudBindings, hudAttrs, 1, 2);
    CREATE_PIPE(vk->pipe_food, foodVert, foodFrag, asmList,
                foodBindings, foodAttrs, 1, 2);

    #undef CREATE_PIPE
    #undef MAKE_STAGE

    vkDestroyShaderModule(vk->device, circleVert, NULL);
    vkDestroyShaderModule(vk->device, circleFrag, NULL);
    vkDestroyShaderModule(vk->device, linesVert, NULL);
    vkDestroyShaderModule(vk->device, linesFrag, NULL);
    vkDestroyShaderModule(vk->device, hudVert, NULL);
    vkDestroyShaderModule(vk->device, hudFrag, NULL);
    vkDestroyShaderModule(vk->device, foodVert, NULL);
    vkDestroyShaderModule(vk->device, foodFrag, NULL);
}

// ---- Public API ----
VKState *vkinit_create(GLFWwindow *window) {
    VKState *vk = calloc(1, sizeof(VKState));
    vk->window = window;

    // 1. Enumerate available instance extensions to find the right surface extension
    uint32_t availExtCount = 0;
    vkEnumerateInstanceExtensionProperties(NULL, &availExtCount, NULL);
    VkExtensionProperties *availExts = malloc(sizeof(VkExtensionProperties) * availExtCount);
    vkEnumerateInstanceExtensionProperties(NULL, &availExtCount, availExts);

    // 2. Get GLFW-required extensions, or fall back to surface detection
    uint32_t glfwExtCount = 0;
    const char **glfwExts = glfwGetRequiredInstanceExtensions(&glfwExtCount);

    const char *surfaceExt = NULL;
    if (glfwExtCount > 0) {
        surfaceExt = "";  // sentinel: extensions are in glfwExts
    } else {
        // GLFW returned nothing — detect platform surface extension manually
        if (ext_available(availExts, availExtCount, "VK_KHR_xlib_surface"))
            surfaceExt = "VK_KHR_xlib_surface";
        else if (ext_available(availExts, availExtCount, "VK_KHR_xcb_surface"))
            surfaceExt = "VK_KHR_xcb_surface";
        else if (ext_available(availExts, availExtCount, "VK_KHR_wayland_surface"))
            surfaceExt = "VK_KHR_wayland_surface";
        else {
            fprintf(stderr, "FATAL: No window system surface extension found.\n");
            fprintf(stderr, "  Your Vulkan driver only supports headless/display mode.\n");
            fprintf(stderr, "  Available surface-related extensions:\n");
            for (uint32_t i = 0; i < availExtCount; i++) {
                const char *n = availExts[i].extensionName;
                if (strstr(n, "surface") || strstr(n, "display") || strstr(n, "xlib") || strstr(n, "xcb") || strstr(n, "wayland"))
                    fprintf(stderr, "    %s\n", n);
            }
            fprintf(stderr, "\n  To fix:\n");
            fprintf(stderr, "    - Install a Vulkan GPU driver (vulkan-radeon, vulkan-intel, nvidia-utils)\n");
            fprintf(stderr, "    - Or run headless: ./scriptbots -h\n");
            free(availExts); free(vk); return NULL;
        }
    }

    // 3. Build final extension list
    int haveDebugUtils = ext_available(availExts, availExtCount, VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    VkDebugUtilsMessengerCreateInfoEXT dbgInfo = make_debug_info();

    if (surfaceExt[0] == '\0') {
        // Use GLFW's extensions
        uint32_t extCount = glfwExtCount + (haveDebugUtils ? 1 : 0);
        const char **exts = malloc(sizeof(const char *) * extCount);
        for (uint32_t i = 0; i < glfwExtCount; i++) exts[i] = glfwExts[i];
        if (haveDebugUtils) exts[glfwExtCount] = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;

        const char *layers[] = { "VK_LAYER_KHRONOS_validation" };

        VkApplicationInfo appInfo = {
            .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
            .pApplicationName = "ScriptBots", .applicationVersion = VK_MAKE_VERSION(1,0,0),
            .pEngineName = "No Engine", .engineVersion = VK_MAKE_VERSION(1,0,0),
            .apiVersion = VK_API_VERSION_1_2,
        };
        VkInstanceCreateInfo ici = {
            .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
            .pApplicationInfo = &appInfo,
            .enabledExtensionCount = extCount, .ppEnabledExtensionNames = exts,
            .enabledLayerCount = 1, .ppEnabledLayerNames = layers,
            .pNext = haveDebugUtils ? &dbgInfo : NULL,
        };

        VkResult instRes = vkCreateInstance(&ici, NULL, &vk->instance);
        if (instRes != VK_SUCCESS) {
            fprintf(stderr, "Vulkan instance creation failed, retrying without validation layers...\n");
            ici.enabledLayerCount = 0; ici.pNext = NULL;
            ici.enabledExtensionCount = glfwExtCount;
            instRes = vkCreateInstance(&ici, NULL, &vk->instance);
            if (instRes != VK_SUCCESS) {
                fprintf(stderr, "FATAL: Vulkan instance creation failed (VkResult=%d).\n", instRes);
                free(exts); free(availExts); free(vk); return NULL;
            }
        }

        // Debug messenger
        if (haveDebugUtils) {
            PFN_vkCreateDebugUtilsMessengerEXT fn = (PFN_vkCreateDebugUtilsMessengerEXT)
                vkGetInstanceProcAddr(vk->instance, "vkCreateDebugUtilsMessengerEXT");
            if (fn) fn(vk->instance, &dbgInfo, NULL, &vk->debug_messenger);
        }
        free(exts);
    } else {
        // Use our detected surface extension (GLFW didn't provide any)
        const char *ourExts[4];
        ourExts[0] = VK_KHR_SURFACE_EXTENSION_NAME;
        ourExts[1] = surfaceExt;
        uint32_t ourCount = 2;
        if (haveDebugUtils) ourExts[ourCount++] = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;

        VkApplicationInfo appInfo = {
            .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
            .pApplicationName = "ScriptBots", .applicationVersion = VK_MAKE_VERSION(1,0,0),
            .pEngineName = "No Engine", .engineVersion = VK_MAKE_VERSION(1,0,0),
            .apiVersion = VK_API_VERSION_1_2,
        };
        const char *layers[] = { "VK_LAYER_KHRONOS_validation" };
        VkInstanceCreateInfo ici = {
            .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
            .pApplicationInfo = &appInfo,
            .enabledExtensionCount = ourCount, .ppEnabledExtensionNames = ourExts,
            .enabledLayerCount = 1, .ppEnabledLayerNames = layers,
            .pNext = haveDebugUtils ? &dbgInfo : NULL,
        };

        VkResult instRes = vkCreateInstance(&ici, NULL, &vk->instance);
        if (instRes != VK_SUCCESS) {
            fprintf(stderr, "Vulkan instance creation failed, retrying without validation layers...\n");
            ici.enabledLayerCount = 0; ici.pNext = NULL;
            ici.enabledExtensionCount = 2;  // surface + platform
            instRes = vkCreateInstance(&ici, NULL, &vk->instance);
            if (instRes != VK_SUCCESS) {
                fprintf(stderr, "FATAL: Vulkan instance creation failed (VkResult=%d).\n", instRes);
                free(availExts); free(vk); return NULL;
            }
        }

        if (haveDebugUtils) {
            PFN_vkCreateDebugUtilsMessengerEXT fn = (PFN_vkCreateDebugUtilsMessengerEXT)
                vkGetInstanceProcAddr(vk->instance, "vkCreateDebugUtilsMessengerEXT");
            if (fn) fn(vk->instance, &dbgInfo, NULL, &vk->debug_messenger);
        }
    }

    free(availExts);

    // 2. Create surface
    {
        VkResult surfRes = glfwCreateWindowSurface(vk->instance, window, NULL, &vk->surface);
        if (surfRes != VK_SUCCESS) {
            fprintf(stderr, "FATAL: surface creation failed (VkResult=%d)\n", surfRes);
            // Check if physical devices are available
            uint32_t devCount = 0;
            vkEnumeratePhysicalDevices(vk->instance, &devCount, NULL);
            fprintf(stderr, "  Available physical devices: %u\n", devCount);
            exit(1);
        }
    }

    // 3. Pick best physical device (must support graphics + present to our surface)
    {
        uint32_t devCount;
        vkEnumeratePhysicalDevices(vk->instance, &devCount, NULL);
        if (devCount == 0) { fprintf(stderr, "FATAL: No Vulkan physical devices found\n"); exit(1); }

        VkPhysicalDevice *devs = malloc(sizeof(VkPhysicalDevice) * devCount);
        vkEnumeratePhysicalDevices(vk->instance, &devCount, devs);

        vk->phys_device = VK_NULL_HANDLE;
        vk->queue_family = UINT32_MAX;
        int bestScore = -1;

        for (uint32_t i = 0; i < devCount; i++) {
            VkPhysicalDeviceProperties props;
            vkGetPhysicalDeviceProperties(devs[i], &props);

            // Find a queue family that supports graphics + present
            uint32_t qfCount;
            vkGetPhysicalDeviceQueueFamilyProperties(devs[i], &qfCount, NULL);
            VkQueueFamilyProperties *qfs = malloc(sizeof(VkQueueFamilyProperties) * qfCount);
            vkGetPhysicalDeviceQueueFamilyProperties(devs[i], &qfCount, qfs);

            uint32_t qfam = UINT32_MAX;
            for (uint32_t j = 0; j < qfCount; j++) {
                VkBool32 present = VK_FALSE;
                vkGetPhysicalDeviceSurfaceSupportKHR(devs[i], j, vk->surface, &present);
                if ((qfs[j].queueFlags & VK_QUEUE_GRAPHICS_BIT) && present) {
                    qfam = j;
                    break;
                }
            }
            free(qfs);

            if (qfam == UINT32_MAX) continue;  // device can't present to our surface — skip

            // Score: discrete=3, integrated=2, other=1
            int score = 0;
            if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)      score = 3;
            else if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU) score = 2;
            else if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU)    score = 1;
            else if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU)            score = 0;
            else                                                                  score = 0;

            if (score > bestScore) {
                bestScore = score;
                vk->phys_device = devs[i];
                vk->queue_family = qfam;
            }
        }
        free(devs);

        if (vk->phys_device == VK_NULL_HANDLE) {
            fprintf(stderr, "FATAL: No physical device supports both graphics and presentation.\n");
            exit(1);
        }

        VkPhysicalDeviceProperties chosenProps;
        vkGetPhysicalDeviceProperties(vk->phys_device, &chosenProps);
        printf("[Vulkan] Selected device: %s (type=%d)\n", chosenProps.deviceName, chosenProps.deviceType);
    }

    // 4. Create device
    {
        float prio = 1.0f;
        VkDeviceQueueCreateInfo qci = {
            .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
            .queueFamilyIndex = vk->queue_family, .queueCount = 1,
            .pQueuePriorities = &prio,
        };
        const char *devExts[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
        VkPhysicalDeviceFeatures features = {0};
        VkDeviceCreateInfo dci = {
            .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
            .queueCreateInfoCount = 1, .pQueueCreateInfos = &qci,
            .enabledExtensionCount = 1, .ppEnabledExtensionNames = devExts,
            .pEnabledFeatures = &features,
        };
        if (vkCreateDevice(vk->phys_device, &dci, NULL, &vk->device) != VK_SUCCESS)
            { fprintf(stderr, "FATAL: device creation failed\n"); exit(1); }
        vkGetDeviceQueue(vk->device, vk->queue_family, 0, &vk->queue);
    }

    // 5. Create command pool
    {
        VkCommandPoolCreateInfo cpci = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
            .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
            .queueFamilyIndex = vk->queue_family,
        };
        vkCreateCommandPool(vk->device, &cpci, NULL, &vk->cmd_pool);

        VkCommandBufferAllocateInfo cbai = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .commandPool = vk->cmd_pool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            .commandBufferCount = 2,
        };
        vkAllocateCommandBuffers(vk->device, &cbai, vk->cmd_buf);
    }

    // 6. Create swapchain (must be before render pass to get format)
    create_swapchain(vk);

    // 7. Create render pass (uses actual swapchain format)
    {
        VkAttachmentDescription colorAtt = {
            .format = vk->sc_format,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
            .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        };
        VkAttachmentReference colorRef = { .attachment = 0, .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
        VkSubpassDescription subpass = {
            .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
            .colorAttachmentCount = 1, .pColorAttachments = &colorRef,
        };
        VkRenderPassCreateInfo rpci = {
            .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
            .attachmentCount = 1, .pAttachments = &colorAtt,
            .subpassCount = 1, .pSubpasses = &subpass,
        };
        if (vkCreateRenderPass(vk->device, &rpci, NULL, &vk->render_pass) != VK_SUCCESS)
            { fprintf(stderr, "FATAL: render pass creation failed\n"); exit(1); }
    }

    // 8. Create swapchain resources (image views, framebuffers — needs render pass)
    create_swapchain_resources(vk);

    // 9. Create descriptor set layout
    {
        VkDescriptorSetLayoutBinding bindings[] = {
            { .binding = 0, .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
              .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_VERTEX_BIT },
            { .binding = 1, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
              .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_VERTEX_BIT },
        };
        VkDescriptorSetLayoutCreateInfo dslci = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
            .bindingCount = 2, .pBindings = bindings,
        };
        vkCreateDescriptorSetLayout(vk->device, &dslci, NULL, &vk->desc_layout);
    }

    // 10. Create pipeline layout (with push constants for all pipeline types)
    {
        VkPushConstantRange pcr = {
            .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
            .offset = 0, .size = 32,  // enough for max push constant struct
        };
        VkPipelineLayoutCreateInfo plci = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
            .setLayoutCount = 1, .pSetLayouts = &vk->desc_layout,
            .pushConstantRangeCount = 1, .pPushConstantRanges = &pcr,
        };
        vkCreatePipelineLayout(vk->device, &plci, NULL, &vk->pipeline_layout);
    }

    // 11. Create static meshes
    create_circle_mesh(vk);
    create_lines_mesh(vk);
    create_hud_mesh(vk);

    // 12. Create pipelines
    create_pipelines(vk);

    // 13. Create buffers (camera UBO, agent SSBO, food VBO)
    create_buffer(vk, sizeof(CameraUBO),
                  VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                  &vk->cam_ubo_buf, &vk->cam_ubo_mem);
    create_buffer(vk, VK_MAX_AGENTS * sizeof(AgentInstance),
                  VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                  &vk->agent_buf, &vk->agent_mem);
    create_buffer(vk, VK_MAX_FOOD_VERTS * sizeof(FoodVertex),
                  VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                  &vk->food_vbuf, &vk->food_vmem);

    // 14. Create descriptor pool & set
    {
        VkDescriptorPoolSize poolSizes[] = {
            { .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, .descriptorCount = 1 },
            { .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount = 1 },
        };
        VkDescriptorPoolCreateInfo dpci = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
            .flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT,
            .maxSets = 1, .poolSizeCount = 2, .pPoolSizes = poolSizes,
        };
        vkCreateDescriptorPool(vk->device, &dpci, NULL, &vk->desc_pool);

        VkDescriptorSetAllocateInfo dsai = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
            .descriptorPool = vk->desc_pool, .descriptorSetCount = 1,
            .pSetLayouts = &vk->desc_layout,
        };
        vkAllocateDescriptorSets(vk->device, &dsai, &vk->desc_set);

        VkDescriptorBufferInfo camInfo = {
            .buffer = vk->cam_ubo_buf, .offset = 0, .range = sizeof(CameraUBO),
        };
        VkDescriptorBufferInfo agentInfo = {
            .buffer = vk->agent_buf, .offset = 0, .range = VK_MAX_AGENTS * sizeof(AgentInstance),
        };
        VkWriteDescriptorSet writes[] = {
            { .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
              .dstSet = vk->desc_set, .dstBinding = 0, .dstArrayElement = 0,
              .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
              .pBufferInfo = &camInfo },
            { .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
              .dstSet = vk->desc_set, .dstBinding = 1, .dstArrayElement = 0,
              .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
              .pBufferInfo = &agentInfo },
        };
        vkUpdateDescriptorSets(vk->device, 2, writes, 0, NULL);
    }

    // 15. Create sync objects
    {
        VkSemaphoreCreateInfo sci = { .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
        VkFenceCreateInfo fci = {
            .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
            .flags = VK_FENCE_CREATE_SIGNALED_BIT,
        };
        for (int i = 0; i < VK_MAX_FRAMES_IN_FLIGHT; i++) {
            vkCreateSemaphore(vk->device, &sci, NULL, &vk->image_avail[i]);
            vkCreateFence(vk->device, &fci, NULL, &vk->in_flight[i]);
        }
        // render_done semaphores: one per swapchain image (avoids reuse)
        vk->render_done = malloc(sizeof(VkSemaphore) * vk->sc_count);
        for (uint32_t i = 0; i < vk->sc_count; i++) {
            vkCreateSemaphore(vk->device, &sci, NULL, &vk->render_done[i]);
        }
        vk->current_frame = 0;
    }

    printf("[Vulkan] Initialized successfully.\n");
    return vk;
}

void vkinit_destroy(VKState *vk) {
    vkDeviceWaitIdle(vk->device);
    for (int i = 0; i < VK_MAX_FRAMES_IN_FLIGHT; i++) {
        vkDestroySemaphore(vk->device, vk->image_avail[i], NULL);
        vkDestroyFence(vk->device, vk->in_flight[i], NULL);
    }
    if (vk->render_done) {
        for (uint32_t i = 0; i < vk->sc_count; i++)
            vkDestroySemaphore(vk->device, vk->render_done[i], NULL);
        free(vk->render_done);
    }
    vkDestroyDescriptorPool(vk->device, vk->desc_pool, NULL);
    vkDestroyDescriptorSetLayout(vk->device, vk->desc_layout, NULL);

    vkDestroyBuffer(vk->device, vk->food_vbuf, NULL);
    vkFreeMemory(vk->device, vk->food_vmem, NULL);
    vkDestroyBuffer(vk->device, vk->agent_buf, NULL);
    vkFreeMemory(vk->device, vk->agent_mem, NULL);
    vkDestroyBuffer(vk->device, vk->cam_ubo_buf, NULL);
    vkFreeMemory(vk->device, vk->cam_ubo_mem, NULL);

    vkDestroyBuffer(vk->device, vk->mesh_hud_vb, NULL);
    vkFreeMemory(vk->device, vk->mesh_hud_mem, NULL);
    vkDestroyBuffer(vk->device, vk->mesh_lines_vb, NULL);
    vkFreeMemory(vk->device, vk->mesh_lines_mem, NULL);
    vkDestroyBuffer(vk->device, vk->mesh_circle_vb, NULL);
    vkFreeMemory(vk->device, vk->mesh_circle_mem, NULL);

    vkDestroyPipeline(vk->device, vk->pipe_food, NULL);
    vkDestroyPipeline(vk->device, vk->pipe_hud, NULL);
    vkDestroyPipeline(vk->device, vk->pipe_lines, NULL);
    vkDestroyPipeline(vk->device, vk->pipe_circle, NULL);
    vkDestroyPipelineLayout(vk->device, vk->pipeline_layout, NULL);

    for (uint32_t i = 0; i < vk->sc_count; i++) {
        vkDestroyFramebuffer(vk->device, vk->sc_framebufs[i], NULL);
        vkDestroyImageView(vk->device, vk->sc_views[i], NULL);
    }
    free(vk->sc_framebufs); free(vk->sc_views); free(vk->sc_images);
    vkDestroySwapchainKHR(vk->device, vk->swapchain, NULL);
    vkDestroyRenderPass(vk->device, vk->render_pass, NULL);
    vkDestroyCommandPool(vk->device, vk->cmd_pool, NULL);
    vkDestroyDevice(vk->device, NULL);
    vkDestroySurfaceKHR(vk->instance, vk->surface, NULL);

    PFN_vkDestroyDebugUtilsMessengerEXT fn = (PFN_vkDestroyDebugUtilsMessengerEXT)
        vkGetInstanceProcAddr(vk->instance, "vkDestroyDebugUtilsMessengerEXT");
    if (fn) fn(vk->instance, vk->debug_messenger, NULL);
    vkDestroyInstance(vk->instance, NULL);
    free(vk);
}

int vkinit_recreate_swapchain(VKState *vk) {
    vkDeviceWaitIdle(vk->device);

    // Destroy old framebuffers and image views (swapchain destroyed in create_swapchain)
    for (uint32_t i = 0; i < vk->sc_count; i++) {
        vkDestroyFramebuffer(vk->device, vk->sc_framebufs[i], NULL);
        vkDestroyImageView(vk->device, vk->sc_views[i], NULL);
    }
    free(vk->sc_framebufs); free(vk->sc_views); free(vk->sc_images);
    vk->sc_framebufs = NULL; vk->sc_views = NULL; vk->sc_images = NULL;
    vk->sc_count = 0;

    // Create new swapchain
    create_swapchain(vk);

    // If swapchain creation returned early (minimized), try again later
    if (vk->needs_recreation) return 0;

    // Recreate render pass (format might have changed if driver acts weird)
    vkDestroyRenderPass(vk->device, vk->render_pass, NULL);
    {
        VkAttachmentDescription colorAtt = {
            .format = vk->sc_format,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
            .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        };
        VkAttachmentReference colorRef = { .attachment = 0, .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
        VkSubpassDescription subpass = {
            .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
            .colorAttachmentCount = 1, .pColorAttachments = &colorRef,
        };
        VkRenderPassCreateInfo rpci = {
            .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
            .attachmentCount = 1, .pAttachments = &colorAtt,
            .subpassCount = 1, .pSubpasses = &subpass,
        };
        if (vkCreateRenderPass(vk->device, &rpci, NULL, &vk->render_pass) != VK_SUCCESS) {
            fprintf(stderr, "FATAL: render pass recreation failed\n"); return 0;
        }
    }

    // Recreate swapchain resources (framebuffers with new render pass + extent)
    create_swapchain_resources(vk);

    // Recreate per-image render_done semaphores
    {
        VkSemaphoreCreateInfo sci = { .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
        if (vk->render_done) {
            for (uint32_t i = 0; i < vk->sc_count; i++)
                vkDestroySemaphore(vk->device, vk->render_done[i], NULL);
            free(vk->render_done);
        }
        vk->render_done = malloc(sizeof(VkSemaphore) * vk->sc_count);
        for (uint32_t i = 0; i < vk->sc_count; i++)
            vkCreateSemaphore(vk->device, &sci, NULL, &vk->render_done[i]);
    }

    // Pipelines reference the render pass — must recreate them too
    vkDestroyPipeline(vk->device, vk->pipe_food, NULL);
    vkDestroyPipeline(vk->device, vk->pipe_hud, NULL);
    vkDestroyPipeline(vk->device, vk->pipe_lines, NULL);
    vkDestroyPipeline(vk->device, vk->pipe_circle, NULL);
    create_pipelines(vk);

    vk->needs_recreation = 0;
    return 1;
}

// Getters
VkRenderPass  vkinit_get_render_pass(VKState *vk)  { return vk->render_pass; }
VkDevice      vkinit_get_device(VKState *vk)        { return vk->device; }
VkPhysicalDevice vkinit_get_phys_device(VKState *vk){ return vk->phys_device; }
uint32_t      vkinit_get_queue_family(VKState *vk)  { return vk->queue_family; }
VkQueue       vkinit_get_queue(VKState *vk)          { return vk->queue; }
VkCommandBuffer vkinit_get_command_buffer(VKState *vk) { return vk->cmd_buf[0]; }
uint32_t      vkinit_get_command_buffer_count(VKState *vk) { return 2; }
VkInstance    vkinit_get_instance(VKState *vk)       { return vk->instance; }
VkSwapchainKHR vkinit_get_swapchain(VKState *vk)     { return vk->swapchain; }
void vkinit_get_extent(VKState *vk, uint32_t *w, uint32_t *h) { *w = vk->sc_extent.width; *h = vk->sc_extent.height; }
int  vkinit_needs_recreation(VKState *vk)    { return vk->needs_recreation; }
void vkinit_set_needs_recreation(VKState *vk) { vk->needs_recreation = 1; }
