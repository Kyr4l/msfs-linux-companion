// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kyr4l
// The Desktop page: every input of a device with its live position on the left, what the selected one does on the right.
#include "ui/Ui.hpp"
#include "ui/UiCommon.hpp"
#include "ui/Theme.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <map>

#include <imgui.h>
#include <imgui_stdlib.h>

#include <linux/input-event-codes.h>

#include "desktop/AmgpImport.hpp"
#include "desktop/Autostart.hpp"
#include "desktop/DesktopEngine.hpp"
#include "desktop/DesktopService.hpp"

namespace lr::app {

using namespace uidetail;

namespace {
using desktop::Direction;
using desktop::Output;
using desktop::OutputKind;

struct OutputChoice {
    const char* label;
    OutputKind kind;
    int code;
};

const std::vector<OutputChoice>& outputChoices() {
    static const std::vector<OutputChoice> choices = [] {
        std::vector<OutputChoice> c = {
            {"Mouse up", OutputKind::MouseMove, static_cast<int>(Direction::Up)},
            {"Mouse down", OutputKind::MouseMove, static_cast<int>(Direction::Down)},
            {"Mouse left", OutputKind::MouseMove, static_cast<int>(Direction::Left)},
            {"Mouse right", OutputKind::MouseMove, static_cast<int>(Direction::Right)},
            {"Wheel up", OutputKind::Wheel, static_cast<int>(Direction::Up)},
            {"Wheel down", OutputKind::Wheel, static_cast<int>(Direction::Down)},
            {"Wheel left", OutputKind::Wheel, static_cast<int>(Direction::Left)},
            {"Wheel right", OutputKind::Wheel, static_cast<int>(Direction::Right)},
            {"Left click", OutputKind::MouseButton, BTN_LEFT},
            {"Right click", OutputKind::MouseButton, BTN_RIGHT},
            {"Middle click", OutputKind::MouseButton, BTN_MIDDLE},
            {"Back", OutputKind::MouseButton, BTN_SIDE},
            {"Forward", OutputKind::MouseButton, BTN_EXTRA},
        };
        for (const auto& k : shortcutKeyChoices()) c.push_back({k.label, OutputKind::Key, k.code});
        return c;
    }();
    return choices;
}

std::string outputText(const Output& o) {
    for (const auto& c : outputChoices())
        if (c.kind == o.kind && c.code == o.code) return c.label;
    return "Key " + std::to_string(o.code);
}

std::string outputsText(const desktop::Binding* b) {
    if (!b || b->outputs.empty()) return "None";
    std::string out;
    for (const auto& o : b->outputs) out += (out.empty() ? "" : " + ") + outputText(o);
    return out;
}

desktop::Binding* findBinding(desktop::Config& cfg, const input::InputId& id, int side) {
    for (auto& b : cfg.bindings)
        if (b.input == id && b.side == side) return &b;
    return nullptr;
}

struct Preset {
    const char* name;
    const char* negative;
    const char* positive;
};
constexpr Preset kPresets[] = {{"Mouse horizontal", "Mouse left", "Mouse right"}, {"Mouse vertical", "Mouse up", "Mouse down"}, {"Wheel", "Wheel down", "Wheel up"},
                               {"Volume", "Volume down", "Volume up"}, {"None", "None", "None"}};

const OutputChoice* choiceByLabel(const char* label) {
    for (const auto& c : outputChoices())
        if (std::string(c.label) == label) return &c;
    return nullptr;
}

constexpr float kUnits = static_cast<float>(desktop::kAxisUnits);

// The travel of an axis: the zone that does nothing around the centre, the zones past the maximum at both ends, the position.
void drawAxisBar(ImDrawList* dl, ImVec2 p, ImVec2 size, float reading, float dead, float maxZone, bool present) {
    const ImVec2 q(p.x + size.x, p.y + size.y);
    auto xOf = [&](float v) { return p.x + (v + 1.0f) * 0.5f * size.x; };
    dl->AddRectFilled(p, q, ImGui::GetColorU32(ui::palette::kSurface), 3.0f);
    const ImU32 endColor = ImGui::GetColorU32(withAlpha(kYellow, 0.16f));
    dl->AddRectFilled(p, ImVec2(xOf(-maxZone), q.y), endColor, 3.0f);
    dl->AddRectFilled(ImVec2(xOf(maxZone), p.y), q, endColor, 3.0f);
    dl->AddRectFilled(ImVec2(xOf(-dead), p.y), ImVec2(xOf(dead), q.y), ImGui::GetColorU32(ui::palette::kBorder));
    dl->AddLine(ImVec2(xOf(0), p.y), ImVec2(xOf(0), q.y), ImGui::GetColorU32(ui::palette::kTextDim));
    if (!present) return;
    const bool active = std::fabs(reading) >= dead;
    const float inset = size.y * 0.32f;
    const float a = std::min(xOf(0), xOf(reading)), b = std::max(xOf(0), xOf(reading));
    dl->AddRectFilled(ImVec2(a, p.y + inset), ImVec2(b, q.y - inset), ImGui::GetColorU32(active ? kAccent : ui::palette::kTextDim));
    const float x = xOf(reading);
    dl->AddRectFilled(ImVec2(x - 1.5f, p.y), ImVec2(x + 1.5f, q.y), ImGui::GetColorU32(ui::palette::kText));
}

std::string rawText(float reading) {
    char text[16];
    snprintf(text, sizeof text, "%+d", static_cast<int>(std::lround(reading * kUnits)));
    return text;
}

// A row of the list: true when it was clicked.
bool listRow(const char* id, bool selected, float height) {
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float w = ImGui::GetContentRegionAvail().x;
    const bool clicked = ImGui::InvisibleButton(id, ImVec2(w, height));
    const bool hovered = ImGui::IsItemHovered();
    if (selected || hovered)
        ImGui::GetWindowDrawList()->AddRectFilled(p, ImVec2(p.x + w, p.y + height), ImGui::GetColorU32(selected ? ui::palette::kAccentSoft : ImGui::GetStyleColorVec4(ImGuiCol_FrameBg)), ImGui::GetFontSize() * 0.3f);
    return clicked;
}
}  // namespace

const std::vector<ShortcutKeyChoice>& shortcutKeyChoices() {
    static const std::vector<ShortcutKeyChoice> keys = [] {
        std::vector<ShortcutKeyChoice> k = {
            {"Volume up", KEY_VOLUMEUP}, {"Volume down", KEY_VOLUMEDOWN}, {"Mute", KEY_MUTE}, {"Play/Pause", KEY_PLAYPAUSE}, {"Next track", KEY_NEXTSONG},
            {"Previous track", KEY_PREVIOUSSONG}, {"Stop", KEY_STOPCD}, {"Enter", KEY_ENTER}, {"Esc", KEY_ESC}, {"Tab", KEY_TAB}, {"Space", KEY_SPACE},
            {"Backspace", KEY_BACKSPACE}, {"Delete", KEY_DELETE}, {"Insert", KEY_INSERT}, {"Home", KEY_HOME}, {"End", KEY_END}, {"Page up", KEY_PAGEUP},
            {"Page down", KEY_PAGEDOWN}, {"Up", KEY_UP}, {"Down", KEY_DOWN}, {"Left", KEY_LEFT}, {"Right", KEY_RIGHT}, {"Print", KEY_SYSRQ},
            {"Ctrl", KEY_LEFTCTRL}, {"Shift", KEY_LEFTSHIFT}, {"Alt", KEY_LEFTALT}, {"Meta", KEY_LEFTMETA},
        };
        static const char* const kF[12] = {"F1", "F2", "F3", "F4", "F5", "F6", "F7", "F8", "F9", "F10", "F11", "F12"};
        static const int kFCodes[12] = {KEY_F1, KEY_F2, KEY_F3, KEY_F4, KEY_F5, KEY_F6, KEY_F7, KEY_F8, KEY_F9, KEY_F10, KEY_F11, KEY_F12};
        for (int i = 0; i < 12; ++i) k.push_back({kF[i], kFCodes[i]});
        static const char* const kL[26] = {"A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K", "L", "M", "N", "O", "P", "Q", "R", "S", "T", "U", "V", "W", "X", "Y", "Z"};
        static const int kLCodes[26] = {KEY_A, KEY_B, KEY_C, KEY_D, KEY_E, KEY_F, KEY_G, KEY_H, KEY_I, KEY_J, KEY_K, KEY_L, KEY_M,
                                        KEY_N, KEY_O, KEY_P, KEY_Q, KEY_R, KEY_S, KEY_T, KEY_U, KEY_V, KEY_W, KEY_X, KEY_Y, KEY_Z};
        for (int i = 0; i < 26; ++i) k.push_back({kL[i], kLCodes[i]});
        static const char* const kD[10] = {"0", "1", "2", "3", "4", "5", "6", "7", "8", "9"};
        static const int kDCodes[10] = {KEY_0, KEY_1, KEY_2, KEY_3, KEY_4, KEY_5, KEY_6, KEY_7, KEY_8, KEY_9};
        for (int i = 0; i < 10; ++i) k.push_back({kD[i], kDCodes[i]});
        return k;
    }();
    return keys;
}

// Waits for the next button press or a clear axis movement, then selects that input.
void Ui::listenForDesktopInput() {
    if (!desktopListening_) return;
    if (desktopBaseline_.empty()) { desktopBaseline_ = devices_; return; }
    for (const auto& d : devices_) {
        const auto base = std::find_if(desktopBaseline_.begin(), desktopBaseline_.end(), [&](const input::DeviceSnapshot& b) { return b.key == d.key; });
        if (base == desktopBaseline_.end()) continue;
        auto select = [&](input::InputKind kind, int code) {
            desktopDevice_ = d.key;
            desktopSelected_ = {d.key, kind, code, 0};
            desktopHasSelection_ = true;
            desktopListening_ = false;
        };
        for (size_t i = 0; i < d.buttonCodes.size() && i < base->buttonState.size(); ++i)
            if (d.buttonState[i] && !base->buttonState[i]) { select(input::InputKind::Button, d.buttonCodes[i]); return; }
        for (size_t i = 0; i < d.axisCodes.size() && i < base->axisValue.size(); ++i)
            if (std::fabs(d.axisValue[i] - base->axisValue[i]) > 0.25f) { select(input::InputKind::Axis, d.axisCodes[i]); return; }
    }
}

void Ui::drawDesktopList(float width) {
    desktop::Config& cfg = ctx_.settings.desktop;
    const float fs = ImGui::GetFontSize();
    ImGui::BeginChild("##desktop_list", ImVec2(width, 0), ImGuiChildFlags_Borders);

    const input::DeviceSnapshot* device = nullptr;
    for (const auto& d : devices_)
        if (d.key == desktopDevice_) device = &d;
    auto isBound = [&](const input::InputId& id) {
        return std::any_of(cfg.bindings.begin(), cfg.bindings.end(), [&](const desktop::Binding& b) { return b.input == id; });
    };
    auto select = [&](const input::InputId& id) { desktopSelected_ = id; desktopHasSelection_ = true; };
    auto isSelected = [&](const input::InputId& id) { return desktopHasSelection_ && desktopSelected_ == id; };

    // what to list: the device's own inputs, or what the bindings remember of a device that is not plugged in
    std::vector<input::InputId> axes, buttons;
    if (device) {
        for (int code : device->axisCodes) axes.push_back({device->key, input::InputKind::Axis, code, 0});
        for (int code : device->buttonCodes) buttons.push_back({device->key, input::InputKind::Button, code, 0});
        for (int hat = 0; hat < device->hatCount; ++hat)
            for (int dir = 0; dir < 4; ++dir) buttons.push_back({device->key, input::InputKind::Hat, hat, dir});
    }
    for (const auto& b : cfg.bindings) {
        if (b.input.device != desktopDevice_ || device) continue;
        auto& list = b.input.kind == input::InputKind::Axis ? axes : buttons;
        if (std::find(list.begin(), list.end(), b.input) == list.end()) list.push_back(b.input);
    }
    auto shown = [&](const input::InputId& id) { return desktopShowAll_ || isBound(id) || isSelected(id); };
    if (!desktopHasSelection_) {  // the page never opens on an empty panel when there is something to show
        for (const auto* list : {&axes, &buttons})
            for (const auto& id : *list)
                if (!desktopHasSelection_ && shown(id)) select(id);
    }

    ImGui::TextDisabled("Axes");
    const float rowH = fs * 3.0f, textDim = fs * 0.82f;
    for (size_t i = 0; i < axes.size(); ++i) {
        const input::InputId& id = axes[i];
        if (!shown(id)) continue;
        ImGui::PushID(id.code);
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float w = ImGui::GetContentRegionAvail().x;
        if (listRow("axis", isSelected(id), rowH)) select(id);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const desktop::Binding* neg = findBinding(cfg, id, -1), *pos = findBinding(cfg, id, 1);
        const desktop::Binding* any = neg ? neg : pos;
        const float now = axisNow(id);
        const float reading = now < 0 ? 0.0f : static_cast<float>(desktop::axisReading(any ? any->mode : desktop::AxisMode::Normal, now));
        const float dead = static_cast<float>(any ? any->deadZone : desktop::kDefaultDeadZone), maxZone = static_cast<float>(any ? any->maxZone : desktop::kDefaultMaxZone);
        const float nameW = fs * 4.6f, valueW = fs * 4.0f, pad = fs * 0.5f;
        const int number = device ? static_cast<int>(std::find(device->axisCodes.begin(), device->axisCodes.end(), id.code) - device->axisCodes.begin()) + 1 : static_cast<int>(i) + 1;
        dl->AddText(ImVec2(p.x + pad, p.y + fs * 0.4f), ImGui::GetColorU32(ui::palette::kText), ("Axis " + std::to_string(number)).c_str());
        dl->AddText(nullptr, textDim, ImVec2(p.x + pad, p.y + fs * 1.6f), ImGui::GetColorU32(ui::palette::kTextDim), input::axisName(id.code));
        const ImVec2 barP(p.x + pad + nameW, p.y + fs * 0.45f), barS(w - nameW - valueW - pad * 3, fs * 0.95f);
        drawAxisBar(dl, barP, barS, reading, dead, maxZone, now >= 0);
        const std::string value = now < 0 ? "--" : rawText(reading);
        dl->AddText(ImVec2(p.x + w - pad - ImGui::CalcTextSize(value.c_str()).x, p.y + fs * 0.4f), ImGui::GetColorU32(ui::palette::kText), value.c_str());
        const std::string negText = outputsText(neg), posText = outputsText(pos);
        dl->AddText(nullptr, textDim, ImVec2(barP.x, p.y + fs * 1.6f), ImGui::GetColorU32(ui::palette::kTextDim), negText.c_str());
        const float posW = ImGui::CalcTextSize(posText.c_str()).x * 0.82f;
        dl->AddText(nullptr, textDim, ImVec2(barP.x + barS.x - posW, p.y + fs * 1.6f), ImGui::GetColorU32(ui::palette::kTextDim), posText.c_str());
        ImGui::PopID();
    }

    ImGui::Spacing();
    ImGui::TextDisabled("Buttons");
    for (const input::InputId& id : buttons) {
        if (!shown(id)) continue;
        ImGui::PushID(id.code * 8 + id.dir + (id.kind == input::InputKind::Hat ? 100000 : 0));
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float w = ImGui::GetContentRegionAvail().x, h = fs * 1.9f;
        if (listRow("button", isSelected(id), h)) select(id);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const float y = p.y + (h - fs) * 0.5f, pad = fs * 0.5f;
        dl->AddText(ImVec2(p.x + pad, y), ImGui::GetColorU32(ui::palette::kText), ctx_.input.describe(id).c_str());
        dl->AddText(ImVec2(p.x + pad + fs * 4.6f, y), ImGui::GetColorU32(ui::palette::kTextDim), outputsText(findBinding(cfg, id, 0)).c_str());
        dl->AddCircleFilled(ImVec2(p.x + w - pad - fs * 0.3f, p.y + h * 0.5f), fs * 0.3f, ImGui::GetColorU32(inputIsPressed(id) ? kGreen : ui::palette::kBorder));
        ImGui::PopID();
    }
    if (axes.empty() && buttons.empty()) ImGui::TextDisabled("No device");
    ImGui::EndChild();
}

void Ui::drawDesktopAxis(const input::InputId& axis) {
    desktop::Config& cfg = ctx_.settings.desktop;
    const float fs = ImGui::GetFontSize();
    auto apply = [&] {
        ctx_.pushDesktopConfig();
        ctx_.markDirty();
    };
    desktop::Binding* neg = findBinding(cfg, axis, -1), *pos = findBinding(cfg, axis, 1);
    const desktop::Binding* any = neg ? neg : pos;
    const desktop::Binding defaults;
    const desktop::Binding& shared = any ? *any : defaults;
    auto forSides = [&](auto&& fn) {
        for (auto& b : cfg.bindings)
            if (b.input == axis) fn(b);
        apply();
    };

    const float now = axisNow(axis);
    const float reading = now < 0 ? 0.0f : static_cast<float>(desktop::axisReading(shared.mode, now));
    const float dead = static_cast<float>(shared.deadZone), maxZone = static_cast<float>(shared.maxZone);

    // heading: the axis, its value, what it does right now
    ImGui::PushFont(nullptr, fs * 1.25f);
    ImGui::Text("%s", ctx_.input.describe(axis).c_str());
    ImGui::PopFont();
    {
        const desktop::Binding* side = reading < 0 ? neg : pos;
        std::string out = "Idle";
        if (now >= 0 && side && std::fabs(reading) >= dead && !side->outputs.empty()) {
            const Output& o = side->outputs[0];
            out = outputText(o);
            if (o.kind == OutputKind::MouseMove) {
                const double amount = std::clamp((std::fabs(reading) - dead) / std::max(0.001f, maxZone - dead), 0.0f, 1.0f);
                out += "  " + std::to_string(static_cast<int>(std::lround(desktop::applyCurve(side->curve, amount, side->sensitivity) * side->mouseSpeed * 20))) + " px/s";
            } else if (o.kind == OutputKind::Wheel) {
                const double amount = std::clamp((std::fabs(reading) - dead) / std::max(0.001f, maxZone - dead), 0.0f, 1.0f);
                char rate[16];
                snprintf(rate, sizeof rate, "  %.1f/s", desktop::applyCurve(side->curve, amount, side->sensitivity) * side->wheelSpeed);
                out += rate;
            }
        }
        ImGui::SameLine();
        ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX() + fs, ImGui::GetContentRegionMax().x - fs * 15.0f));
        ImGui::Text("%s", now < 0 ? "--" : rawText(reading).c_str());
        ImGui::SameLine(0, fs);
        pill(out == "Idle" ? kGrey : kGreen, out.c_str());
    }
    ImGui::Spacing();

    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float w = ImGui::GetContentRegionAvail().x, h = fs * 2.6f;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    drawAxisBar(dl, p, ImVec2(w, h), reading, dead, maxZone, now >= 0);
    ImGui::Dummy(ImVec2(w, h));
    ImGui::TextDisabled("-32767");
    ImGui::SameLine(w * 0.5f);
    ImGui::TextDisabled("0");
    ImGui::SameLine(w - ImGui::CalcTextSize("+32767").x);
    ImGui::TextDisabled("+32767");
    ImGui::Spacing();

    ImGui::BeginDisabled(!any);
    int deadRaw = static_cast<int>(std::lround(shared.deadZone * desktop::kAxisUnits)), maxRaw = static_cast<int>(std::lround(shared.maxZone * desktop::kAxisUnits));
    fieldLabel("Dead zone");
    ImGui::SetNextItemWidth(fs * 8);
    if (ImGui::InputInt("##dead", &deadRaw, 100, 1000)) {
        const double v = std::clamp(deadRaw, 0, maxRaw - 100) / desktop::kAxisUnits;
        forSides([&](desktop::Binding& b) { b.deadZone = v; });
    }
    fieldLabel("Max zone");
    ImGui::SetNextItemWidth(fs * 8);
    if (ImGui::InputInt("##max", &maxRaw, 100, 1000)) {
        const double v = std::clamp(maxRaw, deadRaw + 100, 32767) / desktop::kAxisUnits;
        forSides([&](desktop::Binding& b) { b.maxZone = v; });
    }

    static const std::pair<desktop::AxisMode, const char*> kModes[] = {{desktop::AxisMode::Normal, "Normal"}, {desktop::AxisMode::Negative, "Negative"}, {desktop::AxisMode::Positive, "Positive"},
                                                                       {desktop::AxisMode::NegativeHalf, "Neg half"}, {desktop::AxisMode::PositiveHalf, "Pos half"}};
    fieldLabel("Mode");
    for (const auto& [mode, label] : kModes) {
        const bool on = shared.mode == mode;
        if (on) ImGui::PushStyleColor(ImGuiCol_Button, ui::palette::kAccentSoft);
        if (ImGui::Button(label)) forSides([m = mode](desktop::Binding& b) { b.mode = m; });
        if (on) ImGui::PopStyleColor();
        ImGui::SameLine();
    }
    ImGui::NewLine();
    ImGui::EndDisabled();

    // what each side does
    auto setSide = [&](int side, const OutputChoice* choice) {
        desktop::Binding* b = findBinding(cfg, axis, side);
        if (!choice) {
            if (b) cfg.bindings.erase(cfg.bindings.begin() + (b - cfg.bindings.data()));
            return;
        }
        if (!b) {
            if (cfg.bindings.size() >= desktop::kMaxBindings) return;
            desktop::Binding fresh = shared;  // the other side's dead zone, mode and curve
            fresh.label.clear();
            fresh.input = axis;
            fresh.side = side;
            cfg.bindings.push_back(std::move(fresh));
            b = &cfg.bindings.back();
        }
        b->outputs = {Output{choice->kind, choice->code}};
    };
    const std::string negText = outputsText(neg), posText = outputsText(pos);
    const char* presetName = "Custom";
    for (const Preset& pr : kPresets)
        if (negText == pr.negative && posText == pr.positive) presetName = pr.name;
    fieldLabel("Preset");
    ImGui::SetNextItemWidth(fs * 14);
    if (ImGui::BeginCombo("##preset", presetName)) {
        for (const Preset& pr : kPresets)
            if (ImGui::Selectable(pr.name, std::string(presetName) == pr.name)) {
                setSide(-1, choiceByLabel(pr.negative));
                setSide(1, choiceByLabel(pr.positive));
                apply();
            }
        ImGui::EndCombo();
    }
    auto sideCombo = [&](const char* id, const char* title, int side, const std::string& current) {
        fieldLabel(title);
        ImGui::SetNextItemWidth(fs * 14);
        if (!ImGui::BeginCombo(id, current.c_str())) return;
        if (ImGui::Selectable("None", current == "None")) { setSide(side, nullptr); apply(); }
        for (const auto& c : outputChoices())
            if (ImGui::Selectable(c.label, current == c.label)) { setSide(side, &c); apply(); }
        ImGui::EndCombo();
    };
    sideCombo("##neg", "Negative", -1, negText);
    sideCombo("##pos", "Positive", 1, posText);

    // refreshed: choosing a side above may have moved the bindings
    neg = findBinding(cfg, axis, -1);
    pos = findBinding(cfg, axis, 1);
    any = neg ? neg : pos;
    if (!any) return;

    ImGui::SeparatorText("Speed");
    const bool mouse = (neg && !neg->outputs.empty() && neg->outputs[0].kind == OutputKind::MouseMove) || (pos && !pos->outputs.empty() && pos->outputs[0].kind == OutputKind::MouseMove);
    const bool wheel = (neg && !neg->outputs.empty() && neg->outputs[0].kind == OutputKind::Wheel) || (pos && !pos->outputs.empty() && pos->outputs[0].kind == OutputKind::Wheel);
    if (mouse) {
        int speed = any->mouseSpeed;
        fieldLabel("Mouse");
        ImGui::SetNextItemWidth(fs * 14);
        if (ImGui::SliderInt("##mouse_speed", &speed, 1, 300)) forSides([&](desktop::Binding& b) { b.mouseSpeed = speed; });
    }
    if (wheel) {
        int speed = any->wheelSpeed;
        fieldLabel("Wheel max");
        ImGui::SetNextItemWidth(fs * 14);
        if (ImGui::SliderInt("##wheel_speed", &speed, 1, 100, "%d/s")) forSides([&](desktop::Binding& b) { b.wheelSpeed = speed; });
    }
    if (!mouse && !wheel) return;

    static const std::pair<desktop::MouseCurve, const char*> kCurves[] = {{desktop::MouseCurve::EnhancedPrecision, "Enhanced precision"}, {desktop::MouseCurve::Linear, "Linear"}, {desktop::MouseCurve::Quadratic, "Quadratic"},
                                                                         {desktop::MouseCurve::Cubic, "Cubic"}, {desktop::MouseCurve::QuadraticExtreme, "Quadratic extreme"}, {desktop::MouseCurve::Power, "Power"}};
    const char* curveName = kCurves[0].second;
    for (const auto& [curve, label] : kCurves)
        if (curve == any->curve) curveName = label;
    fieldLabel("Curve");
    ImGui::SetNextItemWidth(fs * 14);
    if (ImGui::BeginCombo("##curve", curveName)) {
        for (const auto& [curve, label] : kCurves)
            if (ImGui::Selectable(label, curve == any->curve)) forSides([c = curve](desktop::Binding& b) { b.curve = c; });
        ImGui::EndCombo();
    }
    any = neg ? findBinding(cfg, axis, -1) : findBinding(cfg, axis, 1);
    if (any->curve == desktop::MouseCurve::Power) {
        float sensitivity = static_cast<float>(any->sensitivity);
        fieldLabel("Sensitivity");
        ImGui::SetNextItemWidth(fs * 14);
        if (ImGui::SliderFloat("##sens", &sensitivity, 0.1f, 5.0f, "%.1f")) forSides([&](desktop::Binding& b) { b.sensitivity = sensitivity; });
    }
    // the curve: speed over the travel between the dead zone and the maximum, with where the axis is now
    const ImVec2 g = ImGui::GetCursorScreenPos();
    const ImVec2 gs(std::min(w, fs * 20.0f), fs * 7.0f);
    dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(g, ImVec2(g.x + gs.x, g.y + gs.y), ImGui::GetColorU32(ui::palette::kSurface), 4.0f);
    auto pointAt = [&](double d) {
        const double y = std::min(1.0, desktop::applyCurve(any->curve, d, any->sensitivity));
        return ImVec2(g.x + static_cast<float>(d) * gs.x, g.y + gs.y - static_cast<float>(y) * (gs.y - 3.0f));
    };
    ImVec2 points[41];
    for (int i = 0; i <= 40; ++i) points[i] = pointAt(i / 40.0);
    dl->AddPolyline(points, 41, ImGui::GetColorU32(kAccent), 0, 2.0f);
    if (now >= 0 && std::fabs(reading) >= dead) dl->AddCircleFilled(pointAt(std::clamp((std::fabs(reading) - dead) / std::max(0.001f, maxZone - dead), 0.0f, 1.0f)), fs * 0.3f, ImGui::GetColorU32(ui::palette::kText));
    ImGui::Dummy(gs);
}

