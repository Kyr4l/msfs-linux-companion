// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kyr4l
#pragma once
// What the two page styles share (McduPages.cpp: the classic pages, McduPagesAirbus.cpp: the Airbus ones). Not for other files.
#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <functional>
#include <sstream>
#include <string>

#include "mcdu/McduPages.hpp"

namespace lr::mcdu {

namespace pages {
inline constexpr size_t kTypedEcho = 22;
inline constexpr size_t kDisksPerPage = 5;

inline std::string upper(std::string s) {
    for (char& c : s)
        if (static_cast<unsigned char>(c) < 0x80) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

inline std::string fit(const std::string& s, size_t width) {
    const std::vector<char32_t> chars = decodeUtf8(s);
    if (chars.size() <= width) return s;
    std::string out;
    for (size_t i = 0; i < width; ++i) out += chars[i] < 0x80 ? static_cast<char>(chars[i]) : '?';
    return out;
}

inline std::string format(const char* pattern, double v) {
    char buf[32];
    snprintf(buf, sizeof buf, pattern, v);
    return buf;
}

inline std::string two(int v) {
    char buf[16];
    snprintf(buf, sizeof buf, "%02d", v);
    return buf;
}

inline std::string sizeText(double gb) { return gb >= 1024.0 ? format("%.1f TB", gb / 1024.0) : format("%.0f GB", gb); }

inline Color colorNamed(const std::string& name) {
    if (name == "amber") return Color::Amber;
    if (name == "red") return Color::Red;
    if (name == "yellow") return Color::Yellow;
    if (name == "cyan") return Color::Cyan;
    if (name == "white") return Color::White;
    if (name == "magenta") return Color::Magenta;
    return Color::Green;
}

inline std::string barText(int percent, int width) {
    const int filled = std::clamp(static_cast<int>(std::lround(percent * width / 100.0)), 0, width);
    return std::string(static_cast<size_t>(filled), '|') + std::string(static_cast<size_t>(width - filled), '.');
}

// a METAR wrapped between words, small and cyan, from one row to another
inline void putWrapped(Screen& s, const std::string& text, int row, int lastRow) {
    std::string line, word;
    std::istringstream words(text);
    while (words >> word && row <= lastRow) {
        if (!line.empty() && line.size() + 1 + word.size() > static_cast<size_t>(kCols)) {
            s.put(row++, 0, line, Color::Cyan, true);
            line.clear();
        }
        line += (line.empty() ? "" : " ") + word;
    }
    if (row <= lastRow && !line.empty()) s.put(row, 0, line, Color::Cyan, true);
}
}  // namespace pages

struct PageHost::Frame {
    Screen screen;
    SideLabels labels;
    std::array<std::function<void()>, 12> actions;

    void left(int line, const std::string& label, std::function<void()> fn, Color c = Color::Cyan) {
        screen.put(line * 2, 0, "<" + pages::fit(pages::upper(label), 11), c);
        side(line - 1, label, std::move(fn));
    }
    void right(int line, const std::string& label, std::function<void()> fn, Color c = Color::Cyan) {
        screen.putRight(line * 2, pages::fit(pages::upper(label), 11) + ">", c);
        side(line + 5, label, std::move(fn));
    }
    void side(int index, const std::string& label, std::function<void()> fn) {
        labels[static_cast<size_t>(index)] = label;
        actions[static_cast<size_t>(index)] = std::move(fn);
    }
    void tag(int line, const std::string& text) { screen.put(line * 2 - 1, 0, pages::fit(text, 24), Color::White, true); }
};

}  // namespace lr::mcdu
