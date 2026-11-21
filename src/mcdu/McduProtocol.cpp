// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kyr4l
#include "mcdu/McduProtocol.hpp"

#include <algorithm>
#include <cstring>

namespace lr::mcdu {

bool isMcduProduct(uint16_t productId) { return productId == 0xbb36 || productId == 0xbb3e || productId == 0xbb3a; }

const char* seatName(uint16_t productId) {
    switch (productId) {
        case 0xbb36: return "Captain";
        case 0xbb3e: return "First officer";
        case 0xbb3a: return "Observer";
        default: return "";
    }
}

std::vector<char32_t> decodeUtf8(std::string_view text) {
    std::vector<char32_t> out;
    out.reserve(text.size());
    for (size_t i = 0; i < text.size();) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        const int extra = c < 0x80 ? 0 : (c & 0xe0) == 0xc0 ? 1 : (c & 0xf0) == 0xe0 ? 2 : (c & 0xf8) == 0xf0 ? 3 : -1;
        if (extra < 0 || i + static_cast<size_t>(extra) >= text.size() + (extra == 0 ? 1 : 0)) { out.push_back(U'?'); ++i; continue; }
        char32_t cp = extra == 0 ? c : c & (0x3f >> extra);
        bool good = true;
        for (int k = 1; k <= extra; ++k) {
            const unsigned char n = static_cast<unsigned char>(text[i + static_cast<size_t>(k)]);
            if ((n & 0xc0) != 0x80) { good = false; break; }
            cp = (cp << 6) | (n & 0x3f);
        }
        if (!good) { out.push_back(U'?'); ++i; continue; }
        out.push_back(cp);
        i += static_cast<size_t>(extra) + 1;
    }
    return out;
}

size_t utf8Length(std::string_view text) { return decodeUtf8(text).size(); }

void Screen::put(int row, int col, std::string_view text, Color color, bool small) {
    if (row < 0 || row >= kRows) return;
    for (char32_t ch : decodeUtf8(text)) {
        if (col >= kCols) break;
        if (col >= 0) at(row, col) = Cell{ch, color, small};
        ++col;
    }
}

void Screen::putRight(int row, std::string_view text, Color color, bool small) { put(row, kCols - static_cast<int>(utf8Length(text)), text, color, small); }

void Screen::putCentered(int row, std::string_view text, Color color, bool small) { put(row, (kCols - static_cast<int>(utf8Length(text))) / 2, text, color, small); }

