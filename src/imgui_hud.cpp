// imgui_hud.cpp — ImGui overlays: agent inspector + diagnostics
#include "vkview.h"
#include "World.h"
#include "Agent.h"
#include "settings.h"

#include "imgui.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

// Portable half → float (fp16 bit layout → IEEE float32)
static float half_to_float(uint16_t h) {
    uint32_t sign = ((uint32_t)h >> 15) & 1u;
    uint32_t exp  = ((uint32_t)h >> 10) & 0x1fu;
    uint32_t mant = (uint32_t)h & 0x3ffu;
    if (exp == 0) {
        if (mant == 0) { uint32_t r = sign << 31; float f; memcpy(&f, &r, 4); return f; }
        exp = 1;
        while ((mant & 0x400u) == 0) { mant <<= 1; exp--; }
        mant &= 0x3ffu;
    } else if (exp == 31) {
        uint32_t r = (sign << 31) | (0xffu << 23) | (mant << 13);
        float f; memcpy(&f, &r, 4); return f;
    }
    uint32_t r = (sign << 31) | (((exp - 15 + 127) & 0xffu) << 23) | (mant << 13);
    float f; memcpy(&f, &r, 4); return f;
}

void imgui_draw_agent_hud(struct VKView *view) {
    struct World *w = view->base->world;
    if (!w) return;

    struct Agent *sel = NULL;
    for (size_t i = 0; i < w->agents.size; i++) {
        if (w->agents.agents[i]->selectflag) { sel = w->agents.agents[i]; break; }
    }
    if (!sel) return;

    view->xtranslate += (-sel->pos.x - view->xtranslate) * 0.05f;
    view->ytranslate += (-sel->pos.y - view->ytranslate) * 0.05f;

    ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(420, 650), ImGuiCond_FirstUseEver);
    ImGui::Begin("Agent Inspector", NULL,
                 ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize |
                 ImGuiWindowFlags_NoFocusOnAppearing);

    ImDrawList *dl = ImGui::GetWindowDrawList();

    // Input neurons
    ImGui::Text("Inputs");
    ImVec2 cursor = ImGui::GetCursorScreenPos();
    float ss = 14.0f;
    for (size_t j = 0; j < BRAIN_INPUT_SIZE; j++) {
        float col = sel->in[j];
        ImU32 c;
        if (j < 18)      c = IM_COL32((int)(col*255), (int)(col*255), (int)(col*255), 255);
        else if (j < 19) c = IM_COL32(0, (int)(col*255), (int)(col*255), 255);
        else             c = IM_COL32(0, (int)(col*255), 0, 255);
        dl->AddRectFilled(ImVec2(cursor.x + ss*j, cursor.y),
                          ImVec2(cursor.x + ss*j + ss - 1, cursor.y + 13), c);
    }
    ImGui::Dummy(ImVec2(ss * BRAIN_INPUT_SIZE, 14));

    // Output neurons
    ImGui::Text("Outputs");
    cursor = ImGui::GetCursorScreenPos();
    for (size_t j = 0; j < BRAIN_OUTPUT_SIZE; j++) {
        float col = sel->out[j];
        dl->AddRectFilled(ImVec2(cursor.x + ss*j, cursor.y),
                          ImVec2(cursor.x + ss*j + ss - 1, cursor.y + 13),
                          IM_COL32((int)(col*255), (int)(col*255), (int)(col*255), 255));
    }
    ImGui::Dummy(ImVec2(ss * BRAIN_OUTPUT_SIZE, 14));

    // Brain weights summary (GPU doesn't expose layer activations)
    ImGui::Text("Brain Weights (GPU)");
    ImGui::Text("  %d params, %.1f KB (fp16)", BRAIN_WEIGHT_FLOATS,
                (float)BRAIN_WEIGHT_UINTS * 4.0f / 1024.0f);
    float wmin = 1e10f, wmax = -1e10f;
    for (int i = 0; i < BRAIN_WEIGHT_UINTS; i++) {
        uint32_t pair = sel->brain[i];
        float flo = half_to_float((uint16_t)(pair & 0xffffu));
        float fhi = half_to_float((uint16_t)(pair >> 16));
        if (flo < wmin) wmin = flo;
        if (flo > wmax) wmax = flo;
        if (fhi < wmin) wmin = fhi;
        if (fhi > wmax) wmax = fhi;
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
        if (sc_x < 0.001f) sc_x = 1.0f;
        if (sc_y < 0.001f) sc_y = 1.0f;

        for (size_t i = 0; i < w->agents.size; i++) {
            struct Agent *a = w->agents.agents[i];
            float sx_fb = (a->pos.x + view->xtranslate) * view->scalemult + fb_w/2.0f;
            float sy_fb = fb_h/2.0f - (a->pos.y + view->ytranslate) * view->scalemult;
            if (sx_fb < -50 || sx_fb > fb_w+50 || sy_fb < -50 || sy_fb > fb_h+50) continue;

            char tmp[64];
            float bx_fb = sx_fb - BOTRADIUS * 2 * view->scalemult;
            float by_fb = sy_fb + 5 + BOTRADIUS * 2 * view->scalemult;
            float bx = bx_fb / sc_x;
            float by = by_fb / sc_y;
            ImU32 white = IM_COL32_WHITE;
            snprintf(tmp, sizeof(tmp), "%jd", (intmax_t)a->gencount);
            fg->AddText(ImVec2(bx, by), white, tmp); by += 12;
            snprintf(tmp, sizeof(tmp), "%d", a->age);
            fg->AddText(ImVec2(bx, by), white, tmp); by += 12;
            snprintf(tmp, sizeof(tmp), "%.2f", a->health);
            fg->AddText(ImVec2(bx, by), white, tmp); by += 12;
            snprintf(tmp, sizeof(tmp), "%.2f", a->repcounter);
            fg->AddText(ImVec2(bx, by), white, tmp);
        }
    }
}

