// vkview.c — Window, input, main loop, ImGui HUD (replaces GLView.c)
#include "vkview.h"
#include "settings.h"
#include "World.h"
#include "Agent.h"
#include "AVXBrain.h"

#include <stdio.h>
#include <string.h>
#include <math.h>
#include <time.h>

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

// ImGui
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_vulkan.h"
#include <stdio.h>

// Forward: draw function from vkdraw.c (C linkage)
extern "C" void vkdraw_frame(struct VKState *vk, vkdraw_imgui_cb imgui_cb, void *imgui_user);

static void render_imgui_to_cmd(VkCommandBuffer cmd, void *user_data) {
    (void)user_data;
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);
}

// GLFW error callback for diagnostics
static void glfw_error_callback(int err, const char *desc) {
    fprintf(stderr, "[GLFW] Error %d: %s\n", err, desc);
}

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
static void framebuffer_size_callback(GLFWwindow *w, int width, int height);

// ---- ImGui HUD ----
static void draw_imgui_hud(void);
static void draw_imgui_diagnostics(void);

// ---- Init ----
void vkview_init(int argc, char **argv) {
    (void)argc; (void)argv;

    memset(&VKVIEW, 0, sizeof(VKVIEW));

    // View state defaults (matches old GLVIEW init)
    VKVIEW.paused    = 0;
    VKVIEW.draw      = 1;
    VKVIEW.skipdraw  = 1;
    VKVIEW.drawfood  = 1;
    VKVIEW.draw_text = 1;
    VKVIEW.modcounter = 0;
    VKVIEW.lastUpdate = 0;
    VKVIEW.frames     = 0;
    VKVIEW.xtranslate = -(WIDTH / 2.0f);
    VKVIEW.ytranslate = -(HEIGHT / 2.0f);
    VKVIEW.scalemult  = 0.4f;
    VKVIEW.downb[0] = VKVIEW.downb[1] = VKVIEW.downb[2] = 0;
    VKVIEW.mousex = VKVIEW.mousey = 0;
    VKVIEW.wwidth  = WWIDTH;
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
    glfwSetMouseButtonCallback(VKVIEW.window, mouse_button_callback);
    glfwSetScrollCallback(VKVIEW.window, scroll_callback);
    glfwSetCursorPosCallback(VKVIEW.window, cursor_pos_callback);
    glfwSetFramebufferSizeCallback(VKVIEW.window, framebuffer_size_callback);

    // Init Vulkan
    VKVIEW.vkstate = vkinit_create(VKVIEW.window);
    if (!VKVIEW.vkstate) {
        fprintf(stderr, "FATAL: Vulkan init failed\n");
        exit(1);
    }

    // Init ImGui
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::StyleColorsDark();

    ImGui_ImplGlfw_InitForVulkan(VKVIEW.window, false);

    VkPhysicalDevice phys = vkinit_get_phys_device(VKVIEW.vkstate);
    VkDevice dev = vkinit_get_device(VKVIEW.vkstate);
    uint32_t qfam = vkinit_get_queue_family(VKVIEW.vkstate);
    VkQueue queue = vkinit_get_queue(VKVIEW.vkstate);
    VkRenderPass rp = vkinit_get_render_pass(VKVIEW.vkstate);

    // ImGui descriptor pool
    VkDescriptorPoolSize pool_sizes[] = {
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1 },
    };
    VkDescriptorPoolCreateInfo dpci = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT,
        .maxSets = 1,
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

// ---- Key callback ----
static void key_callback(GLFWwindow *w, int key, int scancode, int action, int mods) {
    (void)scancode;
    ImGui_ImplGlfw_KeyCallback(w, key, scancode, action, mods);
    if (ImGui::GetIO().WantCaptureKeyboard) return;
    if (action != GLFW_PRESS) return;

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
    case GLFW_KEY_EQUAL:
        VKVIEW.skipdraw++;
        break;
    case GLFW_KEY_MINUS:
        VKVIEW.skipdraw--;
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
        if (mods & GLFW_MOD_CONTROL) base_loadworld(VKVIEW.base);
        break;
    case GLFW_KEY_S:
        if (mods & GLFW_MOD_CONTROL) base_saveworld(VKVIEW.base);
        break;
    case GLFW_KEY_PAGE_UP:
        VKVIEW.scalemult *= 1.05f;
        break;
    case GLFW_KEY_PAGE_DOWN:
        VKVIEW.scalemult *= 0.95f;
        if (VKVIEW.scalemult < 0.01f) VKVIEW.scalemult = 0.01f;
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
    if (ImGui::GetIO().WantCaptureMouse) return;

    double mx, my;
    glfwGetCursorPos(w, &mx, &my);
    VKVIEW.mousex = (int)mx;
    VKVIEW.mousey = (int)my;

    if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS) {
        int wx = (int)((mx - VKVIEW.wwidth / 2.0) / VKVIEW.scalemult - VKVIEW.xtranslate);
        int wy = (int)((VKVIEW.wheight / 2.0 - my) / VKVIEW.scalemult - VKVIEW.ytranslate);
        world_processMouse(VKVIEW.base->world, 0, 0, wx, wy);
    }

    if (button >= 0 && button <= 2) {
        VKVIEW.downb[button] = (action == GLFW_PRESS) ? 1 : 0;
    }
}

