// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kyr4l
#include "ui/Ui.hpp"
#include "ui/UiCommon.hpp"

#include <algorithm>
#include <cmath>

#include <imgui.h>

#include "util/Log.hpp"

namespace lr::app {

using namespace uidetail;

// ---------------------------------------------------------------- Axis calibration (the gates of a lever)

// The track of a lever with the zones the calibration makes of it (cut-off red, idle blue, dry range green, afterburner orange),
// a line at each gate and the live position of the lever. `gateSet` says which gates were pointed out yet (the wizard); without
// it all four count.
void Ui::drawGateBar(const AxisCalibration& cal, float now, float width, const bool* gateSet) {
    const float fs = ImGui::GetFontSize(), h = fs * 1.5f;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    dl->AddRectFilled(p0, ImVec2(p0.x + width, p0.y + h), ImGui::GetColorU32(ImVec4(0.14f, 0.15f, 0.18f, 1.0f)), 3.0f);
    const bool zones = cal.valid() && (!gateSet || (gateSet[0] && gateSet[1] && gateSet[2] && gateSet[3]));
    if (zones) {
        const AxisDetents probe;  // only the zones matter here
        const int columns = static_cast<int>(width);
        for (int x = 0; x < columns; ++x) {
            const double raw = (x + 0.5) / columns;
            const auto r = Engine::detentOutput(probe, cal, raw, AxisZone::Dry);
            ImVec4 col = ImVec4(0.18f, 0.40f, 0.24f, 1.0f);  // dry range
            if (r.zone == AxisZone::Cutoff) col = ImVec4(0.55f, 0.20f, 0.20f, 1.0f);
            else if (r.zone == AxisZone::Afterburner) col = ImVec4(0.75f, 0.45f, 0.12f, 1.0f);
            else if (cal.idleBtn < 0 && std::fabs(raw - cal.idle) <= cal.snap) col = ImVec4(0.20f, 0.35f, 0.65f, 1.0f);
            dl->AddRectFilled(ImVec2(p0.x + x, p0.y), ImVec2(p0.x + x + 1, p0.y + h), ImGui::GetColorU32(col));
        }
    }
    const char* names[4] = {"cut-off", "idle", "dry end", "afterburner"};
    const double at[4] = {cal.cutoff, cal.idle, cal.mil, cal.ab};
    for (int i = 0; i < 4; ++i) {
        if (gateSet && !gateSet[i]) continue;
        const float x = p0.x + static_cast<float>(std::clamp(at[i], 0.0, 1.0)) * width;
        dl->AddLine(ImVec2(x, p0.y), ImVec2(x, p0.y + h + fs * 0.25f), ImGui::GetColorU32(ImVec4(1, 1, 1, 0.9f)), 1.5f);
        const float lw = ImGui::CalcTextSize(names[i]).x;
        dl->AddText(ImVec2(std::clamp(x - lw * 0.5f, p0.x, p0.x + width - lw), p0.y + h + fs * 0.2f + (i % 2) * fs * 1.05f), ImGui::GetColorU32(ImGuiCol_TextDisabled), names[i]);  // two rows: close gates do not overlap
    }
    if (now >= 0) {
        const float x = p0.x + std::clamp(now, 0.0f, 1.0f) * width;
        dl->AddLine(ImVec2(x, p0.y - 2), ImVec2(x, p0.y + h + 2), ImGui::GetColorU32(ImVec4(1.0f, 0.85f, 0.2f, 1.0f)), 3.0f);
    }
    ImGui::Dummy(ImVec2(width, h + fs * 2.4f));
}

Engine::GateState Ui::gateStateNow(const AxisCalibration& cal) const {
    auto closed = [&](int code) { return code >= 0 && inputIsPressed({cal.device, input::InputKind::Button, code, 0}); };
    Engine::GateState g;
    g.cutoff = closed(cal.cutoffBtn);
    g.idle = closed(cal.idleBtn);
    g.mil = closed(cal.milBtn);
    g.ab = closed(cal.abBtn);
    return g;
}

void Ui::openCalibration(const input::InputId& axis) {
    if (calOpen_) return;
    calAxis_ = axis;
    const AxisCalibration* current = ctx_.settings.calibrationFor(axis);
    calDraft_ = current ? *current : AxisCalibration{};
    calDraft_.device = axis.device;
    calDraft_.code = axis.code;
    for (bool& s : calSet_) s = current != nullptr;
    calListening_ = -1;
    calMessage_.clear();
    calOpen_ = true;
    calJustOpened_ = true;
    calWasEnabled_ = ctx_.engine.enabled();
    ctx_.engine.setEnabled(false);  // the lever must not move the simulator while it is being taught
    ctx_.engine.beginGateLearning(calDraft_);
    calSig_ = calDraft_.cutoffBtn * 3 + calDraft_.idleBtn * 5 + calDraft_.milBtn * 7 + calDraft_.abBtn * 11;
}

void Ui::drawCalibrationWizard() {
    if (!calOpen_) return;
    if (calJustOpened_) { ImGui::OpenPopup("Calibrate axis"); calJustOpened_ = false; }
    bool open = true;
    bool close = false;
    const float fs = ImGui::GetFontSize();
    ImGui::SetNextWindowSize(ImVec2(fs * 52, 0), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("Calibrate axis", &open, ImGuiWindowFlags_NoSavedSettings)) {
        const float now = axisNow(calAxis_);
        int* buttons[4] = {&calDraft_.cutoffBtn, &calDraft_.idleBtn, &calDraft_.milBtn, &calDraft_.abBtn};
        double* positions[4] = {&calDraft_.cutoff, &calDraft_.idle, &calDraft_.mil, &calDraft_.ab};

        // a button heard while listening, and the positions the engine noted when the gate buttons moved
        if (calListening_ >= 0) {
            if (auto heard = ctx_.engine.takeCaptured()) {
                if (heard->kind == input::InputKind::Button && heard->device == calAxis_.device) {
                    *buttons[calListening_] = heard->code;
                    calMessage_.clear();
                    calListening_ = -1;
                } else {
                    calMessage_ = "That button is not on this device.";
                    ctx_.engine.beginCapture(true);
                }
            }
        }
        const int sig = calDraft_.cutoffBtn * 3 + calDraft_.idleBtn * 5 + calDraft_.milBtn * 7 + calDraft_.abBtn * 11;
        if (sig != calSig_) { calSig_ = sig; ctx_.engine.beginGateLearning(calDraft_); }
        const auto learned = ctx_.engine.takeGateLearned();
        for (int g = 0; g < 4; ++g)
            if (learned[static_cast<size_t>(g)] && *buttons[g] >= 0) { *positions[g] = *learned[static_cast<size_t>(g)]; calSet_[g] = true; }

        ImGui::Text("%s on %s", ctx_.input.describe(calAxis_).c_str(), deviceDisplayName(calAxis_.device).c_str());
        ImGui::TextDisabled("Bindings are paused while this window is open.");
        ImGui::PushTextWrapPos(fs * 50);
        ImGui::TextUnformatted("Many throttles have a switch at each gate. Press Listen for a gate and move the lever into it: the program notices which "
                               "button closes, and from then on that button decides, exactly, when the lever is in the gate, however close "
                               "the gates are. Then move the lever up through the whole travel once so it learns where the idle gate ends and "
                               "the dry range stops. A gate without a button is judged from the position of the lever.");
        ImGui::PopTextWrapPos();
        ImGui::Spacing();
        if (now < 0) ImGui::TextColored(kYellow, "The device is not connected.");
        drawGateBar(calDraft_, now, fs * 46, calSet_);

        struct Gate { const char* name; const char* position; const char* hint; };
        const Gate gates[4] = {
            {"Cut-off", "Position of the cut-off stop", "Lift the lever over the idle gate and pull it fully back."},
            {"Idle", "Where the lever leaves idle", "Moving the lever up out of the idle gate sets it (or put the lever just above idle and press Set)."},
            {"End of the dry range", "Where the dry range ends", "Moving the lever to the end of the dry range, just before the afterburner gate, sets it (or press Set there)."},
            {"Afterburner", "Position of the afterburner end", "Through the afterburner gate, to the end of the travel."},
        };
        for (int g = 0; g < 4; ++g) {
            ImGui::PushID(g);
            ImGui::Separator();
            const bool hasButton = *buttons[g] >= 0;
            ImGui::AlignTextToFramePadding();
            ImGui::Text("%s", gates[g].name);
            ImGui::SameLine(fs * 14);
            ImGui::TextUnformatted("Button:");
            ImGui::SameLine();
            if (hasButton) {
                ImGui::Text("%s", ctx_.input.describe({calAxis_.device, input::InputKind::Button, *buttons[g], 0}).c_str());
                const bool closed = inputIsPressed({calAxis_.device, input::InputKind::Button, *buttons[g], 0});
                ImGui::SameLine();
                dot(closed ? kGreen : kGrey);
                ImGui::TextDisabled("%s", closed ? "closed" : "open");
            } else {
                ImGui::TextDisabled("none (judged from the position)");
            }
            ImGui::SameLine(fs * 38);
            const bool listening = calListening_ == g;
            if (ImGui::SmallButton(listening ? "Stop listening" : "Listen")) {
                if (listening) { calListening_ = -1; ctx_.engine.cancelCapture(); }
                else { calListening_ = g; calMessage_.clear(); ctx_.engine.beginCapture(true); }
            }
            if (hasButton) {
                ImGui::SameLine();
                if (ImGui::SmallButton("Clear")) *buttons[g] = -1;
            }
            // by number, as the Devices tab shows it
            ImGui::SameLine();
            ImGui::SetNextItemWidth(fs * 5);
            int number = 0;
            for (const auto& d : devices_)
                if (d.key == calAxis_.device && hasButton) {
                    const auto it = std::find(d.buttonCodes.begin(), d.buttonCodes.end(), *buttons[g]);
                    if (it != d.buttonCodes.end()) number = static_cast<int>(it - d.buttonCodes.begin()) + 1;
                }
            if (ImGui::InputInt("##number", &number, 0, 0)) {
                for (const auto& d : devices_)
                    if (d.key == calAxis_.device) *buttons[g] = number >= 1 && number <= static_cast<int>(d.buttonCodes.size()) ? d.buttonCodes[static_cast<size_t>(number - 1)] : -1;
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("The number of the button as the Devices tab shows it (0 = none).");
            if (listening) ImGui::TextColored(kYellow, "    Move the lever into the %s gate...", gates[g].name);

            const bool needsPosition = g == 1 || g == 2 || !hasButton;
            if (needsPosition) {
                ImGui::TextDisabled("    %s", gates[g].position);
                ImGui::SameLine();
                float pct = static_cast<float>(*positions[g] * 100.0);
                ImGui::SetNextItemWidth(fs * 7);
                if (ImGui::DragFloat("##pct", &pct, 0.1f, 0.0f, 100.0f, "%.1f %%")) { *positions[g] = pct / 100.0; calSet_[g] = true; }
                ImGui::SameLine();
                ImGui::BeginDisabled(now < 0);
                if (ImGui::SmallButton("Set from the lever")) { *positions[g] = now; calSet_[g] = true; }
                ImGui::EndDisabled();
                ImGui::SameLine();
                if (calSet_[g]) ImGui::TextColored(kGreen, "set"); else ImGui::TextDisabled("not set yet");
                ImGui::PushTextWrapPos(fs * 50);
                ImGui::TextDisabled("    %s", gates[g].hint);
                ImGui::PopTextWrapPos();
            }
            ImGui::PopID();
        }
        ImGui::Separator();
        if (calDraft_.idleBtn < 0) {
            float snapPct = static_cast<float>(calDraft_.snap * 100.0);
            ImGui::SetNextItemWidth(fs * 10);
            if (ImGui::SliderFloat("Idle band (% of travel, each side)", &snapPct, 0.0f, 5.0f, "%.1f")) calDraft_.snap = snapPct / 100.0;
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Inside this band around the idle gate the value stays exactly at idle, so a lever resting in its detent never wobbles.");
        }

        const bool allSet = (calDraft_.cutoffBtn >= 0 || calSet_[0]) && calSet_[1] && calSet_[2] && (calDraft_.abBtn >= 0 || calSet_[3]);
        const bool valid = calDraft_.valid();
        if (!calMessage_.empty()) ImGui::TextColored(kYellow, "%s", calMessage_.c_str());
        if (!allSet) ImGui::TextDisabled("Set the positions above (or move the lever through its travel) to save.");
        else if (!valid) {
            ImGui::PushTextWrapPos(fs * 50);
            ImGui::TextColored(kYellow, "The positions must be in order (idle, then the end of the dry range, with at least 5%% between them; a gate without a button must lie beyond them, in its own direction).");
            ImGui::PopTextWrapPos();
        } else ImGui::TextColored(kGreen, "Calibration looks good.");
        ImGui::Spacing();
        ImGui::BeginDisabled(!(allSet && valid));
        if (ImGui::Button("Save", ImVec2(fs * 9, 0))) {
            ctx_.settings.setCalibration(calDraft_);
            ctx_.markDirty();
            close = true;
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(fs * 9, 0))) close = true;
        if (ctx_.settings.calibrationFor(calAxis_)) {
            ImGui::SameLine();
            if (ImGui::Button("Remove the calibration")) {
                ctx_.settings.clearCalibration(calAxis_);
                ctx_.markDirty();
                close = true;
            }
        }
        if (close) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    if (!open || close) {
        calOpen_ = false;
        calListening_ = -1;
        ctx_.engine.cancelCapture();
        ctx_.engine.endGateLearning();
        ctx_.engine.setEnabled(calWasEnabled_);
    }
}

}  // namespace lr::app
