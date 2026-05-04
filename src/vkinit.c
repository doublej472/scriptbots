// vkinit.c — Vulkan instance, device, pipelines, buffers (one-time setup)
#include "vkhelpers.h"
#include "vkswap.h"
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
static void stage_to_device(VKState *vk, const void *data, VkDeviceSize size,
                            VkBufferUsageFlags usage, VkBuffer *buf, VkDeviceMemory *mem);

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
uint32_t vk_find_memory_type(VkPhysicalDevice phys_device, uint32_t typeFilter, VkMemoryPropertyFlags props) {
    VkPhysicalDeviceMemoryProperties memProps;
    vkGetPhysicalDeviceMemoryProperties(phys_device, &memProps);
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
        .memoryTypeIndex = vk_find_memory_type(vk->phys_device, mr.memoryTypeBits, props),
    };
    if (vkAllocateMemory(vk->device, &ai, NULL, mem) != VK_SUCCESS) {
        fprintf(stderr, "FATAL: failed to allocate buffer memory\n"); exit(1);
    }
    vkBindBufferMemory(vk->device, *buf, *mem, 0);
}

static void stage_to_device(VKState *vk, const void *data, VkDeviceSize size,
                            VkBufferUsageFlags usage, VkBuffer *buf, VkDeviceMemory *mem) {
    VkBuffer staging; VkDeviceMemory stagingMem;
    create_buffer(vk, size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                  &staging, &stagingMem);
    void *mapped;
    vkMapMemory(vk->device, stagingMem, 0, size, 0, &mapped);
    memcpy(mapped, data, size);
    vkUnmapMemory(vk->device, stagingMem);

    create_buffer(vk, size, usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                  VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, buf, mem);

    VkCommandBufferAllocateInfo cbai = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = vk->cmd_pool_transfer, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
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

    // Release mesh buffer ownership from transfer → graphics queue family
    uint32_t srcFam = vk->transfer_family, dstFam = vk->gfx_family;
    if (srcFam == dstFam) srcFam = dstFam = VK_QUEUE_FAMILY_IGNORED;
    VkBufferMemoryBarrier bmb = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
        .dstAccessMask = 0,
        .srcQueueFamilyIndex = srcFam,
        .dstQueueFamilyIndex = dstFam,
        .buffer = *buf, .size = VK_WHOLE_SIZE,
    };
    vkCmdPipelineBarrier(tmpCmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0,
                         0, NULL, 1, &bmb, 0, NULL);
    vkEndCommandBuffer(tmpCmd);

    VkSubmitInfo si = { .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                        .commandBufferCount = 1, .pCommandBuffers = &tmpCmd };
    vkQueueSubmit(vk->transfer_queue, 1, &si, VK_NULL_HANDLE);
    vkQueueWaitIdle(vk->transfer_queue);

    vkFreeCommandBuffers(vk->device, vk->cmd_pool_transfer, 1, &tmpCmd);
    vkDestroyBuffer(vk->device, staging, NULL);
    vkFreeMemory(vk->device, stagingMem, NULL);
}

// ---- Shader loading ----
VkShaderModule vk_load_shader(VkDevice device, const char *path) {
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
    if (vkCreateShaderModule(device, &smci, NULL, &mod) != VK_SUCCESS) {
        fprintf(stderr, "FATAL: failed to create shader module %s\n", path); exit(1);
    }
    free(code);
    return mod;
}

// ---- Circle mesh generation ----
static void create_circle_mesh(VKState *vk) {
    #define CIRCLE_SEGMENTS 32
    float verts[(CIRCLE_SEGMENTS + 2) * 2];
    verts[0] = 0.0f; verts[1] = 0.0f;
    for (int i = 0; i <= CIRCLE_SEGMENTS; i++) {
        float a = 2.0f * (float)M_PI * i / CIRCLE_SEGMENTS;
        verts[2 + i*2]     = cosf(a);
        verts[2 + i*2 + 1] = sinf(a);
    }
    vk->mesh_circle_verts = CIRCLE_SEGMENTS + 2;
    stage_to_device(vk, verts, sizeof(verts), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                    &vk->mesh_circle_vb, &vk->mesh_circle_mem);
}

