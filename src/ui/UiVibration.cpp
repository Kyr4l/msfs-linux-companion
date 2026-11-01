// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kyr4l
#include "ui/Ui.hpp"
#include "ui/UiCommon.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <ctime>
#include <filesystem>

#include <imgui.h>
#include <imgui_stdlib.h>

#include "app/Paths.hpp"
#include "util/Log.hpp"

namespace lr::app {

using namespace uidetail;

// ---------------------------------------------------------------- Vibration

void Ui::drawVibrationTab() {
    haptics::HapticsConfig& cfg = ctx_.settings.haptics;
    const haptics::HapticsStatus hs = ctx_.haptics.status();
    bool changed = false;

    ImGui::TextDisabled("Vibration for Winwing devices, driven by flight data.");
    changed |= ImGui::Checkbox("Enabled", &cfg.enabled);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(200);
    changed |= ImGui::SliderFloat("Strength", &cfg.master, 0.0f, 1.5f, "%.2f");
    ImGui::SameLine();
    if (ImGui::Button("Stop")) ctx_.haptics.stopAll();
    if (cfg.enabled) {
        ImGui::SameLine();
        if (hs.telemetryFresh) ImGui::TextColored(kGreen, "Live");
        else ImGui::TextColored(kYellow, status_.state == sim::ConnState::Connected ? "Waiting for data" : "No simulator");
    }
    ImGui::ProgressBar(hs.left, ImVec2(220, 0), "left motor");
    ImGui::SameLine();
    ImGui::ProgressBar(hs.right, ImVec2(220, 0), "right motor");

    ImGui::SeparatorText("Devices");
    if (ImGui::Button("Rescan")) ctx_.winwing.rescan();
    const auto devices = ctx_.winwing.devices();
    if (devices.empty()) ImGui::TextColored(kYellow, "No Winwing devices found");
    for (const auto& d : devices) {
        ImGui::PushID(d.key.c_str());
        haptics::DeviceConfig& dc = cfg.devices[d.key];
        if (!dc.identity.known()) dc.identity = d.identity;
        ImGui::Text("%s", d.name.c_str());
        ImGui::SameLine();
        ImGui::TextDisabled("%s  pid %04x  %s", d.path.c_str(), d.pid, d.open ? "" : ("Not open: " + d.error).c_str());
        changed |= ImGui::Checkbox("Vibrate", &dc.enabled);
        const bool single = dc.identity.singleMotor();
        ImGui::SameLine();
        if (single) {
            if (ImGui::Button("Test")) ctx_.haptics.pulse(d.key, 0.6f, 0.6f, 500);
        } else {
            if (ImGui::Button("Test left")) ctx_.haptics.pulse(d.key, 0.6f, 0.0f, 500);
            ImGui::SameLine();
            if (ImGui::Button("Test right")) ctx_.haptics.pulse(d.key, 0.0f, 0.6f, 500);
            ImGui::SameLine();
            if (ImGui::Button("Test both")) ctx_.haptics.pulse(d.key, 0.6f, 0.6f, 500);
        }
        ImGui::SameLine();
        if (ImGui::Button("Stop")) ctx_.haptics.stopAll();
        ImGui::SameLine();
        if (ImGui::Button("Discover")) ctx_.winwing.discover(d.key);

        if (ImGui::TreeNode("Motors")) {
            ImGui::TextDisabled("%s", d.identityIsDefault ? "Default" : "Custom");
            for (const auto& c : d.controllers) {
                ImGui::PushID(c.device);
                ImGui::Text("Controller %d (0x%02x), family %d, \"%s\"", c.device, c.device, c.family, c.name.c_str());
                ImGui::SameLine();
                if (single) {
                    if (ImGui::SmallButton("Motor")) { dc.identity.family = c.family; dc.identity.left = dc.identity.right = {c.device, dc.identity.left.index}; changed = true; }
                } else {
                    if (ImGui::SmallButton("Left")) { dc.identity.family = c.family; dc.identity.left.device = c.device; changed = true; }
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Right")) { dc.identity.family = c.family; dc.identity.right.device = c.device; changed = true; }
                }
                ImGui::PopID();
            }
            bool two = !single;
            if (ImGui::Checkbox("Two motors", &two)) {
                if (two) { dc.identity.right = dc.identity.left; dc.identity.right.index++; }
                else dc.identity.right = dc.identity.left;
                changed = true;
            }
            ImGui::SetNextItemWidth(70);
            changed |= ImGui::InputInt("family", &dc.identity.family, 0, 0);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(70);
            changed |= ImGui::InputInt(single ? "device" : "L dev", &dc.identity.left.device, 0, 0);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(60);
            changed |= ImGui::InputInt(single ? "index" : "L idx", &dc.identity.left.index, 0, 0);
            if (single) {
                dc.identity.right = dc.identity.left;
            } else {
                ImGui::SameLine();
                ImGui::SetNextItemWidth(70);
                changed |= ImGui::InputInt("R dev", &dc.identity.right.device, 0, 0);
                ImGui::SameLine();
                ImGui::SetNextItemWidth(60);
                changed |= ImGui::InputInt("R idx", &dc.identity.right.index, 0, 0);
            }
            ImGui::TreePop();
        }
        ImGui::Separator();
        ImGui::PopID();
    }

    ImGui::SeparatorText("Rules");
    if (ImGui::BeginTable("rules", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("On", ImGuiTableColumnFlags_WidthFixed, 30);
        ImGui::TableSetupColumn("Rule", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn("Gain", ImGuiTableColumnFlags_WidthFixed, 200);
        ImGui::TableSetupColumn("Now", ImGuiTableColumnFlags_WidthFixed, 120);
        ImGui::TableHeadersRow();
        for (const auto& info : haptics::ruleInfos()) {
            ImGui::PushID(info.id);
            haptics::RuleConfig& rc = cfg.rules[info.id];
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            changed |= ImGui::Checkbox("##on", &rc.enabled);
            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(info.label);
            ImGui::TextDisabled("%s", info.description);
            ImGui::TableSetColumnIndex(2);
            ImGui::SetNextItemWidth(-1);
            changed |= ImGui::SliderFloat("##gain", &rc.gain, 0.0f, 2.0f, "%.2f");
            ImGui::TableSetColumnIndex(3);
            auto it = hs.contrib.find(info.id);
            ImGui::ProgressBar(it == hs.contrib.end() ? 0.f : it->second, ImVec2(-1, 0));
            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    if (ImGui::CollapsingHeader("Telemetry")) {
        const sim::Telemetry& t = hs.telemetry;
        if (!t.valid) ImGui::TextDisabled("No data");
        else
            ImGui::Text("on ground %d  GS %.0f kt  IAS %.0f kt  AGL %.0f ft  G %.2f  VS %.0f fpm\nstall %d overspeed %d  gear %.2f  flaps %.2f  spoilers %.2f  N1 %.0f/%.0f  rev %.2f/%.2f  brakes %.2f/%.2f",
                        t.onGround > 0.5, t.groundSpeedKt, t.iasKt, t.altAgl, t.gForce, t.verticalSpeedFpm, t.stallWarning > 0.5, t.overspeedWarning > 0.5,
                        t.gearPct, t.flapsPct, t.spoilersHandle, t.n1Eng1, t.n1Eng2, t.reverse1, t.reverse2, t.brakeLeft, t.brakeRight);
    }

    if (changed) {
        ctx_.dirty = true;
        ctx_.pushHapticsConfig();
    }
}

}  // namespace lr::app
