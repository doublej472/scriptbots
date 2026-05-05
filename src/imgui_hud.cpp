// imgui_hud.cpp — ImGui overlays: agent inspector + diagnostics
#include "Agent.h"
#include "World.h"
#include "settings.h"
#include "vkview.h"

#include "imgui.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

// Portable half → float (fp16 bit layout → IEEE float32)
static float half_to_float(uint16_t h) {
  uint32_t sign = ((uint32_t)h >> 15) & 1u;
  uint32_t exp = ((uint32_t)h >> 10) & 0x1fu;
  uint32_t mant = (uint32_t)h & 0x3ffu;
  if (exp == 0) {
    if (mant == 0) {
      uint32_t r = sign << 31;
      float f;
      memcpy(&f, &r, 4);
      return f;
    }
    exp = 1;
    while ((mant & 0x400u) == 0) {
      mant <<= 1;
      exp--;
    }
    mant &= 0x3ffu;
  } else if (exp == 31) {
    uint32_t r = (sign << 31) | (0xffu << 23) | (mant << 13);
    float f;
    memcpy(&f, &r, 4);
    return f;
  }
  uint32_t r = (sign << 31) | (((exp - 15 + 127) & 0xffu) << 23) | (mant << 13);
  float f;
  memcpy(&f, &r, 4);
  return f;
}

void imgui_draw_agent_hud(struct VKView *view) {
  struct World *w = view->base->world;
  if (!w)
    return;

  struct Agent *sel = w->selected_agent ? w->selected_agent : w->movie_agent;
  if (!sel)
    return;
  size_t sel_idx = w->selected_agent ? w->selected_index : w->movie_index;
  float *sel_in = w->agent_inputs + sel_idx * AGENT_INPUT_FLOATS;
  float *sel_out = w->agent_outputs + sel_idx * AGENT_OUTPUT_FLOATS;

  view->xtranslate += (-sel->pos.x - view->xtranslate) * 0.05f;
  view->ytranslate += (-sel->pos.y - view->ytranslate) * 0.05f;

  ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_Always);
  ImGui::SetNextWindowSize(ImVec2(420, 650), ImGuiCond_FirstUseEver);
  ImGui::Begin("Agent Inspector", NULL,
               ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing);

  ImDrawList *dl = ImGui::GetWindowDrawList();

  // Input neurons
  ImGui::Text("Inputs");
  ImVec2 cursor = ImGui::GetCursorScreenPos();
  float ss = 14.0f;
  for (size_t j = 0; j < BRAIN_INPUT_SIZE; j++) {
    float col = sel_in[j];
    ImU32 c;
    if (j < 18)
      c = IM_COL32((int)(col * 255), (int)(col * 255), (int)(col * 255), 255);
    else if (j < 19)
      c = IM_COL32(0, (int)(col * 255), (int)(col * 255), 255);
    else
      c = IM_COL32(0, (int)(col * 255), 0, 255);
    dl->AddRectFilled(ImVec2(cursor.x + ss * j, cursor.y), ImVec2(cursor.x + ss * j + ss - 1, cursor.y + 13), c);
  }
  ImGui::Dummy(ImVec2(ss * BRAIN_INPUT_SIZE, 14));

  // Output neurons
  ImGui::Text("Outputs");
  cursor = ImGui::GetCursorScreenPos();
  for (size_t j = 0; j < BRAIN_OUTPUT_SIZE; j++) {
    float col = sel_out[j];
    dl->AddRectFilled(ImVec2(cursor.x + ss * j, cursor.y), ImVec2(cursor.x + ss * j + ss - 1, cursor.y + 13),
                      IM_COL32((int)(col * 255), (int)(col * 255), (int)(col * 255), 255));
  }
  ImGui::Dummy(ImVec2(ss * BRAIN_OUTPUT_SIZE, 14));

  // Brain weights summary (GPU doesn't expose layer activations)
  ImGui::Text("Brain Weights (GPU)");
  ImGui::Text("  %d params, %.1f KB (fp16)", BRAIN_WEIGHT_FLOATS, (float)BRAIN_WEIGHT_UINTS * 4.0f / 1024.0f);
  float wmin = 1e10f, wmax = -1e10f;
  for (int i = 0; i < BRAIN_WEIGHT_UINTS; i++) {
    uint32_t pair = sel->brain[i];
    float flo = half_to_float((uint16_t)(pair & 0xffffu));
    float fhi = half_to_float((uint16_t)(pair >> 16));
    if (flo < wmin)
      wmin = flo;
    if (flo > wmax)
      wmax = flo;
    if (fhi < wmin)
      wmin = fhi;
    if (fhi > wmax)
      wmax = fhi;
  }
  ImGui::Text("  Range: [%.3f, %.3f]", wmin, wmax);

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

  // World-space text labels
  if (view->draw_text && view->scalemult > 0.7f) {
    // Compute positions in framebuffer-pixel space (matching the
    // orthographic projection), then convert to screen coordinates
    // for ImGui using DisplayFramebufferScale.
    ImDrawList *fg = ImGui::GetForegroundDrawList();
    float fb_w = (float)view->vkstate->sc_extent.width;
    float fb_h = (float)view->vkstate->sc_extent.height;
    const ImGuiIO &io = ImGui::GetIO();
    float sc_x = io.DisplayFramebufferScale.x;
    float sc_y = io.DisplayFramebufferScale.y;
    if (sc_x < 0.001f)
      sc_x = 1.0f;
    if (sc_y < 0.001f)
      sc_y = 1.0f;

    for (size_t i = 0; i < w->agents.size; i++) {
      struct Agent *a = w->agents.agents[i];
      float sx_fb = (a->pos.x + view->xtranslate) * view->scalemult + fb_w / 2.0f;
      float sy_fb = fb_h / 2.0f - (a->pos.y + view->ytranslate) * view->scalemult;
      if (sx_fb < -50 || sx_fb > fb_w + 50 || sy_fb < -50 || sy_fb > fb_h + 50)
        continue;

      char tmp[64];
      float bx_fb = sx_fb - BOTRADIUS * 2 * view->scalemult;
      float by_fb = sy_fb + 5 + BOTRADIUS * 2 * view->scalemult;
      float bx = bx_fb / sc_x;
      float by = by_fb / sc_y;
      ImU32 white = IM_COL32_WHITE;
      snprintf(tmp, sizeof(tmp), "%jd", (intmax_t)a->gencount);
      fg->AddText(ImVec2(bx, by), white, tmp);
      by += 12;
      snprintf(tmp, sizeof(tmp), "%d", a->age);
      fg->AddText(ImVec2(bx, by), white, tmp);
      by += 12;
      snprintf(tmp, sizeof(tmp), "%.2f", a->health);
      fg->AddText(ImVec2(bx, by), white, tmp);
      by += 12;
      snprintf(tmp, sizeof(tmp), "%.2f", a->repcounter);
      fg->AddText(ImVec2(bx, by), white, tmp);
    }
  }
}

