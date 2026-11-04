// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kyr4l
#pragma once
// Colours and small widgets shared by the files that draw the tabs (Ui*.cpp).
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>

#include <imgui.h>

#include "ui/Theme.hpp"
#include "ui/Ui.hpp"

namespace lr::app::uidetail {

inline const ImVec4 kGreen(0.36f, 0.82f, 0.50f, 1.0f);
inline const ImVec4 kYellow(0.96f, 0.76f, 0.30f, 1.0f);
inline const ImVec4 kRed(0.96f, 0.40f, 0.40f, 1.0f);
inline const ImVec4 kGrey(0.56f, 0.60f, 0.67f, 1.0f);
using ui::palette::kAccent;
using ui::palette::kCode;

inline ImVec4 withAlpha(ImVec4 c, float a) { c.w = a; return c; }

// A switch for a setting that is on or off (bindings active, vibration on). Draws its label to the right.
inline bool toggleSwitch(const char* label, bool* v) {
    const float h = ImGui::GetFrameHeight() * 0.78f, w = h * 1.8f;
    ImGui::PushID(label);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float y = p.y + (ImGui::GetFrameHeight() - h) * 0.5f;
    bool clicked = ImGui::InvisibleButton("##switch", ImVec2(w, ImGui::GetFrameHeight()));
    if (clicked) *v = !*v;
    const bool hovered = ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec4 track = *v ? kAccent : ImGui::GetStyleColorVec4(hovered ? ImGuiCol_FrameBgHovered : ImGuiCol_FrameBgActive);
    dl->AddRectFilled(ImVec2(p.x, y), ImVec2(p.x + w, y + h), ImGui::GetColorU32(track), h * 0.5f);
    const float r = h * 0.5f - 2.5f;
    dl->AddCircleFilled(ImVec2(*v ? p.x + w - h * 0.5f : p.x + h * 0.5f, y + h * 0.5f), r, IM_COL32(240, 243, 248, 255), 20);
    const char* end = std::strstr(label, "##");
    if (!end) end = label + std::strlen(label);
    if (end != label) {
        ImGui::SameLine(0, ImGui::GetStyle().ItemInnerSpacing.x);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(label, end);
        if (ImGui::IsItemClicked()) { *v = !*v; clicked = true; }
    }
    ImGui::PopID();
    return clicked;
}

// A rounded badge with a coloured dot: the state of the simulator, MobiFlight, the aircraft. True when hovered.
inline bool pill(const ImVec4& dotColor, const char* text) {
    const float fs = ImGui::GetFontSize(), padX = fs * 0.7f, h = ImGui::GetFrameHeight();
    const ImVec2 ts = ImGui::CalcTextSize(text);
    const float dotD = dotColor.w > 0 ? fs * 0.5f : 0.0f;
    const ImVec2 size(padX * 2 + (dotD > 0 ? dotD + fs * 0.45f : 0) + ts.x, h);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, ImVec2(p.x + size.x, p.y + size.y), ImGui::GetColorU32(ui::palette::kSurfaceHi), h * 0.5f);
    float x = p.x + padX;
    if (dotD > 0) {
        dl->AddCircleFilled(ImVec2(x + dotD * 0.5f, p.y + h * 0.5f), dotD * 0.5f, ImGui::GetColorU32(dotColor), 16);
        x += dotD + fs * 0.45f;
    }
    dl->AddText(ImVec2(x, p.y + (h - ts.y) * 0.5f), ImGui::GetColorU32(ImGuiCol_Text), text);
    ImGui::Dummy(size);
    return ImGui::IsItemHovered();
}

// A small square button with three dots, for a menu of the less used commands of a row. True when clicked.
inline bool dotsButton(const char* id) {
    const float h = ImGui::GetFrameHeight();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const bool clicked = ImGui::InvisibleButton(id, ImVec2(h, h));
    const bool hovered = ImGui::IsItemHovered(), active = ImGui::IsItemActive();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (hovered || active) dl->AddRectFilled(p, ImVec2(p.x + h, p.y + h), ImGui::GetColorU32(active ? ImGuiCol_ButtonActive : ImGuiCol_ButtonHovered), ImGui::GetStyle().FrameRounding);
    const ImU32 col = ImGui::GetColorU32(hovered ? ImGuiCol_Text : ImGuiCol_TextDisabled);
    for (int i = -1; i <= 1; ++i) dl->AddCircleFilled(ImVec2(p.x + h * 0.5f + i * h * 0.22f, p.y + h * 0.5f), h * 0.07f + 0.6f, col, 8);
    return clicked;
}

// The title of a page, in the large size of the font.
inline void pageTitle(const char* text) {
    ImGui::PushFont(nullptr, ImGui::GetFontSize() * 1.45f);
    ImGui::TextUnformatted(text);
    ImGui::PopFont();
}

// An event or variable name, in the colour of code.
inline void codeText(const char* text) { ImGui::TextColored(kCode, "%s", text); }

inline void dot(const ImVec4& color) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetCursorScreenPos();
    float h = ImGui::GetTextLineHeight();
    dl->AddCircleFilled(ImVec2(p.x + h * 0.5f, p.y + h * 0.5f), h * 0.3f, ImGui::GetColorU32(color));
    ImGui::Dummy(ImVec2(h, h));
    ImGui::SameLine();
}

