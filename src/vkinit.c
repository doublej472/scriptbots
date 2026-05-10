// vkinit.c — Vulkan instance, device, pipelines, buffers (one-time setup)
#include "settings.h"
#include "vkhelpers.h"
#include "vkswap.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

// Helper: check if extension is in the available list
static int ext_available(const VkExtensionProperties *props, uint32_t count, const char *name) {
  for (uint32_t i = 0; i < count; i++)
    if (strcmp(props[i].extensionName, name) == 0)
      return 1;
  return 0;
}

// ---- Debug messenger ----
static VKAPI_ATTR VkBool32 VKAPI_CALL debug_callback(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                                                     VkDebugUtilsMessageTypeFlagsEXT type,
                                                     const VkDebugUtilsMessengerCallbackDataEXT *data, void *user) {
  (void)severity;
  (void)type;
  (void)user;
  fprintf(stderr, "[Vulkan] %s\n", data->pMessage);
  return VK_FALSE;
}

static VkDebugUtilsMessengerCreateInfoEXT make_debug_info(void) {
  return (VkDebugUtilsMessengerCreateInfoEXT){
      .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
      .messageSeverity =
          VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
      .messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                     VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
      .pfnUserCallback = debug_callback,
  };
}

// ---- Static mesh helpers ----
static void create_circle_mesh(VKState *vk) {
#define CIRCLE_SEGMENTS 32
  float verts[(CIRCLE_SEGMENTS + 2) * 2];
  verts[0] = 0.0f;
  verts[1] = 0.0f;
  for (int i = 0; i <= CIRCLE_SEGMENTS; i++) {
    float a = 2.0f * (float)M_PI * i / CIRCLE_SEGMENTS;
    verts[2 + i * 2] = cosf(a);
    verts[2 + i * 2 + 1] = sinf(a);
  }
  vk->mesh_circle_verts = CIRCLE_SEGMENTS + 2;
  vk->mesh_circle = vkm_buffer_create_staged(vk, verts, sizeof(verts),
      VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
      vk->transfer_family, vk->gfx_family);
}

static void create_lines_mesh(VKState *vk) {
  float p8 = (float)M_PI / 8.0f;
  float c2 = cosf(2.0f * p8), s2 = sinf(2.0f * p8);
  float c1 = cosf(1.0f * p8), s1 = sinf(1.0f * p8);
  float verts[] = {
      0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, c2,   s2,   0.0f, 0.0f, 0.0f, 0.0f,
      c1,   s1,   0.0f, 0.0f, 0.0f, 0.0f, c1,   -s1,  0.0f, 0.0f, 0.0f, 0.0f, c2,   -s2,  0.0f,
  };
  vk->mesh_lines_verts = 10;
  vk->mesh_lines = vkm_buffer_create_staged(vk, verts, sizeof(verts),
      VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
      vk->transfer_family, vk->gfx_family);
}

static void create_hud_mesh(VKState *vk) {
  typedef struct { float x, y; uint32_t t; } HudVertex;
  HudVertex quads[4 * 4];
  for (int t = 0; t < 4; t++) {
    int b = t * 4;
    quads[b] = (HudVertex){0.0f, 0.0f, (uint32_t)t};
    quads[b + 1] = (HudVertex){1.0f, 0.0f, (uint32_t)t};
    quads[b + 2] = (HudVertex){0.0f, 1.0f, (uint32_t)t};
    quads[b + 3] = (HudVertex){1.0f, 1.0f, (uint32_t)t};
  }
  vk->mesh_hud = vkm_buffer_create_staged(vk, quads, sizeof(quads),
      VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
      vk->transfer_family, vk->gfx_family);
}

