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

// ---------------------------------------------------------------- Ui

Ui::Ui(AppContext& ctx) : ctx_(ctx), startTab_(ctx.startTab) {}

std::string Ui::deviceDisplayName(const std::string& key) const {
    for (const auto& d : devices_)
        if (d.key == key) return d.name;
    size_t p = key.find(':', 5);
    return p == std::string::npos ? key : key.substr(p + 1) + " (not connected)";
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
    devices_ = ctx_.input.devices();
    status_ = ctx_.bridge.status();
    ctx_.syncLive();  // folds new input events / L: vars reported by the sim into the catalog
    if (ctx_.engine.hasScheduled()) ctx_.engine.tick();  // what a Delay of a Test button held back

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoSavedSettings;
    ImGui::Begin("##main", nullptr, flags);
    drawMenuBar();
    drawStatusBar();
    ImGui::Separator();
    if (ImGui::BeginTabBar("tabs")) {
        auto tab = [&](const char* name, void (Ui::*draw)()) {
            ImGuiTabItemFlags flags = 0;
            if (!startTab_.empty() && startTab_ == name) { flags = ImGuiTabItemFlags_SetSelected; startTab_.clear(); }
            if (ImGui::BeginTabItem(name, nullptr, flags)) { (this->*draw)(); ImGui::EndTabItem(); }
        };
        tab("Bindings", &Ui::drawBindingsTab);
        tab("Devices", &Ui::drawDevicesTab);
        tab("Guides", &Ui::drawGuidesTab);
        tab("Catalog", &Ui::drawCatalogTab);
        tab("Vibration", &Ui::drawVibrationTab);
        tab("Log", &Ui::drawLogTab);
        tab("Settings", &Ui::drawSettingsTab);
        ImGui::EndTabBar();
    }
    ImGui::End();

    if (!splashDone_) drawSplash();
    if (editorOpen_) drawBindingEditor();
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
        if (ImGui::MenuItem("Reload config")) {
            Settings s;
            std::string err;
            if (loadSettings(ctx_.configPath, s, err)) {
                ctx_.settings = std::move(s);
                ctx_.settings.ensureActive();
                ctx_.dirty = false;
                ctx_.pushProfileToEngine();
                ctx_.bridge.setEndpoint(ctx_.settings.host, static_cast<uint16_t>(ctx_.settings.port));
            } else {
                LOG_ERROR("Reload failed: %s", err.c_str());
            }
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Quit")) ctx_.quitRequested = true;
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Simulator")) {
        if (ImGui::MenuItem("Connect now")) ctx_.bridge.connectNow();
        if (ImGui::MenuItem("Disconnect")) ctx_.bridge.disconnect();
        if (ImGui::MenuItem("Re-probe MobiFlight module")) ctx_.bridge.probeMobiFlight();
        ImGui::EndMenu();
    }
    ImGui::EndMenuBar();
    if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_S)) ctx_.save();
}

