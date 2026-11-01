// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kyr4l
#pragma once
#include <string>
#include <vector>

namespace lr::app {
std::string configDir();                 // $XDG_CONFIG_HOME/msfs-linux-companion
std::string stateDir();                 // $XDG_STATE_HOME/msfs-linux-companion (created)
std::string newSessionLogPath();         // stateDir()/session-<date>-<time>-<pid>.log; only the 40 newest session logs are kept
std::string configFile();                // .../config.json
std::string imguiIniFile();              // .../imgui.ini
// Every data file whose name ends with `suffix` (e.g. "_catalog.json"), de-duplicated by file name; the user
// config dir wins over the install dir so a hand-edited catalog overrides the shipped one.
std::vector<std::string> listDataFiles(const std::string& suffix);
// The data/ folders of the loaded plugins: searched by listDataFiles() after the user config dir and before the shipped data, so a plugin
// can add aircraft and replace a shipped file of the same name.
void setPluginDataDirs(std::vector<std::string> dirs);
std::string installedPluginDir();        // <prefix>/lib/msfs-linux-companion/plugins
// Opens an https URL of the FlyByWire docs site in the default browser (xdg-open, no shell). Anything else is refused.
bool openDocsUrl(const std::string& url);
}  // namespace lr::app