// ---- Create graphics pipelines ----
//
// Loads shader modules, builds vertex-input descriptions for each mesh
// type, creates four pipelines (circle, lines, HUD, food), then destroys
// the shader modules.
static void create_pipelines(VKState *vk) {
  VkShaderModule circleVert = vkm_shader_load(vk->device, "shaders/agent_circle.vert.spv");
  VkShaderModule circleFrag = vkm_shader_load(vk->device, "shaders/agent_circle.frag.spv");
  VkShaderModule linesVert = vkm_shader_load(vk->device, "shaders/agent_lines.vert.spv");
  VkShaderModule linesFrag = vkm_shader_load(vk->device, "shaders/agent_lines.frag.spv");
  VkShaderModule hudVert = vkm_shader_load(vk->device, "shaders/agent_hud.vert.spv");
  VkShaderModule hudFrag = vkm_shader_load(vk->device, "shaders/agent_hud.frag.spv");
  VkShaderModule foodVert = vkm_shader_load(vk->device, "shaders/food.vert.spv");
  VkShaderModule foodFrag = vkm_shader_load(vk->device, "shaders/food.frag.spv");

  VkVertexInputBindingDescription circleBindings[] = {
      {.binding = 0, .stride = 2 * sizeof(float), .inputRate = VK_VERTEX_INPUT_RATE_VERTEX},
  };
  VkVertexInputAttributeDescription circleAttrs[] = {
      {.location = 0, .binding = 0, .format = VK_FORMAT_R32G32_SFLOAT, .offset = 0},
  };
  VkPipelineVertexInputStateCreateInfo circleVI = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
      .vertexBindingDescriptionCount = 1,
      .pVertexBindingDescriptions = circleBindings,
      .vertexAttributeDescriptionCount = 1,
      .pVertexAttributeDescriptions = circleAttrs,
  };

  VkVertexInputBindingDescription lineBindings[] = {
      {.binding = 0, .stride = 3 * sizeof(float), .inputRate = VK_VERTEX_INPUT_RATE_VERTEX},
  };
  VkVertexInputAttributeDescription lineAttrs[] = {
      {.location = 0, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = 0},
  };
  VkPipelineVertexInputStateCreateInfo lineVI = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
      .vertexBindingDescriptionCount = 1,
      .pVertexBindingDescriptions = lineBindings,
      .vertexAttributeDescriptionCount = 1,
      .pVertexAttributeDescriptions = lineAttrs,
  };

  VkVertexInputBindingDescription hudBindings[] = {
      {.binding = 0, .stride = 2 * sizeof(float) + sizeof(uint32_t), .inputRate = VK_VERTEX_INPUT_RATE_VERTEX},
  };
  VkVertexInputAttributeDescription hudAttrs[] = {
      {.location = 0, .binding = 0, .format = VK_FORMAT_R32G32_SFLOAT, .offset = 0},
      {.location = 1, .binding = 0, .format = VK_FORMAT_R32_UINT, .offset = 2 * sizeof(float)},
  };
  VkPipelineVertexInputStateCreateInfo hudVI = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
      .vertexBindingDescriptionCount = 1,
      .pVertexBindingDescriptions = hudBindings,
      .vertexAttributeDescriptionCount = 2,
      .pVertexAttributeDescriptions = hudAttrs,
  };

  VkPipelineVertexInputStateCreateInfo nullVI = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
      .vertexBindingDescriptionCount = 0,
      .vertexAttributeDescriptionCount = 0,
  };

  vk->pipe_circle = vkm_pipeline_graphics_create(vk->device, vk->pipeline_layout,
      vk->render_pass, circleVert, circleFrag, &circleVI, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN);
  vk->pipe_lines = vkm_pipeline_graphics_create(vk->device, vk->pipeline_layout,
      vk->render_pass, linesVert, linesFrag, &lineVI, VK_PRIMITIVE_TOPOLOGY_LINE_LIST);
  vk->pipe_hud = vkm_pipeline_graphics_create(vk->device, vk->pipeline_layout,
      vk->render_pass, hudVert, hudFrag, &hudVI, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP);
  vk->pipe_food = vkm_pipeline_graphics_create(vk->device, vk->pipeline_layout,
      vk->render_pass, foodVert, foodFrag, &nullVI, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);

  vkm_shader_destroy(vk->device, circleVert);
  vkm_shader_destroy(vk->device, circleFrag);
  vkm_shader_destroy(vk->device, linesVert);
  vkm_shader_destroy(vk->device, linesFrag);
  vkm_shader_destroy(vk->device, hudVert);
  vkm_shader_destroy(vk->device, hudFrag);
  vkm_shader_destroy(vk->device, foodVert);
  vkm_shader_destroy(vk->device, foodFrag);
}

