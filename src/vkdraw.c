// vkdraw.c — per-frame buffer updates, command recording, and present
#include "vkhelpers.h"
#include "vkview.h"
#include "settings.h"
#include "World.h"
#include "Agent.h"

#include <stdio.h>
#include <string.h>
#include <math.h>

// ---- External access to VKState internals for drawing ----
// (We access these fields directly — they're defined in vkinit.c)
typedef struct VKState {
    VkInstance               instance;
    void                    *debug_messenger_pad;
    VkPhysicalDevice         phys_device;
    VkDevice                 device;
    VkQueue                  queue;
    uint32_t                 queue_family;
    VkSurfaceKHR             surface;

    VkSwapchainKHR    swapchain;
    VkImage           *sc_images;
    VkImageView       *sc_views;
    VkFramebuffer     *sc_framebufs;
    uint32_t           sc_count;
    VkFormat           sc_format;
    VkExtent2D         sc_extent;

    VkRenderPass  render_pass;

    void       *window_pad;
    int         needs_recreation;

    VkCommandPool   cmd_pool;
    VkCommandBuffer cmd_buf[2];

    uint32_t       current_frame;
    VkSemaphore    image_avail[2];
    VkSemaphore   *render_done;  // per-swapchain-image
    VkFence        in_flight[2];

    VkDescriptorSetLayout desc_layout;
    VkDescriptorPool      desc_pool;
    VkDescriptorSet       desc_set;
    VkPipelineLayout      pipeline_layout;

    VkPipeline pipe_circle;
    VkPipeline pipe_lines;
    VkPipeline pipe_hud;
    VkPipeline pipe_food;

    VkBuffer       cam_ubo_buf;
    VkDeviceMemory cam_ubo_mem;
    VkBuffer       agent_buf;
    VkDeviceMemory agent_mem;
    VkBuffer       food_vbuf;
    VkDeviceMemory food_vmem;

    VkBuffer       mesh_circle_vb;
    VkDeviceMemory mesh_circle_mem;
    uint32_t        mesh_circle_verts;

    VkBuffer       mesh_lines_vb;
    VkDeviceMemory mesh_lines_mem;
    uint32_t        mesh_lines_verts;

    VkBuffer       mesh_hud_vb;
    VkDeviceMemory mesh_hud_mem;
} VKState;

// ---- Build orthographic projection matrix (column-major) ----
static void build_ortho(float *out, float left, float right, float top, float bottom,
                        float near_, float far_) {
    memset(out, 0, sizeof(float) * 16);
    out[0]  = 2.0f / (right - left);
    out[5]  = 2.0f / (top - bottom);
    out[10] = -2.0f / (far_ - near_);
    out[12] = -(right + left) / (right - left);
    out[13] = -(top + bottom) / (top - bottom);
    out[14] = -(far_ + near_) / (far_ - near_);
    out[15] = 1.0f;
}

// ---- Update camera UBO ----
static void update_camera(VKState *vk) {
    if (VKVIEW.scalemult < 0.0001f) return;
    float L = -(VKVIEW.wwidth  / 2.0f) / VKVIEW.scalemult - VKVIEW.xtranslate;
    float R = L + VKVIEW.wwidth / VKVIEW.scalemult;
    // Y-axis: old GL code used reversed-Y ortho (top=0, bottom=h),
    // but Vulkan uses Y-down in clip space, so our world Y-up maps naturally.
    // We want world coords to map such that +Y is up on screen.
    float B = -(VKVIEW.wheight / 2.0f) / VKVIEW.scalemult - VKVIEW.ytranslate;
    float T = B + VKVIEW.wheight / VKVIEW.scalemult;

    CameraUBO ubo;
    // Swap top/bottom: we want larger world Y → higher on screen (Vulkan viewport Y=0 is top)
    build_ortho(ubo.mvp, L, R, B, T, -1.0f, 1.0f);

    void *mapped;
    vkMapMemory(vk->device, vk->cam_ubo_mem, 0, sizeof(CameraUBO), 0, &mapped);
    memcpy(mapped, &ubo, sizeof(CameraUBO));
    vkUnmapMemory(vk->device, vk->cam_ubo_mem);
}