static void scroll_callback(GLFWwindow *w, double xoff, double yoff) {
    (void)xoff;
    ImGui_ImplGlfw_ScrollCallback(w, xoff, yoff);
    if (ImGui::GetIO().WantCaptureMouse) return;

    VKVIEW.scalemult += VKVIEW.scalemult * 0.05f * (float)yoff;
    if (VKVIEW.scalemult < 0.01f) VKVIEW.scalemult = 0.01f;
}

static void cursor_pos_callback(GLFWwindow *w, double x, double y) {
    ImGui_ImplGlfw_CursorPosCallback(w, x, y);
    if (ImGui::GetIO().WantCaptureMouse) return;

    int dx = (int)x - VKVIEW.mousex;
    int dy = (int)y - VKVIEW.mousey;

    if (VKVIEW.downb[2]) {  // right button = pan
        VKVIEW.xtranslate += dx / VKVIEW.scalemult;
        VKVIEW.ytranslate += dy / VKVIEW.scalemult;
    }
    VKVIEW.mousex = (int)x;
    VKVIEW.mousey = (int)y;
}

static void framebuffer_size_callback(GLFWwindow *w, int width, int height) {
    (void)w;
    if (width == 0 || height == 0) return;
    VKVIEW.wwidth = width;
    VKVIEW.wheight = height;
    if (VKVIEW.vkstate) vkinit_set_needs_recreation(VKVIEW.vkstate);
}

// ---- Fullscreen toggle ----
void vkview_toggle_fullscreen(void) {
    if (VKVIEW.is_fullscreen) {
        glfwSetWindowMonitor(VKVIEW.window, NULL,
                             0, 0,
                             VKVIEW.prev_width, VKVIEW.prev_height,
                             GLFW_DONT_CARE);
    } else {
        VKVIEW.prev_width = VKVIEW.wwidth;
        VKVIEW.prev_height = VKVIEW.wheight;
        GLFWmonitor *mon = glfwGetPrimaryMonitor();
        const GLFWvidmode *mode = glfwGetVideoMode(mon);
        glfwSetWindowMonitor(VKVIEW.window, mon,
                             0, 0,
                             mode->width, mode->height,
                             mode->refreshRate);
    }
    VKVIEW.is_fullscreen = !VKVIEW.is_fullscreen;
}

