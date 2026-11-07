// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kyr4l
#include "ui/Ui.hpp"
#include "ui/UiCommon.hpp"
#include "ui/Theme.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <ctime>
#include <filesystem>

#include <imgui.h>
#include <imgui_stdlib.h>

#include "app/Paths.hpp"
#include "plugin/PluginManager.hpp"
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
    ImGui::SeparatorText("Appearance");
    {
        struct Size { const char* label; double scale; };
        static const Size sizes[] = {{"Small", 0.9}, {"Normal", 1.0}, {"Large", 1.15}, {"Larger", 1.3}, {"Largest", 1.5}};
        const char* current = "Custom";
        for (const auto& z : sizes)
            if (std::fabs(s.uiScale - z.scale) < 1e-3) current = z.label;
        fieldLabel("Text size");
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 9);
        if (ImGui::BeginCombo("##text_size", current)) {
            for (const auto& z : sizes)
                if (ImGui::Selectable(z.label, std::fabs(s.uiScale - z.scale) < 1e-3)) { s.uiScale = z.scale; ctx_.markDirty(); }
            ImGui::EndCombo();
        }
    }
    {   // colour themes, as small cards with their colours
        const float fs = ImGui::GetFontSize();
        int count = 0;
        const ui::ThemeInfo* list = ui::themes(count);
        fieldLabel("Theme");
        const float startX = ImGui::GetCursorPosX(), cardW = fs * 9.5f, cardH = fs * 2.2f;
        const char* group = "";
        for (int i = 0; i < count; ++i) {
            if (std::strcmp(group, list[i].group) != 0) {
                group = list[i].group;
                if (i) { ImGui::NewLine(); fieldLabel(""); }
                ImGui::TextDisabled("%s", group);
                ImGui::SameLine(startX + fs * 4.5f);
            } else {
                ImGui::SameLine();
                if (ImGui::GetCursorPosX() + cardW > ImGui::GetContentRegionMax().x) { ImGui::NewLine(); ImGui::SetCursorPosX(startX + fs * 4.5f); }
            }
            ImGui::PushID(i);
            const bool on = s.uiTheme == list[i].id;
            const ImVec2 p = ImGui::GetCursorScreenPos();
            if (ImGui::InvisibleButton("theme", ImVec2(cardW, cardH))) { s.uiTheme = list[i].id; ctx_.markDirty(); }
            ImDrawList* dl = ImGui::GetWindowDrawList();
            dl->AddRectFilled(p, ImVec2(p.x + cardW, p.y + cardH), ImGui::GetColorU32(ui::palette::kSurface), fs * 0.35f);
            dl->AddRect(p, ImVec2(p.x + cardW, p.y + cardH), ImGui::GetColorU32(on ? kAccent : ImGui::IsItemHovered() ? ui::palette::kTextDim : ui::palette::kBorder), fs * 0.35f, 0, on ? 2.0f : 1.0f);
            for (int k = 0; k < 3; ++k) {
                const ImVec2 a(p.x + fs * 0.65f + k * fs * 0.8f, p.y + cardH * 0.5f);
                dl->AddCircleFilled(a, fs * 0.3f, ImGui::GetColorU32(ui::themeSwatch(list[i].id, k)));
                dl->AddCircle(a, fs * 0.3f, ImGui::GetColorU32(withAlpha(ui::palette::kTextDim, 0.5f)));
            }
            dl->AddText(ImVec2(p.x + fs * 2.9f, p.y + (cardH - fs) * 0.5f), ImGui::GetColorU32(ui::palette::kText), list[i].name);
            ImGui::PopID();
        }
        fieldLabel("");
        if (ImGui::Checkbox("Use the profile's theme", &s.themeFromProfile)) ctx_.markDirty();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("A profile with a theme of its own (Profiles page) colours the companion while it is active");
    }
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

    drawPluginsSection();

    ImGui::SeparatorText("About");
    ImGui::Text("msfs-linux-companion %s (Dear ImGui %s)", COMPANION_VERSION, IMGUI_VERSION);
    ImGui::TextDisabled("GPL-3.0-only. github.com/Kyr4l/msfs-linux-companion");
}

void Ui::drawPluginsSection() {
    ImGui::SeparatorText("Plugins");
    Settings& s = ctx_.settings;
    const auto* pm = ctx_.plugins;
    if (!pm || pm->plugins().empty()) ImGui::TextDisabled("None");
    if (pm && !pm->plugins().empty() && ImGui::BeginTable("plugins", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("On", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 2);
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn("Version", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 6);
        ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableHeadersRow();
        for (const auto& r : pm->plugins()) {
            ImGui::PushID(r.folder.c_str());
            ImGui::PushID(r.id.c_str());
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            auto& off = s.disabledPlugins;
            bool on = std::find(off.begin(), off.end(), r.id) == off.end();
            if (ImGui::Checkbox("##on", &on)) {
                if (on) off.erase(std::remove(off.begin(), off.end(), r.id), off.end());
                else off.push_back(r.id);
                pluginsChanged_ = true;
                ctx_.dirty = true;
            }
            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(r.name.c_str());
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s%s%s", r.description.c_str(), r.description.empty() ? "" : "\n", r.library.empty() ? r.folder.c_str() : r.library.c_str());
            ImGui::TableSetColumnIndex(2);
            ImGui::TextUnformatted(r.version.c_str());
            ImGui::TableSetColumnIndex(3);
            switch (r.state) {
                case plugin::PluginState::Loaded: ImGui::TextColored(kGreen, r.library.empty() ? "Data" : "Loaded"); break;
                case plugin::PluginState::Disabled: ImGui::TextDisabled("Off"); break;
                case plugin::PluginState::Failed:
                    ImGui::TextColored(kRed, "Error");
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", r.error.c_str());
                    break;
            }
            ImGui::PopID();
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    if (pluginsChanged_) ImGui::TextColored(kYellow, "Restart to apply");
    ImGui::TextDisabled("Folder: %s/plugins", configDir().c_str());
    if (pm)
        for (const auto& h : pm->settingsSections()) {
            ImGui::PushID(h.plugin.c_str());
            ImGui::SeparatorText(h.name.c_str());
            plugin::PluginManager::draw(h);
            ImGui::PopID();
        }
}

}  // namespace lr::app
