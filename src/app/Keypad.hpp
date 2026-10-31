// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kyr4l
#pragma once
#include <string>
#include <vector>

namespace lr::app {

// One key of a keypad drawing (an MCDU keyboard); millimetres from the top left corner of the pad.
struct KeypadKey {
    std::string id, label, group;
    float x = 0, y = 0, w = 0, h = 0;
};

// Where the keys of one unit sit, from data/<aircraft>_keypads.json (tools/build_keypad_layout.py reads them from the aircraft's own model).
struct Keypad {
    std::string id, aircraft, title;
    std::string hEventPrefix;  // the H event a key sends is hEventPrefix + key id
    float width = 0, height = 0;
    std::vector<std::string> sources;
    std::vector<KeypadKey> keys;

    const KeypadKey* key(const std::string& id) const;
};

struct KeypadSet {
    std::vector<Keypad> pads;

    bool load(const std::string& path, std::string& err);  // adds the pads of the file
    const Keypad* find(const std::string& id) const;
};

// A step lists the keys to press in order; "LARROW/RARROW" means either of the two.
std::vector<std::string> splitKeyChoices(const std::string& entry);

}  // namespace lr::app