void imgui_draw_diagnostics(struct VKView *view) {
    struct World *w = view->base->world;
    if (!w || !view->show_diag_window) return;

    ImGui::SetNextWindowPos(ImVec2((float)view->wwidth - 290, 0), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(280, 400), ImGuiCond_FirstUseEver);
    ImGui::Begin("Simulation", &view->show_diag_window);

    // ---- Performance ----
    if (ImGui::CollapsingHeader("Performance", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Text("FPS:    %6.1f  (%.1f ms)", view->smoothFPS, view->smoothFrameMs);
        if (view->minFrameMs > 0.0f && view->maxFrameMs > 0.0f)
            ImGui::Text("Range:  %5.1f - %.1f ms", view->minFrameMs, view->maxFrameMs);

        bool limit = (view->max_fps > 0);
        if (ImGui::Checkbox("Limit FPS", &limit))
            view->max_fps = limit ? 60 : 0;
        if (limit) {
            ImGui::SameLine();
            ImGui::PushItemWidth(50);
            int fps = view->max_fps;
            if (ImGui::InputInt("##maxfps", &fps, 0, 0, ImGuiInputTextFlags_EnterReturnsTrue)) {
                if (fps < 10) fps = 10;
                view->max_fps = fps;
            }
            ImGui::PopItemWidth();
        }

        ImGui::Separator();
        if (view->vkstate && view->vkstate->timestamp_supported)
            ImGui::Text("GPU:   compute %5.2f ms  draw %5.2f ms",
                        view->vkstate->gpu_compute_ms,
                        view->vkstate->gpu_graphics_ms);

        ImGui::Text("Upload: agents %5.2f ms  food %5.2f ms",
                    view->time_agent_upload, view->time_food_upload);
        ImGui::Separator();

        // ---- Frame Timing (hierarchical) ----
        // Each time_* is already a delta (timer_elapsed_ms resets the clock)
        double d_food    = w->time_food;
        double d_sort    = w->time_sort;
        double d_submit  = w->time_submit;
        double d_inputs  = w->time_inputs;
        double d_gpuwait = w->time_compute;
        double d_outputs = w->time_outputs;
        double d_post    = w->time_post_out;
        double d_staging = w->time_staging;
        double d_record  = w->time_record;
        double d_tail    = w->time_total_frame;
        double d_total   = d_food + d_sort + d_submit + d_inputs + d_gpuwait
                          + d_outputs + d_post + d_staging + d_record + d_tail;

        char ftlbl[96];
        snprintf(ftlbl, sizeof(ftlbl), "Frame Timing (Total: %.1f ms)###fttotal", d_total);
        ImGui::SetNextItemOpen(true, ImGuiCond_Once);
        if (ImGui::CollapsingHeader(ftlbl)) {

            // ---- Simulation Step ----
            double d_sim = d_food + d_sort + d_submit + d_inputs + d_gpuwait + d_outputs + d_post;
            snprintf(ftlbl, sizeof(ftlbl), "Sim Step:  %.1f ms (%.0f%%)###ftsim",
                     d_sim, d_total > 0.001 ? d_sim / d_total * 100.0 : 0.0);
            ImGui::SetNextItemOpen(true, ImGuiCond_Once);
            if (ImGui::TreeNodeEx(ftlbl)) {
                ImGui::Text("  Food Update:      %6.1f ms", d_food);
                ImGui::Text("  Spatial Sort:     %6.1f ms", d_sort);
                ImGui::Text("  Submit Compute:   %6.1f ms", d_submit);
                ImGui::Text("  Set Inputs:       %6.1f ms", d_inputs);
                ImGui::Text("  GPU Wait:         %6.1f ms", d_gpuwait);
                ImGui::Text("  Process Outputs:  %6.1f ms", d_outputs);
                ImGui::Text("  Death+Repro+Pop:  %6.1f ms", d_post);
                ImGui::TreePop();
            }

            // ---- Bookkeeping ----
            double d_book = d_staging + d_record;
            snprintf(ftlbl, sizeof(ftlbl), "Bookkeeping:  %.1f ms (%.0f%%)###ftbook",
                     d_book, d_total > 0.001 ? d_book / d_total * 100.0 : 0.0);
            ImGui::SetNextItemOpen(true, ImGuiCond_Once);
            if (ImGui::TreeNodeEx(ftlbl)) {
                ImGui::Text("  Flush Staging:    %6.1f ms", d_staging);
                ImGui::Text("  Record Dispatch:  %6.1f ms", d_record);
                ImGui::TreePop();
            }
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
        if (ImGui::Checkbox("Pause", &paused))  view->paused = paused ? 1 : 0;

        bool df = view->drawfood;
        if (ImGui::Checkbox("Draw food", &df))   view->drawfood = df ? 1 : 0;
        ImGui::SameLine();
        bool dt = view->draw_text;
        if (ImGui::Checkbox("Draw text", &dt))   view->draw_text = dt ? 1 : 0;

        bool cl = w->closed;
        if (ImGui::Checkbox("Closed env", &cl))  w->closed = cl ? 1 : 0;
        ImGui::SameLine();
        bool mm = w->movieMode;
        if (ImGui::Checkbox("Movie mode", &mm))  w->movieMode = mm ? 1 : 0;

        ImGui::SeparatorText("Spawn");
        ImGui::PushItemWidth(60);
        static int spawn_count = 100;
        if (spawn_count < 1) spawn_count = 1;
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
