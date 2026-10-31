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
            else if (std::fabs(raw - cal.idle) <= cal.snap) col = ImVec4(0.20f, 0.35f, 0.65f, 1.0f);
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

void Ui::openCalibration(const input::InputId& axis) {
    if (calOpen_) return;
    calAxis_ = axis;
    const AxisCalibration* current = ctx_.settings.calibrationFor(axis);
    calDraft_ = current ? *current : AxisCalibration{};
    calDraft_.device = axis.device;
    calDraft_.code = axis.code;
    for (bool& s : calSet_) s = current != nullptr;
    calOpen_ = true;
    calJustOpened_ = true;
    calWasEnabled_ = ctx_.engine.enabled();
    ctx_.engine.setEnabled(false);  // the lever must not move the simulator while it is being taught
}

void Ui::drawCalibrationWizard() {
    if (!calOpen_) return;
    if (calJustOpened_) { ImGui::OpenPopup("Calibrate axis"); calJustOpened_ = false; }
    bool open = true;
    bool close = false;
    const float fs = ImGui::GetFontSize();
    ImGui::SetNextWindowSize(ImVec2(fs * 46, 0), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("Calibrate axis", &open, ImGuiWindowFlags_NoSavedSettings)) {
        const float now = axisNow(calAxis_);
        ImGui::Text("%s on %s", ctx_.input.describe(calAxis_).c_str(), deviceDisplayName(calAxis_.device).c_str());
        ImGui::TextDisabled("Bindings are paused while this window is open.");
        ImGui::PushTextWrapPos(fs * 44);
        ImGui::TextUnformatted("Move the lever to each of its four positions, one after the other, and press the button of that position. "
                               "The program learns where the gates are on the axis, so the value sent to the simulator can follow them.");
        ImGui::PopTextWrapPos();
        ImGui::Spacing();
        if (now < 0) ImGui::TextColored(kYellow, "The device is not connected.");
        drawGateBar(calDraft_, now, fs * 40, calSet_);

        struct Gate { const char* button; const char* hint; double* value; };
        const Gate gates[4] = {
            {"Cut-off", "Lift the lever over the idle gate and pull it fully back.", &calDraft_.cutoff},
            {"Idle", "The idle detent: where the lever rests when you pull it back.", &calDraft_.idle},
            {"End of the dry range", "Fully forward, just before the afterburner gate (the last point before it clicks).", &calDraft_.mil},
            {"Afterburner", "Through the afterburner gate, to the end of the travel.", &calDraft_.ab},
        };
        for (int i = 0; i < 4; ++i) {
            ImGui::PushID(i);
            ImGui::BeginDisabled(now < 0);
            if (ImGui::Button(gates[i].button, ImVec2(fs * 12, 0))) { *gates[i].value = now; calSet_[i] = true; }
            ImGui::EndDisabled();
            ImGui::SameLine();
            float pct = static_cast<float>(*gates[i].value * 100.0);
            ImGui::SetNextItemWidth(fs * 7);
            if (ImGui::DragFloat("##pct", &pct, 0.1f, 0.0f, 100.0f, "%.1f %%")) { *gates[i].value = pct / 100.0; calSet_[i] = true; }
            ImGui::SameLine();
            if (calSet_[i]) ImGui::TextColored(kGreen, "set"); else ImGui::TextDisabled("not set yet");
            ImGui::PushTextWrapPos(fs * 44);
            ImGui::TextDisabled("    %s", gates[i].hint);
            ImGui::PopTextWrapPos();
            ImGui::PopID();
        }
        float snapPct = static_cast<float>(calDraft_.snap * 100.0);
        ImGui::SetNextItemWidth(fs * 10);
        if (ImGui::SliderFloat("Idle band (% of travel, each side)", &snapPct, 0.0f, 5.0f, "%.1f")) calDraft_.snap = snapPct / 100.0;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Inside this band around the idle gate the value stays exactly at idle, so a lever resting in its detent never wobbles.");

        const bool allSet = calSet_[0] && calSet_[1] && calSet_[2] && calSet_[3];
        const bool valid = calDraft_.valid();
        if (!allSet) ImGui::TextDisabled("Set all four positions to save.");
        else if (!valid) {
            ImGui::PushTextWrapPos(fs * 44);
            ImGui::TextColored(kYellow, "The gates must be in order (cut-off, idle, end of the dry range, afterburner; or the reverse), with at least 10%% between idle and the end of the dry range.");
            ImGui::PopTextWrapPos();
        }
        else ImGui::TextColored(kGreen, "Calibration looks good.");
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
        ctx_.engine.setEnabled(calWasEnabled_);
    }
}

}  // namespace lr::app
