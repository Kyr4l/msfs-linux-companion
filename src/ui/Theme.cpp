// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kyr4l
#include "ui/Theme.hpp"

#include <cstdio>

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

    ImVec4* c = s.Colors;
    auto rgb = [](int hex, float a = 1.0f) { return ImVec4(((hex >> 16) & 0xff) / 255.0f, ((hex >> 8) & 0xff) / 255.0f, (hex & 0xff) / 255.0f, a); };
    const ImVec4 accent = palette::kAccent;
    c[ImGuiCol_Text] = rgb(0xE4E7EC);
    c[ImGuiCol_TextDisabled] = palette::kTextDim;
    c[ImGuiCol_WindowBg] = rgb(0x14161A);
    c[ImGuiCol_ChildBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_PopupBg] = rgb(0x1C1F25);
    c[ImGuiCol_Border] = palette::kBorder;
    c[ImGuiCol_BorderShadow] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_FrameBg] = rgb(0x23272F);
    c[ImGuiCol_FrameBgHovered] = rgb(0x2B303A);
    c[ImGuiCol_FrameBgActive] = rgb(0x323845);
    c[ImGuiCol_TitleBg] = rgb(0x181A1F);
    c[ImGuiCol_TitleBgActive] = rgb(0x1E2128);
    c[ImGuiCol_TitleBgCollapsed] = rgb(0x181A1F);
    c[ImGuiCol_MenuBarBg] = rgb(0x181A1F);
    c[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ScrollbarGrab] = rgb(0x30353F);
    c[ImGuiCol_ScrollbarGrabHovered] = rgb(0x3A404C);
    c[ImGuiCol_ScrollbarGrabActive] = rgb(0x464D5B);
    c[ImGuiCol_CheckMark] = accent;
    c[ImGuiCol_SliderGrab] = accent;
    c[ImGuiCol_SliderGrabActive] = rgb(0x6FA3FF);
    c[ImGuiCol_Button] = rgb(0x2A2F39);
    c[ImGuiCol_ButtonHovered] = rgb(0x353B47);
    c[ImGuiCol_ButtonActive] = rgb(0x3F4654);
    c[ImGuiCol_Header] = ImVec4(accent.x, accent.y, accent.z, 0.18f);
    c[ImGuiCol_HeaderHovered] = ImVec4(accent.x, accent.y, accent.z, 0.26f);
    c[ImGuiCol_HeaderActive] = ImVec4(accent.x, accent.y, accent.z, 0.34f);
    c[ImGuiCol_Separator] = palette::kBorder;
    c[ImGuiCol_SeparatorHovered] = ImVec4(accent.x, accent.y, accent.z, 0.6f);
    c[ImGuiCol_SeparatorActive] = accent;
    c[ImGuiCol_ResizeGrip] = ImVec4(accent.x, accent.y, accent.z, 0.15f);
    c[ImGuiCol_ResizeGripHovered] = ImVec4(accent.x, accent.y, accent.z, 0.5f);
    c[ImGuiCol_ResizeGripActive] = accent;
    c[ImGuiCol_InputTextCursor] = c[ImGuiCol_Text];
    c[ImGuiCol_Tab] = rgb(0x1E2128);
    c[ImGuiCol_TabHovered] = rgb(0x2B303A);
    c[ImGuiCol_TabSelected] = rgb(0x2A2F39);
    c[ImGuiCol_TabSelectedOverline] = accent;
    c[ImGuiCol_TabDimmed] = rgb(0x1A1C21);
    c[ImGuiCol_TabDimmedSelected] = rgb(0x23272F);
    c[ImGuiCol_TabDimmedSelectedOverline] = ImVec4(accent.x, accent.y, accent.z, 0.5f);
    c[ImGuiCol_PlotLines] = accent;
    c[ImGuiCol_PlotLinesHovered] = rgb(0x6FA3FF);
    c[ImGuiCol_PlotHistogram] = accent;
    c[ImGuiCol_PlotHistogramHovered] = rgb(0x6FA3FF);
    c[ImGuiCol_TableHeaderBg] = rgb(0x1C1F25);
    c[ImGuiCol_TableBorderStrong] = palette::kBorder;
    c[ImGuiCol_TableBorderLight] = rgb(0x22262D);
    c[ImGuiCol_TableRowBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_TableRowBgAlt] = ImVec4(1, 1, 1, 0.022f);
    c[ImGuiCol_TextLink] = accent;
    c[ImGuiCol_TextSelectedBg] = ImVec4(accent.x, accent.y, accent.z, 0.35f);
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