// ---- Line mesh generation ----
static void create_lines_mesh(VKState *vk) {
    float p8 = (float)M_PI / 8.0f;
    float c2 = cosf(2.0f * p8), s2 = sinf(2.0f * p8);
    float c1 = cosf(1.0f * p8), s1 = sinf(1.0f * p8);
    float verts[] = {
        0.0f, 0.0f, 1.0f,   1.0f, 0.0f, 1.0f,
        0.0f, 0.0f, 0.0f,   c2, s2, 0.0f,
        0.0f, 0.0f, 0.0f,   c1, s1, 0.0f,
        0.0f, 0.0f, 0.0f,   c1, -s1, 0.0f,
        0.0f, 0.0f, 0.0f,   c2, -s2, 0.0f,
    };
    vk->mesh_lines_verts = 10;
    stage_to_device(vk, verts, sizeof(verts), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                    &vk->mesh_lines_vb, &vk->mesh_lines_mem);
}

// ---- HUD quad mesh ----
static void create_hud_mesh(VKState *vk) {
    typedef struct { float x, y; uint32_t t; } HudVertex;
    HudVertex quads[4 * 4];
    for (int t = 0; t < 4; t++) {
        int b = t * 4;
        quads[b  ] = (HudVertex){0.0f, 0.0f, (uint32_t)t};
        quads[b+1] = (HudVertex){1.0f, 0.0f, (uint32_t)t};
        quads[b+2] = (HudVertex){0.0f, 1.0f, (uint32_t)t};
        quads[b+3] = (HudVertex){1.0f, 1.0f, (uint32_t)t};
    }
    stage_to_device(vk, quads, sizeof(quads), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                    &vk->mesh_hud_vb, &vk->mesh_hud_mem);
}

// ---- Create pipelines ----
static void create_pipelines(VKState *vk) {
    VkShaderModule circleVert = vk_load_shader(vk->device, "shaders/agent_circle.vert.spv");
    VkShaderModule circleFrag = vk_load_shader(vk->device, "shaders/agent_circle.frag.spv");
    VkShaderModule linesVert  = vk_load_shader(vk->device, "shaders/agent_lines.vert.spv");
    VkShaderModule linesFrag  = vk_load_shader(vk->device, "shaders/agent_lines.frag.spv");
    VkShaderModule hudVert    = vk_load_shader(vk->device, "shaders/agent_hud.vert.spv");
    VkShaderModule hudFrag    = vk_load_shader(vk->device, "shaders/agent_hud.frag.spv");
    VkShaderModule foodVert   = vk_load_shader(vk->device, "shaders/food.vert.spv");
    VkShaderModule foodFrag   = vk_load_shader(vk->device, "shaders/food.frag.spv");

    VkVertexInputBindingDescription circleBindings[] = {
        { .binding = 0, .stride = 2 * sizeof(float), .inputRate = VK_VERTEX_INPUT_RATE_VERTEX },
    };
    VkVertexInputAttributeDescription circleAttrs[] = {
        { .location = 0, .binding = 0, .format = VK_FORMAT_R32G32_SFLOAT, .offset = 0 },
    };

    VkVertexInputBindingDescription lineBindings[] = {
        { .binding = 0, .stride = 3 * sizeof(float), .inputRate = VK_VERTEX_INPUT_RATE_VERTEX },
    };
    VkVertexInputAttributeDescription lineAttrs[] = {
        { .location = 0, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = 0 },
    };

    VkVertexInputBindingDescription hudBindings[] = {
        { .binding = 0, .stride = 2 * sizeof(float) + sizeof(uint32_t), .inputRate = VK_VERTEX_INPUT_RATE_VERTEX },
    };
    VkVertexInputAttributeDescription hudAttrs[] = {
        { .location = 0, .binding = 0, .format = VK_FORMAT_R32G32_SFLOAT, .offset = 0 },
        { .location = 1, .binding = 0, .format = VK_FORMAT_R32_UINT, .offset = 2 * sizeof(float) },
    };

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

    #define MAKE_STAGE(flag, mod) \
        (VkPipelineShaderStageCreateInfo){ \
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, \
            .stage = flag, .module = mod, .pName = "main" }

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
                NULL, NULL, 0, 0);  // procedural quad — no vertex input

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

// ---- Init-stage helpers ----

