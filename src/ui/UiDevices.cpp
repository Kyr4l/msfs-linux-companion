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

// ---------------------------------------------------------------- Devices

void Ui::drawDevicesTab() {
    if (ImGui::Button("Rescan")) ctx_.input.rescan();
    ImGui::SameLine();
    if (auto last = ctx_.engine.lastInput()) {
        if (last->id.kind == input::InputKind::Axis)
            ImGui::Text("Last: %s (%s) %d%%", ctx_.input.describe(last->id).c_str(), deviceDisplayName(last->id.device).c_str(), static_cast<int>(last->axis * 100 + 0.5f));
        else
            ImGui::Text("Last: %s (%s) %s", ctx_.input.describe(last->id).c_str(), deviceDisplayName(last->id.device).c_str(), last->pressed ? "pressed" : "released");
    } else {
        ImGui::TextDisabled("No input yet");
    }
    if (devices_.empty()) {
        ImGui::TextColored(kYellow, "No devices found");
    }
    for (const auto& d : devices_) {
        ImGui::PushID(d.key.c_str());
        if (ImGui::CollapsingHeader(d.name.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::TextDisabled("%s  %04x:%04x  %zu buttons, %d hats, %d axes", d.path.c_str(), d.vendor, d.product, d.buttonCodes.size(), d.hatCount, d.axisCount);
            const float cell = ImGui::GetTextLineHeight() * 1.9f;
            const int perRow = std::max(1, static_cast<int>(ImGui::GetContentRegionAvail().x / (cell + 4)));
            for (size_t i = 0; i < d.buttonCodes.size(); ++i) {
                if (i % static_cast<size_t>(perRow) != 0) ImGui::SameLine();
                const bool on = d.buttonState[i];
                ImGui::PushStyleColor(ImGuiCol_Button, on ? ImVec4(0.2f, 0.7f, 0.3f, 1) : ImVec4(0.25f, 0.25f, 0.28f, 1));
                char label[32];
                snprintf(label, sizeof label, "%zu", i + 1);
                ImGui::Button(label, ImVec2(cell, cell));
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Button %zu (code %d)", i + 1, d.buttonCodes[i]);
                ImGui::PopStyleColor();
            }
            for (size_t a = 0; a < d.axisCodes.size(); ++a) {
                char label[64], overlay[16];
                snprintf(label, sizeof label, "Axis %zu (%s)", a + 1, input::axisName(d.axisCodes[a]));
                snprintf(overlay, sizeof overlay, "%d%%", static_cast<int>(d.axisValue[a] * 100 + 0.5f));
                ImGui::PushID(static_cast<int>(a));
                ImGui::SetNextItemWidth(70);
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted(label);
                ImGui::SameLine(130);
                ImGui::ProgressBar(d.axisValue[a], ImVec2(300, 0), overlay);
                ImGui::SameLine();
                const input::InputId axisId{d.key, input::InputKind::Axis, d.axisCodes[a], 0};
                const AxisCalibration* cal = ctx_.settings.calibrationFor(axisId);
                if (ImGui::SmallButton("Calibrate")) openCalibration(axisId);
                if (cal) {
                    ImGui::SameLine();
                    if (cal->valid()) {
                        std::string gates;
                        const int codes[4] = {cal->cutoffBtn, cal->idleBtn, cal->milBtn, cal->abBtn};
                        const char* names[4] = {"cut-off", "idle", "max", "afterburner"};
                        for (int g = 0; g < 4; ++g)
                            if (codes[g] >= 0) gates += std::string(gates.empty() ? "" : ", ") + names[g] + " " + ctx_.input.describe({d.key, input::InputKind::Button, codes[g], 0});
                        ImGui::TextColored(kGreen, "Calibrated%s%s", gates.empty() ? "" : ": ", gates.c_str());
                    }
                    else ImGui::TextColored(kYellow, "Invalid calibration");
                }
                ImGui::PopID();
            }
            for (int h = 0; h < d.hatCount; ++h) {
                auto [x, y] = d.hats[static_cast<size_t>(h)];
                ImGui::Text("Hat %d: x=%d y=%d  %s%s%s%s", h + 1, x, y, y < 0 ? "Up " : "", x > 0 ? "Right " : "", y > 0 ? "Down " : "", x < 0 ? "Left" : "");
            }
        }
        ImGui::PopID();
    }
}

}  // namespace lr::app
