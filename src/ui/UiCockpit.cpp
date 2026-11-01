// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kyr4l
#include "ui/Ui.hpp"
#include "ui/UiCommon.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include <imgui.h>

namespace lr::app {

using namespace uidetail;

// ---------------------------------------------------------------- Cockpit

static std::string formatChange(double v) {
    char buf[32];
    snprintf(buf, sizeof buf, "%.6g", v);
    return buf;
}

static std::string ago(std::chrono::steady_clock::duration d) {
    const double s = std::chrono::duration<double>(d).count();
    char buf[32];
    if (s < 1.5) snprintf(buf, sizeof buf, "now");
    else if (s < 90) snprintf(buf, sizeof buf, "%.0f s ago", s);
    else snprintf(buf, sizeof buf, "%.0f min ago", s / 60);
    return buf;
}

void Ui::stopCockpitWatch() {
    cockpit_.stop();
    ctx_.bridge.setInputTrace(false);
}

// "Which input is this control?": the cockpit is clicked with the mouse in the simulator, and the input events whose value changed are listed here.
void Ui::drawCockpitTab() {
    cockpitDrawn_ = true;
    const auto now = std::chrono::steady_clock::now();
    const bool connected = status_.state == sim::ConnState::Connected;

    if (cockpit_.running()) {
        for (const auto& c : ctx_.bridge.inputChanges(cockpitSerial_)) {
            cockpit_.add(c);
            cockpitSerial_ = std::max(cockpitSerial_, c.serial);
        }
    }

    ImGui::TextDisabled("Click a control in the sim to see its input event.");

    if (!cockpit_.running()) {
        ImGui::BeginDisabled(!connected);
        if (ImGui::Button("Start")) {
            cockpit_.start(now);
            cockpitSerial_ = 0;
            ctx_.bridge.clearInputChanges();
            ctx_.bridge.setInputTrace(true);
        }
        ImGui::EndDisabled();
        if (!connected) {
            ImGui::SameLine();
            ImGui::TextColored(kYellow, "Not connected");
        }
    } else {
        if (ImGui::Button("Stop")) stopCockpitWatch();
        ImGui::SameLine();
        if (ImGui::Button("Clear")) cockpit_.clear();
        ImGui::SameLine();
        const size_t noisy = cockpit_.noisyCount();
        ImGui::BeginDisabled(noisy == 0);
        ImGui::Checkbox(("Show noisy (" + std::to_string(noisy) + ")").c_str(), &cockpitShowNoisy_);
        ImGui::EndDisabled();
    }

    if (cockpit_.running()) {
        const sim::TraceStatus ts = ctx_.bridge.traceStatus();
        if (cockpit_.learning(now)) {
            ImGui::TextColored(kYellow, "Don't touch the cockpit (%.0f s)", cockpit_.learnSecondsLeft(now) + 0.5);
        } else {
            ImGui::TextColored(kGreen, "Watching %zu events", ts.events);
        }
        ImGui::TextDisabled("Push: %llu  Poll: %llu%s", static_cast<unsigned long long>(ts.pushed), static_cast<unsigned long long>(ts.polled), ts.refused ? "  (push refused)" : "");
    }

    const auto rows = cockpit_.rows(cockpitShowNoisy_);
    if (!cockpit_.running() && rows.empty()) {
        ImGui::Spacing();
        ImGui::TextDisabled("Press Start, then click a control in the sim.");
        return;
    }
    if (rows.empty() && !cockpit_.learning(now)) ImGui::TextDisabled("No changes yet");

    if (ImGui::BeginTable("cockpit_changes", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp, ImVec2(0, 0))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Event", ImGuiTableColumnFlags_WidthStretch, 2.5f);
        ImGui::TableSetupColumn("Change", ImGuiTableColumnFlags_WidthStretch, 1.2f);
        ImGui::TableSetupColumn("When", ImGuiTableColumnFlags_WidthStretch, 0.9f);
        ImGui::TableSetupColumn("##actions", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 22);
        ImGui::TableHeadersRow();
        int shown = 0;
        for (const auto& r : rows) {
            if (++shown > 100) break;
            ImGui::PushID(r.name.c_str());
            ImGui::TableNextRow();
            if (now - r.last < std::chrono::milliseconds(1500)) ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, ImGui::GetColorU32(ImVec4(0.2f, 0.45f, 0.2f, 0.6f)));
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(r.name.c_str());
            if (r.noisy) { ImGui::SameLine(); ImGui::TextDisabled("(noisy)"); }
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%s  ->  %s", formatChange(r.before).c_str(), formatChange(r.now).c_str());
            if (r.changes > 1) { ImGui::SameLine(); ImGui::TextDisabled("x%zu", r.changes); }
            ImGui::TableSetColumnIndex(2);
            ImGui::TextUnformatted(ago(now - r.last).c_str());
            ImGui::TableSetColumnIndex(3);
            if (ImGui::SmallButton("Bind")) {
                Action a;
                a.kind = ActionKind::InputEvent;
                a.name = r.name;
                a.value = r.now;
                openEditorWith({a}, r.name);
            }
            // a switch with whole positions 0..N-1 can also be flipped through them with one button
            const bool whole = r.lowest == 0 && r.highest >= 1 && r.highest <= 12 && r.highest == std::round(r.highest);
            if (whole) {
                ImGui::SameLine();
                if (ImGui::SmallButton("Toggle")) {
                    Action a;
                    a.kind = ActionKind::InputEventToggle;
                    a.name = r.name;
                    a.value = r.highest + 1;
                    openEditorWith({a}, r.name);
                }
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Copy")) ImGui::SetClipboardText(r.name.c_str());
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
}

}  // namespace lr::app
