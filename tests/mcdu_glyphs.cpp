// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kyr4l
// Developer tool: which symbols does the MCDU's font have, and does the screen brightness command work?
// Close the companion first (it owns the unit).   mcdu_glyphs /dev/hidrawN
#include <fcntl.h>
#include <unistd.h>

#include <chrono>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>

#include "mcdu/McduFont.hpp"
#include "mcdu/McduProtocol.hpp"

using namespace lr::mcdu;

static bool send(int fd, const uint8_t* data, size_t size) { return ::write(fd, data, size) == static_cast<ssize_t>(size); }

int main(int argc, char** argv) {
    if (argc < 2) { std::fprintf(stderr, "usage: mcdu_glyphs /dev/hidrawN\n"); return 2; }
    const int fd = ::open(argv[1], O_RDWR | O_CLOEXEC);
    if (fd < 0) { std::perror(argv[1]); return 1; }
    for (const Report& r : initReports()) send(fd, r.data(), r.size());
    if (argc > 3 && std::string(argv[2]) == "--font") {  // mcdu_glyphs /dev/hidrawN --font DATA_DIR: B612 first, then the symbols
        auto slurp = [](const std::string& path) {
            std::ifstream f(path, std::ios::binary);
            std::stringstream ss;
            ss << f.rdbuf();
            return ss.str();
        };
        FontUpload up;
        std::string err;
        if (!buildFontUpload(slurp(std::string(argv[3]) + "/mcdu_b612-font-21x31.json"), slurp(std::string(argv[3]) + "/mcdu_WinWingFontPacketMap-3x31.json"), 255, up, err)) {
            std::fprintf(stderr, "font: %s\n", err.c_str());
            return 1;
        }
        for (const auto& p : up.packets) send(fd, p.data(), p.size());
        std::printf("font %s sent: %zu packets\n", up.name.c_str(), up.packets.size());
    }
    if (argc > 2 && std::string(argv[2]) == "--lamps") {  // one lamp at a time, its number on the screen, 4 s each
        for (Light l : kLamps) {
            const auto off = encodeLight(l, 0);
            send(fd, off.data(), off.size());
        }
        for (size_t i = 0; i < kLamps.size(); ++i) {
            Screen s;
            char line[32];
            snprintf(line, sizeof line, "LAMP %zu", i + 1);
            s.putCentered(5, line, Color::Green);
            snprintf(line, sizeof line, "%zu OF %zu", i + 1, kLamps.size());
            s.putCentered(8, line, Color::White, true);
            for (const Report& r : encodeScreen(s)) send(fd, r.data(), r.size());
            const auto on = encodeLight(kLamps[i], 1);
            send(fd, on.data(), on.size());
            std::printf("lamp %zu (id 0x%02x) on\n", i + 1, static_cast<unsigned>(kLamps[i]));
            std::fflush(stdout);
            std::this_thread::sleep_for(std::chrono::seconds(4));
            const auto off = encodeLight(kLamps[i], 0);
            send(fd, off.data(), off.size());
        }
        Screen done;
        done.putCentered(6, "LAMP TEST DONE", Color::Green);
        for (const Report& r : encodeScreen(done)) send(fd, r.data(), r.size());
        ::close(fd);
        return 0;
    }
    Screen s;
    s.put(0, 0, "GLYPH TEST", Color::Green);
    s.put(1, 0, "1 ( ) \" ? # : ; ! & '");
    s.put(2, 0, "2 { } | \\ ^ _ ` ~ @ $ =");
    s.put(3, 0, "3 ° ☐ □ ← ↑ → ↓ ↔");
    s.put(4, 0, "4 Δ ▲ △ ∆ ◊ ◇ ●");
    s.put(5, 0, "5 ▶ ◀ ▼ ▽ ■ ⬡ ±");
    s.put(6, 0, "6 ( ) \" ? # : ; ! & '", Color::Cyan, true);
    s.put(7, 0, "7 Δ ▲ △ ∆ ◊ ◇ ●", Color::Cyan, true);
    s.put(9, 0, "AMBER", Color::Amber);
    s.put(9, 6, "MAGENTA", Color::Magenta);
    s.put(9, 14, "GREY", Color::Grey);
    s.put(9, 19, "RED", Color::Red);
    s.put(13, 0, "BRIGHTNESS TEST IN 15 S", Color::Amber);
    for (const Report& r : encodeScreen(s, true)) send(fd, r.data(), r.size());
    std::printf("Symbols on screen: rows 1-7. Brightness steps start in 15 s.\n");
    std::fflush(stdout);
    std::this_thread::sleep_for(std::chrono::seconds(15));
    const struct { uint8_t level; const char* name; } steps[] = {{255, "100%"}, {40, "15%"}, {255, "100%"}, {0, "0% (off)"}, {255, "100%"}};
    for (const auto& st : steps) {
        const auto light = encodeLight(Light::Screen, st.level);
        send(fd, light.data(), light.size());
        std::printf("screen brightness %s\n", st.name);
        std::fflush(stdout);
        std::this_thread::sleep_for(std::chrono::seconds(3));
    }
    ::close(fd);
    return 0;
}
