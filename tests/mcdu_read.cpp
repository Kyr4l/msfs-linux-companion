// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kyr4l
// Developer tool: reads captured MCDU pop-out pictures (binary .ppm) with this machine's character samples and prints the text,
// one line per row, to compare the reader with what the page says.  mcdu_read [--bank FILE] [--layout cj4] [--sizes] picture.ppm...
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include "mcdu/ScreenReader.hpp"

using namespace lr::mcdu;

static bool loadPpm(const std::string& path, std::vector<uint8_t>& bgrx, int& w, int& h) {
    std::ifstream f(path, std::ios::binary);
    std::string magic;
    int maxv = 0;
    f >> magic >> w >> h >> maxv;
    f.get();
    if (!f || magic != "P6" || maxv != 255 || w < 1 || h < 1 || w > 8192 || h > 8192) return false;
    std::vector<uint8_t> rgb(static_cast<size_t>(w) * h * 3);
    f.read(reinterpret_cast<char*>(rgb.data()), static_cast<std::streamsize>(rgb.size()));
    if (!f) return false;
    bgrx.assign(static_cast<size_t>(w) * h * 4, 0);
    for (size_t i = 0; i < static_cast<size_t>(w) * h; ++i) bgrx[i * 4] = rgb[i * 3 + 2], bgrx[i * 4 + 1] = rgb[i * 3 + 1], bgrx[i * 4 + 2] = rgb[i * 3];
    return true;
}

static std::string utf8(char32_t c) {
    std::string s;
    if (c < 0x80) s.push_back(static_cast<char>(c));
    else if (c < 0x800) s += {static_cast<char>(0xc0 | (c >> 6)), static_cast<char>(0x80 | (c & 0x3f))};
    else s += {static_cast<char>(0xe0 | (c >> 12)), static_cast<char>(0x80 | ((c >> 6) & 0x3f)), static_cast<char>(0x80 | (c & 0x3f))};
    return s;
}

int main(int argc, char** argv) {
    std::string bankPath = defaultGlyphBankPath();
    std::vector<std::string> files;
    bool exhaustive = false;  // --exhaustive: compare every sample, as a reference for the quick pass
    bool sizes = false;
    std::string dumpPath;  // --dump FILE: every character read, as raw samples (row, col, geo, pixels) for labelling
    const ScreenLayout* layout = &airbusLayout();  // --layout cj4: the CJ4 FMC
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--bank") && i + 1 < argc) bankPath = argv[++i];
        else if (!std::strcmp(argv[i], "--dump") && i + 1 < argc) dumpPath = argv[++i];
        else if (!std::strcmp(argv[i], "--exhaustive")) exhaustive = true;
        else if (!std::strcmp(argv[i], "--sizes")) sizes = true;  // small characters in lower case
        else if (!std::strcmp(argv[i], "--layout") && i + 1 < argc) layout = !std::strcmp(argv[++i], "cj4") ? &wt21Layout() : &airbusLayout();
        else files.push_back(argv[i]);
    }
    bool bankGiven = false;
    for (int i = 1; i < argc; ++i) bankGiven = bankGiven || !std::strcmp(argv[i], "--bank");
    if (!bankGiven) bankPath = defaultGlyphBankPath(layout->bankFile);  // the layout's own samples (the CJ4's are not the Airbus ones)
    GlyphBank bank;
    std::string err;
    if (!bank.load(bankPath, err)) { std::fprintf(stderr, "%s\n", err.c_str()); return 1; }
    for (const auto& file : files) {
        std::vector<uint8_t> img;
        int w = 0, h = 0;
        if (!loadPpm(file, img, w, h)) { std::fprintf(stderr, "cannot read %s\n", file.c_str()); return 1; }
        ScreenReader reader(bank, *layout);
        std::vector<DumpedCell> dumped;
        if (!dumpPath.empty()) reader.setDump(&dumped);
        reader.setExhaustive(exhaustive);
        const auto t0 = std::chrono::steady_clock::now();
        const ReadResult r = reader.read(img.data(), w, h, w * 4);
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        const auto t1 = std::chrono::steady_clock::now();
        if (!dumpPath.empty()) {
            std::ofstream out(dumpPath + "." + std::to_string(&file - files.data()), std::ios::binary);
            for (const auto& d : dumped) {
                const int32_t rc[2] = {d.row, d.col};
                out.write(reinterpret_cast<const char*>(rc), 8);
                out.write(reinterpret_cast<const char*>(d.sample.geo.data()), 12);
                out.write(reinterpret_cast<const char*>(d.sample.pix.data()), static_cast<std::streamsize>(d.sample.pix.size()));
            }
            reader.setDump(nullptr);
        }
        reader.read(img.data(), w, h, w * 4);  // the same picture again: nothing to read
        const double again = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t1).count();
        std::printf("== %s  (%d characters, %d unsure, %.1f ms, unchanged %.2f ms)\n", file.c_str(), r.glyphs, r.unsure, ms, again);
        for (int row = 0; row < kRows; ++row) {
            std::string line;
            for (int c = 0; c < kCols; ++c) {
                char32_t ch = r.screen.at(row, c).ch;
                if (sizes && r.screen.at(row, c).small && ch >= U'A' && ch <= U'Z') ch += 32;
                line += utf8(ch);
            }
            std::printf("%2d|%s|\n", row, line.c_str());
        }
    }
    return 0;
}
