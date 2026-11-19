// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kyr4l
#include "mcdu/Metar.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <sstream>

namespace lr::mcdu {

namespace {
bool digits(const std::string& s, size_t from, size_t count) {
    if (from + count > s.size()) return false;
    return std::all_of(s.begin() + static_cast<long>(from), s.begin() + static_cast<long>(from + count), [](unsigned char c) { return std::isdigit(c); });
}

bool endsWith(const std::string& s, const char* suffix) {
    const std::string t = suffix;
    return s.size() >= t.size() && s.compare(s.size() - t.size(), t.size(), t) == 0;
}

// "18013KT", "VRB03KT", "31008G16KT", "24005MPS"
bool parseWind(const std::string& t, std::string& out) {
    const bool knots = endsWith(t, "KT"), metres = endsWith(t, "MPS");
    if (!knots && !metres) return false;
    const std::string unit = knots ? "KT" : "MPS", body = t.substr(0, t.size() - unit.size());
    if (body.size() < 5) return false;
    const std::string direction = body.substr(0, 3);
    if (direction != "VRB" && !digits(direction, 0, 3)) return false;
    const std::string speed = body.substr(3);
    if (speed.find_first_not_of("0123456789G") != std::string::npos) return false;
    out = direction == "000" && speed.rfind("00", 0) == 0 ? "CALM" : direction + "/" + speed + unit;
    return true;
}

std::string temperatureText(std::string t) {
    if (!t.empty() && t[0] == 'M') t[0] = '-';
    if (t.size() >= 2 && t[t.size() - 2] == '0') t.erase(t.size() - 2, 1);  // "M02" -> "-2", "05" -> "5"
    return t;
}
}  // namespace

bool validAirportCode(const std::string& code) {
    return code.size() == 4 && std::all_of(code.begin(), code.end(), [](unsigned char c) { return std::isdigit(c) || (c >= 'A' && c <= 'Z'); });
}

MetarReport parseMetar(const std::string& input) {
    MetarReport r;
    r.raw = input;
    r.raw.erase(r.raw.find_last_not_of(" \r\n\t") + 1);
    r.raw.erase(0, std::min(r.raw.find_first_not_of(" \r\n\t"), r.raw.size()));
    if (const size_t line = r.raw.find('\n'); line != std::string::npos) r.raw.resize(line);

    std::istringstream in(r.raw);
    std::string t;
    while (in >> t) {
        if (t == "RMK" || t == "TEMPO" || t == "BECMG" || t == "NOSIG") break;  // what follows is a forecast or a remark
        if (t == "METAR" || t == "SPECI" || t == "AUTO" || t == "COR") continue;
        if (r.station.empty()) { r.station = t; continue; }
        if (r.time.empty() && t.size() == 7 && t[6] == 'Z' && digits(t, 0, 6)) { r.time = t.substr(2); continue; }
        if (r.wind.empty() && parseWind(t, r.wind)) continue;
        if (r.visibility.empty()) {
            if (t == "CAVOK") { r.visibility = t; continue; }
            if (t.size() == 4 && digits(t, 0, 4)) { r.visibility = t == "9999" ? "10 KM+" : std::to_string(std::atoi(t.c_str())) + " M"; continue; }
            if (endsWith(t, "SM")) { r.visibility = t; continue; }
        }
        if (r.temperature.empty()) {
            const size_t slash = t.find('/');
            if (slash != std::string::npos && slash >= 2 && t.find_first_not_of("M0123456789/") == std::string::npos) {
                r.temperature = temperatureText(t.substr(0, slash));
                r.dewpoint = temperatureText(t.substr(slash + 1));
                continue;
            }
        }
        if (r.pressure.empty() && t.size() == 5 && digits(t, 1, 4)) {
            if (t[0] == 'Q') r.pressure = std::to_string(std::atoi(t.c_str() + 1)) + " HPA";
            else if (t[0] == 'A') {
                char text[16];
                snprintf(text, sizeof text, "%.2f IN", std::atoi(t.c_str() + 1) / 100.0);
                r.pressure = text;
            }
        }
    }
    return r;
}

}  // namespace lr::mcdu