void Ui::drawDesktopButton(const input::InputId& button) {
    desktop::Config& cfg = ctx_.settings.desktop;
    const float fs = ImGui::GetFontSize();
    ImGui::PushFont(nullptr, fs * 1.25f);
    ImGui::Text("%s", ctx_.input.describe(button).c_str());
    ImGui::PopFont();
    ImGui::SameLine(0, fs);
    pill(inputIsPressed(button) ? kGreen : kGrey, inputIsPressed(button) ? "Pressed" : "Released");
    ImGui::Spacing();
    desktop::Binding* b = findBinding(cfg, button, 0);
    const std::string current = outputsText(b);
    fieldLabel("Action");
    ImGui::SetNextItemWidth(fs * 14);
    if (ImGui::BeginCombo("##action", current.c_str())) {
        if (ImGui::Selectable("None", current == "None") && b) {
            cfg.bindings.erase(cfg.bindings.begin() + (b - cfg.bindings.data()));
            ctx_.pushDesktopConfig();
            ctx_.markDirty();
        }
        for (const auto& c : outputChoices())
            if (ImGui::Selectable(c.label, current == c.label)) {
                if (!b && cfg.bindings.size() < desktop::kMaxBindings) {
                    desktop::Binding fresh;
                    fresh.input = button;
                    cfg.bindings.push_back(std::move(fresh));
                    b = &cfg.bindings.back();
                }
                if (b) b->outputs = {Output{c.kind, c.code}};
                ctx_.pushDesktopConfig();
                ctx_.markDirty();
            }
        ImGui::EndCombo();
    }
}