// ---- Update agent SSBO ----
static int update_agents(VKState *vk) {
    struct World *w = VKVIEW.base->world;
    int count = (int)w->agents.size;
    if (count > VK_MAX_AGENTS) count = VK_MAX_AGENTS;

    AgentInstance *dst;
    vkMapMemory(vk->device, vk->agent_mem, 0, count * sizeof(AgentInstance), 0, (void**)&dst);

    for (int i = 0; i < count; i++) {
        struct Agent *a = w->agents.agents[i];
        dst[i] = (AgentInstance){
            .pos_x = a->pos.x, .pos_y = a->pos.y,
            .color_r = a->red, .color_g = a->gre, .color_b = a->blu,
            .angle = a->angle,
            .health = a->health,
            .herbivore = a->herbivore,
            .soundmul = a->soundmul,
            .spike_length = a->spikeLength,
            .boost = a->boost ? 1 : 0,
            .select_flag = a->selectflag ? 1 : 0,
            .indicator_r = a->ir, .indicator_g = a->ig, .indicator_b = a->ib,
            .indicator_size = a->indicator,
        };
    }

    vkUnmapMemory(vk->device, vk->agent_mem);
    return count;
}

// ---- Update food vertex buffer ----
static int update_food(VKState *vk) {
    struct World *w = VKVIEW.base->world;
    int maxVerts = VK_MAX_FOOD_VERTS;
    if (maxVerts < 4) maxVerts = 4;

    FoodVertex *dst;
    vkMapMemory(vk->device, vk->food_vmem, 0, maxVerts * sizeof(FoodVertex), 0, (void**)&dst);

    int count = 0;
    if (VKVIEW.drawfood) {
        for (int i = 0; i < FOOD_SQUARES_WIDTH; i++) {
            for (int j = 0; j < FOOD_SQUARES_HEIGHT; j++) {
                float f = w->foodGrid.food[i][j].amt / FOODMAX;
                if (f < 0.0001f) continue;

                // Viewport culling in screen space (with proper centering)
                float cellSize = CZ * VKVIEW.scalemult;
                float sx = (i * CZ + VKVIEW.xtranslate) * VKVIEW.scalemult + VKVIEW.wwidth / 2.0f;
                float sy = (j * CZ + VKVIEW.ytranslate) * VKVIEW.scalemult + VKVIEW.wheight / 2.0f;
                if (sx + cellSize < 0.0f || sx > VKVIEW.wwidth ||
                    sy + cellSize < 0.0f || sy > VKVIEW.wheight)
                    continue;

                if (count + 6 > maxVerts) break;
                float g = 0.02f + f * 0.8f;
                float x0 = i * CZ, y0 = j * CZ;
                // Triangle list: two triangles per cell
                dst[count++] = (FoodVertex){ x0,      y0,      0.02f, g, 0.02f };
                dst[count++] = (FoodVertex){ x0 + CZ, y0,      0.02f, g, 0.02f };
                dst[count++] = (FoodVertex){ x0 + CZ, y0 + CZ, 0.02f, g, 0.02f };
                dst[count++] = (FoodVertex){ x0,      y0,      0.02f, g, 0.02f };
                dst[count++] = (FoodVertex){ x0 + CZ, y0 + CZ, 0.02f, g, 0.02f };
                dst[count++] = (FoodVertex){ x0,      y0 + CZ, 0.02f, g, 0.02f };
            }
        }
    }

    vkUnmapMemory(vk->device, vk->food_vmem);
    return count;
}

