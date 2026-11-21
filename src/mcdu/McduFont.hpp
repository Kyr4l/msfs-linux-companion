// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kyr4l
#pragma once
// A font for the unit's screen. The unit takes its characters as bitmaps in one long series of packets (the ones SimAppPro sends to
// install a font); the packets are a recording with the places of every glyph byte, the glyph size, the screen offset and the
// brightness marked, and the bitmaps come from a font file. Both formats and the B612 font file are from mcdu-dotnet / cduhub
// (BSD-3-Clause); B612 is the font Airbus had made for cockpit screens (SIL Open Font License). See THIRD_PARTY_NOTICES.md.
#include <cstdint>
#include <string>
#include <vector>

namespace lr::mcdu {

struct FontUpload {
    std::string name;
    int glyphWidth = 0, glyphHeight = 0;
    std::vector<std::vector<uint8_t>> packets;  // to be written to the unit in this order, each as it is (14 or 64 bytes)
};

// fontJson: {Name, GlyphWidth, GlyphHeight, GlyphFullWidth, LargeGlyphs[{Character, BitArray}], SmallGlyphs}
// mapJson: the packet map for the font's height (WinWingFontPacketMap-3x<height>.json)
// brightness: the screen's level 0..255, which the upload sets too
bool buildFontUpload(const std::string& fontJson, const std::string& mapJson, uint8_t brightness, FontUpload& out, std::string& err);

}  // namespace lr::mcdu
