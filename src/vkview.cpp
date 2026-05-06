// vkview.c — Window, input, main loop, ImGui HUD (replaces GLView.c)
#include "vkview.h"
#include "Agent.h"
#include "World.h"
#include "helpers.h"
#include "settings.h"
#include "vkhelpers.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

// ImGui
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_vulkan.h"
#include <stdio.h>

static void char_callback(GLFWwindow *w, unsigned int codepoint);

static void render_imgui_to_cmd(VkCommandBuffer cmd, void *user_data) {
  (void)user_data;
  ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);
}

static VKViewState build_view_state(struct VKState *vk) {
  uint32_t w, h;
  vkinit_get_extent(vk, &w, &h);
  return (VKViewState){
      .wwidth = (int)w,
      .wheight = (int)h,
      .scalemult = VKVIEW.scalemult,
      .xtranslate = VKVIEW.xtranslate,
      .ytranslate = VKVIEW.ytranslate,
      .drawfood = VKVIEW.drawfood,
      .base = VKVIEW.base,
  };
}

// GLFW error callback for diagnostics
static void glfw_error_callback(int err, const char *desc) { fprintf(stderr, "[GLFW] Error %d: %s\n", err, desc); }

// CheckVkResult helper for ImGui
static void check_vk_result(VkResult err) {
  if (err != VK_SUCCESS) {
    fprintf(stderr, "[Vulkan] ImGui error: VkResult = %d\n", (int)err);
  }
}

// ---- Global ----
extern "C" {
VKView VKVIEW;
}

// ---- Forward callbacks ----
static void key_callback(GLFWwindow *w, int key, int scancode, int action, int mods);
static void mouse_button_callback(GLFWwindow *w, int button, int action, int mods);
static void scroll_callback(GLFWwindow *w, double xoff, double yoff);
static void cursor_pos_callback(GLFWwindow *w, double x, double y);

