// vkdraw.c — per-frame buffer updates, command recording, and present
//
// Compute and graphics pipelines run on dedicated VkQueue instances for
// overlapping execution.  The compute queue writes agent outputs into
// per-chunk mapped buffers; the graphics queue reads a separate agent SSBO
// (populated by the CPU after compute completes).  The compute→graphics
// dependency is enforced via a timeline semaphore (vk->compute_timeline)
// signalled by the compute queue and waited on by both the graphics queue
// and the CPU (for mapped output readback).
//
// CPU-side, world_update (which submits compute, gathers next-frame inputs,
// and waits for the timeline) runs before vkdraw_frame (graphics) on the
// main thread.  The mapped agent/food SSBOs are written only here and read
// only by the graphics queue, so no CPU/GPU race exists.
//
// Frame synchronisation:
//   1. vkWaitForFences(in_flight)        — wait for previous graphics-submit fence
//   2. vkAcquireNextImageKHR             — get swapchain image (image_avail semaphore)
//   3. CPU writes mapped UBO/SSBO (host-coherent)
//   4. vkCmdPipelineBarrier              — HOST_WRITE → shader reads
//   5. Draw calls + ImGui
//   6. vkQueueSubmit(wait: image_avail   — graphics queue waits for swapchain image;  
//                    wait: compute_timeline — …and for compute to finish;
//                    signal: render_done, fence: in_flight)
//   7. vkQueuePresentKHR(wait: render_done)
//
#include "Agent.h"
#include "Base.h"
#include "World.h"
#include "helpers.h"
#include "settings.h"
#include "vkhelpers.h"
#include "vkview.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

// ---- Build an orthographic projection matrix (Vulkan NDC: Y points down, depth 0..1) ----
// Parameters are in world-space order: left < right, bottom < top, near < far.
// out[5] is negated to account for Vulkan's Y-down NDC (screen top = NDC y=-1).
static void build_ortho(float *out, float left, float right, float bottom, float top, float near_, float far_) {
  memset(out, 0, sizeof(float) * 16);
  out[0] = 2.0f / (right - left);
  out[5] = -2.0f / (top - bottom);   // negated for Vulkan Y-down NDC
  out[10] = -2.0f / (far_ - near_);
  out[12] = -(right + left) / (right - left);
  out[13] = (top + bottom) / (top - bottom);
  out[14] = -(far_ + near_) / (far_ - near_);
  out[15] = 1.0f;
}

// ---- Update camera UBO ----
static void update_camera(VKState *vk, const VKViewState *view) {
  if (view->scalemult < 0.0001f)
    return;
  if (!vk->cam_ubo.mapped)
    return;
  float L = -(view->wwidth / 2.0f) / view->scalemult - view->xtranslate;
  float R = L + view->wwidth / view->scalemult;
  float B = -(view->wheight / 2.0f) / view->scalemult - view->ytranslate;
  float T = B + view->wheight / view->scalemult;
  CameraUBO ubo;
  build_ortho(ubo.mvp, L, R, B, T, -1.0f, 1.0f);
  memcpy(vk->cam_ubo.mapped, &ubo, sizeof(CameraUBO));
}