void Ui::drawStatusBar() {
    // Simulator
    ImVec4 c = kRed;
    if (status_.state == sim::ConnState::Connected) c = kGreen;
    else if (status_.state != sim::ConnState::Disconnected) c = kYellow;
    dot(c);
    if (status_.state == sim::ConnState::Connected) {
        ImGui::Text("MSFS%s: connected (%s %s)%s", status_.edition ? (status_.edition == 2024 ? " 2024" : " 2020") : "", status_.simName.c_str(),
                    status_.simVersion.c_str(), status_.paused ? " [paused]" : "");
        if (status_.detectedPort) {
            ImGui::SameLine();
            ImGui::TextDisabled("via auto-detected %s:%u", status_.detectedHost.c_str(), status_.detectedPort);
        }
    } else {
        ImGui::Text("MSFS: %s (%s:%u)", connStateText(status_.state), status_.host.c_str(), status_.port);
        if (!status_.lastError.empty()) {
            ImGui::SameLine();
            ImGui::TextColored(kGrey, "- %s", status_.lastError.c_str());
        }
    }
    ImGui::SameLine(0, 30);
    // MobiFlight
    switch (status_.mobiflight) {
        case sim::ModuleState::Detected: dot(kGreen); ImGui::TextUnformatted("MobiFlight WASM: detected"); break;
        case sim::ModuleState::Probing: dot(kYellow); ImGui::TextUnformatted("MobiFlight WASM: probing..."); break;
        case sim::ModuleState::NotFound: dot(kRed); ImGui::TextUnformatted("MobiFlight WASM: not found"); break;
        default: dot(kGrey); ImGui::TextUnformatted("MobiFlight WASM: unknown"); break;
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("H: events, L: vars and calculator code need the MobiFlight WASM module\nin the MSFS Community folder. K: events (including A32NX.* custom events) work without it.");
    ImGui::SameLine(0, 30);
    {
        // the loaded aircraft by name ("Asobo CJ4"), not by the file name every aircraft shares ("aircraft.CFG")
        std::string shown = "-";
        if (!status_.aircraft.empty()) {
            const sim::AircraftId loadedId = sim::identifyAircraft(status_.aircraft);
            const Catalog* known = ctx_.catalogs.find(loadedId.id);
            shown = known && known->curated ? known->label() : (loadedId.label.empty() ? baseName(status_.aircraft) : loadedId.label);
        }
        ImGui::Text("Aircraft: %s", shown.c_str());
    }
    if (!status_.aircraft.empty() && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", status_.aircraft.c_str());
    ImGui::SameLine(0, 30);
    ImGui::Text("Devices: %zu", devices_.size());
    ImGui::SameLine(0, 30);
    bool enabled = ctx_.engine.enabled();
    if (ImGui::Checkbox("Bindings active", &enabled)) ctx_.engine.setEnabled(enabled);
    if (ctx_.dirty) { ImGui::SameLine(0, 30); ImGui::TextColored(kYellow, "unsaved"); }
    if (!ctx_.lastSaveError.empty()) { ImGui::SameLine(); ImGui::TextColored(kRed, "save failed: %s", ctx_.lastSaveError.c_str()); }
}

// ---------------------------------------------------------------- Splash

// Shown over the main window until the background threads finished their first device scans
// (joysticks via evdev, Winwing controllers via hidraw discovery) so a slow start never looks like a hang.
void Ui::drawSplash() {
    const double t = ImGui::GetTime();
    const bool inputDone = ctx_.input.scanCount() > 0;
    const bool winwingDone = ctx_.winwing.scanCount() > 0;
    if ((inputDone && winwingDone && t > 0.8) || t > 15.0) {
        splashDone_ = true;
        return;
    }
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.5f, vp->WorkPos.y + vp->WorkSize.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(460, 0));
    ImGui::SetNextWindowBgAlpha(0.96f);
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                             ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoNav;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(24, 20));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
    ImGui::Begin("##splash", nullptr, flags);
    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.6f);
    ImGui::TextUnformatted("MSFS Linux Companion");
    ImGui::PopFont();
    ImGui::TextDisabled("version %s - starting up", COMPANION_VERSION);
    ImGui::Spacing();
    auto line = [&](bool done, const char* what, const char* pending) {
        dot(done ? kGreen : kYellow);
        ImGui::TextUnformatted(done ? what : pending);
    };
    char joy[96];
    snprintf(joy, sizeof joy, "Joysticks: %zu found", devices_.size());
    line(inputDone, joy, "Joysticks: scanning /dev/input ...");
    const auto ww = ctx_.winwing.devices();
    char wwText[96];
    snprintf(wwText, sizeof wwText, "Winwing devices: %zu found", ww.size());
    line(winwingDone, wwText, "Winwing devices: discovering controllers over hidraw ...");
    const bool simUp = status_.state == sim::ConnState::Connected;
    line(simUp, "Simulator: connected", status_.state == sim::ConnState::Disconnected ? "Simulator: not running yet (will keep retrying)" : "Simulator: connecting ...");
    ImGui::Spacing();
    // Indeterminate progress bar.
    const float w = ImGui::GetContentRegionAvail().x;
    const float phase = static_cast<float>(std::fmod(t * 0.6, 1.0));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float h = 6.0f;
    dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), ImGui::GetColorU32(ImGuiCol_FrameBg), 3.0f);
    const float segW = w * 0.3f;
    float x0 = p.x + (w + segW) * phase - segW;
    float x1 = x0 + segW;
    x0 = std::max(x0, p.x);
    x1 = std::min(x1, p.x + w);
    if (x1 > x0) dl->AddRectFilled(ImVec2(x0, p.y), ImVec2(x1, p.y + h), ImGui::GetColorU32(ImGuiCol_ButtonActive), 3.0f);
    ImGui::Dummy(ImVec2(w, h));
    ImGui::End();
    ImGui::PopStyleVar(2);
}

}  // namespace lr::app
