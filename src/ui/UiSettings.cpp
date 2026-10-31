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

// ---------------------------------------------------------------- Log

void Ui::drawLogTab() {
    static const char* levels[] = {"debug", "info", "warn", "error"};
    ImGui::SetNextItemWidth(100);
    ImGui::Combo("min level", &logMinLevel_, levels, 4);
    ImGui::SameLine();
    ImGui::Checkbox("auto-scroll", &logAutoScroll_);
    ImGui::SameLine();
    if (ImGui::Button("Clear")) Log::instance().clear();
    ImGui::SameLine();
    ImGui::Text("sent: %llu  exceptions: %llu", static_cast<unsigned long long>(status_.commandsSent), static_cast<unsigned long long>(status_.exceptions));

    const auto fires = ctx_.engine.recentFires();
    if (ImGui::BeginChild("fires", ImVec2(0, ImGui::GetTextLineHeightWithSpacing() * 6), ImGuiChildFlags_Borders)) {
        ImGui::TextDisabled("Recently fired bindings");
        for (auto it = fires.rbegin(); it != fires.rend(); ++it) ImGui::TextUnformatted(it->text.c_str());
    }
    ImGui::EndChild();

    if (ImGui::BeginChild("log", ImVec2(0, 0), ImGuiChildFlags_Borders)) {
        const auto entries = Log::instance().snapshot();
        for (const auto& e : entries) {
            if (static_cast<int>(e.level) < logMinLevel_) continue;
            ImVec4 c = e.level == LogLevel::Error ? kRed : e.level == LogLevel::Warn ? kYellow : e.level == LogLevel::Debug ? kGrey : ImGui::GetStyleColorVec4(ImGuiCol_Text);
            ImGui::TextColored(c, "%s  %s", formatTime(e.time).c_str(), e.text.c_str());
        }
        uint64_t serial = Log::instance().serial();
        if (logAutoScroll_ && serial != logSerialSeen_) ImGui::SetScrollHereY(1.0f);
        logSerialSeen_ = serial;
    }
    ImGui::EndChild();
}

// ---------------------------------------------------------------- Settings

void Ui::drawSettingsTab() {
    Settings& s = ctx_.settings;
    ImGui::SeparatorText("SimConnect (TCP)");
    bool changed = false;
    ImGui::SetNextItemWidth(200);
    changed |= ImGui::InputText("Host", &s.host);
    ImGui::SetNextItemWidth(120);
    changed |= ImGui::InputInt("Port", &s.port);
    changed |= ImGui::Checkbox("Connect automatically", &s.autoConnect);
    if (changed) {
        s.port = std::clamp(s.port, 0, 65535);
        ctx_.bridge.setAutoConnect(s.autoConnect);
        ctx_.dirty = true;
    }
    if (ImGui::Button("Apply & reconnect")) {
        ctx_.bridge.setEndpoint(s.host, static_cast<uint16_t>(s.port));
        ctx_.bridge.connectNow();
        ctx_.dirty = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Disconnect")) ctx_.bridge.disconnect();
    ImGui::TextWrapped("MSFS listens where its SimConnect.xml says (default: port 500). Under Proton it cannot bind ports below 1024, so it "
                       "silently uses a random port instead; when the configured port does not answer, the tool scans wineserver's loopback "
                       "listeners for the SimConnect handshake and uses what it finds (shown in the status bar). Port 0 = scan only. "
                       "To avoid the scan, set a <Port> above 1024 in\n"
                       "steamapps/compatdata/1250410/pfx/drive_c/users/steamuser/AppData/Roaming/Microsoft Flight Simulator/SimConnect.xml  (MSFS 2020)\n"
                       "steamapps/compatdata/2537590/pfx/drive_c/users/steamuser/AppData/Roaming/Microsoft Flight Simulator 2024/SimConnect.xml  (MSFS 2024)");

    ImGui::SeparatorText("Files");
    ImGui::Text("Config:  %s", ctx_.configPath.c_str());
    for (const auto& c : ctx_.catalogs) ImGui::Text("Catalog (%s): %s", c.aircraft.c_str(), c.sourcePath.empty() ? "from the sim / cache" : c.sourcePath.c_str());
    if (ctx_.catalogs.empty()) ImGui::TextUnformatted("Catalog: (none)");
    if (ImGui::Button("Save now")) ctx_.save();

    ImGui::SeparatorText("About");
    ImGui::Text("msfs-linux-companion %s - native Linux SimConnect client, Dear ImGui %s", COMPANION_VERSION, IMGUI_VERSION);
    ImGui::TextDisabled("Free software under the GNU GPL v3 (GPL-3.0-only), no warranty. Source and licences: github.com/Kyr4l/msfs-linux-companion");
    ImGui::TextWrapped("K: events (standard and A32NX.* custom events) go straight through SimConnect. H: events, L: vars and RPN code are executed by the MobiFlight WASM module (install it in the Community folder).");
}

}  // namespace lr::app
