// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kyr4l
#pragma once
// The USB protocol of the WINCTRL (Winwing) MCDU-32, as plain data: nothing here touches a device, so all of it is tested without one.
//
// Nothing below was checked on real hardware yet. It comes from two independent community sources that agree with each other:
// mcdu-dotnet (github.com/vradarserver/cduhub, BSD-3-Clause, see THIRD_PARTY_NOTICES.md) and winwing_mcdu (github.com/schenlap/winwing_mcdu).
//   Output:  0xF0 reports set the panel up once, 0xF2 reports carry the whole screen, 14 byte 0x02 reports set lights.
//   Input:   report 1, 25 bytes: the keys as a bit field from byte 1, the two light sensors at bytes 17 and 19.
#include <array>
#include <bitset>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace lr::mcdu {

inline constexpr uint16_t kVendorId = 0x4098;
inline constexpr int kRows = 14, kCols = 24;
inline constexpr size_t kReportSize = 64;
inline constexpr size_t kInputReportSize = 25;
inline constexpr int kKeyCount = 74;

using Report = std::array<uint8_t, kReportSize>;

// The product id tells which seat the unit is set to (SimAppPro changes it).
bool isMcduProduct(uint16_t productId);
const char* seatName(uint16_t productId);  // "Captain", "First officer", "Observer", "" for another product

enum class Color : uint8_t { Black, Amber, White, Cyan, Green, Magenta, Red, Yellow, Brown, Grey, Khaki };

struct Cell {
    char32_t ch = U' ';
    Color color = Color::White;
    bool small = false;
    bool operator==(const Cell&) const = default;
};

struct Screen {
    std::array<Cell, static_cast<size_t>(kRows* kCols)> cells{};
    bool operator==(const Screen&) const = default;

    Cell& at(int row, int col) { return cells[static_cast<size_t>(row * kCols + col)]; }
    const Cell& at(int row, int col) const { return cells[static_cast<size_t>(row * kCols + col)]; }
    void clear() { cells.fill(Cell{}); }
    // Text is UTF-8. What does not fit is cut off; a row or column outside the screen writes nothing.
    void put(int row, int col, std::string_view text, Color color = Color::White, bool small = false);
    void putRight(int row, std::string_view text, Color color = Color::White, bool small = false);  // ends at the last column
    void putCentered(int row, std::string_view text, Color color = Color::White, bool small = false);
};

size_t utf8Length(std::string_view text);                  // characters, not bytes
std::vector<char32_t> decodeUtf8(std::string_view text);   // a broken sequence gives one '?' per bad byte

// The bit of every key in the input report, in order.
enum class Key : uint8_t {
    L1, L2, L3, L4, L5, L6, R1, R2, R3, R4, R5, R6,
    Dir, Prog, Perf, Init, Data, Blank1, Brt, Fpln, Rad, Fuel, Sec, Atc, Menu, Dim, Airport, Blank2,
    LeftArrow, UpArrow, RightArrow, DownArrow,
    D1, D2, D3, D4, D5, D6, D7, D8, D9, Dot, D0, PlusMinus,
    A, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
    Slash, Space, Ovfy, Clr,
};

// The name the aircraft uses for the key (A320_Neo_CDU_1_BTN_<name>, data/a32nx_keypads.json); "" for a key the aircraft does not have.
const char* keyId(Key key);
const char* keyLabel(Key key);  // what is printed on the key
// The iniBuilds Airbuses of MSFS 2024 (A320neo V2, A321, A330): calculator code that presses this key on the MCDU of the unit's seat. "" for a key they do not have
// here (BRT and DIM stay the unit's own brightness). Checked in the aircraft: the variable goes back to 0 by itself, one write is one press.
std::string iniBuildsKeyCode(Key key, uint16_t productId);

using KeyBits = std::bitset<kKeyCount>;

struct InputState {
    KeyBits keys;
    uint16_t lightLeft = 0, lightRight = 0;
};
bool parseInputReport(const uint8_t* data, size_t size, InputState& out);  // false: not a key report

// Lights. The first three are levels (0..255), the others on or off.
enum class Light : uint8_t {
    Keys = 0x00, Screen = 0x01, Leds = 0x02,
    Fail = 0x08, Fm = 0x09, Mcdu = 0x0a, Menu = 0x0b, Fm1 = 0x0c, Ind = 0x0d, Rdy = 0x0e, Line = 0x0f, Fm2 = 0x10,
};
inline constexpr std::array<Light, 9> kLamps = {Light::Fail, Light::Fm, Light::Mcdu, Light::Menu, Light::Fm1, Light::Ind, Light::Rdy, Light::Line, Light::Fm2};

std::array<uint8_t, 14> encodeLight(Light light, uint8_t value);
std::vector<Report> initReports();                  // the set-up the panel wants once, before the first screen
std::vector<Report> encodeScreen(const Screen& s, bool anyGlyph = false);  // anyGlyph: send every character as it is (to try the font)  // always the whole screen: cells cannot be addressed one by one

// The UTF-8 the panel gets for a character. Its font is not known beyond ASCII and these symbols: anything else is shown as '?'.
std::string glyphBytes(char32_t ch, bool anyGlyph = false);

}  // namespace lr::mcdu