// ---- Windowed-mode graphics setup (swapchain, render pass, meshes, pipelines, descriptors) ----
//
// Creates all resources needed for the windowed rendering path:
//   - Three command pools (gfx, compute, transfer) + gfx command buffers
//   - Swapchain + render pass + framebuffers
//   - Descriptor set layout + pipeline layout (cam UBO + agent SSBO)
//   - Three static meshes (circle, lines, HUD)
//   - Four graphics pipelines (circle, lines, HUD, food)
//   - Per-frame host-visible buffers (cam UBO, agent SSBO, food SSBO)
//   - Descriptor pool + two descriptor sets (agent + food)
//   - Frame synchronisation primitives (image_avail semaphores, in_flight fences,
//     render_done semaphores)
//
// Preconditions: device, queue families, surface, timestamp_pool must be valid.
static void setup_graphics(VKState *vk) {
  // Command pools
  vk->cmd_pool_gfx = vkm_cmdpool_create(vk->device, vk->gfx_family);
  vk->cmd_pool_compute = vkm_cmdpool_create(vk->device, vk->compute_family);
  vk->cmd_pool_transfer = vkm_cmdpool_create(vk->device, vk->transfer_family);
  vkm_cmdpool_alloc_buffers(vk->device, &vk->cmd_pool_gfx, VK_MAX_FRAMES_IN_FLIGHT, vk->cmd_buf);

  if (vk->timestamp_supported) {
    VkCommandBuffer tsCmd;
    VkCommandBufferAllocateInfo ts_cbai = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = vk->cmd_pool_gfx.pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };
    vkAllocateCommandBuffers(vk->device, &ts_cbai, &tsCmd);
    VkCommandBufferBeginInfo ts_bi = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    vkBeginCommandBuffer(tsCmd, &ts_bi);
    vkCmdResetQueryPool(tsCmd, vk->timestamp_pool, 0, 8);
    vkEndCommandBuffer(tsCmd);
    VkSubmitInfo ts_si = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1, .pCommandBuffers = &tsCmd};
    vkQueueSubmit(vk->gfx_queue, 1, &ts_si, VK_NULL_HANDLE);
    vkQueueWaitIdle(vk->gfx_queue);
    vkFreeCommandBuffers(vk->device, vk->cmd_pool_gfx.pool, 1, &tsCmd);
  }

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
    VkAttachmentReference colorRef = {.attachment = 0, .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription subpass = {
        .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .colorAttachmentCount = 1,
        .pColorAttachments = &colorRef,
    };
    VkRenderPassCreateInfo rpci = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .attachmentCount = 1,
        .pAttachments = &colorAtt,
        .subpassCount = 1,
        .pSubpasses = &subpass,
    };
    if (vkCreateRenderPass(vk->device, &rpci, NULL, &vk->render_pass) != VK_SUCCESS) {
      fprintf(stderr, "FATAL: render pass creation failed\n");
      exit(1);
    }
  }
  vkswap_build_resources(vk);

  // Descriptor set layout + pipeline layout
  {
    VkDescriptorSetLayoutBinding bindings[] = {
        {.binding = 0, .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
         .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_VERTEX_BIT},
        {.binding = 1, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
         .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT},
    };
    vk->desc_layout = vkm_desclayout_create(vk->device, 2, bindings);
  }
  vk->pipeline_layout = vkm_pipeline_layout_create(vk->device, 1, &vk->desc_layout, 32,
                                                   VK_SHADER_STAGE_VERTEX_BIT);

  // Static meshes & pipelines
  create_circle_mesh(vk);
  create_lines_mesh(vk);
  create_hud_mesh(vk);
  create_pipelines(vk);

  // Per-frame buffers
  vk->cam_ubo = vkm_buffer_create(vk, sizeof(CameraUBO), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

  vk->agent_capacity = (uint32_t)NUMBOTS;
  if (vk->agent_capacity < 65536)
    vk->agent_capacity = 65536;
  vk->agent_buf = vkm_buffer_create(vk, vk->agent_capacity * sizeof(AgentInstance),
      VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
  vk->food_buf = vkm_buffer_create(vk, (VkDeviceSize)FOOD_SQUARES_WIDTH * FOOD_SQUARES_HEIGHT * sizeof(float),
      VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

  // Descriptor sets
  {
    VkDescriptorPoolSize poolSizes[] = {
        {.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, .descriptorCount = 2},
        {.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount = 2},
    };
    vk->desc_pool = vkm_descpool_create(vk->device, 2, poolSizes, 2, true);

    VkDescriptorSetLayout agent_layouts[] = {vk->desc_layout};
    vkm_descset_alloc(vk->device, &vk->desc_pool, 1, agent_layouts, &vk->desc_set);
    vkm_descset_write_buffer(vk->device, vk->desc_set, 0,
        VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, &vk->cam_ubo, sizeof(CameraUBO));
    vkm_descset_write_buffer(vk->device, vk->desc_set, 1,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &vk->agent_buf, VK_WHOLE_SIZE);

    VkDescriptorSetLayout food_layouts[] = {vk->desc_layout};
    vkm_descset_alloc(vk->device, &vk->desc_pool, 1, food_layouts, &vk->desc_set_food);
    vkm_descset_write_buffer(vk->device, vk->desc_set_food, 0,
        VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, &vk->cam_ubo, sizeof(CameraUBO));
    vkm_descset_write_buffer(vk->device, vk->desc_set_food, 1,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &vk->food_buf, VK_WHOLE_SIZE);
  }

  // Frame synchronisation
  for (int i = 0; i < VK_MAX_FRAMES_IN_FLIGHT; i++) {
    vk->image_avail[i] = vkm_semaphore_create(vk->device);
    vk->in_flight[i] = vkm_fence_create(vk->device, true);
  }
  vk->render_done = malloc(sizeof(VKM_Semaphore) * vk->sc_count);
  vk->render_done_count = vk->sc_count;
  for (uint32_t i = 0; i < vk->sc_count; i++)
    vk->render_done[i] = vkm_semaphore_create(vk->device);
  vk->current_frame = 0;
}

// ============================================================================
// Unified instance + device creation (windowed and headless)
// ============================================================================
// Creates VkInstance, picks physical device, creates VkDevice with Vulkan 1.2
// features (timeline semaphore, buffer device address, subgroup broadcast).
// When headless=true: no surface, no gfx queue family, no swapchain extension,
//                     no timestamp query pool.
// Returns true on success, false on failure (caller must free vk).
static bool init_instance_device(VKState *vk, bool headless) {
  // --- Instance ---
  {
    uint32_t availExtCount = 0;
    vkEnumerateInstanceExtensionProperties(NULL, &availExtCount, NULL);
    VkExtensionProperties *availExts = malloc(sizeof(VkExtensionProperties) * availExtCount);
    vkEnumerateInstanceExtensionProperties(NULL, &availExtCount, availExts);

    int haveDebugUtils = ext_available(availExts, availExtCount, VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    VkDebugUtilsMessengerCreateInfoEXT dbgInfo = make_debug_info();

    if (headless) {
      // Headless: no surface extensions needed
      const char *exts[1];
      uint32_t extCount = 0;
      if (haveDebugUtils)
        exts[extCount++] = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;

      VkApplicationInfo appInfo = {
          .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
          .pApplicationName = "ScriptBots-Headless",
          .applicationVersion = VK_MAKE_VERSION(1, 0, 0),
          .pEngineName = "No Engine", .engineVersion = VK_MAKE_VERSION(1, 0, 0),
          .apiVersion = VK_API_VERSION_1_2,
      };
      const char *layers[] = {"VK_LAYER_KHRONOS_validation"};
      VkInstanceCreateInfo ici = {
          .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
          .pApplicationInfo = &appInfo,
          .enabledExtensionCount = extCount,
          .ppEnabledExtensionNames = exts,
          .enabledLayerCount = 1, .ppEnabledLayerNames = layers,
          .pNext = haveDebugUtils ? &dbgInfo : NULL,
      };
      VkResult res = vkCreateInstance(&ici, NULL, &vk->instance);
      if (res != VK_SUCCESS) {
        fprintf(stderr, "[Headless] instance creation failed, retrying without validation...\n");
        ici.enabledLayerCount = 0;
        ici.pNext = NULL;
        ici.enabledExtensionCount = extCount;
        res = vkCreateInstance(&ici, NULL, &vk->instance);
        if (res != VK_SUCCESS) {
          fprintf(stderr, "FATAL: Vulkan instance creation failed (VkResult=%d).\n", res);
          free(availExts);
          return false;
        }
      }
      if (haveDebugUtils) {
        PFN_vkCreateDebugUtilsMessengerEXT fn =
            (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(vk->instance, "vkCreateDebugUtilsMessengerEXT");
        if (fn) fn(vk->instance, &dbgInfo, NULL, &vk->debug_messenger);
      }
    } else {
      // Windowed: surface extensions via GLFW
      uint32_t glfwExtCount = 0;
      const char **glfwExts = glfwGetRequiredInstanceExtensions(&glfwExtCount);
      if (glfwExtCount == 0) {
        fprintf(stderr, "FATAL: GLFW returned 0 required instance extensions\n");
        free(availExts);
        return false;
      }

      VkApplicationInfo appInfo = {
          .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
          .pApplicationName = "ScriptBots",
          .applicationVersion = VK_MAKE_VERSION(1, 0, 0),
          .pEngineName = "No Engine", .engineVersion = VK_MAKE_VERSION(1, 0, 0),
          .apiVersion = VK_API_VERSION_1_2,
      };
      const char *layers[] = {"VK_LAYER_KHRONOS_validation"};

      uint32_t extCount = glfwExtCount + (haveDebugUtils ? 1 : 0);
      const char **exts = malloc(sizeof(const char *) * extCount);
      for (uint32_t i = 0; i < glfwExtCount; i++) exts[i] = glfwExts[i];
      if (haveDebugUtils) exts[glfwExtCount] = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;

      VkInstanceCreateInfo ici = {
          .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
          .pApplicationInfo = &appInfo,
          .enabledExtensionCount = extCount, .ppEnabledExtensionNames = exts,
          .enabledLayerCount = 1, .ppEnabledLayerNames = layers,
          .pNext = haveDebugUtils ? &dbgInfo : NULL,
      };
      VkResult res = vkCreateInstance(&ici, NULL, &vk->instance);
      if (res != VK_SUCCESS) {
        fprintf(stderr, "Vulkan instance creation failed, retrying without validation...\n");
        ici.enabledLayerCount = 0; ici.pNext = NULL; ici.enabledExtensionCount = glfwExtCount;
        res = vkCreateInstance(&ici, NULL, &vk->instance);
        if (res != VK_SUCCESS) {
          fprintf(stderr, "FATAL: Vulkan instance creation failed (VkResult=%d).\n", res);
          free(exts); free(availExts);
          return false;
        }
      }
      if (haveDebugUtils) {
        PFN_vkCreateDebugUtilsMessengerEXT fn =
            (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(vk->instance, "vkCreateDebugUtilsMessengerEXT");
        if (fn) fn(vk->instance, &dbgInfo, NULL, &vk->debug_messenger);
      }
      free(exts);
    }
    free(availExts);
  }

  // --- Surface (windowed only) ---
  if (!headless) {
    VkResult surfRes = glfwCreateWindowSurface(vk->instance, vk->window, NULL, &vk->surface);
    if (surfRes != VK_SUCCESS) {
      fprintf(stderr, "FATAL: surface creation failed (VkResult=%d)\n", surfRes);
      exit(1);
    }
  }

  // --- Pick physical device ---
  // Prefer discrete GPUs with dedicated queue families for compute and transfer.
  {
    uint32_t devCount;
    vkEnumeratePhysicalDevices(vk->instance, &devCount, NULL);
    if (devCount == 0) {
      fprintf(stderr, "FATAL: No Vulkan physical devices found\n");
      if (headless) return false;
      exit(1);
    }
    VkPhysicalDevice *devs = malloc(sizeof(VkPhysicalDevice) * devCount);
    vkEnumeratePhysicalDevices(vk->instance, &devCount, devs);
    vk->phys_device = VK_NULL_HANDLE;
    vk->gfx_family = vk->compute_family = vk->transfer_family = UINT32_MAX;
    int bestScore = -1;

    for (uint32_t i = 0; i < devCount; i++) {
      VkPhysicalDeviceProperties props;
      vkGetPhysicalDeviceProperties(devs[i], &props);

      int score = 0;
      if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)       score = 1000;
      else if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU) score = 500;
      else if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU)    score = 100;
      else continue;

      uint32_t qfCount;
      vkGetPhysicalDeviceQueueFamilyProperties(devs[i], &qfCount, NULL);
      VkQueueFamilyProperties *qfs = malloc(sizeof(VkQueueFamilyProperties) * qfCount);
      vkGetPhysicalDeviceQueueFamilyProperties(devs[i], &qfCount, qfs);

      uint32_t best_gfx = UINT32_MAX, best_comp = UINT32_MAX, best_xfer = UINT32_MAX;
      int gfx_cost = 999, comp_cost = 999, xfer_cost = 999;

      for (uint32_t j = 0; j < qfCount; j++) {
        VkQueueFlags f = qfs[j].queueFlags;
        int cap_count = __builtin_popcount((unsigned int)f);

        if (!headless) {
          VkBool32 can_present = VK_FALSE;
          vkGetPhysicalDeviceSurfaceSupportKHR(devs[i], j, vk->surface, &can_present);
          if ((f & VK_QUEUE_GRAPHICS_BIT) && can_present && cap_count < gfx_cost) {
            gfx_cost = cap_count; best_gfx = j;
          }
        }
        if (f & VK_QUEUE_COMPUTE_BIT) {
          int cost = cap_count + ((f & VK_QUEUE_GRAPHICS_BIT) ? 8 : 0);
          if (cost < comp_cost) { comp_cost = cost; best_comp = j; }
        }
        if (f & VK_QUEUE_TRANSFER_BIT) {
          int cost = cap_count + ((f & VK_QUEUE_GRAPHICS_BIT) ? 8 : 0) + ((f & VK_QUEUE_COMPUTE_BIT) ? 4 : 0);
          if (cost < xfer_cost) { xfer_cost = cost; best_xfer = j; }
        }
      }
      free(qfs);

      if (headless && best_comp == UINT32_MAX) continue;
      if (!headless && best_gfx == UINT32_MAX) continue;

      uint32_t gfx  = headless ? 0 : best_gfx;
      uint32_t comp = best_comp != UINT32_MAX ? best_comp : gfx;
      uint32_t xfer = best_xfer != UINT32_MAX ? best_xfer : comp;

      if (!headless && comp != gfx) score += 10;
      if (xfer != comp && (!headless || xfer != gfx)) score += 20;

      if (score > bestScore) {
        bestScore = score;
        vk->phys_device = devs[i];
        if (!headless) vk->gfx_family = gfx;
        vk->compute_family = comp;
        vk->transfer_family = xfer;
      }
    }
    free(devs);

    if (vk->phys_device == VK_NULL_HANDLE) {
      fprintf(stderr, "FATAL: No suitable Vulkan device found.\n");
      if (headless) return false;
      exit(1);
    }

    VkPhysicalDeviceProperties chosenProps;
    vkGetPhysicalDeviceProperties(vk->phys_device, &chosenProps);
    vk->timestamp_period = chosenProps.limits.timestampPeriod;
    printf("[%s] %s | score=%d | %scompute=%u transfer=%u\n",
           headless ? "Headless" : "Vulkan", chosenProps.deviceName, bestScore,
           headless ? "" : "gfx=0 ", vk->compute_family, vk->transfer_family);
  }

  // --- Create device ---
  {
    float prio = 1.0f;
    uint32_t fams[3];
    int fc = 0;
    if (!headless) fams[fc++] = vk->gfx_family;
    if (vk->compute_family != fams[0]) fams[fc++] = vk->compute_family;
    if (vk->transfer_family != fams[0] && (fc < 2 || vk->transfer_family != fams[1]))
      fams[fc++] = vk->transfer_family;

    VkDeviceQueueCreateInfo qcis[3];
    for (int i = 0; i < fc; i++)
      qcis[i] = (VkDeviceQueueCreateInfo){
          .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
          .queueFamilyIndex = fams[i], .queueCount = 1, .pQueuePriorities = &prio,
      };

    VkPhysicalDeviceVulkan12Features vk12features = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
        .timelineSemaphore  = VK_TRUE,
        .bufferDeviceAddress = VK_TRUE,
        .subgroupBroadcastDynamicId = VK_TRUE,
    };
    VkPhysicalDeviceFeatures features = {0};  // Vulkan 1.0 features — all false;
                                               // Vulkan 1.2 features (timelineSemaphore,
                                               // bufferDeviceAddress, etc.) are enabled
                                               // in the pNext chain above via
                                               // VkPhysicalDeviceVulkan12Features.
    const char *devExts[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    VkDeviceCreateInfo dci = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext = &vk12features,
        .queueCreateInfoCount = (uint32_t)fc,
        .pQueueCreateInfos = qcis,
        .enabledExtensionCount = headless ? 0u : 1u,
        .ppEnabledExtensionNames = headless ? NULL : devExts,
        .pEnabledFeatures = &features,
    };
    if (vkCreateDevice(vk->phys_device, &dci, NULL, &vk->device) != VK_SUCCESS) {
      fprintf(stderr, "FATAL: device creation failed\n");
      if (headless) return false;
      exit(1);
    }
    if (!headless) vkGetDeviceQueue(vk->device, vk->gfx_family, 0, &vk->gfx_queue);
    vkGetDeviceQueue(vk->device, vk->compute_family, 0, &vk->compute_queue);
    vkGetDeviceQueue(vk->device, vk->transfer_family, 0, &vk->transfer_queue);

    // Timeline semaphore for compute→graphics/CPU synchronization
    {
      VkSemaphoreTypeCreateInfo stci = {
          .sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO,
          .semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE,
          .initialValue = 0,
      };
      VkSemaphoreCreateInfo sci = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, .pNext = &stci};
      vkCreateSemaphore(vk->device, &sci, NULL, &vk->compute_timeline);
      vk->compute_timeline_value = 1;
    }

    // Timestamp queries (windowed only)
    if (!headless) {
      vk->timestamp_pool = vkm_querypool_create(vk->device, VK_QUERY_TYPE_TIMESTAMP, 8);
      vk->timestamp_supported = (vk->timestamp_pool != VK_NULL_HANDLE);
    }
  }
  return true;
}

