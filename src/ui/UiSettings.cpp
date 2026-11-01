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
    ImGui::Combo("Level", &logMinLevel_, levels, 4);
    ImGui::SameLine();
    ImGui::Checkbox("Auto-scroll", &logAutoScroll_);
    ImGui::SameLine();
    if (ImGui::Button("Clear")) Log::instance().clear();
    ImGui::SameLine();
    ImGui::Text("Sent: %llu  Errors: %llu", static_cast<unsigned long long>(status_.commandsSent), static_cast<unsigned long long>(status_.exceptions));

    const auto fires = ctx_.engine.recentFires();
    if (ImGui::BeginChild("fires", ImVec2(0, ImGui::GetTextLineHeightWithSpacing() * 6), ImGuiChildFlags_Borders)) {
        ImGui::TextDisabled("Recent bindings");
        for (auto it = fires.rbegin(); it != fires.rend(); ++it) ImGui::TextUnformatted(it->text.c_str());
    }
    ImGui::EndChild();

    if (ImGui::BeginChild("log", ImVec2(0, 0), ImGuiChildFlags_Borders)) {
        // up to 3000 lines: copied when the log has changed, and only the ones in view are drawn
        const uint64_t serial = Log::instance().serial();
        if (serial != logCacheSerial_ || logMinLevel_ != logCacheLevel_) {
            if (serial != logCacheSerial_) logEntries_ = Log::instance().snapshot();
            logShown_.clear();
            for (size_t i = 0; i < logEntries_.size(); ++i)
                if (static_cast<int>(logEntries_[i].level) >= logMinLevel_) logShown_.push_back(i);
            logCacheSerial_ = serial;
            logCacheLevel_ = logMinLevel_;
        }
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(logShown_.size()));
        while (clipper.Step()) {
            for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
                const LogEntry& e = logEntries_[logShown_[static_cast<size_t>(row)]];
                ImVec4 c = e.level == LogLevel::Error ? kRed : e.level == LogLevel::Warn ? kYellow : e.level == LogLevel::Debug ? kGrey : ImGui::GetStyleColorVec4(ImGuiCol_Text);
                ImGui::TextColored(c, "%s  %s", formatTime(e.time).c_str(), e.text.c_str());
            }
        }
        if (logAutoScroll_ && serial != logSerialSeen_) ImGui::SetScrollHereY(1.0f);
        logSerialSeen_ = serial;
    }
    ImGui::EndChild();
}

// ---------------------------------------------------------------- Settings

void Ui::drawSettingsTab() {
    Settings& s = ctx_.settings;
    ImGui::SeparatorText("Connection");
    bool changed = false;
    fieldLabel("Host");
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 14);
    changed |= ImGui::InputText("##host", &s.host);
    fieldLabel("Port");
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 9);
    changed |= ImGui::InputInt("##port", &s.port);
    changed |= ImGui::Checkbox("Auto-connect", &s.autoConnect);
    if (changed) {
        s.port = std::clamp(s.port, 0, 65535);
        ctx_.bridge.setAutoConnect(s.autoConnect);
        ctx_.dirty = true;
    }
    if (ImGui::Button("Connect")) {
        ctx_.bridge.setEndpoint(s.host, static_cast<uint16_t>(s.port));
        ctx_.bridge.connectNow();
        ctx_.dirty = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Disconnect")) ctx_.bridge.disconnect();
    ImGui::TextDisabled("Port 0 scans for the port.");

    ImGui::SeparatorText("Files");
    ImGui::TextWrapped("Config:  %s", ctx_.configPath.c_str());
    for (const auto& c : ctx_.catalogs) ImGui::TextWrapped("Catalog (%s): %s", c.aircraft.c_str(), c.sourcePath.empty() ? "from the sim / cache" : c.sourcePath.c_str());
    if (ctx_.catalogs.empty()) ImGui::TextUnformatted("Catalog: (none)");
    if (ImGui::Button("Save")) ctx_.save();

    ImGui::SeparatorText("About");
    ImGui::Text("msfs-linux-companion %s (Dear ImGui %s)", COMPANION_VERSION, IMGUI_VERSION);
    ImGui::TextDisabled("GPL-3.0-only. github.com/Kyr4l/msfs-linux-companion");
}

}  // namespace lr::app