// ---- ImGui HUD for selected agent ----
static void draw_imgui_hud(void) {
    struct World *w = VKVIEW.base->world;
    if (!w) return;

    // Find selected agent
    struct Agent *sel = NULL;
    for (size_t i = 0; i < w->agents.size; i++) {
        if (w->agents.agents[i]->selectflag) { sel = w->agents.agents[i]; break; }
    }
    if (!sel) return;

    // Smooth camera lerp toward selected agent
    VKVIEW.xtranslate += (-sel->pos.x - VKVIEW.xtranslate) * 0.05f;
    VKVIEW.ytranslate += (-sel->pos.y - VKVIEW.ytranslate) * 0.05f;

    ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(420, 650), ImGuiCond_FirstUseEver);
    ImGui::Begin("Agent Inspector", NULL,
                 ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize);

    ImDrawList *dl = ImGui::GetWindowDrawList();

    // --- Input neurons ---
    ImGui::Text("Inputs");
    ImVec2 cursor = ImGui::GetCursorScreenPos();
    float ss = 14.0f;
    for (size_t j = 0; j < BRAIN_INPUT_SIZE; j++) {
        float col = sel->in[j];
        ImU32 c;
        if (j < 18)
            c = IM_COL32((int)(col*255), (int)(col*255), (int)(col*255), 255);
        else if (j < 19)
            c = IM_COL32(0, (int)(col*255), (int)(col*255), 255);
        else
            c = IM_COL32(0, (int)(col*255), 0, 255);
        dl->AddRectFilled(
            ImVec2(cursor.x + ss * j, cursor.y),
            ImVec2(cursor.x + ss * j + ss - 1, cursor.y + 13), c);
    }
    ImGui::Dummy(ImVec2(ss * BRAIN_INPUT_SIZE, 14));

    // --- Output neurons ---
    ImGui::Text("Outputs");
    cursor = ImGui::GetCursorScreenPos();
    for (size_t j = 0; j < BRAIN_OUTPUT_SIZE; j++) {
        float col = sel->out[j];
        ImU32 c = IM_COL32((int)(col*255), (int)(col*255), (int)(col*255), 255);
        dl->AddRectFilled(
            ImVec2(cursor.x + ss * j, cursor.y),
            ImVec2(cursor.x + ss * j + ss - 1, cursor.y + 13), c);
    }
    ImGui::Dummy(ImVec2(ss * BRAIN_OUTPUT_SIZE, 14));

    // --- Brain layers ---
    ImGui::Text("Brain Weights");
    for (int layer = 0; layer < BRAIN_DEPTH; layer++) {
        ImGui::Text("  Layer %d", layer);
        cursor = ImGui::GetCursorScreenPos();
        ss = 8.0f;
        for (int k = 0; k < BRAIN_WIDTH; k++) {
            for (size_t l = 0; l < BRAIN_ELEMENTS_PER_VECTOR; l++) {
                int offx = k * (BRAIN_ELEMENTS_PER_VECTOR + 1) + l;
                float col = sel->brain->layers[layer].inputs[k][l];
                ImU32 c = IM_COL32((int)(col*255), (int)(col*255), (int)(col*255), 255);
                dl->AddRectFilled(
                    ImVec2(cursor.x + ss * offx, cursor.y),
                    ImVec2(cursor.x + ss * offx + ss - 1, cursor.y + ss - 1), c);
            }
        }
        ImGui::Dummy(ImVec2(
            ss * (BRAIN_WIDTH * (BRAIN_ELEMENTS_PER_VECTOR + 1)), ss));
    }

    ImGui::Separator();
    ImGui::Text("Health:       %.3f", sel->health);
    ImGui::Text("Position:     %.1f, %.1f", sel->pos.x, sel->pos.y);
    ImGui::Text("Angle:        %.3f", sel->angle);
    ImGui::Text("Children:     %d", sel->numchildren);
    ImGui::Text("Generation:   %jd", (intmax_t)sel->gencount);
    ImGui::Text("Age:          %d", sel->age);
    ImGui::Text("Rep counter:  %.3f", sel->repcounter);
    ImGui::Text("Herbivore:    %.3f", sel->herbivore);
    ImGui::Text("Mutate rate:  %.4f", sel->MUTRATE1);
    ImGui::Text("Mutate mag:   %.4f", sel->MUTRATE2);
    ImGui::Text("Wheel L/R:    %.4f, %.4f", sel->w2, sel->w1);

    ImGui::End();

    // World-space text labels for nearby agents when zoomed in
    if (VKVIEW.draw_text && VKVIEW.scalemult > 0.7f) {
        ImDrawList *fg = ImGui::GetForegroundDrawList();
        for (size_t i = 0; i < w->agents.size; i++) {
            struct Agent *a = w->agents.agents[i];
            // Project world → screen (Y flipped for Vulkan viewport: Y=0 is top)
            float sx = (a->pos.x + VKVIEW.xtranslate) * VKVIEW.scalemult + VKVIEW.wwidth / 2.0f;
            float sy = VKVIEW.wheight / 2.0f - (a->pos.y + VKVIEW.ytranslate) * VKVIEW.scalemult;
            if (sx < -50 || sx > VKVIEW.wwidth + 50 || sy < -50 || sy > VKVIEW.wheight + 50) continue;

            char tmp[64];
            int yoff = 0;
            snprintf(tmp, sizeof(tmp), "%jd", (intmax_t)a->gencount);
            fg->AddText(ImVec2(sx - BOTRADIUS * 2 * VKVIEW.scalemult,
                               sy + 5 + BOTRADIUS * 2 * VKVIEW.scalemult + yoff),
                        IM_COL32_WHITE, tmp);
            yoff += 12;
            snprintf(tmp, sizeof(tmp), "%d", a->age);
            fg->AddText(ImVec2(sx - BOTRADIUS * 2 * VKVIEW.scalemult,
                               sy + 5 + BOTRADIUS * 2 * VKVIEW.scalemult + yoff),
                        IM_COL32_WHITE, tmp);
            yoff += 12;
            snprintf(tmp, sizeof(tmp), "%.2f", a->health);
            fg->AddText(ImVec2(sx - BOTRADIUS * 2 * VKVIEW.scalemult,
                               sy + 5 + BOTRADIUS * 2 * VKVIEW.scalemult + yoff),
                        IM_COL32_WHITE, tmp);
            yoff += 12;
            snprintf(tmp, sizeof(tmp), "%.2f", a->repcounter);
            fg->AddText(ImVec2(sx - BOTRADIUS * 2 * VKVIEW.scalemult,
                               sy + 5 + BOTRADIUS * 2 * VKVIEW.scalemult + yoff),
                        IM_COL32_WHITE, tmp);
        }
    }
}