void imgui_draw_diagnostics(struct VKView *view) {
  struct World *w = view->base->world;
  if (!w || !view->show_diag_window)
    return;

  ImGui::SetNextWindowPos(ImVec2((float)view->wwidth - 330, 0), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSize(ImVec2(320, 500), ImGuiCond_FirstUseEver);
  ImGui::Begin("Simulation", &view->show_diag_window);

  // ---- Performance ----
  if (ImGui::CollapsingHeader("Performance", ImGuiTreeNodeFlags_DefaultOpen)) {
    struct FrameTiming *t = &w->timing;

    // FPS header
    ImGui::Text("FPS: %.1f  (%.1f ms)   Agents: %u", view->smoothFPS, view->smoothFrameMs, t->agent_count);
    if (view->minFrameMs > 0.0f && view->maxFrameMs > 0.0f)
      ImGui::Text("Range: %.1f – %.1f ms", view->minFrameMs, view->maxFrameMs);

    bool limit = (view->max_fps > 0);
    if (ImGui::Checkbox("Limit FPS", &limit))
      view->max_fps = limit ? 60 : 0;
    if (limit) {
      ImGui::SameLine();
      ImGui::PushItemWidth(50);
      int fps = view->max_fps;
      if (ImGui::InputInt("##maxfps", &fps, 0, 0, ImGuiInputTextFlags_EnterReturnsTrue)) {
        if (fps < 10)
          fps = 10;
        view->max_fps = fps;
      }
      ImGui::PopItemWidth();
    }

    ImGui::Separator();

    // Column headers
    ImGui::Columns(4, "timing", false);
    ImGui::SetColumnWidth(0, 165);
    ImGui::SetColumnWidth(1, 55);
    ImGui::SetColumnWidth(2, 55);
    ImGui::SetColumnWidth(3, 50);
    ImGui::Text("Phase");
    ImGui::NextColumn();
    ImGui::Text("CPU ms");
    ImGui::NextColumn();
    ImGui::Text("GPU ms");
    ImGui::NextColumn();
    ImGui::Text("BW");
    ImGui::NextColumn();
    ImGui::Separator();

    // Helper: print a row. cpu<0 = "—", gpu<0 = "—", bw=0 = "—"
    auto row = [&](const char *label, float cpu_ms, float gpu_ms, float bw) {
      ImGui::Text("%s", label);
      ImGui::NextColumn();
      if (cpu_ms >= 0.0f)
        ImGui::Text("%.1f", cpu_ms);
      else
        ImGui::Text("—");
      ImGui::NextColumn();
      if (gpu_ms >= 0.0f)
        ImGui::Text("%.1f", gpu_ms);
      else
        ImGui::Text("—");
      ImGui::NextColumn();
      if (bw > 0.0f)
        ImGui::Text("%.1f", bw);
      else
        ImGui::Text("—");
      ImGui::NextColumn();
    };
    auto sep = [&]() {
      ImGui::Separator();
      ImGui::Separator();
    };

    // ── Simulation pipeline ──
    row("Food Update", t->food_update, -1.0f, 0.0f);
    row("Spatial Sort", t->spatial_sort, -1.0f, 0.0f);
    row("Submit Compute", t->submit_compute, -1.0f, 0.0f);
    row("Set Inputs", t->input_staging, -1.0f, 0.0f);

    sep();
    // GPU compute — CPU wait time and actual GPU time side by side
    row("GPU Compute [wait]", t->gpu_wait, t->gpu_compute_ms, 0.0f);
    sep();

    row("Process Outputs", t->output_physics, -1.0f, 0.0f);
    row("Death/Repro/Bots", t->death_repro, -1.0f, 0.0f);
    row("Flush Staging", t->flush_staging, -1.0f, 0.0f);
    row("Record Compute", t->record_compute, -1.0f, 0.0f);

    sep();
    // ── Draw pipeline ──
    float draw_bw =
        (t->draw_upload > 0.001f && t->render_bytes > 0) ? (float)t->render_bytes / (t->draw_upload * 1e6f) : 0.0f;
    row("Draw Upload", t->draw_upload, -1.0f, draw_bw);
    row("Draw Record", t->draw_record, -1.0f, 0.0f);
    row("GPU Draw", -1.0f, t->gpu_draw_ms, 0.0f);

    ImGui::Columns(1);
    ImGui::Separator();

    // Total
    ImGui::Text("Frame total: %.1f ms", (double)t->frame_total_ms);
    double total_gpu = (double)(t->gpu_compute_ms + t->gpu_draw_ms);
    if (total_gpu > 0.0) {
      double overhead = t->frame_total_ms - total_gpu;
      if (overhead > 1.0)
        ImGui::Text("(CPU-limited: +%.1f ms CPU overhead over GPU work)", overhead);
      else
        ImGui::Text("(GPU-limited: CPU waits on GPU)");
    }
  }

  // ---- Simulation State ----
  if (ImGui::CollapsingHeader("Simulation")) {
    ImGui::Text("Agents:     %5zu", w->agents.size);
    ImGui::Text("Food:       %5.2f", world_getTotalFood(w));
    ImGui::Text("Herbivores: %5d", world_numHerbivores(w));
    ImGui::Text("Carnivores: %5d", world_numCarnivores(w));
    ImGui::Text("Epoch:      %5d", w->current_epoch);
    ImGui::Text("Zoom:       %5.2fx", view->scalemult);
    ImGui::Text("Frames:     %5d", view->totalFrames);
  }

  // ---- Controls ----
  if (ImGui::CollapsingHeader("Controls")) {
    bool paused = view->paused;
    if (ImGui::Checkbox("Pause", &paused))
      view->paused = paused ? 1 : 0;

    bool df = view->drawfood;
    if (ImGui::Checkbox("Draw food", &df))
      view->drawfood = df ? 1 : 0;
    ImGui::SameLine();
    bool dt = view->draw_text;
    if (ImGui::Checkbox("Draw text", &dt))
      view->draw_text = dt ? 1 : 0;

    bool cl = w->closed;
    if (ImGui::Checkbox("Closed env", &cl))
      w->closed = cl ? 1 : 0;
    ImGui::SameLine();
    bool mm = w->movieMode;
    if (ImGui::Checkbox("Movie mode", &mm))
      w->movieMode = mm ? 1 : 0;

    ImGui::SeparatorText("Spawn");
    ImGui::PushItemWidth(60);
    static int spawn_count = 100;
    if (spawn_count < 1)
      spawn_count = 1;
    ImGui::InputInt("Count", &spawn_count, 0, 0);
    ImGui::PopItemWidth();

    if (ImGui::Button("Herbivores"))
      world_addRandomBots(w, spawn_count);
    ImGui::SameLine();
    if (ImGui::Button("Carnivores")) {
      for (int i = 0; i < spawn_count; i++)
        world_addCarnivore(w);
    }

    ImGui::Spacing();
    if (ImGui::Button("Reset world"))
      world_reset(w);
  }

  ImGui::End();
}
