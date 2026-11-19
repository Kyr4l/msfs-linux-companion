// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kyr4l
#include "desktop/SimPresence.hpp"

#include <cctype>
#include <cstdio>
#include <cstring>

#include <dirent.h>
#include <unistd.h>

#include "sim/SteamLog.hpp"
#include "util/Isolated.hpp"

namespace lr::desktop {

namespace {
// The program itself, by its first argument (S:\steamapps\common\MSFS2024\FlightSimulator2024.exe). Its process name is no help: under Proton
// the game renames its main thread ("ACE: WinMain_Z"). A launcher or a shell that merely mentions the game has another first argument.
bool processListHasSim() {
    DIR* dir = opendir("/proc");
    if (!dir) return false;
    bool found = false;
    while (dirent* e = readdir(dir)) {
        if (!std::isdigit(static_cast<unsigned char>(e->d_name[0]))) continue;
        char path[300];
        snprintf(path, sizeof path, "/proc/%s/cmdline", e->d_name);
        FILE* f = fopen(path, "r");
        if (!f) continue;
        char arg[512];
        const size_t n = fread(arg, 1, sizeof arg - 1, f);
        fclose(f);
        arg[n] = '\0';  // the arguments are separated by zeros: this looks at the first one
        const char* name = strstr(arg, "FlightSimulator");
        if (name && strstr(name, ".exe")) { found = true; break; }
    }
    closedir(dir);
    return found;
}
}  // namespace

bool simProcessRunning() {
    if (isolatedRun()) return false;
    // A sandbox has its own process list: only Steam's log can tell. Outside one the process list is the truth, and it also covers
    // a Steam that crashed without logging the end of the game.
    static const bool sandboxed = access("/.flatpak-info", F_OK) == 0;
    return sandboxed ? sim::steamSaysSimRunning() : processListHasSim();
}

}  // namespace lr::desktop
