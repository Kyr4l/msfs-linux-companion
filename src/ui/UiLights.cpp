// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kyr4l
// Lights: the colours stored in a device, drawn on a picture of it (data/*_lights.json), the settings it stores, and live brightness.
// Stored values are read with opcode 0x05; a live level is the light command only (SET_LEDX, opcode 0x49), which the device forgets
// when unplugged. Only a setting marked "save" in the device file is ever written, through WinwingHidManager::saveConfigByte (a
// backup of the words around it is kept first, and the read-back must show only that byte changed).
#include "ui/Ui.hpp"
#include "ui/UiCommon.hpp"
#include "ui/Theme.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>

#include "app/Paths.hpp"

#include <imgui.h>

namespace lr::app {
using namespace uidetail;

namespace {
std::string deviceKey(const LightDevice& d) {
    char k[16];
    snprintf(k, sizeof k, "%04x:%04x", d.vendor, d.product);
    return k;
}

ImU32 ledColor(const LightRgb& c, float alpha = 1.f) {
    const float f = 0.35f + 0.65f * c.level / 255.f;  // a dim LED is drawn darker, never black: its colour stays readable
    return ImGui::GetColorU32(ImVec4(c.r / 255.f * f, c.g / 255.f * f, c.b / 255.f * f, alpha));
}
}  // namespace

void Ui::drawLightsTab() {
    if (!lightsLoaded_) {
        lightDevices_ = loadLightDevices();
        lightsLoaded_ = true;
    }
    if (lightDevices_.empty()) {
        ImGui::TextDisabled("No light files");
        return;
    }
    if (lightsReading_.valid() && lightsReading_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) lightsRead_ = lightsReading_.get();
    // a save finished (for whichever device it was): its result, and that device read again; edits on screen belong to the device shown
    if (lightSaving_.valid() && lightSaving_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        const std::string err = lightSaving_.get();
        if (lightSaveKey_ == lightLevelsKey_) {
            lightSaveResult_ = err.empty() ? "Saved" : err;
            if (err.empty()) std::fill(lightPending_.begin(), lightPending_.end(), -1);
        }
        if (lightsRead_ && lightsRead_->key == lightSaveKey_) lightsRead_.reset();
    }
    bool reading = lightsReading_.valid();

    // the devices that have a light file, plugged in or not
    const auto plugged = ctx_.winwing.devices();
    lightDevice_ = std::min(lightDevice_, lightDevices_.size() - 1);
    for (size_t i = 0; i < lightDevices_.size(); ++i) {
        if (i) ImGui::SameLine();
        if (ImGui::RadioButton(lightDevices_[i].name.c_str(), lightDevice_ == i) && lightDevice_ != i) {
            lightDevice_ = i;
            lightSelected_ = 0;
        }
    }
    const LightDevice& dev = lightDevices_[lightDevice_];
    const std::string key = deviceKey(dev);
    const bool present = std::any_of(plugged.begin(), plugged.end(), [&](const auto& w) { return w.key == key && w.open; });
    const bool supported = dev.driver == "winwing";

    auto startRead = [&] {
        if (lightsReading_.valid()) return;  // one read at a time: replacing a running one would wait for it here
        reading = true;
        lightsReading_ = std::async(std::launch::async, [&w = ctx_.winwing, d = dev, key] {
            LightsRead r;
            r.key = key;
            for (const auto& s : d.states) {
                std::vector<uint8_t> bytes;
                if (!w.readConfigBlock(key, d.device, d.family, s.offset, d.leds.size() * 4, bytes)) {
                    r.error = "No answer";
                    r.states.clear();
                    return r;
                }
                r.states.push_back(decodeLightState(d, bytes));
            }
            for (const auto& s : d.settings) {
                std::vector<uint8_t> bytes;
                const bool ok = w.readConfigBlock(key, s.device, s.family, s.offset, 4, bytes) && bytes.size() == 4;
                r.settings.push_back(ok ? bytes[static_cast<size_t>(s.byte)] : -1);
                if (!ok) r.error = "No answer";
            }
            return r;
        });
    };
    if (present && supported && !reading && (!lightsRead_ || lightsRead_->key != key)) startRead();  // the first look reads it

    // "Read only" for a device nothing can be changed on; Refresh at the right edge
    const bool editable = !dev.levels.empty() || std::any_of(dev.settings.begin(), dev.settings.end(), [](const LightSetting& s) { return s.save; });
    const float noteW = editable ? 0.f : ImGui::CalcTextSize("Read only").x + ImGui::GetStyle().ItemSpacing.x;
    ImGui::SameLine(ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX() - ImGui::CalcTextSize("Refresh").x - ImGui::GetStyle().FramePadding.x * 2 - noteW);
    if (!editable) {
        ImGui::TextColored(kYellow, "Read only");
        ImGui::SameLine();
    }
    ImGui::BeginDisabled(!present || !supported || reading);
    if (ImGui::Button("Refresh")) startRead();
    ImGui::EndDisabled();

    if (!supported) ImGui::TextColored(kYellow, "Driver %s not supported", dev.driver.c_str());
    else if (!present) ImGui::TextDisabled("Not connected");
    else if (reading) ImGui::TextDisabled("Reading");
    else if (lightsRead_ && !lightsRead_->error.empty()) ImGui::TextColored(kYellow, "%s", lightsRead_->error.c_str());

    if (dev.states.empty() || dev.leds.empty()) {  // a device with settings only (no stored colours)
        drawLightSettings(dev, key, present && supported);
        return;
    }

    // the lighting states
    lightState_ = std::min(lightState_, dev.states.size() - 1);
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("State");
    for (size_t i = 0; i < dev.states.size(); ++i) {
        ImGui::SameLine();
        if (ImGui::RadioButton((dev.states[i].name + "##state").c_str(), lightState_ == i)) lightState_ = i;
    }
    const std::vector<LightRgb>* colors = nullptr;
    if (lightsRead_ && lightsRead_->key == key && lightState_ < lightsRead_->states.size()) colors = &lightsRead_->states[lightState_];
    auto colorOf = [&](int id) -> const LightRgb* {
        for (size_t i = 0; colors && i < dev.leds.size() && i < colors->size(); ++i)
            if (dev.leds[i].id == id) return &(*colors)[i];
        return nullptr;
    };

    // the picture
    const float listW = std::min(320.f, ImGui::GetContentRegionAvail().x * 0.34f);
    const float picW = std::max(200.f, ImGui::GetContentRegionAvail().x - listW - ImGui::GetStyle().ItemSpacing.x);
    const float picH = picW / dev.aspect;
    ImGui::BeginChild("lights_picture", ImVec2(picW, picH + 8), false, ImGuiWindowFlags_NoScrollbar);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 o = ImGui::GetCursorScreenPos();
    dl->AddRectFilled(o, ImVec2(o.x + picW, o.y + picH), ImGui::GetColorU32(ImVec4(0.06f, 0.07f, 0.09f, 1.f)), 10.f);
    const ImU32 grey = ImGui::GetColorU32(ImVec4(0.55f, 0.58f, 0.62f, 1.f)), body = ImGui::GetColorU32(ImVec4(0.14f, 0.16f, 0.19f, 1.f));
    const ImU32 light = ImGui::GetColorU32(ImVec4(0.78f, 0.79f, 0.81f, 1.f)), selectedRing = ImGui::GetColorU32(ImVec4(0.30f, 0.55f, 1.f, 1.f));
    for (size_t i = 0; i < dev.parts.size(); ++i) {
        const LightPart& p = dev.parts[i];
        const ImVec2 a(o.x + p.x * picW, o.y + p.y * picH), b(a.x + p.w * picW, a.y + p.h * picH);
        const LightRgb* tc = colorOf(p.textLed);
        const LightRgb* bc = colorOf(p.barLed);
        const ImU32 textCol = tc ? ledColor(*tc) : grey;
        const bool sel = lightSelected_ != 0 && (p.textLed == lightSelected_ || p.barLed == lightSelected_);
        const ImVec2 ts = ImGui::CalcTextSize(p.text.c_str());
        if (p.shape == "knob") {
            const float r = std::min(b.x - a.x, b.y - a.y) * 0.5f;
            dl->AddCircleFilled(ImVec2((a.x + b.x) / 2, (a.y + b.y) / 2), r, light, 32);
        } else if (p.shape == "lever") {
            dl->AddRectFilled(a, b, body, 8.f);
            dl->AddText(ImVec2((a.x + b.x - ts.x) / 2, (a.y + b.y - ts.y) / 2), grey, p.text.c_str());
        } else if (p.shape == "key") {
            dl->AddRectFilled(a, b, body, 5.f);
            const float barW = (b.x - a.x) * 0.28f, cx = (a.x + b.x) / 2, cy = (a.y + b.y) / 2;
            dl->AddText(ImVec2(cx - ts.x - barW * 0.15f, cy - ts.y / 2), textCol, p.text.c_str());
            const ImU32 barCol = bc ? ledColor(*bc) : grey;
            for (int k = 0; k < 2; ++k) dl->AddRectFilled(ImVec2(cx + barW * 0.2f, cy - 4 + k * 6), ImVec2(cx + barW * 1.2f, cy - 2 + k * 6), barCol);
        } else if (p.shape == "rocker" || p.shape == "toggle") {
            dl->AddRectFilled(a, b, body, 6.f);
            const float cx = (a.x + b.x) / 2;
            if (p.shape == "rocker") dl->AddRectFilled(ImVec2(cx - (b.x - a.x) * 0.22f, a.y + (b.y - a.y) * 0.25f), ImVec2(cx + (b.x - a.x) * 0.22f, b.y - (b.y - a.y) * 0.25f), light, 4.f);
            else dl->AddRectFilled(ImVec2(cx - 3, a.y + (b.y - a.y) * 0.15f), ImVec2(cx + 3, a.y + (b.y - a.y) * 0.6f), light, 3.f);
            dl->AddText(ImVec2(cx - ts.x / 2, b.y - ts.y - 3), textCol, p.text.c_str());
        } else if (p.shape == "tag") {
            dl->AddRect(a, b, textCol, 3.f);
            dl->AddText(ImVec2((a.x + b.x - ts.x) / 2, (a.y + b.y - ts.y) / 2), textCol, p.text.c_str());
        } else {
            dl->AddText(ImVec2((a.x + b.x - ts.x) / 2, (a.y + b.y - ts.y) / 2), textCol, p.text.c_str());
        }
        if (sel) dl->AddRect(ImVec2(a.x - 3, a.y - 3), ImVec2(b.x + 3, b.y + 3), selectedRing, 6.f, 0, 2.f);
        if (p.textLed || p.barLed) {
            ImGui::SetCursorScreenPos(a);
            ImGui::PushID(static_cast<int>(i));
            if (ImGui::InvisibleButton("part", ImVec2(std::max(1.f, b.x - a.x), std::max(1.f, b.y - a.y)))) lightSelected_ = p.barLed ? p.barLed : p.textLed;
            ImGui::PopID();
        }
    }
    ImGui::EndChild();

    // the LEDs
    ImGui::SameLine();
    ImGui::BeginChild("lights_list", ImVec2(listW, picH + 8), true);
    ImGui::TextUnformatted("LEDs");
    ImGui::Separator();
    for (const auto& led : dev.leds) {
        ImGui::PushID(led.id);
        const LightRgb* c = colorOf(led.id);
        const ImVec4 swatch = c ? ImVec4(c->r / 255.f, c->g / 255.f, c->b / 255.f, 1.f) : ImVec4(0.3f, 0.3f, 0.3f, 1.f);
        ImGui::ColorButton("##c", swatch, ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoPicker, ImVec2(ImGui::GetFrameHeight(), ImGui::GetFrameHeight()));
        ImGui::SameLine();
        char label[96];
        snprintf(label, sizeof label, "LED %d  %s", led.id, led.name.c_str());
        if (ImGui::Selectable(label, lightSelected_ == led.id, 0, ImVec2(0, ImGui::GetFrameHeight()))) lightSelected_ = led.id;
        if (led.guess && ImGui::IsItemHovered()) ImGui::SetTooltip("Not confirmed");
        if (c) {
            ImGui::SameLine(listW - ImGui::CalcTextSize("100%").x - ImGui::GetStyle().WindowPadding.x * 2);
            ImGui::TextDisabled("%d%%", c->level * 100 / 255);
        }
        ImGui::PopID();
    }
    if (const LightRgb* c = colorOf(lightSelected_)) {
        ImGui::Separator();
        ImGui::Text("LED %d", lightSelected_);
        ImGui::TextDisabled("Color");
        ImGui::SameLine();
        ImGui::Text("#%02X%02X%02X", c->r, c->g, c->b);
        ImGui::TextDisabled("Brightness");
        ImGui::SameLine();
        ImGui::Text("%d%%", c->level * 100 / 255);
    }
    ImGui::EndChild();
    drawLightSettings(dev, key, present && supported);
}

// Saves every changed setting to the device, one after another, off the UI thread: for each, the words around it are kept as a
// backup file first and the read-back must show only that byte changed (WinwingHidManager::saveConfigByte). Stops at the first problem.
void Ui::startLightSave(const std::string& key, const std::vector<std::pair<LightSetting, int>>& changes) {
    if (lightSaving_.valid() || changes.empty()) return;
    lightSaveResult_.clear();
    lightSaveKey_ = key;
    lightSaving_ = std::async(std::launch::async, [&w = ctx_.winwing, key, changes]() -> std::string {
        namespace fs = std::filesystem;
        const fs::path dir = fs::path(stateDir()) / "winwing-backups";
        std::error_code ec;
        fs::create_directories(dir, ec);
        for (const auto& [s, value] : changes) {
            if (!s.save) continue;
            std::string error;
            auto keep = [&](const std::vector<uint8_t>& words, uint32_t from) {  // the backup, written before the device is touched
                char name[96];
                const std::time_t now = std::time(nullptr);
                std::tm tm{};
                localtime_r(&now, &tm);
                snprintf(name, sizeof name, "%s_%02x%02x_%03x_%04d%02d%02d-%02d%02d%02d.bin", key.substr(key.find(':') + 1).c_str(), s.device, s.family, from,
                         tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
                std::ofstream f(dir / name, std::ios::binary);
                f.write(reinterpret_cast<const char*>(words.data()), static_cast<std::streamsize>(words.size()));
                f.flush();
            };
            if (!w.saveConfigByte(key, s.device, s.family, s.offset, s.byte, static_cast<uint8_t>(value), keep, error)) return s.name + ": " + error;
        }
        std::vector<fs::path> files;  // the newest 20 backups are kept
        for (const auto& e : fs::directory_iterator(dir, ec))
            if (e.path().extension() == ".bin") files.push_back(e.path());
        std::sort(files.begin(), files.end());
        for (size_t i = 0; i + 20 < files.size(); ++i) fs::remove(files[i], ec);
        return {};
    });
}

// The settings the device stores, each with its usual control: change them (a level is shown live on the device), the changed ones
// are marked, and Save writes them all; Discard puts them back.
void Ui::drawLightSettings(const LightDevice& dev, const std::string& key, bool usable) {
    if (dev.settings.empty() && dev.levels.empty()) return;
    if (lightLevelsKey_ != key) {  // another device: its own edits
        lightLevelsKey_ = key;
        lightLevels_.assign(dev.levels.size(), {});
        lightPending_.assign(dev.settings.size(), -1);
        lightSaveResult_.clear();
    }
    lightLevels_.resize(dev.levels.size());
    lightPending_.resize(dev.settings.size(), -1);
    if (lightLevelSending_.valid() && lightLevelSending_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) lightLevelSending_.get();

    const bool readHere = lightsRead_ && lightsRead_->key == key;
    const bool saving = lightSaving_.valid();
    auto storedAt = [&](size_t i) { return readHere && i < lightsRead_->settings.size() ? lightsRead_->settings[i] : -1; };
    auto levelFor = [&](const std::string& name) -> int {
        for (size_t l = 0; l < dev.levels.size(); ++l)
            if (dev.levels[l].setting == name) return static_cast<int>(l);
        return -1;
    };
    auto percentOf = [](int v) { return (v * 100 + 127) / 255; };
    auto valueOf = [](int pct) { return (pct * 255 + 50) / 100; };  // as SimAppPro: 0..100 % to 0..255
    // a level is sent live: one light command at a time, the latest value wins
    auto sendLive = [&](size_t l, int value) {
        if (!usable || l >= dev.levels.size()) return;
        lightLevels_[l].percent = value;  // here: the 0..255 value to send
        if (!lightLevelSending_.valid() && lightLevels_[l].sent != value) {
            lightLevels_[l].sent = value;
            const LightLevel lv = dev.levels[l];
            lightLevelSending_ = std::async(std::launch::async, [&w = ctx_.winwing, key, lv, value] { w.setLevel(key, lv.device, lv.family, lv.index, value); });
        }
    };
    for (size_t l = 0; l < dev.levels.size(); ++l)  // a value that could not go out yet (one was on its way)
        if (lightLevels_[l].percent >= 0 && lightLevels_[l].percent != lightLevels_[l].sent) sendLive(l, lightLevels_[l].percent);

    ImGui::SeparatorText("Settings");
    bool anyChanged = false;
    for (size_t i = 0; i < dev.settings.size(); ++i) {
        const LightSetting& set = dev.settings[i];
        const int stored = storedAt(i);
        int& pending = lightPending_[i];
        if (pending >= 0 && pending == stored) pending = -1;  // set back to what the device has
        const bool changed = pending >= 0;
        anyChanged = anyChanged || changed;
        const int shown = changed ? pending : stored;
        ImGui::PushID(static_cast<int>(i));
        if (changed) dot(kYellow);
        fieldLabel(set.name.c_str());
        const bool editable = set.save && usable && !saving && stored >= 0;
        if (set.kind == "percent" && set.save) {
            int pct = shown >= 0 ? percentOf(shown) : 0;
            ImGui::SetNextItemWidth(ImGui::GetFontSize() * 14);
            ImGui::BeginDisabled(!editable);
            if (ImGui::SliderInt("##v", &pct, 0, 100, shown < 0 ? "--" : "%d%%")) {
                pending = valueOf(pct);
                if (const int l = levelFor(set.name); l >= 0) sendLive(static_cast<size_t>(l), pending);
            }
            ImGui::EndDisabled();
        } else if ((set.kind == "on_off" || set.kind == "off_on") && set.save) {
            bool on = shown >= 0 && (set.kind == "on_off" ? shown != 0 : shown == 0);
            ImGui::BeginDisabled(!editable);
            if (toggleSwitch(shown < 0 ? "--" : on ? "On" : "Off", &on)) pending = set.kind == "on_off" ? (on ? 1 : 0) : (on ? 0 : 1);
            ImGui::EndDisabled();
        } else {
            ImGui::TextUnformatted(shown < 0 ? "--" : describeLightSetting(set, shown).c_str());
        }
        ImGui::PopID();
    }
    // a light that is set live only (no stored setting behind it)
    for (size_t l = 0; l < dev.levels.size(); ++l) {
        const LightLevel& lv = dev.levels[l];
        if (!lv.setting.empty()) continue;
        ImGui::PushID(static_cast<int>(1000 + l));
        fieldLabel(lv.name.c_str());
        int pct = lightLevels_[l].percent >= 0 ? percentOf(lightLevels_[l].percent) : 0;
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 14);
        ImGui::BeginDisabled(!usable);
        if (ImGui::SliderInt("##live", &pct, 0, 100, "%d%%")) sendLive(l, valueOf(pct));
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Not saved: lasts until the device is unplugged");
        ImGui::PopID();
    }

    const bool savable = std::any_of(dev.settings.begin(), dev.settings.end(), [](const LightSetting& c) { return c.save; });
    if (!savable) return;
    const float fs = ImGui::GetFontSize();
    ImGui::BeginDisabled(!anyChanged || saving || !usable);
    if (ImGui::Button("Save", ImVec2(fs * 9, 0))) {
        std::vector<std::pair<LightSetting, int>> changes;
        for (size_t i = 0; i < dev.settings.size(); ++i)
            if (lightPending_[i] >= 0 && dev.settings[i].save) changes.push_back({dev.settings[i], lightPending_[i]});
        startLightSave(key, changes);
    }
    ImGui::SameLine();
    if (ImGui::Button("Discard", ImVec2(fs * 9, 0))) {
        for (size_t i = 0; i < dev.settings.size(); ++i) {
            if (lightPending_[i] < 0) continue;
            const int l = levelFor(dev.settings[i].name), stored = storedAt(i);
            if (l >= 0 && stored >= 0) sendLive(static_cast<size_t>(l), stored);  // the light back as the device has it
            lightPending_[i] = -1;
        }
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (saving) ImGui::TextDisabled("Saving");
    else if (!lightSaveResult_.empty()) {
        if (lightSaveResult_ == "Saved") ImGui::TextDisabled("Saved");
        else ImGui::TextColored(kYellow, "%s", lightSaveResult_.c_str());
    }
}

}  // namespace lr::app
