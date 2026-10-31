// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kyr4l
#pragma once
// Developer build only (cmake -DCOMPANION_UI_LINT=ON): reports the mistakes in the way the windows are drawn that Dear ImGui itself only
// shows when the widget is hovered or the build has asserts on: widgets that share an identifier (the second one cannot be clicked)
// and the recoverable errors it repairs by itself (a Begin without End, a pushed style never popped). Each is written once to stderr
// as "[ui-lint] ...", with the label of the widget and the window it was drawn in.
namespace lr::ui {

void lintInstall();   // after ImGui::CreateContext()
void lintEndFrame();  // after the frame was drawn, before ImGui::Render()

}  // namespace lr::ui
