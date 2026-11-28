// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kyr4l
#pragma once
// Reads the text of an aircraft's MCDU from a picture of its pop-out window, for aircraft that give the text to no one (the iniBuilds
// A320neo V2, A321 and A330 of MSFS 2024, the FMC of the CJ4). Every cell of the grid is compared with samples of the aircraft's own characters.
// Those samples are not part of this program: the aircraft's font is not ours to hand out, so they are learned on the user's machine
// and kept in a file of their own (defaultGlyphBankPath()).
//
// Measured on captures of both aircraft (442 x 366 pop-out): 99% of the characters right on pages the samples did not come from.
#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "mcdu/McduProtocol.hpp"

namespace lr::mcdu {

inline constexpr int kGlyphW = 19, kGlyphH = 23;

struct GlyphSample {
    char32_t ch = U'?';
    bool small = false;
    std::array<float, 3> geo{};  // height and width of the character, its top relative to the cell's centre (pixels)
    std::array<uint8_t, kGlyphW * kGlyphH> pix{};
};

class GlyphBank {
public:
    bool load(const std::string& path, std::string& err);
    bool save(const std::string& path, std::string& err) const;
    void add(const GlyphSample& s) { samples_.push_back(s); }
    const std::vector<GlyphSample>& samples() const { return samples_; }
    bool empty() const { return samples_.empty(); }

private:
    std::vector<GlyphSample> samples_;
};

// Where an aircraft family's screen sits in its pop-out at the size the pop-out opens with, and how its text is laid out.
struct ScreenLayout {
    const char* name;      // for the log
    const char* window;    // how the X11 name of the pop-out window starts
    const char* bankFile;  // the sample file, in the user's data folder
    int refW, refH;        // the pop-out at the size it opens with (pixels)
    int cols, rows;        // the grid: a 25th column is the Airbus scratchpad's spare one
    float x0, pitchX;      // centre of the first column, distance between columns
    std::array<float, 16> rowY;  // centre of each row
    float panelX, panelY, panelW, panelH;  // the screen's ground inside the window
    bool messageRow;       // the last row is a message line under the scratchpad (CJ4): shown on the unit's last row while the scratchpad is empty
    const char* popOutHint;  // what to do when no pop-out is found
};
const ScreenLayout& airbusLayout();  // the iniBuilds Airbuses
const ScreenLayout& wt21Layout();    // the CJ4 (Working Title WT21) FMC

std::string defaultGlyphBankPath(const char* file = "mcdu-glyphs.bin");  // $XDG_DATA_HOME/msfs-linux-companion/<file>
Color classifyColor(float r, float g, float b);

struct DumpedCell {  // a character as the reader sees it, before it is matched (for learning samples)
    int row = 0, col = 0;
    GlyphSample sample;
};

struct ReadResult {
    Screen screen;
    int glyphs = 0;  // cells with a character in them
    int unsure = 0;  // of those, the ones that looked like nothing learned
    float scale = 1.f;  // the screen's size against the size the pop-out opens with: below about 0.7 characters get too small to tell apart
};

class ScreenReader {
public:
    static constexpr int kMaxCols = 26, kMaxRows = 16, kMaxRefW = 1024;  // the largest grid and reference width a layout may have

    explicit ScreenReader(const GlyphBank& bank, const ScreenLayout& layout = airbusLayout());
    const ScreenLayout& layout() const { return *layout_; }
    // The picture as the X server sends it: 4 bytes a pixel, blue green red unused.
    ReadResult read(const uint8_t* bgrx, int width, int height, int stride);
    void reset();  // forget what was read: the next picture is read whole
    // every character read from now on is also added there (all cells are read again so that each one is)
    void setDump(std::vector<DumpedCell>* dump) {
        dump_ = dump;
        if (dump) reset();
    }
    // compare every sample (slow): to check that the quick pass loses nothing
    void setExhaustive(bool on) {
        if (on != exhaustive_) reset();
        exhaustive_ = on;
    }

private:
    struct Prepared;
    struct CellResult {
        bool blank = true, unsure = false;
        char32_t ch = U' ';
        bool small = false;
        Color color = Color::White;
    };
    struct Cache {
        uint64_t hash = 0;
        bool valid = false;
        CellResult cell;
    };
    // the character centred on column cx of row r, whose text lies between yTop and yBottom (its centre cy)
    CellResult readCell(const uint8_t* img, int stride, int r, int cx, int cy, int yTop, int yBottom) const;

    const ScreenLayout* layout_;
    std::shared_ptr<const Prepared> bank_;
    std::vector<Cache> cache_;
    std::vector<uint8_t> scaled_;
    struct Taps { float key[6] = {}; std::vector<std::vector<std::pair<int, float>>> x, y; } taps_;  // the scaling table of the last size
    std::vector<DumpedCell>* dump_ = nullptr;
    bool exhaustive_ = false;
};

}  // namespace lr::mcdu
