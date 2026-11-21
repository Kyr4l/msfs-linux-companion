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
#include "desktop/DesktopService.hpp"
#include "plugin/PluginManager.hpp"
#include "util/Log.hpp"

namespace lr::app {

using namespace uidetail;

// ---------------------------------------------------------------- Ui

Ui::Ui(AppContext& ctx) : ctx_(ctx), startTab_(ctx.startTab) {}

void Ui::closing() {
    if (cockpit_.running()) stopCockpitWatch();
    ctx_.guideProbes.clear();
    ctx_.refreshWatches();
    if (ctx_.desktop) ctx_.desktop->mcdu().setPanelOpen(false);
}

std::string Ui::deviceDisplayName(const std::string& key) const {
    for (const auto& d : devices_)
        if (d.key == key) return tidyDeviceName(d.name);
    size_t p = key.find(':', 5);
    return p == std::string::npos ? key : tidyDeviceName(key.substr(p + 1)) + " (not connected)";
}

float Ui::axisNow(const input::InputId& id) const {
    if (id.kind != input::InputKind::Axis) return -1.0f;
    for (const auto& d : devices_) {
        if (d.key != id.device) continue;
        auto it = std::find(d.axisCodes.begin(), d.axisCodes.end(), id.code);
        if (it != d.axisCodes.end()) return d.axisValue[static_cast<size_t>(it - d.axisCodes.begin())];
    }
    return -1.0f;
}

bool Ui::inputIsPressed(const input::InputId& id) const {
    if (id.kind == input::InputKind::Axis) return false;
    for (const auto& d : devices_) {
        if (d.key != id.device) continue;
        if (id.kind == input::InputKind::Button) {
            auto it = std::find(d.buttonCodes.begin(), d.buttonCodes.end(), id.code);
            return it != d.buttonCodes.end() && d.buttonState[static_cast<size_t>(it - d.buttonCodes.begin())];
        }
        if (id.code < 0 || id.code >= 4) return false;
        auto [x, y] = d.hats[id.code];
        switch (id.dir) {
            case 0: return y < 0;
            case 1: return x > 0;
            case 2: return y > 0;
            case 3: return x < 0;
        }
    }
    return false;
}

void Ui::draw() {
    {   // the theme: the active profile's when asked for and it has one, else the chosen one
        const Profile* ap = ctx_.settings.active();
        const std::string& want = ctx_.settings.themeFromProfile && ap && !ap->uiTheme.empty() ? ap->uiTheme : ctx_.settings.uiTheme;
        const bool themeChanged = want != ui::themeId();
        if (themeChanged) ui::setTheme(want.c_str());
        if (themeChanged || std::fabs(ui::themeUserScale() - static_cast<float>(ctx_.settings.uiScale)) > 1e-3f) ui::applyTheme(static_cast<float>(ctx_.settings.uiScale));
    }
    cockpitDrawn_ = false;
    mcduPanelDrawn_ = false;
    devices_ = ctx_.input.devices();
    status_ = ctx_.bridge.status();
    ctx_.syncLive();  // folds new input events / L: vars reported by the sim into the catalog
    ctx_.autoSwitchProfile();
    if (ctx_.engine.hasScheduled()) ctx_.engine.tick();  // what a Delay of a Test button held back

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoSavedSettings;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::Begin("##main", nullptr, flags);
    ImGui::PopStyleVar(2);
    drawMenuBar();
    if (!startTab_.empty()) {  // --tab NAME: a built-in page, or a plugin's tab of that name
        page_ = startTab_;
        if (page_ == "Cockpit") page_ = "Catalog", cockpitOpen_ = true;  // the Cockpit page became part of the Catalog page
        pagePlugin_.clear();
        if (ctx_.plugins)
            for (const auto& h : ctx_.plugins->tabs())
                if (h.name == startTab_) { pagePlugin_ = h.plugin; break; }
        startTab_.clear();
    }
    if (ctx_.startEditBinding >= 0) {
        if (ctx_.startEditBinding < static_cast<int>(ctx_.settings.ensureActive().bindings.size())) openEditor(ctx_.startEditBinding);
        ctx_.startEditBinding = -1;
    }
    drawTopBar();
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() - ImGui::GetStyle().ItemSpacing.y);  // the rail starts right under the bar
    drawRail();
    ImGui::SameLine(0, 0);
    const ImGuiStyle& style = ImGui::GetStyle();
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(style.WindowPadding.x * 1.4f, style.WindowPadding.y * 1.3f));
    ImGui::BeginChild("##page", ImVec2(0, 0), ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar();
    bool drawn = false;
    if (pagePlugin_.empty()) {
        struct Page { const char* name; void (Ui::*draw)(); };
        static const Page pages[] = {{"Home", &Ui::drawHomeTab}, {"Bindings", &Ui::drawBindingsTab}, {"Profiles", &Ui::drawProfilesTab}, {"Devices", &Ui::drawDevicesTab}, {"Guides", &Ui::drawGuidesTab},
                                     {"Catalog", &Ui::drawCatalogTab}, {"Vibration", &Ui::drawVibrationTab}, {"Lights", &Ui::drawLightsTab}, {"Desktop", &Ui::drawDesktopTab}, {"MCDU", &Ui::drawMcduTab},
                                     {"Log", &Ui::drawLogTab},           {"Settings", &Ui::drawSettingsTab}};
        for (const auto& pg : pages)
            if (page_ == pg.name) {
                pageTitle(pg.name);
                ImGui::Spacing();
                ImGui::BeginChild("##content", ImVec2(0, 0));
                (this->*pg.draw)();
                ImGui::EndChild();
                drawn = true;
            }
    } else if (ctx_.plugins) {
        for (const auto& h : ctx_.plugins->tabs())
            if (h.plugin == pagePlugin_ && h.name == page_) {
                pageTitle(h.name.c_str());
                ImGui::Spacing();
                ImGui::PushID(h.plugin.c_str());
                ImGui::BeginChild("##content", ImVec2(0, 0));
                plugin::PluginManager::draw(h);
                ImGui::EndChild();
                ImGui::PopID();
                drawn = true;
            }
    }
    if (!drawn) { page_ = "Home"; pagePlugin_.clear(); }  // a plugin that went away, an unknown --tab
    ImGui::EndChild();
    ImGui::End();
    if (ctx_.plugins)
        for (const auto& h : ctx_.plugins->windows()) {
            ImGui::PushID(h.plugin.c_str());
            plugin::PluginManager::draw(h);
            ImGui::PopID();
        }

    if (cockpit_.running() && !cockpitDrawn_) stopCockpitWatch();  // the tab was left: no more reading of every input event
    if (!mcduPanelDrawn_ && ctx_.desktop) ctx_.desktop->mcdu().setPanelOpen(false);
    drawCalibrationWizard();

    // Debounced auto-save.
    static double lastDirtyAt = -1;
    if (ctx_.dirty) {
        double now = ImGui::GetTime();
        if (lastDirtyAt < 0) lastDirtyAt = now;
        if (now - lastDirtyAt > 1.0) { ctx_.save(); lastDirtyAt = -1; }
    } else {
        lastDirtyAt = -1;
    }
}

