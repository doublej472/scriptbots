// vkview.h — replaces GLView.h: window, input, draw loop
#pragma once

#include "Base.h"
#include "vkhelpers.h"

#include <stdint.h>
#include <stdbool.h>

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct VKView {
    // --- Window ---
    GLFWwindow *window;
    int  wwidth, wheight;
    int  prev_width, prev_height;
    bool is_fullscreen;

    // --- Vulkan state (opaque) ---
    struct VKState *vkstate;
    VkDescriptorPool imgui_descriptor_pool;

    // --- View state (same fields as old GLVIEW) ---
    struct Base *base;
    int  paused;
    int  draw;
    int  skipdraw;
    int  drawfood;
    char buf[100];
    char buf2[256];
    int  modcounter;
    int  lastUpdate;
    int  frames;
    int  totalFrames;       // total rendered frames
    float smoothFPS;        // exponential moving average FPS
    float smoothFrameMs;    // exponential moving average frame time (ms)
    float minFrameMs;       // minimum frame time this period
    float maxFrameMs;       // maximum frame time this period

    float scalemult;
    float xtranslate, ytranslate;
    int  downb[3];
    int  mousex, mousey;
    int  draw_text;
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

#ifdef __cplusplus
}
#endif
