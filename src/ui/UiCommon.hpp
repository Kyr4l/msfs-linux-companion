// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kyr4l
#pragma once
// Colours and small widgets shared by the files that draw the tabs (Ui*.cpp).
#include <algorithm>
#include <cctype>
#include <chrono>
#include <ctime>
#include <string>
#include <vector>

#include <imgui.h>

#include "ui/Ui.hpp"

namespace lr::app::uidetail {

inline const ImVec4 kGreen(0.35f, 0.85f, 0.40f, 1.0f);
inline const ImVec4 kYellow(0.95f, 0.80f, 0.25f, 1.0f);
inline const ImVec4 kRed(0.95f, 0.35f, 0.35f, 1.0f);
inline const ImVec4 kGrey(0.6f, 0.6f, 0.6f, 1.0f);

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
