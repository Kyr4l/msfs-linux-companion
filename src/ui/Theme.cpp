// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kyr4l
#include "ui/Theme.hpp"

#include <cstdio>
#include <cstring>

namespace lr::embedded {
extern const unsigned char kFontUi[];
extern const unsigned int kFontUi_size;
}  // namespace lr::embedded

namespace lr::ui {

void loadFonts() {
    ImGuiIO& io = ImGui::GetIO();
    ImFontConfig cfg;
    cfg.FontDataOwnedByAtlas = false;  // the bytes live in the program
    cfg.OversampleH = 2;
    cfg.PixelSnapH = false;
    snprintf(cfg.Name, sizeof cfg.Name, "Roboto Medium");
    ImFont* f = io.Fonts->AddFontFromMemoryTTF(const_cast<unsigned char*>(embedded::kFontUi), static_cast<int>(embedded::kFontUi_size), kBaseFontSize, &cfg);
    if (!f) io.Fonts->AddFontDefault();
}

static float g_dpiScale = 1.0f, g_userScale = 1.0f;

namespace {
struct Palette {
    const char* id;
    const char* name;
    const char* group;
    unsigned bg, panel, panel2, glass, line, text, dim, accent, accentInk, ok, warn, bad;
    bool light;
};
// panel tones and display colours of each flight deck, not paint codes
constexpr Palette kThemes[] = {
    {"slate", "Slate", "General", 0x15181D, 0x1C2027, 0x232831, 0x232831, 0x2E343F, 0xE5E8ED, 0x8E98A8, 0x4F8FFF, 0x0B1630, 0x5BD17F, 0xF2BD4B, 0xF26A6A, false},
    {"night", "Night", "General", 0x15110D, 0x1D1813, 0x251F18, 0x251F18, 0x342B21, 0xEADCC6, 0xA79479, 0xE8A33D, 0x2A1A04, 0x9FC46B, 0xE8A33D, 0xE0624F, false},
    {"day", "Daylight", "General", 0xEEF0F3, 0xFFFFFF, 0xF5F6F8, 0xF5F6F8, 0xD7DBE1, 0x1C222B, 0x687384, 0x2463EB, 0xFFFFFF, 0x1F8F4E, 0xB36B00, 0xC8382B, true},
    {"contrast", "High contrast", "General", 0x000000, 0x0B0B0B, 0x161616, 0x161616, 0x5A5A5A, 0xFFFFFF, 0xC9C9C9, 0xFFD400, 0x000000, 0x3DFF8A, 0xFFD400, 0xFF5B5B, false},
    {"airbus", "Airbus", "Aircraft", 0x1B2127, 0x252D35, 0x2E3740, 0x07090B, 0x3E4955, 0xEEF2F5, 0xA3B0BC, 0x3FD0EA, 0x062027, 0x46D97E, 0xF5A623, 0xFF4B3E, false},
    {"b787", "Boeing 787", "Aircraft", 0x121416, 0x1B1E21, 0x24282C, 0x000000, 0x343A40, 0xF1F2F3, 0xA3A9B0, 0xFF5FD2, 0x2A0622, 0x4FE08A, 0xF2B134, 0xFF4D4D, false},
    {"b777", "Boeing 777", "Aircraft", 0x221B15, 0x2F261E, 0x3A2F25, 0x0D0A07, 0x54453A, 0xF1E5CF, 0xBDA98C, 0xE3C58E, 0x2B1D0C, 0x8FD18A, 0xF0A940, 0xF0675A, false},
    {"citation", "Citation", "Aircraft", 0x1A1D21, 0x25292E, 0x2E3338, 0x050607, 0x3D434A, 0xE9ECEF, 0x9EA6AE, 0x37D2E6, 0x052126, 0x3FE07A, 0xF0B429, 0xFF5A4F, false},
    {"g1000", "Garmin G1000", "Aircraft", 0x0C0D0F, 0x17191C, 0x202327, 0x000000, 0x33373D, 0xF5F6F7, 0x9AA1A9, 0xFF4FD8, 0x2A0525, 0x3EE86F, 0xFFD23F, 0xFF4646, false},
    {"fighter", "Fighter HUD", "Aircraft", 0x1D2124, 0x2A2F33, 0x33393E, 0x071006, 0x454C52, 0xD9F7D0, 0x93A69A, 0x6DFF5A, 0x082105, 0x6DFF5A, 0xFFCF3D, 0xFF5348, false},
};
constexpr int kThemeCount = static_cast<int>(sizeof kThemes / sizeof kThemes[0]);
int g_theme = 0;

ImVec4 hex(unsigned v, float a = 1.0f) { return ImVec4(((v >> 16) & 0xff) / 255.0f, ((v >> 8) & 0xff) / 255.0f, (v & 0xff) / 255.0f, a); }
ImVec4 mix(ImVec4 a, ImVec4 b, float t) { return ImVec4(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t); }
ImVec4 alpha(ImVec4 c, float a) { c.w = a; return c; }
}  // namespace

const ThemeInfo* themes(int& count) {
    static ThemeInfo list[kThemeCount];
    for (int i = 0; i < kThemeCount; ++i) list[i] = {kThemes[i].id, kThemes[i].name, kThemes[i].group};
    count = kThemeCount;
    return list;
}

const char* themeId() { return kThemes[g_theme].id; }

void setTheme(const char* id) {
    g_theme = 0;
    for (int i = 0; id && i < kThemeCount; ++i)
        if (std::strcmp(kThemes[i].id, id) == 0) g_theme = i;
}

ImVec4 themeBackground() { return hex(kThemes[g_theme].bg); }

ImVec4 themeSwatch(const char* id, int which) {
    for (const auto& t : kThemes)
        if (std::strcmp(t.id, id) == 0) return hex(which == 0 ? t.panel : which == 1 ? t.glass : t.accent);
    return ImVec4(0, 0, 0, 1);
}

float themeUserScale() { return g_userScale; }

void applyTheme(float userScale) { applyTheme(g_dpiScale, userScale); }

void applyTheme(float dpiScale, float userScale) {
    g_dpiScale = dpiScale;
    g_userScale = userScale;
    ImGuiStyle& s = ImGui::GetStyle();
    s = ImGuiStyle();  // from the defaults, so a change of scale does not compound

    s.WindowPadding = ImVec2(14, 12);
    s.FramePadding = ImVec2(9, 5);
    s.CellPadding = ImVec2(8, 5);
    s.ItemSpacing = ImVec2(9, 7);
    s.ItemInnerSpacing = ImVec2(6, 5);
    s.IndentSpacing = 18;
    s.ScrollbarSize = 12;
    s.GrabMinSize = 10;
    s.WindowRounding = 8;
    s.ChildRounding = 6;
    s.FrameRounding = 5;
    s.PopupRounding = 6;
    s.ScrollbarRounding = 8;
    s.GrabRounding = 4;
    s.TabRounding = 5;
    s.WindowBorderSize = 1;
    s.ChildBorderSize = 1;
    s.PopupBorderSize = 1;
    s.FrameBorderSize = 0;
    s.TabBorderSize = 0;
    s.SeparatorTextBorderSize = 1;
    s.SeparatorTextPadding = ImVec2(0, 4);
    s.WindowTitleAlign = ImVec2(0.0f, 0.5f);
    s.SelectableTextAlign = ImVec2(0.0f, 0.5f);

    const Palette& t = kThemes[g_theme];
    const ImVec4 bg = hex(t.bg), panel = hex(t.panel), panel2 = hex(t.panel2), glass = hex(t.glass), line = hex(t.line);
    const ImVec4 text = hex(t.text), dim = hex(t.dim), accent = hex(t.accent);
    palette::kAccent = accent;
    palette::kAccentSoft = alpha(accent, 0.22f);
    palette::kAccentInk = hex(t.accentInk);
    palette::kSurface = panel;
    palette::kSurfaceHi = panel2;
    palette::kGlass = glass;
    palette::kBorder = line;
    palette::kText = text;
    palette::kTextDim = dim;
    palette::kCode = mix(accent, text, 0.35f);
    palette::kOk = hex(t.ok);
    palette::kWarn = hex(t.warn);
    palette::kBad = hex(t.bad);

    ImVec4* c = s.Colors;
    c[ImGuiCol_Text] = text;
    c[ImGuiCol_TextDisabled] = dim;
    c[ImGuiCol_WindowBg] = bg;
    c[ImGuiCol_ChildBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_PopupBg] = panel;
    c[ImGuiCol_Border] = line;
    c[ImGuiCol_BorderShadow] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_FrameBg] = glass;
    c[ImGuiCol_FrameBgHovered] = mix(glass, line, 0.5f);
    c[ImGuiCol_FrameBgActive] = mix(glass, line, 0.8f);
    c[ImGuiCol_TitleBg] = panel;
    c[ImGuiCol_TitleBgActive] = panel2;
    c[ImGuiCol_TitleBgCollapsed] = panel;
    c[ImGuiCol_MenuBarBg] = panel;
    c[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ScrollbarGrab] = line;
    c[ImGuiCol_ScrollbarGrabHovered] = mix(line, text, 0.15f);
    c[ImGuiCol_ScrollbarGrabActive] = mix(line, text, 0.3f);
    c[ImGuiCol_CheckMark] = accent;
    c[ImGuiCol_CheckboxSelectedBg] = mix(glass, accent, 0.25f);  // a ticked box: tinted, the tick in the accent
    c[ImGuiCol_TreeLines] = line;
    c[ImGuiCol_UnsavedMarker] = hex(t.warn);
    c[ImGuiCol_SliderGrab] = accent;
    c[ImGuiCol_SliderGrabActive] = mix(accent, text, 0.3f);
    c[ImGuiCol_Button] = panel2;
    c[ImGuiCol_ButtonHovered] = mix(panel2, accent, 0.18f);
    c[ImGuiCol_ButtonActive] = mix(panel2, accent, 0.32f);
    c[ImGuiCol_Header] = alpha(accent, 0.18f);
    c[ImGuiCol_HeaderHovered] = alpha(accent, 0.26f);
    c[ImGuiCol_HeaderActive] = alpha(accent, 0.34f);
    c[ImGuiCol_Separator] = line;
    c[ImGuiCol_SeparatorHovered] = alpha(accent, 0.6f);
    c[ImGuiCol_SeparatorActive] = accent;
    c[ImGuiCol_ResizeGrip] = alpha(accent, 0.15f);
    c[ImGuiCol_ResizeGripHovered] = alpha(accent, 0.5f);
    c[ImGuiCol_ResizeGripActive] = accent;
    c[ImGuiCol_InputTextCursor] = text;
    c[ImGuiCol_Tab] = panel;
    c[ImGuiCol_TabHovered] = mix(panel2, accent, 0.18f);
    c[ImGuiCol_TabSelected] = panel2;
    c[ImGuiCol_TabSelectedOverline] = accent;
    c[ImGuiCol_TabDimmed] = panel;
    c[ImGuiCol_TabDimmedSelected] = panel2;
    c[ImGuiCol_TabDimmedSelectedOverline] = alpha(accent, 0.5f);
    c[ImGuiCol_PlotLines] = accent;
    c[ImGuiCol_PlotLinesHovered] = mix(accent, text, 0.3f);
    c[ImGuiCol_PlotHistogram] = accent;
    c[ImGuiCol_PlotHistogramHovered] = mix(accent, text, 0.3f);
    c[ImGuiCol_TableHeaderBg] = panel;
    c[ImGuiCol_TableBorderStrong] = line;
    c[ImGuiCol_TableBorderLight] = mix(bg, line, 0.6f);
    c[ImGuiCol_TableRowBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_TableRowBgAlt] = t.light ? ImVec4(0, 0, 0, 0.025f) : ImVec4(1, 1, 1, 0.022f);
    c[ImGuiCol_TextLink] = accent;
    c[ImGuiCol_TextSelectedBg] = alpha(accent, 0.35f);
    c[ImGuiCol_DragDropTarget] = accent;
    c[ImGuiCol_NavCursor] = accent;
    c[ImGuiCol_NavWindowingHighlight] = ImVec4(1, 1, 1, 0.7f);
    c[ImGuiCol_NavWindowingDimBg] = ImVec4(0, 0, 0, 0.5f);
    c[ImGuiCol_ModalWindowDimBg] = ImVec4(0, 0, 0, 0.55f);

    s.ScaleAllSizes(dpiScale * userScale);
#if IMGUI_VERSION_NUM >= 19200
    s.FontSizeBase = kBaseFontSize;
    s.FontScaleMain = userScale;
    s.FontScaleDpi = dpiScale;
#endif
}

}  // namespace lr::ui