// ---- Init ----
void vkview_init(int argc, char **argv) {
  (void)argc;
  (void)argv;

  memset(&VKVIEW, 0, sizeof(VKVIEW));

  // View state defaults (matches old GLVIEW init)
  VKVIEW.paused = 0;
  VKVIEW.draw = 1;
  VKVIEW.drawfood = 1;
  VKVIEW.draw_text = 1;
  VKVIEW.modcounter = 0;
  VKVIEW.lastUpdate = 0;
  VKVIEW.frames = 0;
  VKVIEW.xtranslate = -(WIDTH / 2.0f);
  VKVIEW.ytranslate = -(HEIGHT / 2.0f);
  VKVIEW.scalemult = 0.4f;
  VKVIEW.downb[0] = VKVIEW.downb[1] = 0;
  VKVIEW.mousex = VKVIEW.mousey = 0;
  VKVIEW.max_fps = 0;
  VKVIEW.show_perf = true;
  VKVIEW.show_sim = true;
  VKVIEW.frame_start = 0.0;
  VKVIEW.wwidth = WWIDTH; // placeholder; overwritten below with actual window size
  VKVIEW.wheight = WHEIGHT;

  // Init GLFW
  glfwSetErrorCallback(glfw_error_callback);
  if (!glfwInit()) {
    fprintf(stderr, "FATAL: glfwInit failed\n");
    exit(1);
  }

  if (!glfwVulkanSupported()) {
    fprintf(stderr, "FATAL: Vulkan is not supported. No Vulkan loader/ICD found.\n");
    fprintf(stderr, "  Install a Vulkan driver (e.g. vulkan-radeon, vulkan-intel, vulkan-swrast)\n");
    exit(1);
  }

  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

  VKVIEW.window = glfwCreateWindow(WWIDTH, WHEIGHT, "ScriptBots", NULL, NULL);
  if (!VKVIEW.window) {
    fprintf(stderr, "FATAL: glfwCreateWindow failed\n");
    exit(1);
  }

  // Set callbacks
  glfwSetKeyCallback(VKVIEW.window, key_callback);
  glfwSetCharCallback(VKVIEW.window, char_callback);
  glfwSetMouseButtonCallback(VKVIEW.window, mouse_button_callback);
  glfwSetScrollCallback(VKVIEW.window, scroll_callback);
  glfwSetCursorPosCallback(VKVIEW.window, cursor_pos_callback);
  // Init Vulkan
  VKVIEW.vkstate = vkinit_create(VKVIEW.window);
  if (!VKVIEW.vkstate) {
    fprintf(stderr, "FATAL: Vulkan init failed\n");
    exit(1);
  }

  // Seed VKVIEW dimensions from the window content area (logical
  // screen coordinates).  glfwGetCursorPos returns coords in this
  // same space, so mouse→world transforms need the window size, not
  // the (potentially HiDPI-scaled) framebuffer size.
  // Rendering dimensions come separately from swapchain extent.
  glfwGetWindowSize(VKVIEW.window, &VKVIEW.wwidth, &VKVIEW.wheight);

  // Framebuffer resize callback — needed because OUT_OF_DATE is not
  // guaranteed by all drivers/platforms after a window resize.
  glfwSetWindowUserPointer(VKVIEW.window, VKVIEW.vkstate);
  glfwSetFramebufferSizeCallback(VKVIEW.window, [](GLFWwindow *w, int, int) {
    auto *vk = static_cast<VKState *>(glfwGetWindowUserPointer(w));
    if (vk)
      vk->framebuffer_resized = 1;
  });

  // Init ImGui
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO &io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
  ImGui::StyleColorsDark();

  ImGui_ImplGlfw_InitForVulkan(VKVIEW.window, false);

  VkPhysicalDevice phys = vkinit_get_phys_device(VKVIEW.vkstate);
  VkDevice dev = vkinit_get_device(VKVIEW.vkstate);
  uint32_t qfam = vkinit_get_gfx_family(VKVIEW.vkstate);
  VkQueue queue = vkinit_get_gfx_queue(VKVIEW.vkstate);
  VkRenderPass rp = vkinit_get_render_pass(VKVIEW.vkstate);

  // ImGui descriptor pool
  VkDescriptorPoolSize pool_sizes[] = {
      {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4},
  };
  VkDescriptorPoolCreateInfo dpci = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
      .flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT,
      .maxSets = 4,
      .poolSizeCount = 1,
      .pPoolSizes = pool_sizes,
  };
  VkDescriptorPool imguiPool;
  vkCreateDescriptorPool(dev, &dpci, NULL, &imguiPool);
  VKVIEW.imgui_descriptor_pool = imguiPool;

  VkInstance instance = vkinit_get_instance(VKVIEW.vkstate);
  VkSwapchainKHR swapchain = vkinit_get_swapchain(VKVIEW.vkstate);

  uint32_t scImageCount;
  vkGetSwapchainImagesKHR(dev, swapchain, &scImageCount, NULL);

  ImGui_ImplVulkan_InitInfo vkInfo = {};
  vkInfo.ApiVersion = VK_API_VERSION_1_2;
  vkInfo.Instance = instance;
  vkInfo.PhysicalDevice = phys;
  vkInfo.Device = dev;
  vkInfo.QueueFamily = qfam;
  vkInfo.Queue = queue;
  vkInfo.PipelineCache = VK_NULL_HANDLE;
  vkInfo.DescriptorPool = imguiPool;
  vkInfo.RenderPass = rp;
  vkInfo.Subpass = 0;
  vkInfo.MinImageCount = scImageCount;
  vkInfo.ImageCount = scImageCount;
  vkInfo.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
  vkInfo.Allocator = NULL;
  vkInfo.CheckVkResultFn = check_vk_result;

  ImGui_ImplVulkan_Init(&vkInfo);
  ImGui_ImplVulkan_CreateFontsTexture();
  ImGui::GetIO().DisplaySize = ImVec2((float)VKVIEW.wwidth, (float)VKVIEW.wheight);

  printf("[VKView] Initialized GLFW+Vulkan+ImGui.\n");
}

