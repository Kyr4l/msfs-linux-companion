// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kyr4l
// The method follows McduFontPacketMap / FontWriter of mcdu-dotnet (Copyright (c) 2025 Andrew Whewell, BSD-3-Clause).
#include "mcdu/McduFont.hpp"

#include <map>

#include <nlohmann/json.hpp>

#include "mcdu/McduProtocol.hpp"

namespace lr::mcdu {

namespace {
constexpr int kDisplayWidth = 16 * 2 + 23 * 24;  // pixels across the unit's screen (mcdu-dotnet Metrics)
constexpr size_t kMaxPackets = 4096;
constexpr uint8_t kPrefix = 0x32;  // the first byte of the MCDU's commands, as in McduProtocol.cpp

int hexDigit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

char32_t firstCodepoint(const std::string& s) {
    const auto v = decodeUtf8(s);
    return v.empty() ? 0 : v[0];
}

// The offsets of a glyph's bytes in the series: runs written as -1, first, last.
std::vector<int> decompress(const std::vector<int>& map) {
    std::vector<int> out;
    for (size_t i = 0; i < map.size(); ++i) {
        if (map[i] != -1) {
            out.push_back(map[i]);
        } else if (i + 2 < map.size()) {
            for (int o = map[i + 1]; o <= map[i + 2] && out.size() < 4096; ++o) out.push_back(o);
            i += 2;
        }
    }
    return out;
}

// '1' or 'X' is a lit pixel; 8 pixels a byte, the first one in the high bit.
std::vector<uint8_t> glyphBytes(const nlohmann::json& rows, int& bytesPerRow) {
    std::vector<uint8_t> out;
    bytesPerRow = 0;
    for (const auto& r : rows) {
        const std::string bits = r.get<std::string>();
        const int n = static_cast<int>((bits.size() + 7) / 8);
        if (!bytesPerRow) bytesPerRow = n;
        if (n != bytesPerRow) throw std::runtime_error("rows of different widths");
        for (int b = 0; b < n; ++b) {
            uint8_t v = 0;
            for (int k = 0; k < 8; ++k) {
                const size_t i = static_cast<size_t>(b * 8 + k);
                if (i < bits.size() && (bits[i] == '1' || bits[i] == 'X')) v |= static_cast<uint8_t>(0x80 >> k);
            }
            out.push_back(v);
        }
    }
    return out;
}
}  // namespace

bool buildFontUpload(const std::string& fontJson, const std::string& mapJson, uint8_t brightness, FontUpload& out, std::string& err) {
    try {
        const auto font = nlohmann::json::parse(fontJson);
        const auto map = nlohmann::json::parse(mapJson);
        const int narrow = font.at("GlyphWidth").get<int>(), full = font.value("GlyphFullWidth", 0), height = font.at("GlyphHeight").get<int>();
        const int width = full >= narrow ? full : narrow;  // the full width fills the 24 columns
        if (map.at("GlyphHeight").get<int>() != height) throw std::runtime_error("the packet map is for another glyph height");
        if (map.at("GlyphWidth").get<int>() / 8 != width / 8) throw std::runtime_error("the packet map is for another glyph width");
        int yBase;
        switch (height) {
            case 29: yBase = 17; break;
            case 30: yBase = 4; break;
            case 31:
            case 32: yBase = 0; break;
            default: throw std::runtime_error("no screen offset known for this glyph height");
        }
        const int xOffset = 0x24 + (kDisplayWidth - width * 24) / 2, yOffset = 0x14 + yBase;

        // the recorded packets as one series; the marked places are zero until filled
        const auto& hexPackets = map.at("Packets");
        if (hexPackets.size() > kMaxPackets) throw std::runtime_error("too many packets");
        std::vector<uint8_t> blob;
        std::vector<size_t> lengths;
        for (const auto& p : hexPackets) {
            const std::string hex = p.get<std::string>();
            const size_t start = blob.size();
            for (size_t i = 0; i + 1 < hex.size();) {
                if (hex[i] == '{') {  // {CP}: the command prefix
                    blob.push_back(kPrefix);
                    blob.push_back(0xbb);
                    const size_t close = hex.find('}', i);
                    if (close == std::string::npos) throw std::runtime_error("broken {CP}");
                    i = close + 1;
                    continue;
                }
                const int hi = hexDigit(hex[i]), lo = hexDigit(hex[i + 1]);
                blob.push_back(static_cast<uint8_t>(hi < 0 || lo < 0 ? 0 : (hi << 4) | lo));  // _ H L W X Y: filled below
                i += 2;
            }
            lengths.push_back(blob.size() - start);
            if (lengths.back() != 14 && lengths.back() != 64) throw std::runtime_error("a packet of an unexpected size");
        }
        auto put = [&](int offset, uint8_t v) {
            if (offset < 0 || static_cast<size_t>(offset) >= blob.size()) throw std::runtime_error("an offset outside the packets");
            blob[static_cast<size_t>(offset)] = v;
        };

        auto fill = [&](const char* glyphsKey, const char* offsetsKey) {
            std::map<char32_t, std::vector<int>> places;
            for (const auto& o : map.at(offsetsKey)) places[firstCodepoint(o.at("Character").get<std::string>())] = decompress(o.at("GlyphMap").get<std::vector<int>>());
            for (const auto& g : font.at(glyphsKey)) {
                const auto it = places.find(firstCodepoint(g.at("Character").get<std::string>()));
                if (it == places.end()) continue;  // a character this map has no place for
                int bytesPerRow = 0;
                const auto bytes = glyphBytes(g.at("BitArray"), bytesPerRow);
                if (bytes.size() != it->second.size()) throw std::runtime_error("a glyph does not fit its place in the packets");
                for (size_t i = 0; i < bytes.size(); ++i) put(it->second[i], bytes[i]);
            }
        };
        fill("LargeGlyphs", "LargeGlyphOffsets");
        fill("SmallGlyphs", "SmallGlyphOffsets");
        for (int o : map.value("GlyphWidthOffsets", std::vector<int>{})) put(o, static_cast<uint8_t>(width));
        for (int o : map.value("GlyphHeightOffsets", std::vector<int>{})) put(o, static_cast<uint8_t>(height));
        if (const int o = map.value("XOffsetOffset", -1); o >= 0) put(o, static_cast<uint8_t>(xOffset));
        if (const int o = map.value("YOffsetOffset", -1); o >= 0) put(o, static_cast<uint8_t>(yOffset));
        if (const int o = map.value("DisplayBrightnessOffset", -1); o >= 0) put(o, brightness);

        FontUpload result;
        result.name = font.value("Name", "");
        result.glyphWidth = width;
        result.glyphHeight = height;
        size_t at = 0;
        for (size_t len : lengths) {
            result.packets.emplace_back(blob.begin() + static_cast<std::ptrdiff_t>(at), blob.begin() + static_cast<std::ptrdiff_t>(at + len));
            at += len;
        }
        out = std::move(result);
        return true;
    } catch (const std::exception& e) {
        err = e.what();
        return false;
    }
}

}  // namespace lr::mcdu