// ---- Update agent SSBO ----
static uint32_t update_agents(VKState *vk, const VKViewState *view) {
  struct World *w = view->base->world;
  if (!vk->agent_buf.mapped)
    return 0;
  uint32_t count = (uint32_t)w->agents.size;
  // Grow SSBO if population outgrows capacity (round up to next power of two)
  if (count > vk->agent_capacity) {
    uint32_t np = 1;
    while (np < count)
      np <<= 1;
    vkdraw_resize_agents(vk, np);
  }
  if (count > vk->agent_capacity)
    count = vk->agent_capacity; // defensive

  // Fast path: contiguous render cache → SSBO in a single memcpy.
  // The cache is updated incrementally by agent_output_processor_range
  // and world_flush_staging, so no scatter-reads of 14 KB Agent mallocs.
  AgentInstance *dst = vk->agent_buf.mapped;
  if (w->agent_render_data) {
    memcpy(dst, w->agent_render_data, count * sizeof(AgentInstance));
    // Patch select_flag for selected/movie agent (pointer compare, no field reads)
    for (uint32_t i = 0; i < count; i++) {
      struct Agent *a = w->agents.agents[i];
      if (a == w->selected_agent || a == w->movie_agent)
        dst[i].select_flag = 1;
    }
  } else {
    // Fallback: scatter-read from Agent structs (used when render cache
    // not yet allocated, e.g., headless mode or early startup)
    for (uint32_t i = 0; i < count; i++) {
      struct Agent *a = w->agents.agents[i];
      dst[i] = (AgentInstance){
          .pos_x = a->pos.x,
          .pos_y = a->pos.y,
          .color_r = a->red,
          .color_g = a->gre,
          .color_b = a->blu,
          .angle = a->angle,
          .health = a->health,
          .herbivore = a->herbivore,
          .soundmul = a->soundmul,
          .spike_length = a->spikeLength,
          .boost = a->boost ? 1 : 0,
          .select_flag = (a == w->selected_agent || a == w->movie_agent) ? 1 : 0,
          .indicator_r = a->ir,
          .indicator_g = a->ig,
          .indicator_b = a->ib,
          .indicator_size = a->indicator,
      };
    }
  }
  return count;
}

static void update_food(VKState *vk, const VKViewState *view) {
  struct World *w = view->base->world;
  if (!view->drawfood || !vk->food_buf.mapped)
    return;
  uint32_t n = w->foodGrid.total_cells;
  memcpy(vk->food_buf.mapped, w->foodGrid.food_amounts, n * sizeof(float));
}