inline const char* connStateText(sim::ConnState s) {
    switch (s) {
        case sim::ConnState::Disconnected: return "Disconnected";
        case sim::ConnState::Connecting: return "Connecting";
        case sim::ConnState::Opening: return "Handshaking";
        case sim::ConnState::Connected: return "Connected";
    }
    return "?";
}

inline std::string baseName(const std::string& path) {
    size_t p = path.find_last_of("/\\");
    return p == std::string::npos ? path : path.substr(p + 1);
}

// The words of a search box, lower case: a row matches when every word is somewhere in its text.
inline std::vector<std::string> searchWords(const std::string& text) {
    std::vector<std::string> words;
    std::string word;
    for (unsigned char c : text + ' ') {
        if (c != ' ') {
            word += static_cast<char>(std::tolower(c));
        } else if (!word.empty()) {
            words.push_back(std::move(word));
            word.clear();
        }
    }
    return words;
}

inline bool matchesAll(std::string text, const std::vector<std::string>& words) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return std::all_of(words.begin(), words.end(), [&](const std::string& w) { return text.find(w) != std::string::npos; });
}

// The name a device reports, without a word said twice in a row ("WINCTRL WINCTRL URSA MINOR" -> "WINCTRL URSA MINOR").
inline std::string tidyDeviceName(const std::string& name) {
    std::string out, word, previous;
    for (char c : name + ' ') {
        if (c != ' ') {
            word += c;
            continue;
        }
        if (!word.empty() && word != previous) out += (out.empty() ? "" : " ") + word;
        if (!word.empty()) previous = word;
        word.clear();
    }
    return out;
}

inline std::string formatTime(std::chrono::system_clock::time_point tp) {
    std::time_t t = std::chrono::system_clock::to_time_t(tp);
    std::tm tm{};
    localtime_r(&t, &tm);
    char buf[16];
    strftime(buf, sizeof buf, "%H:%M:%S", &tm);
    return buf;
}

// A label on the left and the widget at a fixed column, so a form reads as a list: call it, then draw the widget.
inline void fieldLabel(const char* text) {
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(text);
    ImGui::SameLine(ImGui::GetFontSize() * 11.0f);
}

inline bool actionKindCombo(const char* label, ActionKind& kind) {
    static const ActionKind kinds[] = {ActionKind::SimEvent, ActionKind::InputEvent, ActionKind::InputEventToggle, ActionKind::HEvent,
                                       ActionKind::LVarSet, ActionKind::LVarToggle, ActionKind::CalcCode, ActionKind::Delay, ActionKind::Plugin};
    constexpr int n = 9;
    int idx = 0;
    for (int i = 0; i < n; ++i)
        if (kinds[i] == kind) idx = i;
    bool changed = false;
    if (ImGui::BeginCombo(label, actionKindLabel(kind))) {
        for (int i = 0; i < n; ++i) {
            if (ImGui::Selectable(actionKindLabel(kinds[i]), i == idx)) { kind = kinds[i]; changed = true; }
        }
        ImGui::EndCombo();
    }
    return changed;
}

// The little picture of a control in the panel views: 0 push button, 1 knob, 2 selector.
inline void drawControlIcon(ImDrawList* dl, ImVec2 ic, float ir, int style) {
    const ImU32 colDim = ImGui::GetColorU32(ImGuiCol_TextDisabled);
    if (style == 0) {  // push button: a lit cap
        dl->AddRectFilled(ImVec2(ic.x - ir, ic.y - ir * 0.8f), ImVec2(ic.x + ir, ic.y + ir * 0.8f), ImGui::GetColorU32(ImVec4(0.75f, 0.78f, 0.85f, 1)), ir * 0.25f);
        dl->AddRectFilled(ImVec2(ic.x - ir * 0.7f, ic.y - ir * 0.55f), ImVec2(ic.x + ir * 0.7f, ic.y - ir * 0.1f), ImGui::GetColorU32(ImVec4(0.30f, 0.62f, 0.95f, 1)), ir * 0.15f);
    } else if (style == 1) {  // knob: a disc with a pointer
        dl->AddCircleFilled(ic, ir, ImGui::GetColorU32(ImVec4(0.92f, 0.62f, 0.20f, 1)), 24);
        dl->AddLine(ic, ImVec2(ic.x + ir * 0.55f, ic.y - ir * 0.75f), ImGui::GetColorU32(ImVec4(0.1f, 0.1f, 0.1f, 1)), 2.0f);
    } else {  // selector: a disc with position ticks
        dl->AddCircleFilled(ic, ir, ImGui::GetColorU32(ImVec4(0.45f, 0.78f, 0.55f, 1)), 24);
        for (int t = -1; t <= 1; ++t) dl->AddCircleFilled(ImVec2(ic.x + t * ir * 0.9f, ic.y - ir * 1.1f), 1.8f, colDim);
        dl->AddLine(ic, ImVec2(ic.x, ic.y - ir * 0.85f), ImGui::GetColorU32(ImVec4(0.1f, 0.1f, 0.1f, 1)), 2.0f);
    }
}

}  // namespace lr::app::uidetail
