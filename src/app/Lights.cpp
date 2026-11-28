// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kyr4l
#include "app/Lights.hpp"

#include <fstream>
#include <sstream>

#include <nlohmann/json.hpp>

#include "app/Paths.hpp"
#include "util/Log.hpp"

namespace lr::app {

namespace {
constexpr size_t kMaxLeds = 256, kMaxStates = 8, kMaxParts = 512, kMaxSettings = 32, kMaxLevels = 16;
constexpr uint32_t kMaxOffset = 0x1000;  // the stored settings the read command reaches

long number(const nlohmann::json& v) {  // 64 or "0x40"
    if (v.is_number_integer()) return v.get<long>();
    if (v.is_string()) return std::stol(v.get<std::string>(), nullptr, 0);
    throw std::runtime_error("not a number");
}
}  // namespace

bool parseLightDevice(const std::string& text, LightDevice& out, std::string& err) {
    try {
        const auto j = nlohmann::json::parse(text);
        LightDevice d;
        d.name = j.at("name").get<std::string>();
        d.driver = j.at("driver").get<std::string>();
        const std::string usb = j.at("usb").get<std::string>();
        const size_t colon = usb.find(':');
        if (colon == std::string::npos) throw std::runtime_error("usb is \"vendor:product\" in hexadecimal");
        d.vendor = static_cast<uint16_t>(std::stoul(usb.substr(0, colon), nullptr, 16));
        d.product = static_cast<uint16_t>(std::stoul(usb.substr(colon + 1), nullptr, 16));
        if (j.contains("winwing")) {
            d.device = static_cast<int>(number(j["winwing"].at("device")));
            d.family = static_cast<int>(number(j["winwing"].at("family")));
        }
        d.aspect = j.value("aspect", 1.5f);
        if (!(d.aspect > 0.2f && d.aspect < 5.f)) throw std::runtime_error("aspect out of range");
        for (const auto& s : j.value("states", nlohmann::json::array())) {
            if (d.states.size() >= kMaxStates) break;
            d.states.push_back({s.at("name").get<std::string>(), static_cast<uint32_t>(number(s.at("offset")))});
        }
        for (const auto& l : j.value("leds", nlohmann::json::array())) {
            if (d.leds.size() >= kMaxLeds) break;
            d.leds.push_back({static_cast<int>(number(l.at("id"))), l.at("name").get<std::string>(), l.value("guess", false)});
        }
        for (const auto& p : j.value("parts", nlohmann::json::array())) {
            if (d.parts.size() >= kMaxParts) break;
            LightPart part;
            part.text = p.value("text", "");
            part.shape = p.value("shape", "label");
            const auto& r = p.at("rect");
            part.x = r.at(0), part.y = r.at(1), part.w = r.at(2), part.h = r.at(3);
            part.textLed = p.value("text_led", 0);
            part.barLed = p.value("bar_led", 0);
            d.parts.push_back(part);
        }
        for (const auto& s : j.value("settings", nlohmann::json::array())) {
            if (d.settings.size() >= kMaxSettings) break;
            LightSetting ls;
            ls.name = s.at("name").get<std::string>();
            ls.kind = s.at("kind").get<std::string>();
            if (ls.kind != "percent" && ls.kind != "on_off" && ls.kind != "off_on" && ls.kind != "key_axis") throw std::runtime_error("unknown setting kind " + ls.kind);
            ls.device = static_cast<int>(number(s.at("device")));
            ls.family = static_cast<int>(number(s.at("family")));
            ls.offset = static_cast<uint32_t>(number(s.at("offset")));
            ls.byte = static_cast<int>(number(s.value("byte", nlohmann::json(0))));
            if (ls.offset >= kMaxOffset || ls.offset % 4 != 0 || ls.byte < 0 || ls.byte > 3) throw std::runtime_error("setting " + ls.name + ": offset or byte out of range");
            ls.save = s.value("save", false);
            if (ls.save && ls.kind == "key_axis") throw std::runtime_error("setting " + ls.name + ": key / axis modes cannot be saved yet");
            d.settings.push_back(ls);
        }
        for (const auto& l : j.value("levels", nlohmann::json::array())) {
            if (d.levels.size() >= kMaxLevels) break;
            LightLevel lv;
            lv.name = l.at("name").get<std::string>();
            lv.setting = l.value("setting", "");
            lv.device = static_cast<int>(number(l.at("device")));
            lv.family = static_cast<int>(number(l.at("family")));
            lv.index = static_cast<int>(number(l.at("index")));
            if (lv.index < 0 || lv.index > 31) throw std::runtime_error("level " + lv.name + ": index out of range");
            d.levels.push_back(lv);
        }
        if ((d.states.empty() || d.leds.empty()) && d.settings.empty() && d.levels.empty()) throw std::runtime_error("no states and leds, settings or levels");
        out = std::move(d);
        return true;
    } catch (const std::exception& e) {
        err = e.what();
        return false;
    }
}

std::vector<LightDevice> loadLightDevices() {
    std::vector<LightDevice> out;
    for (const auto& path : listDataFiles("_lights.json")) {
        std::ifstream f(path);
        std::stringstream ss;
        ss << f.rdbuf();
        LightDevice d;
        std::string err;
        if (!parseLightDevice(ss.str(), d, err)) {
            LOG_WARN("Ignoring %s: %s", path.c_str(), err.c_str());
            continue;
        }
        d.file = path;
        bool dup = false;
        for (const auto& o : out) dup = dup || (o.vendor == d.vendor && o.product == d.product);  // the first one found wins (user, plugin, shipped)
        if (!dup) out.push_back(std::move(d));
    }
    return out;
}

std::string describeLightSetting(const LightSetting& s, int value) {
    if (value < 0 || value > 255) return "?";
    if (s.kind == "percent") return std::to_string((value * 100 + 127) / 255) + "%";
    if (s.kind == "on_off") return value ? "On" : "Off";
    if (s.kind == "off_on") return value == 0 ? "On" : value == 1 ? "Off" : "?";
    if (s.kind == "key_axis") return value == 1 ? "Key" : value == 2 ? "Axis" : value == 3 ? "Both" : "?";
    return "?";
}

std::vector<LightRgb> decodeLightState(const LightDevice& d, const std::vector<uint8_t>& bytes) {
    std::vector<LightRgb> out;
    for (size_t i = 0; i < d.leds.size() && (i + 1) * 4 <= bytes.size(); ++i) out.push_back({bytes[i * 4], bytes[i * 4 + 1], bytes[i * 4 + 2], bytes[i * 4 + 3]});
    return out;
}

}  // namespace lr::app
