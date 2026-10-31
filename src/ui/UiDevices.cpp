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
            ImGui::Text("Last input: %s on %s (moved to %d%%)", ctx_.input.describe(last->id).c_str(), deviceDisplayName(last->id.device).c_str(), static_cast<int>(last->axis * 100 + 0.5f));
        else
            ImGui::Text("Last input: %s on %s (%s)", ctx_.input.describe(last->id).c_str(), deviceDisplayName(last->id.device).c_str(), last->pressed ? "pressed" : "released");
    } else {
        ImGui::TextDisabled("Press a button or move an axis to see it here.");
    }
    if (devices_.empty()) {
        ImGui::TextColored(kYellow, "No joystick found under /dev/input. Check the Log tab for permission errors.");
    }
    for (const auto& d : devices_) {
        ImGui::PushID(d.key.c_str());
        if (ImGui::CollapsingHeader(d.name.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::TextDisabled("%s   id %04x:%04x   %zu buttons, %d hats, %d axes   key: %s", d.path.c_str(), d.vendor, d.product, d.buttonCodes.size(), d.hatCount, d.axisCount, d.key.c_str());
            const float cell = ImGui::GetTextLineHeight() * 1.9f;
            const int perRow = std::max(1, static_cast<int>(ImGui::GetContentRegionAvail().x / (cell + 4)));
            for (size_t i = 0; i < d.buttonCodes.size(); ++i) {
                if (i % static_cast<size_t>(perRow) != 0) ImGui::SameLine();
                const bool on = d.buttonState[i];
                ImGui::PushStyleColor(ImGuiCol_Button, on ? ImVec4(0.2f, 0.7f, 0.3f, 1) : ImVec4(0.25f, 0.25f, 0.28f, 1));
                char label[32];
                snprintf(label, sizeof label, "%zu", i + 1);
                ImGui::Button(label, ImVec2(cell, cell));
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Button %zu (evdev code %d)", i + 1, d.buttonCodes[i]);
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
