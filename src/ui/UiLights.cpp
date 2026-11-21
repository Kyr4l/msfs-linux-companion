// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kyr4l
// Lights: the colours stored in a device, drawn on a picture of it (data/*_lights.json). Read only: nothing is written to the device.
#include "ui/Ui.hpp"
#include "ui/UiCommon.hpp"
#include "ui/Theme.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>

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
            return r;
        });
    };
    if (present && supported && !reading && (!lightsRead_ || lightsRead_->key != key)) startRead();  // the first look reads it

    ImGui::SameLine(ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX() - ImGui::CalcTextSize("Refresh").x - ImGui::GetStyle().FramePadding.x * 2 -
                    ImGui::CalcTextSize("Read only").x - ImGui::GetStyle().ItemSpacing.x * 2);
    ImGui::TextColored(kYellow, "Read only");
    ImGui::SameLine();
    ImGui::BeginDisabled(!present || !supported || reading);
    if (ImGui::Button("Refresh")) startRead();
    ImGui::EndDisabled();

    if (!supported) ImGui::TextColored(kYellow, "Driver %s not supported", dev.driver.c_str());
    else if (!present) ImGui::TextDisabled("Not connected");
    else if (reading) ImGui::TextDisabled("Reading");
    else if (lightsRead_ && !lightsRead_->error.empty()) ImGui::TextColored(kYellow, "%s", lightsRead_->error.c_str());

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
}

}  // namespace lr::app