// ---- Cleanup ----
void vkview_cleanup(void) {
  VkDevice dev = vkinit_get_device(VKVIEW.vkstate);
  vkDeviceWaitIdle(dev);

  ImGui_ImplVulkan_Shutdown();
  ImGui_ImplGlfw_Shutdown();
  ImGui::DestroyContext();

  vkDestroyDescriptorPool(dev, VKVIEW.imgui_descriptor_pool, NULL);
  vkinit_destroy(VKVIEW.vkstate);
  glfwDestroyWindow(VKVIEW.window);
  glfwTerminate();
}

// ---- Char callback (needed for ImGui text input) ----
static void char_callback(GLFWwindow *w, unsigned int codepoint) {
  (void)w;
  ImGui_ImplGlfw_CharCallback(w, codepoint);
}

// ---- Key callback ----
static void key_callback(GLFWwindow *w, int key, int scancode, int action, int mods) {
  (void)scancode;
  ImGui_ImplGlfw_KeyCallback(w, key, scancode, action, mods);
  if (ImGui::GetIO().WantCaptureKeyboard)
    return;
  if (action != GLFW_PRESS)
    return;

  vkview_process_normal_key(key, mods);
}

void vkview_process_normal_key(int key, int mods) {
  switch (key) {
  case GLFW_KEY_ESCAPE:
    printf("\nESC key pressed, shutting down\n");
    glfwSetWindowShouldClose(VKVIEW.window, 1);
    break;
  case GLFW_KEY_R:
    world_reset(VKVIEW.base->world);
    printf("Agents reset\n");
    break;
  case GLFW_KEY_P:
    VKVIEW.paused = !VKVIEW.paused;
    break;
  case GLFW_KEY_D:
    VKVIEW.draw = !VKVIEW.draw;
    break;
  case GLFW_KEY_F:
    if (mods & GLFW_MOD_CONTROL)
      vkview_toggle_fullscreen();
    else
      VKVIEW.drawfood = !VKVIEW.drawfood;
    break;
  case GLFW_KEY_H:
    world_addRandomBots(VKVIEW.base->world, 100);
    break;
  case GLFW_KEY_Q:
    for (int i = 0; i < 100; i++)
      world_addCarnivore(VKVIEW.base->world);
    break;
  case GLFW_KEY_C:
    VKVIEW.base->world->closed = VKVIEW.base->world->closed ? 0 : 1;
    printf("Environment closed now = %i\n", VKVIEW.base->world->closed);
    break;
  case GLFW_KEY_Z:
    VKVIEW.xtranslate = -(WIDTH / 2.0f);
    VKVIEW.ytranslate = -(HEIGHT / 2.0f);
    VKVIEW.scalemult = 0.4f;
    break;
  case GLFW_KEY_T:
    VKVIEW.draw_text = !VKVIEW.draw_text;
    break;
  case GLFW_KEY_M:
    VKVIEW.base->world->movieMode = !VKVIEW.base->world->movieMode;
    break;
  case GLFW_KEY_L:
    if (mods & GLFW_MOD_CONTROL) {
      if (!base_loadworld(VKVIEW.base))
        break;
      if (VKVIEW.base->world->brain_gpu == NULL && VKVIEW.vkstate) {
        VKVIEW.base->world->brain_gpu = VKVIEW.vkstate;
        VKVIEW.base->world->brain_slot = 0;
        VKState *vk = VKVIEW.vkstate;
        vkDeviceWaitIdle(vk->device);
        vkResetFences(vk->device, 1, &vk->compute_fence);
        vkbrain_reset_counts(vk);
        // Reassign GPU brain slots for loaded agents
        size_t total = VKVIEW.base->world->agents.size;
        for (size_t i = 0; i < total; i++) {
          struct Agent *a = VKVIEW.base->world->agents.agents[i];
          a->brain_chunk = ~0u;
          if (!vkbrain_assign_slot(vk, a, &a->brain_chunk, &a->brain_index)) {
            fprintf(stderr, "GPU memory exhausted loading agent %zu\n", i);
            break;
          }
        }
        vkbrain_upload_all(vk, VKVIEW.base->world);
        vkbrain_try_reclaim_last(vk);
        // Seed both input buffer slots so first dispatch has valid data
        world_seed_inputs(VKVIEW.base->world);
        vkbrain_record_dispatch(vk, 0);
        printf("Re-uploaded brains to GPU after load (%zu agents).\n", total);
      }
    }
    break;
  case GLFW_KEY_S:
    if (mods & GLFW_MOD_CONTROL)
      base_saveworld(VKVIEW.base);
    break;
  case GLFW_KEY_PAGE_UP:
    VKVIEW.scalemult *= 1.05f;
    break;
  case GLFW_KEY_PAGE_DOWN:
    VKVIEW.scalemult *= 0.95f;
    if (VKVIEW.scalemult < 0.01f)
      VKVIEW.scalemult = 0.01f;
    break;
  case GLFW_KEY_UP:
    VKVIEW.ytranslate += 5.0f / VKVIEW.scalemult;
    break;
  case GLFW_KEY_DOWN:
    VKVIEW.ytranslate -= 5.0f / VKVIEW.scalemult;
    break;
  case GLFW_KEY_RIGHT:
    VKVIEW.xtranslate -= 5.0f / VKVIEW.scalemult;
    break;
  case GLFW_KEY_LEFT:
    VKVIEW.xtranslate += 5.0f / VKVIEW.scalemult;
    break;
  default:
    break;
  }
}

