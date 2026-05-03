// imgui_hud.cpp — ImGui overlays: agent inspector + diagnostics
#include "vkview.h"
#include "World.h"
#include "Agent.h"
#include "settings.h"

#include "imgui.h"
#include <stdio.h>
#include <math.h>

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
                 ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize);

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
    ImGui::Text("  %d params, %.1f KB", BRAIN_WEIGHT_FLOATS,
                BRAIN_WEIGHT_FLOATS * 4.0f / 1024.0f);
    float wmin = 1e10f, wmax = -1e10f;
    for (int i = 0; i < BRAIN_WEIGHT_FLOATS; i++) {
        if (sel->brain[i] < wmin) wmin = sel->brain[i];
        if (sel->brain[i] > wmax) wmax = sel->brain[i];
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
    if (!w) return;

    ImGui::SetNextWindowPos(ImVec2((float)view->wwidth - 260, 0), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.5f);
    ImGui::Begin("##diag", NULL,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs |
                 ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing);

    ImGui::Text("FPS:    %6.1f  (%.1f ms)", view->smoothFPS, view->smoothFrameMs);
    if (view->minFrameMs > 0.0f && view->maxFrameMs > 0.0f)
        ImGui::Text("Frame:  %5.1f - %.1f ms", view->minFrameMs, view->maxFrameMs);
    ImGui::Separator();
    ImGui::Text("Agents:     %5zu", w->agents.size);
    ImGui::Text("Food:       %5.2f", world_getTotalFood(w));
    ImGui::Text("Herbivores: %5d", world_numHerbivores(w));
    ImGui::Text("Carnivores: %5d", world_numCarnivores(w));
    ImGui::Text("Epoch:      %5d", w->current_epoch);
    ImGui::Text("Zoom:       %5.2fx", view->scalemult);
    ImGui::Text("Frames:     %5d", view->totalFrames);
    ImGui::Separator();
    ImGui::Text("Frame timing:");
    ImGui::Text("  Sort:     %6.1f ms", w->time_sort);
    ImGui::Text("  Inputs:   %6.1f ms", w->time_inputs);
    ImGui::Text("  GPU wait: %6.1f ms", w->time_compute);
    ImGui::Text("  Outputs:  %6.1f ms", w->time_outputs);
    ImGui::Text("  Flush:    %6.1f ms", w->time_flush);
    ImGui::Text("  Stage:    %6.1f ms", w->time_staging);
    ImGui::Text("  Record:   %6.1f ms", w->time_record);
    ImGui::Text("  Total:    %6.1f ms", w->time_total_frame);
    ImGui::Text("  Agent #:  %5zu", w->agents.size);

    ImGui::End();
}