void Ui::drawMenuBar() {
    if (!ImGui::BeginMenuBar()) return;
    if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("Save", "Ctrl+S")) ctx_.save();
        if (ImGui::MenuItem("Reload")) {
            Settings s;
            std::string err;
            if (loadSettings(ctx_.configPath, s, err)) {
                ctx_.settings = std::move(s);
                ctx_.settings.ensureActive();
                ctx_.dirty = false;
                ctx_.pushProfileToEngine();
                ctx_.bridge.setEndpoint(ctx_.settings.host, static_cast<uint16_t>(ctx_.settings.port));
                ctx_.bridge.setAutoConnect(ctx_.settings.autoConnect);
                ctx_.pushHapticsConfig();
                ctx_.pushDesktopConfig();
            } else {
                LOG_ERROR("Reload failed: %s", err.c_str());
            }
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Quit")) ctx_.quitRequested = true;
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Simulator")) {
        if (ImGui::MenuItem("Connect")) ctx_.bridge.connectNow();
        if (ImGui::MenuItem("Disconnect")) ctx_.bridge.disconnect();
        if (ImGui::MenuItem("Detect MobiFlight")) ctx_.bridge.probeMobiFlight();
        ImGui::EndMenu();
    }
    if (ctx_.plugins)
        for (const auto& h : ctx_.plugins->menus()) {
            ImGui::PushID(h.plugin.c_str());
            if (ImGui::BeginMenu(h.name.c_str())) { plugin::PluginManager::draw(h); ImGui::EndMenu(); }
            ImGui::PopID();
        }
    ImGui::EndMenuBar();
    if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_S)) ctx_.save();
}

