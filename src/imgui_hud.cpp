// imgui_hud.cpp — ImGui overlays: agent inspector + diagnostics
#include "Agent.h"
#include "Base.h"
#include "World.h"
#include "settings.h"
#include "vkhelpers.h"
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

  // Read output directly from GPU mapped buffer (stable during draw phase)
  float sel_out_buf[BRAIN_OUTPUT_SIZE];
  float *sel_out = sel_out_buf;
  memset(sel_out_buf, 0, sizeof(sel_out_buf));
  if (view->vkstate && sel->brain_chunk != ~0u) {
    VKState *vk = view->vkstate;
    BrainChunk *c = &vk->chunks[sel->brain_chunk];
    memcpy(sel_out_buf, (const float *)c->outputs[(uint32_t)w->brain_slot].mapped + sel->brain_index * BRAIN_OUTPUT_SIZE,
           BRAIN_OUTPUT_SIZE * sizeof(float));
  }

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

void imgui_draw_performance(struct VKView *view) {
  struct World *w = view->base->world;
  if (!w || !view->show_perf)
    return;

  ImGui::SetNextWindowPos(ImVec2((float)view->wwidth - 310, 0), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSize(ImVec2(300, 350), ImGuiCond_FirstUseEver);
  ImGui::Begin("Performance", &view->show_perf);

  struct FrameTiming *t = &w->timing;
  ImGui::Text("FPS: %.1f  (%.1f ms)   Agents: %u", view->smoothFPS, view->smoothFrameMs, t->agent_count);

  float sim_cpu = t->food_update + t->spatial_sort + t->compute_submit + t->input_staging + t->output_processing +
                  t->death_repro + t->flush_staging + t->record_compute;
  float draw_cpu = t->draw_upload + t->draw_record;
  float gpu_work = t->gpu_compute_ms + t->gpu_draw_ms;

  ImGui::Spacing();

  // ── Simulation (CPU) ──
  ImGui::Text("Simulation (CPU)");
  ImGui::SameLine(210);
  ImGui::Text("ms");
  ImGui::Separator();

  auto row = [&](const char *label, float ms) {
    ImGui::Text("  %s", label);
    ImGui::SameLine(210);
    ImGui::Text("%.1f", ms);
  };

  row("Food Growth", t->food_update);
  row("Spatial Sort", t->spatial_sort);
  row("Compute Submit", t->compute_submit);
  row("Input Staging", t->input_staging);
  row("CPU Wait GPU", t->gpu_wait);
  row("Process Outputs", t->output_processing);
  row("Death / Repro", t->death_repro);
  row("Flush Staging", t->flush_staging);
  row("Record Dispatch", t->record_compute);

  // ── GPU ──
  if (t->gpu_compute_ms > 0.0f || t->gpu_draw_ms > 0.0f) {
    ImGui::Spacing();
    ImGui::Text("GPU");
    ImGui::SameLine(210);
    ImGui::Text("ms");
    ImGui::Separator();
    if (t->gpu_compute_ms > 0.0f)
      row("GPU Compute", t->gpu_compute_ms);
    if (t->gpu_draw_ms > 0.0f)
      row("GPU Draw", t->gpu_draw_ms);
  }

  // ── Draw (CPU) ──
  ImGui::Spacing();
  ImGui::Text("Draw (CPU)");
  ImGui::SameLine(210);
  ImGui::Text("ms");
  ImGui::Separator();
  row("Upload + Record", draw_cpu);

  // ── Summary ──
  ImGui::Spacing();
  ImGui::Separator();
  ImGui::Text("Frame: %.1f ms", t->frame_total_ms);
  float accounted = sim_cpu + gpu_work + draw_cpu;
  float remainder = t->frame_total_ms - accounted;
  ImGui::TextDisabled("  CPU work %.1f  |  GPU %.1f", sim_cpu + draw_cpu, gpu_work);
  ImGui::TextDisabled("  sync / misc +%.1f ms", remainder > 0.0f ? remainder : 0.0f);

  ImGui::End();
}

void imgui_draw_sim_controls(struct VKView *view) {
  struct World *w = view->base->world;
  if (!w || !view->show_sim)
    return;

  ImGui::SetNextWindowPos(ImVec2(10, 670), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSize(ImVec2(300, 360), ImGuiCond_FirstUseEver);
  ImGui::Begin("Simulation", &view->show_sim);

  // ---- State ----
  ImGui::Text("Agents:     %5zu", w->agents.size);
  ImGui::Text("Food:       %5.2f", world_getTotalFood(w));
  ImGui::Text("Herbivores: %5d", world_numHerbivores(w));
  ImGui::Text("Carnivores: %5d", world_numCarnivores(w));
  ImGui::Text("Epoch:      %5d", w->current_epoch);
  ImGui::Text("Zoom:       %5.2fx", view->scalemult);
  ImGui::Text("Frames:     %5d", view->totalFrames);

  ImGui::Separator();

  // ---- Toggles ----
  bool paused = view->paused;
  if (ImGui::Checkbox("Pause", &paused))
    view->paused = paused ? 1 : 0;
  ImGui::SameLine();
  bool cl = w->closed;
  if (ImGui::Checkbox("Closed env", &cl))
    w->closed = cl ? 1 : 0;

  bool df = view->drawfood;
  if (ImGui::Checkbox("Draw food", &df))
    view->drawfood = df ? 1 : 0;
  ImGui::SameLine();
  bool dt = view->draw_text;
  if (ImGui::Checkbox("Draw text", &dt))
    view->draw_text = dt ? 1 : 0;

  bool mm = w->movieMode;
  if (ImGui::Checkbox("Movie mode", &mm))
    w->movieMode = mm ? 1 : 0;

  ImGui::Separator();

  // ---- Spawn ----
  ImGui::Text("Spawn");
  static int spawn_count = 100;
  ImGui::PushItemWidth(80);
  ImGui::InputInt("Count", &spawn_count, 0, 0);
  if (spawn_count < 1)
    spawn_count = 1;
  ImGui::PopItemWidth();

  if (ImGui::Button("Herbivores")) {
    for (int i = 0; i < spawn_count; i++)
      world_addHerbivore(w);
  }
  ImGui::SameLine();
  if (ImGui::Button("Carnivores")) {
    for (int i = 0; i < spawn_count; i++)
      world_addCarnivore(w);
  }
  ImGui::SameLine();
  if (ImGui::Button("Mixed"))
    world_addRandomBots(w, spawn_count);

  ImGui::Separator();

  // ---- World save / load ----
  ImGui::Text("World");
  static char world_path[256] = "world.dat";
  ImGui::PushItemWidth(-1);
  ImGui::InputText("##worldpath", world_path, sizeof(world_path));
  ImGui::PopItemWidth();

  if (ImGui::Button("Save")) {
    snprintf(VKVIEW.base->world_file, sizeof(VKVIEW.base->world_file), "%s", world_path);
    base_saveworld(VKVIEW.base);
  }
  ImGui::SameLine();
  bool can_load = (world_path[0] != '\0');
  if (!can_load)
    ImGui::BeginDisabled();
  if (ImGui::Button("Load")) {
    snprintf(VKVIEW.base->world_file, sizeof(VKVIEW.base->world_file), "%s", world_path);
    if (base_loadworld(VKVIEW.base) && VKVIEW.base->world->brain_gpu == NULL && VKVIEW.vkstate) {
      VKVIEW.base->world->brain_gpu = VKVIEW.vkstate;
      VKVIEW.base->world->brain_slot = 0;
      VKState *vk = VKVIEW.vkstate;
      vkDeviceWaitIdle(vk->device);
      vkbrain_reset_counts(vk);
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
      world_seed_inputs(VKVIEW.base->world);
      vkbrain_record_dispatch(vk, 0);
    }
  }
  if (!can_load)
    ImGui::EndDisabled();

  ImGui::Spacing();
  if (ImGui::Button("Reset world"))
    world_reset(w);

  ImGui::End();
}
