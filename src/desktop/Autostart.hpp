// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kyr4l
#pragma once
// Start with the desktop session (an entry in ~/.config/autostart), and the antiMicroX entry that would run next to it.
#include <string>

namespace lr::desktop {

bool autostartEnabled();
bool setAutostart(bool on, std::string& err);
std::string autostartCommand();  // how this copy of the program is started, in the tray

bool antimicroxAutostarts();
bool disableAntimicroxAutostart(std::string& err);  // renames the entry, so it can be put back by hand

}  // namespace lr::desktop