// ---- Diagnostics overlay ----
static void draw_imgui_diagnostics(void) {
    struct World *w = VKVIEW.base->world;
    if (!w) return;

    ImGui::SetNextWindowPos(ImVec2((float)VKVIEW.wwidth - 260, 0), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.5f);
    ImGui::Begin("##diag", NULL,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs |
                 ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing);

    ImGui::Text("FPS: %.1f", VKVIEW.frames * (1000.0f / 250.0f));
    ImGui::Text("Agents: %zu", w->agents.size);
    ImGui::Text("Herbivores: %d", world_numHerbivores(w));
    ImGui::Text("Carnivores: %d", world_numCarnivores(w));
    ImGui::Text("Epoch: %d", w->current_epoch);
    ImGui::Text("Zoom: %.2f", VKVIEW.scalemult);

    ImGui::End();
}

// ---- Main loop ----
static const int MILLS_PER_UPDATE = 250;

void vkview_main_loop(void) {
    while (!glfwWindowShouldClose(VKVIEW.window)) {
        glfwPollEvents();

        // World simulation (same logic as old glutIdleFunc)
        VKVIEW.modcounter++;
        if (!VKVIEW.paused) {
            world_update(VKVIEW.base->world);
        }

        // FPS tracking
        double currentTime = glfwGetTime() * 1000.0;
        VKVIEW.frames++;
        if ((currentTime - VKVIEW.lastUpdate) >= MILLS_PER_UPDATE) {
            int num_herbs = world_numHerbivores(VKVIEW.base->world);
            int num_carns = world_numCarnivores(VKVIEW.base->world);
            snprintf(VKVIEW.buf, sizeof(VKVIEW.buf),
                     "FPS: %.2f Agents: %zu Herb: %d Carn: %d Epoch: %d",
                     VKVIEW.frames * (1000.0f / MILLS_PER_UPDATE),
                     VKVIEW.base->world->agents.size,
                     num_herbs, num_carns,
                     VKVIEW.base->world->current_epoch);
            glfwSetWindowTitle(VKVIEW.window, VKVIEW.buf);
            VKVIEW.frames = 0;
            VKVIEW.lastUpdate = (int)currentTime;
        }

        // Frame skip logic
        bool shouldDraw = false;
        if (VKVIEW.skipdraw <= 0 && VKVIEW.draw) {
            clock_t endwait;
            float mult = -0.005f * (VKVIEW.skipdraw - 1);
            endwait = clock() + mult * CLOCKS_PER_SEC;
            while (clock() < endwait) {}
            shouldDraw = true;
        } else if (VKVIEW.draw) {
            if (VKVIEW.skipdraw > 0) {
                if (VKVIEW.modcounter % VKVIEW.skipdraw == 0)
                    shouldDraw = true;
            }
        }

        if (shouldDraw) {
            // Check if swapchain needs recreation (resize, out-of-date)
            if (vkinit_needs_recreation(VKVIEW.vkstate)) {
                vkinit_recreate_swapchain(VKVIEW.vkstate);
                // If still needs recreation (minimized), skip this frame
                if (vkinit_needs_recreation(VKVIEW.vkstate)) continue;
                // Re-create ImGui font texture for new render pass
                ImGui_ImplVulkan_CreateFontsTexture();
            }

            // Start ImGui frame
            ImGui_ImplVulkan_NewFrame();
            ImGui_ImplGlfw_NewFrame();
            ImGui::NewFrame();

            draw_imgui_hud();
            draw_imgui_diagnostics();

            ImGui::Render();

            // Draw world via Vulkan (+ ImGui rendered inside same pass)
            vkdraw_frame(VKVIEW.vkstate, render_imgui_to_cmd, NULL);
        } else {
            // Even when not drawing, we need to pump events
            // A small sleep prevents busy-waiting
            glfwWaitEventsTimeout(0.001);
        }
    }
}