// Creates instance, surface, picks physical device, creates logical device.
// Returns 0 on failure (vk partially initialised, caller must free).
static int init_core(VKState *vk) {
    uint32_t availExtCount = 0;
    vkEnumerateInstanceExtensionProperties(NULL, &availExtCount, NULL);
    VkExtensionProperties *availExts = malloc(sizeof(VkExtensionProperties) * availExtCount);
    vkEnumerateInstanceExtensionProperties(NULL, &availExtCount, availExts);

    uint32_t glfwExtCount = 0;
    const char **glfwExts = glfwGetRequiredInstanceExtensions(&glfwExtCount);

    const char *surfaceExt = NULL;
    if (glfwExtCount > 0) {
        surfaceExt = "";
    } else {
        if (ext_available(availExts, availExtCount, "VK_KHR_xlib_surface"))
            surfaceExt = "VK_KHR_xlib_surface";
        else if (ext_available(availExts, availExtCount, "VK_KHR_xcb_surface"))
            surfaceExt = "VK_KHR_xcb_surface";
        else if (ext_available(availExts, availExtCount, "VK_KHR_wayland_surface"))
            surfaceExt = "VK_KHR_wayland_surface";
        else {
            fprintf(stderr, "FATAL: No window system surface extension found.\n");
            free(availExts); return 0;
        }
    }

    int haveDebugUtils = ext_available(availExts, availExtCount, VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    VkDebugUtilsMessengerCreateInfoEXT dbgInfo = make_debug_info();

    if (surfaceExt[0] == '\0') {
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
                free(exts); free(availExts); return 0;
            }
        }
        if (haveDebugUtils) {
            PFN_vkCreateDebugUtilsMessengerEXT fn = (PFN_vkCreateDebugUtilsMessengerEXT)
                vkGetInstanceProcAddr(vk->instance, "vkCreateDebugUtilsMessengerEXT");
            if (fn) fn(vk->instance, &dbgInfo, NULL, &vk->debug_messenger);
        }
        free(exts);
    } else {
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
            ici.enabledExtensionCount = 2;
            instRes = vkCreateInstance(&ici, NULL, &vk->instance);
            if (instRes != VK_SUCCESS) {
                fprintf(stderr, "FATAL: Vulkan instance creation failed (VkResult=%d).\n", instRes);
                free(availExts); return 0;
            }
        }
        if (haveDebugUtils) {
            PFN_vkCreateDebugUtilsMessengerEXT fn = (PFN_vkCreateDebugUtilsMessengerEXT)
                vkGetInstanceProcAddr(vk->instance, "vkCreateDebugUtilsMessengerEXT");
            if (fn) fn(vk->instance, &dbgInfo, NULL, &vk->debug_messenger);
        }
    }

    free(availExts);

    // Create surface
    {
        VkResult surfRes = glfwCreateWindowSurface(vk->instance, vk->window, NULL, &vk->surface);
        if (surfRes != VK_SUCCESS) {
            fprintf(stderr, "FATAL: surface creation failed (VkResult=%d)\n", surfRes);
            uint32_t devCount = 0;
            vkEnumeratePhysicalDevices(vk->instance, &devCount, NULL);
            fprintf(stderr, "  Available physical devices: %u\n", devCount);
            exit(1);
        }
    }

    // Pick best physical device and dedicated queue families.
    // Strategy: prefer discrete GPUs, and for each role (graphics / compute /
    // transfer) pick the queue family with the fewest capability bits so that
    // work can run on separate hardware engines (SDMA, ACE) concurrently.
    {
        uint32_t devCount;
        vkEnumeratePhysicalDevices(vk->instance, &devCount, NULL);
        if (devCount == 0) { fprintf(stderr, "FATAL: No Vulkan physical devices found\n"); exit(1); }

        VkPhysicalDevice *devs = malloc(sizeof(VkPhysicalDevice) * devCount);
        vkEnumeratePhysicalDevices(vk->instance, &devCount, devs);
        vk->phys_device = VK_NULL_HANDLE;
        vk->gfx_family = vk->compute_family = vk->transfer_family = UINT32_MAX;
        int bestScore = -1;

        for (uint32_t i = 0; i < devCount; i++) {
            VkPhysicalDeviceProperties props;
            vkGetPhysicalDeviceProperties(devs[i], &props);

            // Strongly prefer discrete GPUs
            int score = 0;
            if      (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)   score = 1000;
            else if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU) score = 500;
            else if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU)    score = 100;
            else                                                                  continue;

            // Find the best queue family for each role.
            // For each role we prefer the family with the smallest popcount
            // of VkQueueFlags — fewer capabilities = more specialised.
            // Graphics must also support presentation.
            uint32_t qfCount;
            vkGetPhysicalDeviceQueueFamilyProperties(devs[i], &qfCount, NULL);
            VkQueueFamilyProperties *qfs = malloc(sizeof(VkQueueFamilyProperties) * qfCount);
            vkGetPhysicalDeviceQueueFamilyProperties(devs[i], &qfCount, qfs);

            uint32_t best_gfx = UINT32_MAX, best_comp = UINT32_MAX, best_xfer = UINT32_MAX;
            int      gfx_cost = 999,       comp_cost = 999,       xfer_cost = 999;

            for (uint32_t j = 0; j < qfCount; j++) {
                VkQueueFlags f = qfs[j].queueFlags;
                int cap_count = __builtin_popcount((unsigned int)f);
                VkBool32 can_present = VK_FALSE;
                vkGetPhysicalDeviceSurfaceSupportKHR(devs[i], j, vk->surface, &can_present);

                // Graphics: GRAPHICS + present, minimal extra caps
                if ((f & VK_QUEUE_GRAPHICS_BIT) && can_present) {
                    if (cap_count < gfx_cost) { gfx_cost = cap_count; best_gfx = j; }
                }
                // Compute: COMPUTE, penalise GRAPHICS (want async compute)
                if (f & VK_QUEUE_COMPUTE_BIT) {
                    int cost = cap_count + ((f & VK_QUEUE_GRAPHICS_BIT) ? 8 : 0);
                    if (cost < comp_cost) { comp_cost = cost; best_comp = j; }
                }
                // Transfer: TRANSFER, penalise GRAPHICS+COMPUTE (want SDMA)
                if (f & VK_QUEUE_TRANSFER_BIT) {
                    int cost = cap_count
                             + ((f & VK_QUEUE_GRAPHICS_BIT)  ? 8 : 0)
                             + ((f & VK_QUEUE_COMPUTE_BIT) ? 4 : 0);
                    if (cost < xfer_cost) { xfer_cost = cost; best_xfer = j; }
                }
            }
            free(qfs);

            if (best_gfx == UINT32_MAX) continue;

            // Fall back to less-specialised families when a dedicated one is absent
            uint32_t gfx  = best_gfx;
            uint32_t comp = (best_comp != UINT32_MAX) ? best_comp : gfx;
            uint32_t xfer = (best_xfer != UINT32_MAX) ? best_xfer : comp;

            // Boost score for having truly separate (concurrent) queue families
            if (comp != gfx)                  score += 10;
            if (xfer != gfx && xfer != comp)  score += 20;

            if (score > bestScore) {
                bestScore = score;
                vk->phys_device     = devs[i];
                vk->gfx_family      = gfx;
                vk->compute_family  = comp;
                vk->transfer_family = xfer;
            }
        }
        free(devs);

        if (vk->phys_device == VK_NULL_HANDLE) {
            fprintf(stderr, "FATAL: No suitable Vulkan device found.\n");
            exit(1);
        }

        VkPhysicalDeviceProperties chosenProps;
        vkGetPhysicalDeviceProperties(vk->phys_device, &chosenProps);
        printf("[Vulkan] %s | score=%d | gfx=%u compute=%u transfer=%u\n",
               chosenProps.deviceName, bestScore,
               vk->gfx_family, vk->compute_family, vk->transfer_family);
    }

    // Create device (up to 3 distinct queue families)
    {
        float prio = 1.0f;
        uint32_t fams[3]; int fc = 0;
        fams[fc++] = vk->gfx_family;
        if (vk->compute_family  != fams[0]) fams[fc++] = vk->compute_family;
        if (vk->transfer_family != fams[0] && vk->transfer_family != fams[1])
                                            fams[fc++] = vk->transfer_family;

        VkDeviceQueueCreateInfo qcis[3];
        for (int i = 0; i < fc; i++)
            qcis[i] = (VkDeviceQueueCreateInfo){
                .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
                .queueFamilyIndex = fams[i], .queueCount = 1,
                .pQueuePriorities = &prio,
            };

        const char *devExts[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
        VkPhysicalDeviceFeatures features = {0};
        VkDeviceCreateInfo dci = {
            .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
            .queueCreateInfoCount = (uint32_t)fc, .pQueueCreateInfos = qcis,
            .enabledExtensionCount = 1, .ppEnabledExtensionNames = devExts,
            .pEnabledFeatures = &features,
        };
        if (vkCreateDevice(vk->phys_device, &dci, NULL, &vk->device) != VK_SUCCESS)
            { fprintf(stderr, "FATAL: device creation failed\n"); exit(1); }
        vkGetDeviceQueue(vk->device, vk->gfx_family,      0, &vk->gfx_queue);
        vkGetDeviceQueue(vk->device, vk->compute_family,  0, &vk->compute_queue);
        vkGetDeviceQueue(vk->device, vk->transfer_family, 0, &vk->transfer_queue);
    }
    return 1;
}

