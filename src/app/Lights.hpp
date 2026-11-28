// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kyr4l
#pragma once
// The lights of a device, described by a file of its own (data/*_lights.json) so a device is added, or corrected, by sharing one
// file: how its stored colours are read, its LEDs, its lighting states, and a drawing of where each LED shows; settings it stores
// (read only), and lights whose brightness can be set live (the light command only: it lasts until the device is unplugged and is
// never saved to the device).
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
struct LightSetting {  // a value the device stores, read with the read command (Winwing opcode 0x05) only
    std::string name;
    std::string kind;     // "percent" (0..255), "on_off" (0 = off), "off_on" (0 = on), "key_axis" (1 key, 2 axis, 3 both)
    int device = 0, family = 0;
    uint32_t offset = 0;  // the 4-byte word that holds it
    int byte = 0;         // which byte of that word, 0..3
    bool save = false;    // the page may save it to the device (lights only: "percent", "on_off", "off_on"); checked on the device first
};
struct LightLevel {       // a light whose brightness is set live (Winwing SET_LEDX, opcode 0x49)
    std::string name;
    std::string setting;  // the stored setting it starts from ("" for none)
    int device = 0, family = 0, index = 0;
};
struct LightDevice {
    std::string file, name, driver;
    uint16_t vendor = 0, product = 0;
    int device = 0, family = 0;  // winwing: the controller that stores the colours
    float aspect = 1.5f;         // the picture's width / height
    std::vector<LightState> states;
    std::vector<LightLed> leds;
    std::vector<LightPart> parts;
    std::vector<LightSetting> settings;
    std::vector<LightLevel> levels;
};

bool parseLightDevice(const std::string& text, LightDevice& out, std::string& err);
std::vector<LightDevice> loadLightDevices();  // every data/*_lights.json; a broken file is logged and left out
// The colours of one state from the bytes stored at its offset: red, green, blue, level for each LED in turn.
std::vector<LightRgb> decodeLightState(const LightDevice& d, const std::vector<uint8_t>& bytes);
// A stored value as the page shows it: "50%", "On", "Off", "Key", "Axis", "Both"; "?" for a value the kind does not know.
std::string describeLightSetting(const LightSetting& s, int value);

}  // namespace lr::app