// The state of everything the program talks to, as badges, and the switch that pauses the bindings.
void Ui::drawStatusBar() {
    const float fs = ImGui::GetFontSize(), gap = fs * 0.5f;
    char text[256];
    // Simulator
    ImVec4 c = kRed;
    if (status_.state == sim::ConnState::Connected) c = kGreen;
    else if (status_.state != sim::ConnState::Disconnected) c = kYellow;
    if (status_.state == sim::ConnState::Connected)
        snprintf(text, sizeof text, "MSFS%s%s", status_.edition ? (status_.edition == 2024 ? " 2024" : " 2020") : "", status_.paused ? "  paused" : !status_.simRunning ? "  menu" : "");
    else
        snprintf(text, sizeof text, "MSFS  %s", connStateText(status_.state));
    if (pill(c, text)) {
        if (status_.state == sim::ConnState::Connected)
            ImGui::SetTooltip("%s %s%s%s", status_.simName.c_str(), status_.simVersion.c_str(), status_.detectedPort ? ("\n" + status_.detectedHost + ":" + std::to_string(status_.detectedPort)).c_str() : "", "");
        else
            ImGui::SetTooltip("%s:%u%s%s", status_.host.c_str(), status_.port, status_.lastError.empty() ? "" : "\n", status_.lastError.c_str());
    }
    ImGui::SameLine(0, gap);
    // MobiFlight
    ImVec4 mc = kGrey;
    const char* mt = "MobiFlight";
    switch (status_.mobiflight) {
        case sim::ModuleState::Detected: mc = kGreen; break;
        case sim::ModuleState::Probing: mc = kYellow; mt = "MobiFlight  detecting"; break;
        case sim::ModuleState::NotFound: mc = kRed; mt = "MobiFlight  not found"; break;
        default: break;
    }
    if (pill(mc, mt)) ImGui::SetTooltip("H: events, L: variables and calculator code need MobiFlight.");
    ImGui::SameLine(0, gap);
    {
        // the loaded aircraft by name ("Asobo CJ4"), not by the file name every aircraft shares ("aircraft.CFG")
        std::string shown = "No aircraft";
        if (!status_.aircraft.empty()) {
            const sim::AircraftId loadedId = sim::identifyAircraft(status_.aircraft);
            const Catalog* known = ctx_.catalogs.find(loadedId.id);
            shown = known && known->curated ? known->label() : (loadedId.label.empty() ? baseName(status_.aircraft) : loadedId.label);
        }
        if (pill(ImVec4(0, 0, 0, 0), shown.c_str()) && !status_.aircraft.empty()) ImGui::SetTooltip("%s", status_.aircraft.c_str());
    }
    ImGui::SameLine(0, gap);
    {   // the active profile: a menu to pick another one
        Settings& s = ctx_.settings;
        const Profile& p = s.ensureActive();
        const bool autoNow = ctx_.autoSwitchedTo == p.name;
        std::string label = "Profile  " + p.name + (autoNow ? "  (auto)" : "") + "###topprofile";
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, ImGui::GetFrameHeight() * 0.5f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(fs * 0.7f, ImGui::GetStyle().FramePadding.y));
        if (ImGui::Button(label.c_str())) ImGui::OpenPopup("top_profiles");
        ImGui::PopStyleVar(2);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(autoNow ? "Switched to automatically for the loaded aircraft" : "Click to use another profile");
        if (ImGui::BeginPopup("top_profiles")) {
            for (const auto& q : s.profiles) {
                if (ImGui::Selectable(q.name.c_str(), q.name == p.name)) {
                    s.activeProfile = q.name;
                    ctx_.autoSwitchedTo.clear();
                    viewAircraft_.clear();
                    ctx_.markDirty();
                }
            }
            ImGui::Separator();
            if (ImGui::Selectable("Manage profiles...")) { page_ = "Profiles"; pagePlugin_.clear(); }
            ImGui::EndPopup();
        }
    }
    ImGui::SameLine(0, gap);
    snprintf(text, sizeof text, devices_.size() == 1 ? "%zu device" : "%zu devices", devices_.size());
    pill(ImVec4(0, 0, 0, 0), text);
    if (ctx_.dirty) { ImGui::SameLine(0, gap); pill(kYellow, "Unsaved"); }
    if (!ctx_.lastSaveError.empty()) {
        ImGui::SameLine(0, gap);
        if (pill(kRed, "Save failed")) ImGui::SetTooltip("%s", ctx_.lastSaveError.c_str());
    }
    if (ctx_.plugins)
        for (const auto& h : ctx_.plugins->statusItems()) {
            ImGui::SameLine(0, gap * 2);
            ImGui::PushID(h.plugin.c_str());
            ImGui::BeginGroup();
            plugin::PluginManager::draw(h);
            ImGui::EndGroup();
            ImGui::PopID();
        }
    // the switch at the right end
    bool enabled = ctx_.engine.enabled();
    const float switchW = ImGui::GetFrameHeight() * 0.78f * 1.8f + ImGui::GetStyle().ItemInnerSpacing.x + ImGui::CalcTextSize("Bindings active").x;
    const float right = ImGui::GetWindowContentRegionMax().x - switchW;
    ImGui::SameLine(0, gap * 2);
    if (ImGui::GetCursorPosX() < right) ImGui::SetCursorPosX(right);
    if (toggleSwitch("Bindings active", &enabled)) ctx_.engine.setEnabled(enabled);
}

}  // namespace lr::app
