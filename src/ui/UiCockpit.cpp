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

    ImGui::TextWrapped("Click or turn a control in the cockpit with the mouse, in the simulator: the cockpit input events (B:) of the loaded aircraft whose value changes are "
                       "listed here, the latest first, ready to be bound to a button. Watching stops when you leave this tab.");

    if (!cockpit_.running()) {
        ImGui::BeginDisabled(!connected);
        if (ImGui::Button("Start watching")) {
            cockpit_.start(now);
            cockpitSerial_ = 0;
            ctx_.bridge.clearInputChanges();
            ctx_.bridge.setInputTrace(true);
        }
        ImGui::EndDisabled();
        if (!connected) {
            ImGui::SameLine();
            ImGui::TextColored(kYellow, "The simulator is not connected.");
        }
    } else {
        if (ImGui::Button("Stop watching")) stopCockpitWatch();
        ImGui::SameLine();
        if (ImGui::Button("Clear the list")) cockpit_.clear();
        ImGui::SameLine();
        const size_t noisy = cockpit_.noisyCount();
        ImGui::BeginDisabled(noisy == 0);
        ImGui::Checkbox(("Also the " + std::to_string(noisy) + " that change by themselves").c_str(), &cockpitShowNoisy_);
        ImGui::EndDisabled();
    }

    if (cockpit_.running()) {
        const sim::TraceStatus ts = ctx_.bridge.traceStatus();
        if (cockpit_.learning(now)) {
            ImGui::TextColored(kYellow, "Keep your hands off the cockpit for %.0f s: learning which controls change by themselves (clocks, needles).", cockpit_.learnSecondsLeft(now) + 0.5);
        } else {
            ImGui::TextColored(kGreen, "Watching %zu input events: click or turn a control now.", ts.events);
        }
        ImGui::TextDisabled("%s  |  changes announced by the sim: %llu, found by reading the values back: %llu", ts.refused ? "the sim refused the subscription: reading back only" : ts.subscribed ? "subscribed to the sim's announcements" : "not subscribed yet",
                            static_cast<unsigned long long>(ts.pushed), static_cast<unsigned long long>(ts.polled));
    }

    const auto rows = cockpit_.rows(cockpitShowNoisy_);
    if (!cockpit_.running() && rows.empty()) {
        ImGui::Spacing();
        ImGui::TextDisabled("Press Start watching, wait three seconds, then click a control in the simulator.");
        return;
    }
    if (rows.empty() && !cockpit_.learning(now)) ImGui::TextDisabled("Nothing has changed yet.");

    if (ImGui::BeginTable("cockpit_changes", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp, ImVec2(0, 0))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Input event", ImGuiTableColumnFlags_WidthStretch, 2.5f);
        ImGui::TableSetupColumn("Changed from / to", ImGuiTableColumnFlags_WidthStretch, 1.2f);
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
            if (r.noisy) { ImGui::SameLine(); ImGui::TextDisabled("(changes by itself)"); }
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%s  ->  %s", formatChange(r.before).c_str(), formatChange(r.now).c_str());
            if (r.changes > 1) { ImGui::SameLine(); ImGui::TextDisabled("x%zu", r.changes); }
            ImGui::TableSetColumnIndex(2);
            ImGui::TextUnformatted(ago(now - r.last).c_str());
            ImGui::TableSetColumnIndex(3);
            if (ImGui::SmallButton("Bind to a button")) {
                Action a;
                a.kind = ActionKind::InputEvent;
                a.name = r.name;
                a.value = r.now;
                openEditorWith({a}, r.name);
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("A new binding that sets %s to %s; then press the button it is for.", r.name.c_str(), formatChange(r.now).c_str());
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
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("A new binding that steps %s through its %d positions (0 to %d) at each press.", r.name.c_str(), static_cast<int>(r.highest) + 1, static_cast<int>(r.highest));
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Copy name")) ImGui::SetClipboardText(r.name.c_str());
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
}

}  // namespace lr::app
