// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kyr4l
#pragma once
// The lights of a device, described by a file of its own (data/*_lights.json) so a device is added, or corrected, by sharing one
// file: how its stored colours are read, its LEDs, its lighting states, and a drawing of where each LED shows.
// Read only for now: nothing here writes to a device.
#include <cstdint>
#include <string>
#include <vector>

namespace lr::app {

struct LightRgb {
    uint8_t r = 0, g = 0, b = 0, level = 0;  // level: the LED's brightness, 0..255
};
struct LightLed {
    int id = 0;
    std::string name;
    bool guess = false;  // which part it lights is not confirmed yet
};
struct LightPart {          // something drawn on the device picture
    std::string text;       // printed on it
    std::string shape;      // key, label, tag, knob, lever, toggle, rocker
    float x = 0, y = 0, w = 0, h = 0;  // 0..1 of the picture
    int textLed = 0, barLed = 0;       // the LED that colours its text, and the one of its bar (keys); 0: none
};
struct LightState {
    std::string name;
    uint32_t offset = 0;  // where its colours are stored
};
struct LightDevice {
    std::string file, name, driver;
    uint16_t vendor = 0, product = 0;
    int device = 0, family = 0;  // winwing: the controller that stores the colours
    float aspect = 1.5f;         // the picture's width / height
    std::vector<LightState> states;
    std::vector<LightLed> leds;
    std::vector<LightPart> parts;
};

bool parseLightDevice(const std::string& text, LightDevice& out, std::string& err);
std::vector<LightDevice> loadLightDevices();  // every data/*_lights.json; a broken file is logged and left out
// The colours of one state from the bytes stored at its offset: red, green, blue, level for each LED in turn.
std::vector<LightRgb> decodeLightState(const LightDevice& d, const std::vector<uint8_t>& bytes);

}  // namespace lr::app