// ---- Public API ----
VKState *vkinit_create(GLFWwindow *window) {
    VKState *vk = calloc(1, sizeof(VKState));
    vk->window = window;

    if (!init_core(vk)) { free(vk); return NULL; }

    // Command pools (one per queue family) & per-frame graphics command buffers
    {
        VkCommandPoolCreateInfo cpci = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
            .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        };
        cpci.queueFamilyIndex = vk->gfx_family;
        vkCreateCommandPool(vk->device, &cpci, NULL, &vk->cmd_pool_gfx);
        cpci.queueFamilyIndex = vk->compute_family;
        vkCreateCommandPool(vk->device, &cpci, NULL, &vk->cmd_pool_compute);
        cpci.queueFamilyIndex = vk->transfer_family;
        vkCreateCommandPool(vk->device, &cpci, NULL, &vk->cmd_pool_transfer);

        VkCommandBufferAllocateInfo cbai = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .commandPool = vk->cmd_pool_gfx, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            .commandBufferCount = 2,
        };
        vkAllocateCommandBuffers(vk->device, &cbai, vk->cmd_buf);
    }

    // Swapchain + render pass
    vkswap_create(vk);
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
    vkswap_build_resources(vk);

    // Descriptor set layout
    {
        VkDescriptorSetLayoutBinding bindings[] = {
            { .binding = 0, .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
              .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_VERTEX_BIT },
            { .binding = 1, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
              .descriptorCount = 1,
              .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT },
        };
        VkDescriptorSetLayoutCreateInfo dslci = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
            .bindingCount = 2, .pBindings = bindings,
        };
        vkCreateDescriptorSetLayout(vk->device, &dslci, NULL, &vk->desc_layout);
    }

    // Pipeline layout
    {
        VkPushConstantRange pcr = {
            .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
            .offset = 0, .size = 32,
        };
        VkPipelineLayoutCreateInfo plci = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
            .setLayoutCount = 1, .pSetLayouts = &vk->desc_layout,
            .pushConstantRangeCount = 1, .pPushConstantRanges = &pcr,
        };
        vkCreatePipelineLayout(vk->device, &plci, NULL, &vk->pipeline_layout);
    }

    // Static meshes & pipelines
    create_circle_mesh(vk);
    create_lines_mesh(vk);
    create_hud_mesh(vk);
    create_pipelines(vk);

    // Per-frame data buffers
    create_buffer(vk, sizeof(CameraUBO),
                  VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                  &vk->cam_ubo_buf, &vk->cam_ubo_mem);
    create_buffer(vk, VK_MAX_AGENTS * sizeof(AgentInstance),
                  VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                  &vk->agent_buf, &vk->agent_mem);
    create_buffer(vk, (VkDeviceSize)FOOD_SQUARES_WIDTH * FOOD_SQUARES_HEIGHT * sizeof(float),
                  VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                  &vk->food_data_buf, &vk->food_data_mem);

    vkMapMemory(vk->device, vk->cam_ubo_mem, 0, sizeof(CameraUBO), 0, (void**)&vk->mapped_cam);
    vkMapMemory(vk->device, vk->agent_mem, 0, VK_WHOLE_SIZE, 0, (void**)&vk->mapped_agents);
    vkMapMemory(vk->device, vk->food_data_mem, 0, VK_WHOLE_SIZE, 0, (void**)&vk->mapped_food_data);

    // Descriptor pools & sets (agents + food)
    {
        VkDescriptorPoolSize poolSizes[] = {
            { .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,  .descriptorCount = 2 },
            { .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount = 2 },
        };
        VkDescriptorPoolCreateInfo dpci = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
            .flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT,
            .maxSets = 2, .poolSizeCount = 2, .pPoolSizes = poolSizes,
        };
        vkCreateDescriptorPool(vk->device, &dpci, NULL, &vk->desc_pool);

        // Agent descriptor set (camera UBO + agent SSBO)
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
        VkWriteDescriptorSet w0[] = {
            { .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
              .dstSet = vk->desc_set, .dstBinding = 0, .dstArrayElement = 0,
              .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
              .pBufferInfo = &camInfo },
            { .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
              .dstSet = vk->desc_set, .dstBinding = 1, .dstArrayElement = 0,
              .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
              .pBufferInfo = &agentInfo },
        };
        vkUpdateDescriptorSets(vk->device, 2, w0, 0, NULL);

        // Food descriptor set (camera UBO + food SSBO)
        VkDescriptorSetLayout food_layouts[] = { vk->desc_layout };
        dsai.pSetLayouts = food_layouts;
        vkAllocateDescriptorSets(vk->device, &dsai, &vk->desc_set_food);

        VkDescriptorBufferInfo foodInfo = {
            .buffer = vk->food_data_buf, .offset = 0, .range = VK_WHOLE_SIZE,
        };
        VkWriteDescriptorSet w1[] = {
            { .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
              .dstSet = vk->desc_set_food, .dstBinding = 0, .dstArrayElement = 0,
              .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
              .pBufferInfo = &camInfo },
            { .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
              .dstSet = vk->desc_set_food, .dstBinding = 1, .dstArrayElement = 0,
              .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
              .pBufferInfo = &foodInfo },
        };
        vkUpdateDescriptorSets(vk->device, 2, w1, 0, NULL);
    }

    // Frame synchronisation
    {
        VkSemaphoreCreateInfo sci = { .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
        VkFenceCreateInfo fci = { .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
                                   .flags = VK_FENCE_CREATE_SIGNALED_BIT };
        for (int i = 0; i < VK_MAX_FRAMES_IN_FLIGHT; i++) {
            vkCreateSemaphore(vk->device, &sci, NULL, &vk->image_avail[i]);
            vkCreateFence(vk->device, &fci, NULL, &vk->in_flight[i]);
        }
        vk->render_done = malloc(sizeof(VkSemaphore) * vk->sc_count);
        vk->render_done_count = vk->sc_count;
        for (uint32_t i = 0; i < vk->sc_count; i++)
            vkCreateSemaphore(vk->device, &sci, NULL, &vk->render_done[i]);
        vk->current_frame = 0;
    }

    vkbrain_init(vk, NUMBOTS);
    printf("[Vulkan] Initialized successfully.\n");
    return vk;
}