// ---- Draw command recording ----
//
// Records a complete frame: host-write barriers, render pass begin,
// food grid (SSBO-based fullscreen quad), agent bodies (instanced
// circle fan), selection/indicator rings, view cone + spike lines,
// HUD elements, optional ImGui, render pass end.
static void record_draws(VkCommandBuffer cmd, VKState *vk, const VKViewState *view, uint32_t agentCount,
                         vkdraw_imgui_cb imgui_cb, void *imgui_user, uint32_t imgIdx) {
  // CPU writes → GPU reads: camera UBO (uniform), agent SSBO (storage), food SSBO (storage)
  VkBufferMemoryBarrier bufBarriers[3] = {
      {.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
       .srcAccessMask = VK_ACCESS_HOST_WRITE_BIT,
       .dstAccessMask = VK_ACCESS_UNIFORM_READ_BIT,
       .buffer = vk->cam_ubo.buffer,
       .offset = 0,
       .size = VK_WHOLE_SIZE},
      {.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
       .srcAccessMask = VK_ACCESS_HOST_WRITE_BIT,
       .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
       .buffer = vk->agent_buf.buffer,
       .offset = 0,
       .size = VK_WHOLE_SIZE},
      {.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
       .srcAccessMask = VK_ACCESS_HOST_WRITE_BIT,
       .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
       .buffer = vk->food_buf.buffer,
       .offset = 0,
       .size = VK_WHOLE_SIZE},
  };
  vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_HOST_BIT,
                       VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, NULL, 3,
                       bufBarriers, 0, NULL);

  VkClearValue clear = {{{0.0f, 0.0f, 0.0f, 1.0f}}};
  VkRenderPassBeginInfo rpbi = {
      .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
      .renderPass = vk->render_pass,
      .framebuffer = vk->sc_framebufs[imgIdx],
      .renderArea = {{0, 0}, vk->sc_extent},
      .clearValueCount = 1,
      .pClearValues = &clear,
  };
  if (vk->timestamp_supported) {
    uint32_t base = vk->timestamp_frame_idx * 4 + 2;
    vkCmdResetQueryPool(cmd, vk->timestamp_pool, base, 2);
  }

  vkCmdBeginRenderPass(cmd, &rpbi, VK_SUBPASS_CONTENTS_INLINE);

  if (vk->timestamp_supported)
    vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT, vk->timestamp_pool, vk->timestamp_frame_idx * 4 + 2);

  VkViewport vp = {0, 0, (float)vk->sc_extent.width, (float)vk->sc_extent.height, 0.0f, 1.0f};
  vkCmdSetViewport(cmd, 0, 1, &vp);
  VkRect2D scissor = {{0, 0}, vk->sc_extent};
  vkCmdSetScissor(cmd, 0, 1, &scissor);

  // Food grid (SSBO-based: single 6-vertex quad, fragment shader reads SSBO)
  if (view->base->world && view->drawfood) {
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vk->pipeline_layout, 0, 1, &vk->desc_set_food, 0,
                            NULL);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vk->pipe_food.pipeline);
    PushConstFood pcFood = {.worldW = (float)WIDTH, .worldH = (float)HEIGHT, .cellSize = (float)CZ, .foodMax = FOODMAX};
    vkCmdPushConstants(cmd, vk->pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(pcFood), &pcFood);
    vkCmdDraw(cmd, 6, 1, 0, 0);
  }

  // Back to agent descriptor set
  vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vk->pipeline_layout, 0, 1, &vk->desc_set, 0, NULL);

  // Agent bodies, selection rings, indicator rings (conditional on draw_agents)
  if (view->draw_agents) {
    VkDeviceSize vbOff = 0;

    // Agent bodies
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vk->pipe_circle.pipeline);
    vkCmdBindVertexBuffers(cmd, 0, 1, &vk->mesh_circle.buffer, &vbOff);
    PushConstCircle pcBody = {.botRadius = BOTRADIUS, .agentOffset = 0, .type = 0};
    vkCmdPushConstants(cmd, vk->pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(pcBody), &pcBody);
    vkCmdDraw(cmd, vk->mesh_circle_verts, agentCount, 0, 0);

    // Selection rings
    PushConstCircle pcSel = {.botRadius = BOTRADIUS + 5.0f, .type = 1};
    vkCmdPushConstants(cmd, vk->pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(pcSel), &pcSel);
    vkCmdDraw(cmd, vk->mesh_circle_verts, agentCount, 0, 0);

    // Indicator / event rings
    PushConstCircle pcInd = {.botRadius = BOTRADIUS, .type = 2};
    vkCmdPushConstants(cmd, vk->pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(pcInd), &pcInd);
    vkCmdDraw(cmd, vk->mesh_circle_verts, agentCount, 0, 0);

    // View cone + spike lines
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vk->pipe_lines.pipeline);
    vkCmdBindVertexBuffers(cmd, 0, 1, &vk->mesh_lines.buffer, &vbOff);
    PushConstLines pcLines = {.coneLength = BOTRADIUS * 4.0f, .spikeScale = BOTRADIUS * 3.0f};
    vkCmdPushConstants(cmd, vk->pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(pcLines), &pcLines);
    vkCmdDraw(cmd, vk->mesh_lines_verts, agentCount, 0, 0);

    // HUD elements
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vk->pipe_hud.pipeline);
    vkCmdBindVertexBuffers(cmd, 0, 1, &vk->mesh_hud.buffer, &vbOff);
    PushConstHud pcHud = {.agentOffset = 0};
    vkCmdPushConstants(cmd, vk->pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(pcHud), &pcHud);
    vkCmdDraw(cmd, 4, agentCount, 0, 0);
    vkCmdDraw(cmd, 4, agentCount, 4, 0);
    vkCmdDraw(cmd, 4, agentCount, 8, 0);
    vkCmdDraw(cmd, 4, agentCount, 12, 0);
  }

  if (imgui_cb)
    imgui_cb(cmd, imgui_user);
  if (vk->timestamp_supported)
    vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT, vk->timestamp_pool, vk->timestamp_frame_idx * 4 + 3);

  vkCmdEndRenderPass(cmd);
}