// ---- Mouse callbacks ----
static void mouse_button_callback(GLFWwindow *w, int button, int action, int mods) {
  (void)mods;
  ImGui_ImplGlfw_MouseButtonCallback(w, button, action, mods);
  if (ImGui::GetIO().WantCaptureMouse)
    return;

  double mx, my;
  glfwGetCursorPos(w, &mx, &my);
  VKVIEW.mousex = (int)mx;
  VKVIEW.mousey = (int)my;

  if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS) {
    int wx = (int)((mx - VKVIEW.wwidth / 2.0) / VKVIEW.scalemult - VKVIEW.xtranslate);
    int wy = (int)((VKVIEW.wheight / 2.0 - my) / VKVIEW.scalemult - VKVIEW.ytranslate);
    world_processMouse(VKVIEW.base->world, 0, 0, wx, wy);
  }

  if (button == GLFW_MOUSE_BUTTON_RIGHT) {
    VKVIEW.downb[1] = (action == GLFW_PRESS) ? 1 : 0;
  }
}

static void scroll_callback(GLFWwindow *w, double xoff, double yoff) {
  (void)xoff;
  ImGui_ImplGlfw_ScrollCallback(w, xoff, yoff);
  if (ImGui::GetIO().WantCaptureMouse)
    return;

  VKVIEW.scalemult += VKVIEW.scalemult * 0.05f * (float)yoff;
  if (VKVIEW.scalemult < 0.01f)
    VKVIEW.scalemult = 0.01f;
}

static void cursor_pos_callback(GLFWwindow *w, double x, double y) {
  ImGui_ImplGlfw_CursorPosCallback(w, x, y);
  if (ImGui::GetIO().WantCaptureMouse)
    return;

  int dx = (int)x - VKVIEW.mousex;
  int dy = (int)y - VKVIEW.mousey;

  if (VKVIEW.downb[1]) { // right button = pan
    VKVIEW.xtranslate += dx / VKVIEW.scalemult;
    VKVIEW.ytranslate -= dy / VKVIEW.scalemult;
  }
  VKVIEW.mousex = (int)x;
  VKVIEW.mousey = (int)y;
}

