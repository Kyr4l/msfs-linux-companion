// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kyr4l
#pragma once
// The look of the program: the built-in font, the colours and the spacing of every widget.
#include <imgui.h>

namespace lr::ui {

// Loads the built-in font into the atlas (before the first frame). Falls back to Dear ImGui's own font if it cannot be read.
void loadFonts();

// Colours and spacing, scaled by the screen's scale times the user's text size (Settings). Call again when either changes.
void applyTheme(float dpiScale, float userScale);
void applyTheme(float userScale);  // the screen's scale as last given
float themeUserScale();            // the user scale the theme was last made for

// The font size of the body text at user scale 1 (pixels before the screen's scale).
constexpr float kBaseFontSize = 15.0f;

// Colour themes: general ones and ones drawn from a flight deck (panel tones, "glass" fields like its displays, its display colours).
struct ThemeInfo {
    const char* id;     // saved in the settings ("slate")
    const char* name;   // shown ("Boeing 777")
    const char* group;  // "General" or "Aircraft"
};
const ThemeInfo* themes(int& count);
const char* themeId();                // the theme in use
void setTheme(const char* id);        // unknown ids fall back to Slate; takes effect at the next applyTheme
ImVec4 themeBackground();             // the window background (the GL clear colour)
ImVec4 themeSwatch(const char* id, int which);  // 0 panel, 1 glass, 2 accent: the little preview of a theme

// Palette, for widgets drawn by hand. Set by applyTheme from the theme in use.
namespace palette {
inline ImVec4 kAccent(0.31f, 0.56f, 1.00f, 1.00f);
inline ImVec4 kAccentSoft(0.31f, 0.56f, 1.00f, 0.22f);
inline ImVec4 kAccentInk(0.04f, 0.09f, 0.19f, 1.00f);    // text on an accent-coloured button
inline ImVec4 kSurface(0.110f, 0.122f, 0.145f, 1.00f);   // panels, the rail
inline ImVec4 kSurfaceHi(0.145f, 0.160f, 0.192f, 1.00f); // a raised panel, a card
inline ImVec4 kGlass(0.137f, 0.153f, 0.184f, 1.00f);     // text fields, key caps (the displays of an aircraft theme)
inline ImVec4 kBorder(0.200f, 0.220f, 0.260f, 1.00f);
inline ImVec4 kText(0.894f, 0.906f, 0.925f, 1.00f);
inline ImVec4 kTextDim(0.56f, 0.60f, 0.67f, 1.00f);
inline ImVec4 kCode(0.62f, 0.78f, 1.00f, 1.00f);         // event and variable names
inline ImVec4 kOk(0.36f, 0.82f, 0.50f, 1.0f);
inline ImVec4 kWarn(0.96f, 0.76f, 0.30f, 1.0f);
inline ImVec4 kBad(0.96f, 0.40f, 0.40f, 1.0f);
}  // namespace palette

}  // namespace lr::ui