// ---- Public API ----
VKState *vkinit_create(GLFWwindow *window) {
  VKState *vk = calloc(1, sizeof(VKState));
  vk->window = window;

  if (!init_instance_device(vk, false)) {
    free(vk);
    return NULL;
  }

  setup_graphics(vk);
  vkbrain_init(vk, NUMBOTS);
  printf("[Vulkan] Initialized successfully.\n");
  return vk;
}

VKState *vkinit_create_headless(void) {
  VKState *vk = calloc(1, sizeof(VKState));

  if (!init_instance_device(vk, true)) {
    vkinit_destroy_headless(vk);
    return NULL;
  }

  vk->cmd_pool_compute = vkm_cmdpool_create(vk->device, vk->compute_family);
  vk->cmd_pool_transfer = vkm_cmdpool_create(vk->device, vk->transfer_family);

  vkbrain_init(vk, NUMBOTS);
  printf("[Headless] Vulkan compute initialised successfully.\n");
  return vk;
}

void vkinit_destroy(VKState *vk) {
  if (!vk->device) return;
  vkDeviceWaitIdle(vk->device);
  vkbrain_destroy(vk);
  vkm_querypool_destroy(vk->device, vk->timestamp_pool);

  vkm_buffer_destroy(vk->device, &vk->food_buf);
  vkm_buffer_destroy(vk->device, &vk->agent_buf);
  vkm_buffer_destroy(vk->device, &vk->cam_ubo);

  for (int i = 0; i < VK_MAX_FRAMES_IN_FLIGHT; i++) {
    vkm_semaphore_destroy(vk->device, &vk->image_avail[i]);
    vkm_fence_destroy(vk->device, &vk->in_flight[i]);
  }
  vkm_descpool_destroy(vk->device, &vk->desc_pool);
  vkm_desclayout_destroy(vk->device, vk->desc_layout);

  vkm_buffer_destroy(vk->device, &vk->mesh_hud);
  vkm_buffer_destroy(vk->device, &vk->mesh_lines);
  vkm_buffer_destroy(vk->device, &vk->mesh_circle);

  vkm_pipeline_destroy(vk->device, &vk->pipe_food);
  vkm_pipeline_destroy(vk->device, &vk->pipe_hud);
  vkm_pipeline_destroy(vk->device, &vk->pipe_lines);
  vkm_pipeline_destroy(vk->device, &vk->pipe_circle);
  if (vk->pipeline_layout)
    vkDestroyPipelineLayout(vk->device, vk->pipeline_layout, NULL);

  if (vk->compute_timeline)
    vkDestroySemaphore(vk->device, vk->compute_timeline, NULL);

  vkswap_destroy(vk);
  if (vk->render_pass)
    vkDestroyRenderPass(vk->device, vk->render_pass, NULL);
  vkm_cmdpool_destroy(vk->device, &vk->cmd_pool_gfx);
  vkm_cmdpool_destroy(vk->device, &vk->cmd_pool_compute);
  vkm_cmdpool_destroy(vk->device, &vk->cmd_pool_transfer);
  vkDestroyDevice(vk->device, NULL);
  if (vk->surface)
    vkDestroySurfaceKHR(vk->instance, vk->surface, NULL);

  PFN_vkDestroyDebugUtilsMessengerEXT fn =
      (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(vk->instance, "vkDestroyDebugUtilsMessengerEXT");
  if (fn) fn(vk->instance, vk->debug_messenger, NULL);
  vkDestroyInstance(vk->instance, NULL);
  free(vk);
}

void vkinit_destroy_headless(VKState *vk) {
  if (vk->device) {
    vkDeviceWaitIdle(vk->device);
    vkbrain_destroy(vk);
    if (vk->compute_timeline)
      vkDestroySemaphore(vk->device, vk->compute_timeline, NULL);
    vkm_cmdpool_destroy(vk->device, &vk->cmd_pool_compute);
    vkm_cmdpool_destroy(vk->device, &vk->cmd_pool_transfer);
    vkDestroyDevice(vk->device, NULL);
  }
  if (vk->debug_messenger) {
    PFN_vkDestroyDebugUtilsMessengerEXT fn =
        (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(vk->instance, "vkDestroyDebugUtilsMessengerEXT");
    if (fn) fn(vk->instance, vk->debug_messenger, NULL);
  }
  if (vk->instance)
    vkDestroyInstance(vk->instance, NULL);
  free(vk);
}

// Getters
VkRenderPass vkinit_get_render_pass(VKState *vk) { return vk->render_pass; }
VkDevice vkinit_get_device(VKState *vk) { return vk->device; }
VkPhysicalDevice vkinit_get_phys_device(VKState *vk) { return vk->phys_device; }
uint32_t vkinit_get_gfx_family(VKState *vk) { return vk->gfx_family; }
VkQueue vkinit_get_gfx_queue(VKState *vk) { return vk->gfx_queue; }
VkCommandBuffer vkinit_get_command_buffer(VKState *vk) { return vk->cmd_buf[0]; }
VkInstance vkinit_get_instance(VKState *vk) { return vk->instance; }

// ---- Dynamic agent SSBO resize ----
void vkdraw_resize_agents(VKState *vk, uint32_t new_capacity) {
  if (new_capacity <= vk->agent_capacity)
    return;
  vkDeviceWaitIdle(vk->device);

  vk->agent_capacity = new_capacity;
  vkm_buffer_resize(vk, &vk->agent_buf,
                    new_capacity * sizeof(AgentInstance),
                    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);

  vkm_descset_write_buffer(vk->device, vk->desc_set, 1,
      VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &vk->agent_buf, VK_WHOLE_SIZE);

  printf("[VKDraw] Resized agent SSBO to %u agents (%.1f MB)\n", new_capacity,
         (double)(new_capacity * sizeof(AgentInstance)) / (1024 * 1024));
}