// ---- Fullscreen toggle ----
void vkview_toggle_fullscreen(void) {
  if (VKVIEW.is_fullscreen) {
    glfwSetWindowMonitor(VKVIEW.window, NULL, 0, 0, VKVIEW.prev_width, VKVIEW.prev_height, GLFW_DONT_CARE);
  } else {
    VKVIEW.prev_width = VKVIEW.wwidth;
    VKVIEW.prev_height = VKVIEW.wheight;
    GLFWmonitor *mon = glfwGetPrimaryMonitor();
    const GLFWvidmode *mode = glfwGetVideoMode(mon);
    glfwSetWindowMonitor(VKVIEW.window, mon, 0, 0, mode->width, mode->height, mode->refreshRate);
  }
  VKVIEW.is_fullscreen = !VKVIEW.is_fullscreen;
}

// ---- Swapchain recreation helper ----
// Called before world_update each frame. Recreates the swapchain when
// OUT_OF_DATE or SUBOPTIMAL is signalled by acquire/present.
// Returns true if a recreation actually happened.
static bool vkview_recreate_swapchain_if_needed(VKView *view) {
  VKState *vk = view->vkstate;
  if (!vkinit_needs_recreation(vk) && !vk->framebuffer_resized)
    return false;

  // Clear the resize flag before recreation so we don't loop if
  // create_swapchain bails (minimized).  The callback will re-set it.
  vk->framebuffer_resized = 0;

  if (!vkinit_recreate_swapchain(vk))
    return false; // minimized window — try again next frame

  // Update VKVIEW dimensions from the window content area (screen
  // coordinates).  glfwGetCursorPos and ImGui use this space.
  // Rendering uses swapchain extent (framebuffer pixels) separately.
  // SetMinImageCount tells ImGui to rebuild its internal per-image
  // render buffers to match the new swapchain image count.
  glfwGetWindowSize(view->window, &view->wwidth, &view->wheight);
  ImGui_ImplVulkan_SetMinImageCount(vk->sc_count);
  return true;
}

// ---- Headless main loop ----
void vkview_main_loop_headless(void) {
  int steps = 0;
  while (VKVIEW.base->world->stopSim == 0) {
    if (VKVIEW.max_steps > 0 && steps >= VKVIEW.max_steps)
      break;

    VKState *vk = VKVIEW.vkstate;
    if (vk && vk->timestamp_supported)
      vk->timestamp_frame_idx ^= 1;

    world_update(VKVIEW.base->world);
    steps++;
  }
  printf("Headless simulation stopped (epoch %d, %zu agents, %d steps)\n", VKVIEW.base->world->current_epoch,
         VKVIEW.base->world->agents.size, steps);
}

// ---- Main loop ----
static const int MILLS_PER_UPDATE = 250;

