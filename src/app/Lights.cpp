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
constexpr size_t kMaxLeds = 256, kMaxStates = 8, kMaxParts = 512;

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
        for (const auto& s : j.at("states")) {
            if (d.states.size() >= kMaxStates) break;
            d.states.push_back({s.at("name").get<std::string>(), static_cast<uint32_t>(number(s.at("offset")))});
        }
        for (const auto& l : j.at("leds")) {
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
        if (d.states.empty() || d.leds.empty()) throw std::runtime_error("no states or no leds");
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

std::vector<LightRgb> decodeLightState(const LightDevice& d, const std::vector<uint8_t>& bytes) {
    std::vector<LightRgb> out;
    for (size_t i = 0; i < d.leds.size() && (i + 1) * 4 <= bytes.size(); ++i) out.push_back({bytes[i * 4], bytes[i * 4 + 1], bytes[i * 4 + 2], bytes[i * 4 + 3]});
    return out;
}

}  // namespace lr::app