// ---- Main draw function called each frame ----
void vkdraw_frame(VKState *vk, vkdraw_imgui_cb imgui_cb, void *imgui_user) {
    uint32_t cf = vk->current_frame;

    // Safety checks
    if (!VKVIEW.base || !VKVIEW.base->world) return;

    // Wait for this frame's previous work to complete
    vkWaitForFences(vk->device, 1, &vk->in_flight[cf], VK_TRUE, UINT64_MAX);
    vkResetFences(vk->device, 1, &vk->in_flight[cf]);

    // Acquire swapchain image
    uint32_t imgIdx;
    VkResult res = vkAcquireNextImageKHR(vk->device, vk->swapchain, UINT64_MAX,
                                         vk->image_avail[cf], VK_NULL_HANDLE, &imgIdx);
    if (res != VK_SUCCESS && res != VK_SUBOPTIMAL_KHR) {
        // OUT_OF_DATE, SURFACE_LOST, etc. — signal swapchain recreation
        if (res == VK_ERROR_OUT_OF_DATE_KHR) vk->needs_recreation = 1;
        return;
    }

    vkResetCommandBuffer(vk->cmd_buf[cf], 0);

    // Update dynamic buffers
    update_camera(vk);
    int agentCount = update_agents(vk);
    int foodVertCount = update_food(vk);

    // Record command buffer
    VkCommandBufferBeginInfo cbbi = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    vkBeginCommandBuffer(vk->cmd_buf[cf], &cbbi);

    VkClearValue clear = {{{ 0.0f, 0.0f, 0.0f, 1.0f }}};
    VkRenderPassBeginInfo rpbi = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass = vk->render_pass,
        .framebuffer = vk->sc_framebufs[imgIdx],
        .renderArea = {{ 0, 0 }, vk->sc_extent },
        .clearValueCount = 1, .pClearValues = &clear,
    };
    vkCmdBeginRenderPass(vk->cmd_buf[cf], &rpbi, VK_SUBPASS_CONTENTS_INLINE);

    // Set dynamic viewport/scissor
    VkViewport vp = { 0, 0, (float)vk->sc_extent.width, (float)vk->sc_extent.height, 0.0f, 1.0f };
    vkCmdSetViewport(vk->cmd_buf[cf], 0, 1, &vp);
    VkRect2D scissor = {{ 0, 0 }, vk->sc_extent };
    vkCmdSetScissor(vk->cmd_buf[cf], 0, 1, &scissor);

    vkCmdBindDescriptorSets(vk->cmd_buf[cf], VK_PIPELINE_BIND_POINT_GRAPHICS,
                            vk->pipeline_layout, 0, 1, &vk->desc_set, 0, NULL);

    VkDeviceSize vbOff = 0;

    // ---- 1. Agent bodies ----
    vkCmdBindPipeline(vk->cmd_buf[cf], VK_PIPELINE_BIND_POINT_GRAPHICS, vk->pipe_circle);
    vkCmdBindVertexBuffers(vk->cmd_buf[cf], 0, 1, &vk->mesh_circle_vb, &vbOff);
    {
        PushConstCircle pc = { .botRadius = BOTRADIUS, .agentOffset = 0 };
        vkCmdPushConstants(vk->cmd_buf[cf], vk->pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT,
                           0, sizeof(pc), &pc);
        vkCmdDraw(vk->cmd_buf[cf], vk->mesh_circle_verts, agentCount, 0, 0);
    }

    // ---- 2. Selection rings (same circle mesh, larger radius) ----
    {
        PushConstCircle pc = { .botRadius = BOTRADIUS + 5.0f, .agentOffset = 0 };
        vkCmdPushConstants(vk->cmd_buf[cf], vk->pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT,
                           0, sizeof(pc), &pc);
        vkCmdDraw(vk->cmd_buf[cf], vk->mesh_circle_verts, agentCount, 0, 0);
    }

    // ---- 3. Indicator rings ----
    {
        PushConstCircle pc = { .botRadius = BOTRADIUS + 2.0f, .agentOffset = 0 };
        vkCmdPushConstants(vk->cmd_buf[cf], vk->pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT,
                           0, sizeof(pc), &pc);
        vkCmdDraw(vk->cmd_buf[cf], vk->mesh_circle_verts, agentCount, 0, 0);
    }

    // ---- 4. View cone + spike lines ----
    vkCmdBindPipeline(vk->cmd_buf[cf], VK_PIPELINE_BIND_POINT_GRAPHICS, vk->pipe_lines);
    vkCmdBindVertexBuffers(vk->cmd_buf[cf], 0, 1, &vk->mesh_lines_vb, &vbOff);
    {
        PushConstLines pc = {
            .coneLength = BOTRADIUS * 4.0f,
            .spikeScale = BOTRADIUS * 3.0f,
            .agentOffset = 0,
        };
        vkCmdPushConstants(vk->cmd_buf[cf], vk->pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT,
                           0, sizeof(pc), &pc);
        vkCmdDraw(vk->cmd_buf[cf], vk->mesh_lines_verts, agentCount, 0, 0);
    }

    // ---- 5. HUD elements (health bar, herbivore, sound) ----
    vkCmdBindPipeline(vk->cmd_buf[cf], VK_PIPELINE_BIND_POINT_GRAPHICS, vk->pipe_hud);
    vkCmdBindVertexBuffers(vk->cmd_buf[cf], 0, 1, &vk->mesh_hud_vb, &vbOff);
    {
        PushConstHud pc = { .agentOffset = 0 };
        vkCmdPushConstants(vk->cmd_buf[cf], vk->pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT,
                           0, sizeof(pc), &pc);

        vkCmdDraw(vk->cmd_buf[cf], 4, agentCount, 0, 0);   // type 0: health bg
        vkCmdDraw(vk->cmd_buf[cf], 4, agentCount, 4, 0);   // type 1: health fg
        vkCmdDraw(vk->cmd_buf[cf], 4, agentCount, 8, 0);   // type 2: herbivore
        vkCmdDraw(vk->cmd_buf[cf], 4, agentCount, 12, 0);  // type 3: sound
    }

    // ---- 6. Food grid ----
    if (foodVertCount > 0) {
        vkCmdBindPipeline(vk->cmd_buf[cf], VK_PIPELINE_BIND_POINT_GRAPHICS, vk->pipe_food);
        vkCmdBindVertexBuffers(vk->cmd_buf[cf], 0, 1, &vk->food_vbuf, &vbOff);
        vkCmdDraw(vk->cmd_buf[cf], foodVertCount, 1, 0, 0);
    }

    // ---- 7. ImGui (rendered on top of everything) ----
    if (imgui_cb) imgui_cb(vk->cmd_buf[cf], imgui_user);

    vkCmdEndRenderPass(vk->cmd_buf[cf]);
    vkEndCommandBuffer(vk->cmd_buf[cf]);

    // Submit
    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo si = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .waitSemaphoreCount = 1, .pWaitSemaphores = &vk->image_avail[cf],
        .pWaitDstStageMask = &waitStage,
        .commandBufferCount = 1, .pCommandBuffers = &vk->cmd_buf[cf],
        .signalSemaphoreCount = 1, .pSignalSemaphores = &vk->render_done[imgIdx],
    };
    vkQueueSubmit(vk->queue, 1, &si, vk->in_flight[cf]);

    // Present
    VkPresentInfoKHR pi = {
        .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .waitSemaphoreCount = 1, .pWaitSemaphores = &vk->render_done[imgIdx],
        .swapchainCount = 1, .pSwapchains = &vk->swapchain,
        .pImageIndices = &imgIdx,
    };
    VkResult presentRes = vkQueuePresentKHR(vk->queue, &pi);
    if (presentRes == VK_ERROR_OUT_OF_DATE_KHR) {
        vk->needs_recreation = 1;
    }
    (void)presentRes;

    vk->current_frame = (cf + 1) % 2;

    VKVIEW.food_vert_count = foodVertCount;
    VKVIEW.agent_count = agentCount;
}