void vkview_main_loop(void) {
  while (!glfwWindowShouldClose(VKVIEW.window)) {
    glfwPollEvents();
    VKVIEW.frame_start = glfwGetTime();

    struct World *w = VKVIEW.base->world;
    struct timespec t_frame;
    timer_reset(&t_frame);

    vkview_recreate_swapchain_if_needed(&VKVIEW);

    // World simulation (same logic as old glutIdleFunc)
    VKVIEW.modcounter++;
    if (!VKVIEW.paused) {
      // Flip timestamp slot-set before recording this frame's GPU commands.
      // world_record_compute (inside world_update) and vkdraw_frame both use
      // this index so compute + graphics timestamps land in the same set.
      VKState *vk = VKVIEW.vkstate;
      if (vk && vk->timestamp_supported)
        vk->timestamp_frame_idx ^= 1;

      world_update(VKVIEW.base->world);
    }

    // FPS tracking with exponential moving average
    double currentTime = glfwGetTime() * 1000.0;
    double elapsed = currentTime - VKVIEW.lastUpdate;
    VKVIEW.frames++;

    if (elapsed >= MILLS_PER_UPDATE) {
      float instantFPS = VKVIEW.frames * (1000.0f / (float)elapsed);
      float instantMs = (float)elapsed / VKVIEW.frames;

      // Exponential moving average (α = 0.2)
      if (VKVIEW.smoothFPS < 0.1f) {
        VKVIEW.smoothFPS = instantFPS;
        VKVIEW.smoothFrameMs = instantMs;
      } else {
        VKVIEW.smoothFPS = VKVIEW.smoothFPS * 0.8f + instantFPS * 0.2f;
        VKVIEW.smoothFrameMs = VKVIEW.smoothFrameMs * 0.8f + instantMs * 0.2f;
      }

      // Min/max tracking
      if (instantMs < VKVIEW.minFrameMs || VKVIEW.minFrameMs == 0.0f)
        VKVIEW.minFrameMs = instantMs;
      if (instantMs > VKVIEW.maxFrameMs)
        VKVIEW.maxFrameMs = instantMs;

      VKVIEW.totalFrames += VKVIEW.frames;

      snprintf(VKVIEW.buf, sizeof(VKVIEW.buf), "ScriptBots | %.0f FPS (%.1f ms) | Agents: %zu | Epoch: %d",
               VKVIEW.smoothFPS, VKVIEW.smoothFrameMs, VKVIEW.base->world->agents.size,
               VKVIEW.base->world->current_epoch);
      glfwSetWindowTitle(VKVIEW.window, VKVIEW.buf);

      VKVIEW.frames = 0;
      VKVIEW.lastUpdate = (int)currentTime;
      VKVIEW.minFrameMs = 0.0f;
      VKVIEW.maxFrameMs = 0.0f;
    }

    if (VKVIEW.draw) {
      // Start ImGui frame
      ImGui_ImplVulkan_NewFrame();
      ImGui_ImplGlfw_NewFrame();
      ImGui::NewFrame();

      imgui_draw_agent_hud(&VKVIEW);
      imgui_draw_performance(&VKVIEW);
      imgui_draw_sim_controls(&VKVIEW);

      ImGui::Render();

      // Draw world via Vulkan (+ ImGui rendered inside same pass)
      VKViewState vs = build_view_state(VKVIEW.vkstate);
      vkdraw_frame(VKVIEW.vkstate, &vs, render_imgui_to_cmd, NULL);

      // Read GPU timestamps from PREVIOUS frame's slot set.
      // Compute: previous frame's compute_fence was waited in world_wait_compute.
      // Graphics: previous frame's in_flight fence was waited at top of vkdraw_frame.
      VKState *vk = VKVIEW.vkstate;
      if (vk && vk->timestamp_supported) {
        uint32_t read_base = (1u - vk->timestamp_frame_idx) * 4;
        struct {
          uint64_t value;
          uint64_t avail;
        } results[4];
        if (vkGetQueryPoolResults(vk->device, vk->timestamp_pool, read_base, 4, sizeof(results), results,
                                  sizeof(results[0]),
                                  VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT) == VK_SUCCESS) {
          float ns = vk->timestamp_period;
          struct World *w = VKVIEW.base->world;
          if (results[0].avail && results[1].avail)
            w->timing.gpu_compute_ms = (float)((int64_t)(results[1].value - results[0].value)) * ns * 1e-6f;
          if (results[2].avail && results[3].avail)
            w->timing.gpu_draw_ms = (float)((int64_t)(results[3].value - results[2].value)) * ns * 1e-6f;
        }
      }
    } else {
      // Even when not drawing, we need to pump events
      // A small sleep prevents busy-waiting
      glfwWaitEventsTimeout(0.001);
    }
    if (w)
      w->timing.frame_total_ms = (float)timer_since_ms(&t_frame);

    // FPS limiter — sleep, re-check on wake, repeat until budget elapsed
    if (VKVIEW.max_fps > 0) {
      double target = 1.0 / (double)VKVIEW.max_fps;
      double elapsed;
      do {
        elapsed = glfwGetTime() - VKVIEW.frame_start;
        double rem = target - elapsed;
        if (rem > 0.0)
          glfwWaitEventsTimeout(rem);
      } while (glfwGetTime() - VKVIEW.frame_start < target);
    }
  }
}