void vkinit_destroy(VKState *vk) {
    vkDeviceWaitIdle(vk->device);
    vkbrain_destroy(vk);
    vkUnmapMemory(vk->device, vk->food_data_mem);
    vkUnmapMemory(vk->device, vk->agent_mem);
    vkUnmapMemory(vk->device, vk->cam_ubo_mem);
    for (int i = 0; i < VK_MAX_FRAMES_IN_FLIGHT; i++) {
        vkDestroySemaphore(vk->device, vk->image_avail[i], NULL);
        vkDestroyFence(vk->device, vk->in_flight[i], NULL);
    }
    vkDestroyDescriptorPool(vk->device, vk->desc_pool, NULL);
    vkDestroyDescriptorSetLayout(vk->device, vk->desc_layout, NULL);

    vkDestroyBuffer(vk->device, vk->food_data_buf, NULL);
    vkFreeMemory(vk->device, vk->food_data_mem, NULL);
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

    vkswap_destroy(vk);
    vkDestroyRenderPass(vk->device, vk->render_pass, NULL);
    vkDestroyCommandPool(vk->device, vk->cmd_pool_gfx, NULL);
    vkDestroyCommandPool(vk->device, vk->cmd_pool_compute, NULL);
    vkDestroyCommandPool(vk->device, vk->cmd_pool_transfer, NULL);
    vkDestroyDevice(vk->device, NULL);
    vkDestroySurfaceKHR(vk->instance, vk->surface, NULL);

    PFN_vkDestroyDebugUtilsMessengerEXT fn = (PFN_vkDestroyDebugUtilsMessengerEXT)
        vkGetInstanceProcAddr(vk->instance, "vkDestroyDebugUtilsMessengerEXT");
    if (fn) fn(vk->instance, vk->debug_messenger, NULL);
    vkDestroyInstance(vk->instance, NULL);
    free(vk);
}

// Getters
VkRenderPass  vkinit_get_render_pass(VKState *vk)  { return vk->render_pass; }
VkDevice      vkinit_get_device(VKState *vk)        { return vk->device; }
VkPhysicalDevice vkinit_get_phys_device(VKState *vk){ return vk->phys_device; }
uint32_t      vkinit_get_gfx_family(VKState *vk)  { return vk->gfx_family; }
VkQueue       vkinit_get_gfx_queue(VKState *vk)   { return vk->gfx_queue; }
VkCommandBuffer vkinit_get_command_buffer(VKState *vk) { return vk->cmd_buf[0]; }
uint32_t      vkinit_get_command_buffer_count(VKState *vk) { return 2; }
VkInstance    vkinit_get_instance(VKState *vk)       { return vk->instance; }