// ---- Main draw function ----
void vkdraw_frame(VKState *vk, const VKViewState *view, vkdraw_imgui_cb imgui_cb, void *imgui_user) {
  if (!view->base || !view->base->world)
    return;
  if (!vk->sc_framebufs)
    return;
  uint32_t cf = vk->current_frame;

  // Acquire swapchain image
  if (!vk->in_flight[cf].fence) {
    fprintf(stderr, "FATAL: in_flight fence is NULL\n");
    return;
  }
  vkWaitForFences(vk->device, 1, &vk->in_flight[cf].fence, VK_TRUE, UINT64_MAX);
  uint32_t imgIdx;
  VkResult res = vkAcquireNextImageKHR(vk->device, vk->swapchain, UINT64_MAX,
                                       vk->image_avail[cf].semaphore, VK_NULL_HANDLE, &imgIdx);
  if (res == VK_ERROR_OUT_OF_DATE_KHR || res == VK_SUBOPTIMAL_KHR) {
    vk->needs_recreation = 1;
    return;
  }
  if (res != VK_SUCCESS)
    return;
  vkResetFences(vk->device, 1, &vk->in_flight[cf].fence);
  vkResetCommandBuffer(vk->cmd_buf[cf], 0);

  // Update CPU-side per-frame data
  struct World *w = view->base->world;
  struct timespec t0;
  double prev, now;
  timer_reset(&t0);
  update_camera(vk, view);
  int agentCount = update_agents(vk, view);
  update_food(vk, view);
  now = timer_since_ms(&t0);
  w->timing.draw_upload = (float)now;
  w->timing.agent_count = agentCount;
  prev = now;

  // Record commands + submit
  VkCommandBufferBeginInfo cbbi = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
      .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
  };
  vkBeginCommandBuffer(vk->cmd_buf[cf], &cbbi);
  record_draws(vk->cmd_buf[cf], vk, view, agentCount, imgui_cb, imgui_user, imgIdx);
  vkEndCommandBuffer(vk->cmd_buf[cf]);

  VkPipelineStageFlags waitStages[] = {
      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
      VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,  // graphics waits for compute output (agent SSBO reads)
  };

  // Wait on both the swapchain image semaphore AND the compute timeline semaphore.
  // This ensures the GPU agent SSBO is fully written before graphics reads it.
  // The timeline semaphore info must provide a value for EVERY semaphore in pWaitSemaphores.
  // The first value (index 0) is ignored for the binary image_avail semaphore.
  uint64_t wait_values[2] = {0, vk->compute_timeline_value - 1};
  VkTimelineSemaphoreSubmitInfo tsi = {
      .sType                     = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO,
      .waitSemaphoreValueCount   = 2,
      .pWaitSemaphoreValues      = wait_values,
  };
  VkSemaphore wait_semas[] = {vk->image_avail[cf].semaphore, vk->compute_timeline};
  VkSubmitInfo si = {
      .sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO,
      .pNext                = &tsi,
      .waitSemaphoreCount   = 2,
      .pWaitSemaphores      = wait_semas,
      .pWaitDstStageMask    = waitStages,
      .commandBufferCount   = 1,
      .pCommandBuffers      = &vk->cmd_buf[cf],
      .signalSemaphoreCount = 1,
      .pSignalSemaphores    = &vk->render_done[imgIdx].semaphore,
  };
  vkQueueSubmit(vk->gfx_queue, 1, &si, vk->in_flight[cf].fence);
  w->timing.draw_record = (float)(timer_since_ms(&t0) - prev);

  VkPresentInfoKHR pi = {
      .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
      .waitSemaphoreCount = 1,
      .pWaitSemaphores = &vk->render_done[imgIdx].semaphore,
      .swapchainCount = 1,
      .pSwapchains = &vk->swapchain,
      .pImageIndices = &imgIdx,
  };
  VkResult presentRes = vkQueuePresentKHR(vk->gfx_queue, &pi);
  if (presentRes == VK_ERROR_OUT_OF_DATE_KHR || presentRes == VK_SUBOPTIMAL_KHR)
    vk->needs_recreation = 1;
  vk->current_frame = (cf + 1) % VK_MAX_FRAMES_IN_FLIGHT;
}
