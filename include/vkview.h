// vkview.h — replaces GLView.h: window, input, draw loop
#pragma once

#include "Base.h"
#include "vkhelpers.h"

#include <stdbool.h>
#include <stdint.h>

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct VKView {
  // --- Window ---
  GLFWwindow *window;
  int wwidth, wheight;
  int prev_width, prev_height;
  bool is_fullscreen;

  // --- Vulkan state (opaque) ---
  struct VKState *vkstate;
  VkDescriptorPool imgui_descriptor_pool;

  // --- View state (same fields as old GLVIEW) ---
  struct Base *base;
  int paused;
  int draw;
  int drawfood;
  char buf[100];
  char buf2[256];
  int modcounter;
  int lastUpdate;
  int frames;
  int totalFrames;     // total rendered frames
  float smoothFPS;     // exponential moving average FPS
  float smoothFrameMs; // exponential moving average frame time (ms)
  float minFrameMs;    // minimum frame time this period
  float maxFrameMs;    // maximum frame time this period

  float scalemult;
  float xtranslate, ytranslate;
  int downb[2];
  int mousex, mousey;
  int draw_text;

  // --- FPS limiter ---
  int max_fps;        // 0 = unlimited, else clamped to ≥10
  double frame_start; // timestamp of start of current frame

  // --- Diagnostics window ---
  bool show_diag_window; // collapse state survives across frames
} VKView;

extern VKView VKVIEW;

// Lifecycle
void vkview_init(int argc, char **argv);
void vkview_main_loop(void);
void vkview_cleanup(void);

// Called from input callbacks (exposed for main.c if needed)
void vkview_process_normal_key(int key, int mods);
void vkview_process_mouse_click(int button, int action, double x, double y);

// Toggle fullscreen
void vkview_toggle_fullscreen(void);

// ImGui overlays (implemented in imgui_hud.cpp)
void imgui_draw_agent_hud(struct VKView *view);
void imgui_draw_diagnostics(struct VKView *view);

#ifdef __cplusplus
}
#endif