namespace {
struct KeyName { const char* id; const char* label; };
// in the order of enum Key; the ids are the aircraft's own
constexpr KeyName kKeyNames[kKeyCount] = {
    {"L1", "-"}, {"L2", "-"}, {"L3", "-"}, {"L4", "-"}, {"L5", "-"}, {"L6", "-"},
    {"R1", "-"}, {"R2", "-"}, {"R3", "-"}, {"R4", "-"}, {"R5", "-"}, {"R6", "-"},
    {"DIR", "DIR"}, {"PROG", "PROG"}, {"PERF", "PERF"}, {"INIT", "INIT"}, {"DATA", "DATA"}, {"", ""}, {"", "BRT"}, {"FPLN", "F-PLN"},
    {"RAD", "RAD NAV"}, {"FUEL", "FUEL PRED"}, {"SEC", "SEC F-PLN"}, {"ATC", "ATC COMM"}, {"MENU", "MCDU MENU"}, {"", "DIM"}, {"AIRPORT", "AIRPORT"}, {"", ""},
    {"LARROW", "<"}, {"UARROW", "^"}, {"RARROW", ">"}, {"DARROW", "v"},
    {"1", "1"}, {"2", "2"}, {"3", "3"}, {"4", "4"}, {"5", "5"}, {"6", "6"}, {"7", "7"}, {"8", "8"}, {"9", "9"}, {"DOT", "."}, {"0", "0"}, {"PLUSMINUS", "+/-"},
    {"A", "A"}, {"B", "B"}, {"C", "C"}, {"D", "D"}, {"E", "E"}, {"F", "F"}, {"G", "G"}, {"H", "H"}, {"I", "I"}, {"J", "J"}, {"K", "K"}, {"L", "L"}, {"M", "M"},
    {"N", "N"}, {"O", "O"}, {"P", "P"}, {"Q", "Q"}, {"R", "R"}, {"S", "S"}, {"T", "T"}, {"U", "U"}, {"V", "V"}, {"W", "W"}, {"X", "X"}, {"Y", "Y"}, {"Z", "Z"},
    {"SLASH", "/"}, {"SP", "SP"}, {"OVFY", "OVFY"}, {"CLR", "CLR"},
};

constexpr uint8_t kCommandPrefix = 0x32;  // the first byte of the MCDU's commands; the second is 0xbb

int hexValue(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

Report reportFromHex(std::string_view hex) {
    Report r{};
    size_t n = 0;
    for (size_t i = 0; i + 1 < hex.size() && n < r.size();) {
        if (hex[i] == '{') {  // {CP}: the command prefix
            r[n++] = kCommandPrefix;
            if (n < r.size()) r[n++] = 0xbb;
            i += 4;
            continue;
        }
        const int hi = hexValue(hex[i]), lo = hexValue(hex[i + 1]);
        r[n++] = static_cast<uint8_t>((hi << 4) | lo);
        i += 2;
    }
    return r;
}
}  // namespace

const char* keyId(Key key) { return kKeyNames[static_cast<size_t>(key)].id; }
const char* keyLabel(Key key) { return kKeyNames[static_cast<size_t>(key)].label; }

std::string iniBuildsKeyCode(Key key, uint16_t productId) {
    const char* name = keyId(key);  // letters and digits have the same name
    switch (key) {
        case Key::L1: name = "LSK1L"; break;
        case Key::L2: name = "LSK2L"; break;
        case Key::L3: name = "LSK3L"; break;
        case Key::L4: name = "LSK4L"; break;
        case Key::L5: name = "LSK5L"; break;
        case Key::L6: name = "LSK6L"; break;
        case Key::R1: name = "LSK1R"; break;
        case Key::R2: name = "LSK2R"; break;
        case Key::R3: name = "LSK3R"; break;
        case Key::R4: name = "LSK4R"; break;
        case Key::R5: name = "LSK5R"; break;
        case Key::R6: name = "LSK6R"; break;
        case Key::Fpln: name = "FPLAN"; break;
        case Key::Rad: name = "RADNAV"; break;
        case Key::LeftArrow: name = "PREV"; break;
        case Key::RightArrow: name = "NEXT"; break;
        case Key::UpArrow: name = "UARROW"; break;
        case Key::DownArrow: name = "DOWN"; break;
        case Key::Dot: name = "DECIMAL"; break;
        case Key::PlusMinus: name = "PLUS"; break;
        case Key::Space: name = "SPACE"; break;
        case Key::Ovfy: name = "OVERFLY"; break;
        default: break;
    }
    if (!*name) return {};
    return std::string("1 (>L:INI_MCDU") + (productId == 0xbb3e ? "2_" : "1_") + name + ")";  // the first officer's unit works the right-hand MCDU
}

bool parseInputReport(const uint8_t* data, size_t size, InputState& out) {
    if (!data || size < kInputReportSize || data[0] != 1) return false;
    KeyBits keys;
    for (size_t i = 0; i < static_cast<size_t>(kKeyCount); ++i)
        if (data[1 + i / 8] & (1u << (i % 8))) keys.set(i);
    out.keys = keys;
    out.lightLeft = static_cast<uint16_t>(data[17] | (data[18] << 8));
    out.lightRight = static_cast<uint16_t>(data[19] | (data[20] << 8));
    return true;
}

std::array<uint8_t, 14> encodeLight(Light light, uint8_t value) {
    return {0x02, kCommandPrefix, 0xbb, 0x00, 0x00, 0x03, 0x49, static_cast<uint8_t>(light), value, 0x00, 0x00, 0x00, 0x00, 0x00};
}

std::vector<Report> initReports() {
    // The fonts and colours the panel starts with (Winwing's defaults), as sent by SimAppPro; from mcdu-dotnet's InitialiseBasicFontsAndColours.
    static constexpr const char* kInit[] = {
        "f0000138{CP}00001e0100005f6331000000000000{CP}0000180100005f6331000008000000340018000e001800{CP}0000190100005f633100000e00000000",
        "f00002380000000100050000000200000000000000{CP}0000190100005f633100000e0000000100060000000300000000000000{CP}00001901000000000000",
        "f00003385f633100000e0000000200000000ff0400000000000000{CP}0000190100005f633100000e000000020000a5ffff0500000000000000{CP}00000000",
        "f00004380000190100005f633100000e0000000200ffffffff0600000000000000{CP}0000190100005f633100000e0000000200ffff00ff0700000000000000",
        "f000053800000000{CP}0000190100005f633100000e00000002003dff00ff0800000000000000{CP}0000190100005f633100000e0000000200ff6300000000",
        "f0000638ffff0900000000000000{CP}0000190100005f633100000e00000002000000ffff0a00000000000000{CP}0000190100005f633100000e0000000000",
        "f00007380000020000ffffff0b00000000000000{CP}0000190100005f633100000e0000000200425c61ff0c00000000000000{CP}0000190100005f00000000",
        "f0000838633100000e0000000200777777ff0d00000000000000{CP}0000190100005f633100000e00000002005e7379ff0e00000000000000{CP}0000000000",
        "f000093800190100005f633100000e0000000300000000ff0f00000000000000{CP}0000190100005f633100000e000000030000a5ffff100000000000000000",
        "f0000a38000000{CP}0000190100005f633100000e0000000300ffffffff1100000000000000{CP}0000190100005f633100000e0000000300ffff0000000000",
        "f0000b38ff1200000000000000{CP}0000190100005f633100000e00000003003dff00ff1300000000000000{CP}0000190100005f633100000e000000000000",
        "f0000c38000300ff63ffff1400000000000000{CP}0000190100005f633100000e00000003000000ffff1500000000000000{CP}0000190100005f6300000000",
        "f0000d383100000e000000030000ffffff1600000000000000{CP}0000190100005f633100000e0000000300425c61ff1700000000000000{CP}000000000000",
        "f0000e38190100005f633100000e0000000300777777ff1800000000000000{CP}0000190100005f633100000e00000003005e7379ff19000000000000000000",
        "f0000f380000{CP}0000190100005f633100000e0000000400000000001a00000000000000{CP}0000190100005f633100000e00000004000100000000000000",
        "f00010381b00000000000000{CP}0000190100005f633100000e0000000400020000001c00000000000000{CP}00001a0100005f633100000100000000000000",
        "f000111202{CP}00001c0100005f6331000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000",
    };
    std::vector<Report> out;
    out.reserve(std::size(kInit));
    for (const char* hex : kInit) out.push_back(reportFromHex(hex));
    return out;
}

std::string glyphBytes(char32_t ch, bool anyGlyph) {
    // what the unit's font has (tried on a unit with firmware 1.09; anything else shows as a filled block): ASCII, °, the arrows,
    // ☐, Δ (the overfly mark), ▶ ◀ and ⬡ (drawn as a diamond). □ is drawn as ☐.
    if (ch == 0x25a1) ch = 0x2610;
    const bool known = anyGlyph || (ch >= 0x20 && ch <= 0x7e) || ch == 0xb0 || (ch >= 0x2190 && ch <= 0x2193) || ch == 0x2610 || ch == 0x0394 ||
                       ch == 0x25b6 || ch == 0x25c0 || ch == 0x2b21;
    if (!known) return "?";
    std::string s;
    if (ch < 0x80) s.push_back(static_cast<char>(ch));
    else if (ch < 0x800) { s.push_back(static_cast<char>(0xc0 | (ch >> 6))); s.push_back(static_cast<char>(0x80 | (ch & 0x3f))); }
    else { s.push_back(static_cast<char>(0xe0 | (ch >> 12))); s.push_back(static_cast<char>(0x80 | ((ch >> 6) & 0x3f))); s.push_back(static_cast<char>(0x80 | (ch & 0x3f))); }
    return s;
}

std::vector<Report> encodeScreen(const Screen& s, bool anyGlyph) {
    std::vector<Report> out;
    out.reserve(18);
    Report r{};
    r[0] = 0xf2;
    size_t n = 1;
    auto add = [&](uint8_t b) {
        r[n++] = b;
        if (n == r.size()) {  // a cell may run over into the next report
            out.push_back(r);
            r.fill(0);
            r[0] = 0xf2;
            n = 1;
        }
    };
    const size_t total = s.cells.size();
    for (size_t i = 0; i < total; ++i) {
        const Cell& c = s.cells[i];
        unsigned code = static_cast<unsigned>(c.color) * 0x21u;
        if (c.small) code += 0x16bu;
        if (i == 0) code += 1;  // the panel marks the first and the last cell of a screen this way
        else if (i + 1 == total) code += 2;
        add(static_cast<uint8_t>(code & 0xff));
        add(static_cast<uint8_t>(code >> 8));
        for (char b : glyphBytes(c.ch, anyGlyph)) add(static_cast<uint8_t>(b));
    }
    if (n > 1) out.push_back(r);  // the rest of the last report stays zero
    return out;
}

}  // namespace lr::mcdu
