// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kyr4l
#pragma once
// Whether Steam says MSFS 2020 or 2024 is running. Its log is only read again after it changed, so asking often costs one stat() per folder.
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

#include <sys/stat.h>

#include "sim/ProbeSelect.hpp"

namespace lr::sim {

inline bool steamSaysSimRunning() {
    const char* home = std::getenv("HOME");
    if (!home) return false;
    const std::string h = home;
    struct Seen { long long size = -1, mtime = -1; bool running = false; };
    thread_local Seen seen[3];
    const std::string roots[3] = {h + "/.local/share/Steam", h + "/.steam/steam", h + "/.var/app/com.valvesoftware.Steam/.local/share/Steam"};
    bool running = false;
    for (int i = 0; i < 3; ++i) {
        const std::string path = roots[i] + "/logs/gameprocess_log.txt";
        struct stat st{};
        if (::stat(path.c_str(), &st) != 0) { seen[i] = Seen{}; continue; }
        if (st.st_size != seen[i].size || st.st_mtime != seen[i].mtime) {
            seen[i].size = st.st_size;
            seen[i].mtime = st.st_mtime;
            seen[i].running = false;
            std::ifstream f(path, std::ios::binary | std::ios::ate);
            if (f) {
                // the log grows by a few kB a day and Steam starts a new one at 4 MB: the last MB covers any session
                const std::streamoff size = f.tellg();
                const std::streamoff from = size > (1 << 20) ? size - (1 << 20) : 0;
                f.seekg(from);
                std::string text(static_cast<size_t>(size - from), '\0');
                f.read(text.data(), static_cast<std::streamsize>(text.size()));
                seen[i].running = steamAppRunning(text, {"1250410", "2537590"});
            }
        }
        running = running || seen[i].running;
    }
    return running;
}

}  // namespace lr::sim
