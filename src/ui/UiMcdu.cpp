// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kyr4l
// The MCDU page: a panel laid out like the real unit, what its side keys do, and the unit's settings.
#include "ui/Ui.hpp"
#include "ui/UiCommon.hpp"
#include "ui/Theme.hpp"

#include <algorithm>
#include <cfloat>

#include <imgui.h>
#include <imgui_stdlib.h>

#include "desktop/DesktopService.hpp"
#include "mcdu/McduPages.hpp"

namespace lr::app {

using namespace uidetail;

namespace {
ImU32 cellColor(mcdu::Color c) {
    switch (c) {
        case mcdu::Color::Black: return IM_COL32(60, 60, 60, 255);
        case mcdu::Color::Amber: return IM_COL32(255, 165, 0, 255);
        case mcdu::Color::White: return IM_COL32(240, 240, 240, 255);
        case mcdu::Color::Cyan: return IM_COL32(0, 225, 255, 255);
        case mcdu::Color::Green: return IM_COL32(60, 255, 90, 255);
        case mcdu::Color::Magenta: return IM_COL32(255, 80, 255, 255);
        case mcdu::Color::Red: return IM_COL32(255, 60, 60, 255);
        case mcdu::Color::Yellow: return IM_COL32(255, 240, 60, 255);
        case mcdu::Color::Brown: return IM_COL32(170, 110, 60, 255);
        case mcdu::Color::Grey: return IM_COL32(140, 140, 140, 255);
        case mcdu::Color::Khaki: return IM_COL32(190, 180, 120, 255);
    }
    return IM_COL32_WHITE;
}

// The UI font has no arrows or box: the same symbols in plain characters.
std::string cellText(char32_t ch) {
    switch (ch) {
        case 0x2190: return "<";
        case 0x2192: return ">";
        case 0x2191: return "^";
        case 0x2193: return "v";
        case 0x2610: return "#";
        case 0xb0: return "\xc2\xb0";
        default: return ch >= 0x20 && ch < 0x7f ? std::string(1, static_cast<char>(ch)) : std::string("?");
    }
}

enum class KeyShape { Side, Page, Letter, Digit };

struct PanelKey {
    mcdu::Key key;
    const char* label;
    float x, y, w, h;  // millimetres from the top left of the keypad, as in data/a32nx_keypads.json
    KeyShape shape;
};

using K = mcdu::Key;
constexpr float kSideW = 10.1f, kSideH = 6.5f, kPageW = 17.3f, kPageH = 8.9f, kAlphaW = 11.3f, kAlphaH = 10.9f, kDigit = 10.4f;
// Where the keys are on the unit. BRT, DIM and the two blank keys are not in the aircraft's data: they sit where the order of the keys in the USB report puts them.
const PanelKey kPanelKeys[] = {
    {K::L1, "", 0, 0, kSideW, kSideH, KeyShape::Side}, {K::L2, "", 0, 12.6f, kSideW, kSideH, KeyShape::Side}, {K::L3, "", 0, 25.9f, kSideW, kSideH, KeyShape::Side},
    {K::L4, "", 0, 39.2f, kSideW, kSideH, KeyShape::Side}, {K::L5, "", 0, 52.4f, kSideW, kSideH, KeyShape::Side}, {K::L6, "", 0, 66.5f, kSideW, kSideH, KeyShape::Side},
    {K::R1, "", 146.1f, 0, kSideW, kSideH, KeyShape::Side}, {K::R2, "", 146.1f, 12.6f, kSideW, kSideH, KeyShape::Side}, {K::R3, "", 146.1f, 25.9f, kSideW, kSideH, KeyShape::Side},
    {K::R4, "", 146.1f, 39.2f, kSideW, kSideH, KeyShape::Side}, {K::R5, "", 146.1f, 52.4f, kSideW, kSideH, KeyShape::Side}, {K::R6, "", 146.1f, 66.5f, kSideW, kSideH, KeyShape::Side},
    {K::Dir, "DIR", 15.1f, 92.3f, kPageW, kPageH, KeyShape::Page}, {K::Prog, "PROG", 34.2f, 92.3f, kPageW, kPageH, KeyShape::Page}, {K::Perf, "PERF", 53.4f, 92.3f, kPageW, kPageH, KeyShape::Page},
    {K::Init, "INIT", 72.5f, 92.3f, kPageW, kPageH, KeyShape::Page}, {K::Data, "DATA", 91.7f, 92.3f, kPageW, kPageH, KeyShape::Page}, {K::Blank1, "", 110.8f, 92.3f, kPageW, kPageH, KeyShape::Page},
    {K::Brt, "BRT", 129.7f, 92.3f, kPageW, kPageH, KeyShape::Page},
    {K::Fpln, "F-PLN", 15.1f, 105.5f, kPageW, kPageH, KeyShape::Page}, {K::Rad, "RAD\nNAV", 34.2f, 105.5f, kPageW, kPageH, KeyShape::Page}, {K::Fuel, "FUEL\nPRED", 53.4f, 105.5f, kPageW, kPageH, KeyShape::Page},
    {K::Sec, "SEC\nF-PLN", 72.5f, 105.5f, kPageW, kPageH, KeyShape::Page}, {K::Atc, "ATC\nCOMM", 91.7f, 105.5f, kPageW, kPageH, KeyShape::Page}, {K::Menu, "MCDU\nMENU", 110.8f, 105.5f, kPageW, kPageH, KeyShape::Page},
    {K::Dim, "DIM", 129.7f, 105.5f, kPageW, kPageH, KeyShape::Page},
    {K::Airport, "AIR\nPORT", 15.1f, 118.4f, kPageW, kPageH, KeyShape::Page}, {K::Blank2, "", 34.2f, 118.4f, kPageW, kPageH, KeyShape::Page},
    {K::LeftArrow, "<", 15.1f, 132.0f, kPageW, kPageH, KeyShape::Page}, {K::UpArrow, "^", 34.2f, 131.9f, kPageW, kPageH, KeyShape::Page},
    {K::RightArrow, ">", 15.1f, 144.9f, kPageW, kPageH, KeyShape::Page}, {K::DownArrow, "v", 34.2f, 144.9f, kPageW, kPageH, KeyShape::Page},
    {K::A, "A", 65.9f, 124.2f, kAlphaW, kAlphaH, KeyShape::Letter}, {K::B, "B", 81.9f, 124.2f, kAlphaW, kAlphaH, KeyShape::Letter}, {K::C, "C", 97.9f, 124.2f, kAlphaW, kAlphaH, KeyShape::Letter},
    {K::D, "D", 113.8f, 124.2f, kAlphaW, kAlphaH, KeyShape::Letter}, {K::E, "E", 129.7f, 124.2f, kAlphaW, kAlphaH, KeyShape::Letter},
    {K::F, "F", 65.9f, 138.9f, kAlphaW, kAlphaH, KeyShape::Letter}, {K::G, "G", 81.9f, 138.9f, kAlphaW, kAlphaH, KeyShape::Letter}, {K::H, "H", 97.9f, 138.9f, kAlphaW, kAlphaH, KeyShape::Letter},
    {K::I, "I", 113.8f, 138.9f, kAlphaW, kAlphaH, KeyShape::Letter}, {K::J, "J", 129.7f, 138.9f, kAlphaW, kAlphaH, KeyShape::Letter},
    {K::K, "K", 65.9f, 155.5f, kAlphaW, kAlphaH, KeyShape::Letter}, {K::L, "L", 81.9f, 155.5f, kAlphaW, kAlphaH, KeyShape::Letter}, {K::M, "M", 97.9f, 155.5f, kAlphaW, kAlphaH, KeyShape::Letter},
    {K::N, "N", 113.8f, 155.5f, kAlphaW, kAlphaH, KeyShape::Letter}, {K::O, "O", 129.7f, 155.5f, kAlphaW, kAlphaH, KeyShape::Letter},
    {K::P, "P", 65.9f, 171.6f, kAlphaW, kAlphaH, KeyShape::Letter}, {K::Q, "Q", 81.9f, 171.6f, kAlphaW, kAlphaH, KeyShape::Letter}, {K::R, "R", 97.9f, 171.6f, kAlphaW, kAlphaH, KeyShape::Letter},
    {K::S, "S", 113.8f, 171.6f, kAlphaW, kAlphaH, KeyShape::Letter}, {K::T, "T", 129.7f, 171.6f, kAlphaW, kAlphaH, KeyShape::Letter},
    {K::U, "U", 65.9f, 187.7f, kAlphaW, kAlphaH, KeyShape::Letter}, {K::V, "V", 81.9f, 187.7f, kAlphaW, kAlphaH, KeyShape::Letter}, {K::W, "W", 97.9f, 187.7f, kAlphaW, kAlphaH, KeyShape::Letter},
    {K::X, "X", 113.8f, 187.7f, kAlphaW, kAlphaH, KeyShape::Letter}, {K::Y, "Y", 129.7f, 187.7f, kAlphaW, kAlphaH, KeyShape::Letter},
    {K::Z, "Z", 65.9f, 203.8f, kAlphaW, kAlphaH, KeyShape::Letter}, {K::Slash, "/", 81.9f, 203.8f, kAlphaW, kAlphaH, KeyShape::Letter}, {K::Space, "SP", 97.9f, 203.8f, kAlphaW, kAlphaH, KeyShape::Letter},
    {K::Ovfy, "OVFY", 113.8f, 203.8f, kAlphaW, kAlphaH, KeyShape::Letter}, {K::Clr, "CLR", 129.7f, 203.8f, kAlphaW, kAlphaH, KeyShape::Letter},
    {K::D1, "1", 17.9f, 161.7f, kDigit, kDigit, KeyShape::Digit}, {K::D2, "2", 33.8f, 161.7f, kDigit, kDigit, KeyShape::Digit}, {K::D3, "3", 49.8f, 161.7f, kDigit, kDigit, KeyShape::Digit},
    {K::D4, "4", 17.9f, 175.9f, kDigit, kDigit, KeyShape::Digit}, {K::D5, "5", 33.8f, 175.9f, kDigit, kDigit, KeyShape::Digit}, {K::D6, "6", 49.8f, 175.9f, kDigit, kDigit, KeyShape::Digit},
    {K::D7, "7", 17.9f, 190.2f, kDigit, kDigit, KeyShape::Digit}, {K::D8, "8", 33.8f, 190.2f, kDigit, kDigit, KeyShape::Digit}, {K::D9, "9", 49.8f, 190.2f, kDigit, kDigit, KeyShape::Digit},
    {K::Dot, ".", 17.9f, 204.0f, kDigit, kDigit, KeyShape::Digit}, {K::D0, "0", 33.8f, 204.0f, kDigit, kDigit, KeyShape::Digit}, {K::PlusMinus, "+/-", 49.8f, 204.0f, kDigit, kDigit, KeyShape::Digit},
};

// the lamps, in the order of mcdu::kLamps; "" = the blank one. Their places are approximate.
struct PanelLamp { size_t index; const char* label; float x, y; };
const PanelLamp kPanelLamps[] = {{0, "FAIL", -6.0f, 130}, {2, "MCDU\nMENU", -6.0f, 144}, {1, "FM", -6.0f, 158}, {4, "FM1", 146.0f, 130}, {5, "IND", 146.0f, 144}, {6, "RDY", 146.0f, 158}, {7, "", 146.0f, 172}, {8, "FM2", 146.0f, 186}};

constexpr float kMarginX = 8.0f, kMarginTop = 18.0f, kMarginBottom = 10.0f, kPadW = 156.2f, kPadH = 214.7f;
constexpr float kScreenX = 14.0f, kScreenY = -13.25f, kScreenW = 128.0f, kRowH = 6.6f;

void drawCentered(ImDrawList* dl, ImFont* font, float size, ImVec2 a, ImVec2 b, ImU32 color, const char* text) {
    // line by line: ImGui would keep the lines of a block aligned to the left
    int lines = 1;
    for (const char* c = text; *c; ++c) lines += *c == '\n';
    float y = (a.y + b.y - size * static_cast<float>(lines)) * 0.5f;
    for (const char* start = text;;) {
        const char* end = start;
        while (*end && *end != '\n') ++end;
        const ImVec2 ts = font->CalcTextSizeA(size, FLT_MAX, 0.0f, start, end);
        dl->AddText(font, size, ImVec2((a.x + b.x - ts.x) * 0.5f, y), color, start, end);
        y += size;
        if (!*end) break;
        start = end + 1;
    }
}
}  // namespace

void Ui::drawMcduPanel(float width) {
    mcdu::McduService& svc = ctx_.desktop->mcdu();
    const mcdu::ServiceStatus st = svc.status();
    const float scale = width / (kPadW + kMarginX * 2.0f);
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const float height = (kPadH + kMarginTop + kMarginBottom) * scale;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImFont* font = ImGui::GetFont();
    auto at = [&](float x, float y) { return ImVec2(origin.x + (x + kMarginX) * scale, origin.y + (y + kMarginTop) * scale); };

    dl->AddRectFilled(origin, ImVec2(origin.x + width, origin.y + height), IM_COL32(43, 46, 51, 255), 4.0f * scale);
    dl->AddRect(origin, ImVec2(origin.x + width, origin.y + height), IM_COL32(68, 72, 78, 255), 4.0f * scale);

    // the screen
    const ImVec2 sa = at(kScreenX, kScreenY), sb = at(kScreenX + kScreenW, kScreenY + kRowH * mcdu::kRows);
    dl->AddRectFilled(sa, sb, IM_COL32(5, 6, 8, 255), 1.5f * scale);
    const mcdu::Screen screen = svc.screen();
    const float cellW = kScreenW * scale / mcdu::kCols, cellH = kRowH * scale;
    for (int r = 0; r < mcdu::kRows; ++r)
        for (int c = 0; c < mcdu::kCols; ++c) {
            const mcdu::Cell& cell = screen.at(r, c);
            if (cell.ch == U' ') continue;
            const std::string text = cellText(cell.ch);
            const float size = cellH * (cell.small ? 0.72f : 0.95f);
            const ImVec2 ts = font->CalcTextSizeA(size, FLT_MAX, 0.0f, text.c_str());
            dl->AddText(font, size, ImVec2(sa.x + c * cellW + (cellW - ts.x) * 0.5f, sa.y + r * cellH + (cellH - ts.y) * 0.5f), cellColor(cell.color), text.c_str());
        }

    // the lamps
    for (const PanelLamp& l : kPanelLamps) {
        const ImVec2 a = at(l.x, l.y), b = at(l.x + 12.5f, l.y + 7.5f);
        const bool lit = st.lamps[l.index];
        dl->AddRectFilled(a, b, lit ? IM_COL32(46, 107, 58, 255) : IM_COL32(38, 41, 45, 255), 1.0f);
        if (*l.label) drawCentered(dl, font, 2.9f * scale, a, b, lit ? IM_COL32(191, 245, 201, 255) : IM_COL32(142, 152, 168, 255), l.label);
    }

    // the keys
    const ImVec2 cursor = ImGui::GetCursorPos();
    for (const PanelKey& k : kPanelKeys) {
        const int index = static_cast<int>(k.key);
        const ImVec2 a = at(k.x, k.y), b = at(k.x + k.w, k.y + k.h);
        ImGui::SetCursorScreenPos(a);
        ImGui::PushID(index);
        if (ImGui::InvisibleButton("key", ImVec2(b.x - a.x, b.y - a.y))) svc.pressKey(k.key);
        const bool held = ImGui::IsItemActive(), hovered = ImGui::IsItemHovered();
        ImGui::PopID();
        bool live = false;
        if (k.shape == KeyShape::Side) live = !st.sideLabels[static_cast<size_t>(index)].empty();
        const ImU32 fill = held ? IM_COL32(95, 100, 108, 255) : live ? IM_COL32(14, 74, 87, 255) : hovered ? IM_COL32(74, 78, 85, 255) : IM_COL32(59, 63, 69, 255);
        const ImU32 edge = live ? IM_COL32(0, 225, 255, 255) : IM_COL32(85, 89, 95, 255);
        const float round = k.shape == KeyShape::Digit ? (b.x - a.x) * 0.5f : 1.2f * scale;
        dl->AddRectFilled(a, b, fill, round);
        dl->AddRect(a, b, edge, round);
        if (*k.label) {
            const size_t length = std::char_traits<char>::length(k.label);
            const float size = (k.shape == KeyShape::Page ? (length > 5 ? 3.2f : 3.8f) : length > 2 ? 3.2f : 5.2f) * scale;
            drawCentered(dl, font, size, a, b, IM_COL32(242, 243, 244, 255), k.label);
        }
    }
    ImGui::SetCursorPos(cursor);
    ImGui::Dummy(ImVec2(width, height));
}

void Ui::drawMcduTab() {
    if (!ctx_.desktop) return;
    mcduPanelDrawn_ = true;
    mcdu::McduService& svc = ctx_.desktop->mcdu();
    svc.setPanelOpen(true);
    const mcdu::ServiceStatus st = svc.status();
    desktop::McduConfig& cfg = ctx_.settings.mcdu;
    auto apply = [&] {
        ctx_.pushDesktopConfig();
        ctx_.markDirty();
    };

    if (st.deviceOpen) pill(kGreen, ("Connected  " + st.seat).c_str());
    else if (!st.error.empty()) pill(kRed, st.error.c_str());
    else pill(kGrey, "Not connected");
    ImGui::SameLine();
    if (st.simMode) pill(st.simScreen ? kGreen : kYellow, st.simScreen ? "Aircraft screen" : "MSFS running");
    else pill(kGreen, "Desktop");
    if (st.sleeping) { ImGui::SameLine(); pill(kGrey, "Asleep"); }
    ImGui::Spacing();

    const float fs = ImGui::GetFontSize();
    const float panelW = std::min(fs * 31, ImGui::GetContentRegionAvail().x * 0.5f);
    ImGui::BeginChild("##mcdu_panel", ImVec2(panelW, 0), ImGuiChildFlags_None);
    drawMcduPanel(panelW - ImGui::GetStyle().ScrollbarSize);
    ImGui::EndChild();
    ImGui::SameLine(0, fs);
    ImGui::BeginChild("##mcdu_settings", ImVec2(0, 0));

    ImGui::SeparatorText("Side keys");
    if (ImGui::BeginTable("mcdu_side", 4, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("##l", ImGuiTableColumnFlags_WidthFixed, fs * 2);
        ImGui::TableSetupColumn("##lt", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn("##rt", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn("##r", ImGuiTableColumnFlags_WidthFixed, fs * 2);
        for (size_t i = 0; i < 6; ++i) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextDisabled("L%zu", i + 1);
            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(st.sideLabels[i].empty() ? "-" : st.sideLabels[i].c_str());
            ImGui::TableSetColumnIndex(2);
            const std::string& right = st.sideLabels[i + 6];
            const char* rightText = right.empty() ? "-" : right.c_str();
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize(rightText).x));
            ImGui::TextUnformatted(rightText);
            ImGui::TableSetColumnIndex(3);
            ImGui::TextDisabled("R%zu", i + 1);
        }
        ImGui::EndTable();
    }

    ImGui::SeparatorText("System page");
    auto choice = [&](const char* label, std::string& value, std::initializer_list<std::pair<const char*, const char*>> options) {
        const char* preview = options.begin()->second;
        for (const auto& [id, text] : options)
            if (value == id) preview = text;
        fieldLabel(label);
        ImGui::SetNextItemWidth(fs * 14);
        if (!ImGui::BeginCombo((std::string("##") + label).c_str(), preview)) return;
        for (const auto& [id, text] : options)
            if (ImGui::Selectable(text, value == id)) {
                value = id;
                apply();
            }
        ImGui::EndCombo();
    };
    choice("Look", cfg.systemLook, {{"list", "List"}, {"bars", "Bars"}});
    choice("GPUs", cfg.gpuView, {{"both", "Both shown"}, {"switch", "Switch key"}, {"page", "Own page"}});
    choice("Disks", cfg.diskView, {{"switch", "Switch key"}, {"pick", "Pick from list"}, {"page", "All on a page"}});

    ImGui::SeparatorText("Brightness");
    ImGui::SetNextItemWidth(fs * 14);
    if (ImGui::SliderInt("Screen", &cfg.screenPercent, 10, 100, "%d %%")) apply();
    ImGui::SetNextItemWidth(fs * 14);
    if (ImGui::SliderInt("Keys", &cfg.keysPercent, 0, 100, "%d %%")) apply();
    ImGui::SetNextItemWidth(fs * 14);
    if (ImGui::SliderInt("Lights", &cfg.ledPercent, 0, 100, "%d %%")) apply();
    if (ImGui::Checkbox("Automatic", &cfg.autoBrightness)) apply();
    ImGui::SetNextItemWidth(fs * 14);
    if (ImGui::SliderInt("Sleep after", &cfg.sleepMinutes, 0, 120, cfg.sleepMinutes ? "%d min" : "Never")) apply();

    ImGui::SeparatorText("Type page");
    int layout = cfg.keyboardLayout == "azerty" ? 1 : 0;
    ImGui::SetNextItemWidth(fs * 14);
    if (ImGui::Combo("Keyboard", &layout, "QWERTY\0AZERTY\0")) {
        cfg.keyboardLayout = layout ? "azerty" : "qwerty";
        apply();
    }

    ImGui::SeparatorText("Weather page");
    {
        std::string airport = cfg.homeAirport;
        fieldLabel("Airport");
        ImGui::SetNextItemWidth(fs * 6);
        if (ImGui::InputText("##airport", &airport, ImGuiInputTextFlags_CharsUppercase | ImGuiInputTextFlags_CharsNoBlank)) {
            if (airport.size() > 4) airport.resize(4);
            if (airport.empty() || mcdu::validAirportCode(airport)) {
                cfg.homeAirport = airport;
                apply();
            }
        }
    }

    ImGui::SeparatorText("Direct To page");
    {
        std::vector<std::string> apps = cfg.directApps.empty() ? mcdu::defaultDirectApps() : cfg.directApps;
        apps.resize(static_cast<size_t>(desktop::kDirectApps));
        bool appsChanged = false;
        for (int i = 0; i < desktop::kDirectApps; ++i) {
            ImGui::PushID(i);
            char label[8];
            snprintf(label, sizeof label, "L%d", i + 1);
            fieldLabel(label);
            ImGui::SetNextItemWidth(fs * 14);
            appsChanged |= ImGui::InputText("##direct_app", &apps[static_cast<size_t>(i)]);
            ImGui::PopID();
        }
        if (appsChanged) {
            cfg.directApps = apps;
            apply();
        }
    }

    ImGui::SeparatorText("Comms page");
    fieldLabel("Voice app");
    ImGui::SetNextItemWidth(fs * 14);
    if (ImGui::InputText("##voice_app", &cfg.commsApp)) apply();

    ImGui::SeparatorText("Shortcuts");
    static const char* const kLists[] = {"Keys", "Keys 2", "Comms"};
    ImGui::SetNextItemWidth(fs * 14);
    ImGui::Combo("Page", &mcduKeysList_, kLists, 3);
    const bool azerty = cfg.keyboardLayout == "azerty";
    std::vector<desktop::Shortcut>& target = mcduKeysList_ == 0 ? cfg.shortcuts : mcduKeysList_ == 1 ? cfg.shortcuts2 : cfg.commsShortcuts;
    const std::vector<desktop::Shortcut> defaults = mcduKeysList_ == 0 ? mcdu::defaultShortcuts() : mcduKeysList_ == 1 ? mcdu::defaultShortcuts2(azerty) : mcdu::defaultCommsShortcuts();
    const int slots = mcduKeysList_ == 2 ? desktop::kCommsSlots : desktop::kShortcutSlots;
    const auto& keys = shortcutKeyChoices();
    auto keyName = [&](int code) -> const char* {
        for (const auto& k : keys)
            if (k.code == code) return k.label;
        return code ? "Other" : "None";
    };
    std::vector<desktop::Shortcut> shown = target.empty() ? defaults : target;
    shown.resize(static_cast<size_t>(slots));
    bool changed = false;
    if (ImGui::BeginTable("mcdu_keys", 5, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("Key", ImGuiTableColumnFlags_WidthFixed, fs * 2.5f);
        ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthStretch, 1.2f);
        ImGui::TableSetupColumn("Hold", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn("Hold##2", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn("Press", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableHeadersRow();
        for (int i = 0; i < slots; ++i) {
            desktop::Shortcut& sc = shown[static_cast<size_t>(i)];
            ImGui::PushID(i);
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            if (mcduKeysList_ == 2) ImGui::TextUnformatted("L4");
            else ImGui::Text("%c%d", i < 6 ? 'L' : 'R', i % 6 + 1);
            ImGui::TableSetColumnIndex(1);
            ImGui::SetNextItemWidth(-1);
            changed |= ImGui::InputText("##label", &sc.label);
            const int hold1 = sc.keys.size() >= 2 ? sc.keys[0] : 0;
            const int hold2 = sc.keys.size() >= 3 ? sc.keys[1] : 0;
            const int press = sc.keys.empty() ? 0 : sc.keys.back();
            int newHold1 = hold1, newHold2 = hold2, newPress = press;
            auto picker = [&](const char* id, int& code) {
                ImGui::SetNextItemWidth(-1);
                if (!ImGui::BeginCombo(id, keyName(code))) return;
                if (ImGui::Selectable("None", code == 0)) code = 0;
                for (const auto& k : keys)
                    if (ImGui::Selectable(k.label, k.code == code)) code = k.code;
                ImGui::EndCombo();
            };
            ImGui::TableSetColumnIndex(2);
            picker("##hold1", newHold1);
            ImGui::TableSetColumnIndex(3);
            picker("##hold2", newHold2);
            ImGui::TableSetColumnIndex(4);
            picker("##press", newPress);
            if (newHold1 != hold1 || newHold2 != hold2 || newPress != press) {
                sc.keys.clear();
                for (int code : {newHold1, newHold2, newPress})
                    if (code) sc.keys.push_back(code);
                changed = true;
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    if (changed) {
        target = shown;
        apply();
    }
    if (!target.empty() && ImGui::Button("Reset keys")) {
        target.clear();
        apply();
    }
    ImGui::EndChild();
}

}  // namespace lr::app
