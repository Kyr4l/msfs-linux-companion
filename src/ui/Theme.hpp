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

// Palette, for widgets drawn by hand.
namespace palette {
inline const ImVec4 kAccent(0.31f, 0.56f, 1.00f, 1.00f);
inline const ImVec4 kAccentSoft(0.31f, 0.56f, 1.00f, 0.22f);
inline const ImVec4 kSurface(0.110f, 0.122f, 0.145f, 1.00f);   // panels, the sidebar
inline const ImVec4 kSurfaceHi(0.145f, 0.160f, 0.192f, 1.00f); // a raised panel, a card
inline const ImVec4 kBorder(0.200f, 0.220f, 0.260f, 1.00f);
inline const ImVec4 kTextDim(0.56f, 0.60f, 0.67f, 1.00f);
inline const ImVec4 kCode(0.62f, 0.78f, 1.00f, 1.00f);         // event and variable names
}  // namespace palette

}  // namespace lr::ui