void Ui::drawDesktopTab() {
    if (!ctx_.desktop) return;
    desktop::Config& cfg = ctx_.settings.desktop;
    const desktop::ServiceStatus st = ctx_.desktop->status();
    const float fs = ImGui::GetFontSize();
    auto apply = [&] {
        ctx_.pushDesktopConfig();
        ctx_.markDirty();
    };

    listenForDesktopInput();
    if (desktopDevice_.empty()) {  // the device that has bindings, else the first one there is
        if (!cfg.bindings.empty()) desktopDevice_ = cfg.bindings[0].input.device;
        else if (!devices_.empty()) desktopDevice_ = devices_[0].key;
    }

    if (toggleSwitch("Desktop controls", &cfg.enabled)) apply();
    ImGui::SameLine(0, fs);
    if (!st.error.empty()) pill(kRed, st.error.c_str());
    else if (!cfg.enabled) pill(kGrey, "Off");
    else if (st.simRunning) pill(kYellow, "Paused: MSFS running");
    else if (st.controlsActive) pill(kGreen, "Active");
    else pill(kYellow, "Starting");

    ImGui::SameLine(0, fs);
    ImGui::SetNextItemWidth(fs * 16);
    if (ImGui::BeginCombo("##device", desktopDevice_.empty() ? "No device" : deviceDisplayName(desktopDevice_).c_str())) {
        for (const auto& d : devices_)
            if (ImGui::Selectable(tidyDeviceName(d.name).c_str(), d.key == desktopDevice_)) { desktopDevice_ = d.key; desktopHasSelection_ = false; }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("Bound", !desktopShowAll_)) desktopShowAll_ = false;
    ImGui::SameLine();
    if (ImGui::RadioButton("All", desktopShowAll_)) desktopShowAll_ = true;
    ImGui::SameLine(0, fs);
    if (ImGui::Button(desktopListening_ ? "Press or move" : "Detect")) {
        desktopListening_ = !desktopListening_;
        desktopBaseline_.clear();
    }
    ImGui::SameLine();
    if (ImGui::Button("Import")) ImGui::OpenPopup("desktop_import");
    ImGui::SameLine();
    if (ImGui::Button("Background")) ImGui::OpenPopup("desktop_background");

    if (ImGui::BeginPopup("desktop_import")) {
        if (desktopImportPath_.empty()) {
            const char* home = std::getenv("HOME");
            desktopImportPath_ = cfg.importedFrom.empty() ? std::string(home ? home : "") + "/Documents/antimicrox/desktop.joystick.amgp" : cfg.importedFrom;
        }
        ImGui::SetNextItemWidth(fs * 26);
        ImGui::InputText("##amgp", &desktopImportPath_);
        ImGui::SameLine();
        if (ImGui::Button("Import##go")) {
            const desktop::ImportResult r = desktop::importAmgpFile(desktopImportPath_, devices_);
            desktopImportNotes_ = r.notes;
            desktopImportError_ = r.error;
            desktopImportDone_ = r.ok;
            if (r.ok) {
                cfg.bindings = r.bindings;
                cfg.importedFrom = desktopImportPath_;
                if (!cfg.bindings.empty()) desktopDevice_ = cfg.bindings[0].input.device;
                apply();
            }
        }
        if (!desktopImportError_.empty()) ImGui::TextColored(kRed, "%s", desktopImportError_.c_str());
        else if (desktopImportDone_) ImGui::TextColored(kGreen, "%zu bindings imported", cfg.bindings.size());
        for (const auto& n : desktopImportNotes_) ImGui::TextColored(kYellow, "%s", n.c_str());
        ImGui::EndPopup();
    }
    if (ImGui::BeginPopup("desktop_background")) {
        if (ImGui::Checkbox("Close to tray", &ctx_.settings.closeToTray)) ctx_.markDirty();
        bool autostart = desktop::autostartEnabled();
        if (ImGui::Checkbox("Start at login", &autostart)) {
            std::string err;
            if (!desktop::setAutostart(autostart, err)) desktopImportError_ = err;
        }
        if (desktop::antimicroxAutostarts()) {
            ImGui::TextColored(kYellow, "antiMicroX starts at login");
            ImGui::SameLine();
            if (ImGui::SmallButton("Disable")) {
                std::string err;
                if (!desktop::disableAntimicroxAutostart(err)) desktopImportError_ = err;
            }
        }
        ImGui::EndPopup();
    }
    ImGui::Spacing();

    const float listW = std::max(fs * 22.0f, ImGui::GetContentRegionAvail().x * 0.42f);
    drawDesktopList(listW);
    ImGui::SameLine();
    ImGui::BeginChild("##desktop_inspector", ImVec2(0, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding);
    if (!desktopHasSelection_) ImGui::TextDisabled("Select an input");
    else if (desktopSelected_.kind == input::InputKind::Axis) drawDesktopAxis(desktopSelected_);
    else drawDesktopButton(desktopSelected_);
    ImGui::EndChild();
}

}  // namespace lr::app
