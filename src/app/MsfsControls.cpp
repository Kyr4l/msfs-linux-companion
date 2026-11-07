// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kyr4l
#include "app/MsfsControls.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace lr::app {

namespace fs = std::filesystem;

namespace {

// the value of attribute `name` in the tag that starts at `from` (up to the next '>'), or empty
std::string attribute(const std::string& text, size_t from, const char* name) {
    const size_t end = text.find('>', from);
    const std::string key = std::string(name) + "=\"";
    const size_t at = text.find(key, from);
    if (at == std::string::npos || (end != std::string::npos && at > end)) return {};
    const size_t v = at + key.size();
    const size_t q = text.find('"', v);
    return q == std::string::npos ? std::string() : text.substr(v, q - v);
}

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// "WINCTRL WINCTRL URSA MINOR ..." (evdev) and "WINCTRL URSA MINOR ..." (the sim): the same device
std::string squash(const std::string& name) {
    std::istringstream in(lower(name));
    std::string word, last, out;
    while (in >> word) {
        if (word == last) continue;
        if (!out.empty()) out += ' ';
        out += word;
        last = word;
    }
    return out;
}

}  // namespace

bool parseMsfsControlProfile(const std::string& text, MsfsControlProfile& out) {
    const size_t dev = text.find("<Device ");
    if (dev == std::string::npos) return false;
    out.deviceName = attribute(text, dev, "DeviceName");
    if (out.deviceName.empty()) return false;
    const size_t fn = text.find("<FriendlyName");
    if (fn != std::string::npos) {
        const size_t gt = text.find('>', fn), lt = text.find('<', gt == std::string::npos ? fn : gt);
        if (gt != std::string::npos && lt != std::string::npos) out.friendlyName = text.substr(gt + 1, lt - gt - 1);
    }
    const size_t cat = text.find("<AircraftInfo ");
    if (cat != std::string::npos) out.category = attribute(text, cat, "CategoryName");
    out.buttons.clear();
    for (size_t at = text.find("<Action ", dev); at != std::string::npos; at = text.find("<Action ", at + 1)) {
        const size_t gt = text.find('>', at);
        if (gt == std::string::npos) break;
        if (text[gt - 1] == '/') continue;  // <Action .../>: not bound
        const size_t close = text.find("</Action>", gt);
        if (close == std::string::npos) break;
        std::string action = attribute(text, at, "ActionName");
        if (action.rfind("KEY_", 0) == 0) action = action.substr(4);
        const std::string body = text.substr(gt, close - gt);
        const std::string tag = "Information=\"Joystick Button ";
        for (size_t k = body.find(tag); k != std::string::npos; k = body.find(tag, k + 1)) {
            const int n = std::atoi(body.c_str() + k + tag.size());
            if (n > 0) out.buttons.emplace_back(n, action);
        }
    }
    return true;
}

std::vector<MsfsControlProfile> readMsfsControlProfiles(const std::vector<std::string>& rootsIn) {
    std::vector<std::string> roots = rootsIn;
    if (roots.empty()) {
        const char* home = std::getenv("HOME");
        const std::string h = home ? home : "";
        roots = {h + "/.local/share/Steam/userdata", h + "/.steam/steam/userdata", h + "/.var/app/com.valvesoftware.Steam/.local/share/Steam/userdata"};
    }
    std::vector<MsfsControlProfile> out;
    std::vector<std::string> seen;  // ~/.steam/steam is often a link to ~/.local/share/Steam
    std::error_code ec;
    for (const auto& root : roots) {
        if (!fs::is_directory(root, ec)) continue;
        for (const auto& user : fs::directory_iterator(root, ec))
            for (const char* app : {"2537590", "1250410"}) {
                const fs::path dir = user.path() / app / "remote";
                if (!fs::is_directory(dir, ec)) continue;
                const std::string canon = fs::weakly_canonical(dir, ec).string();
                if (std::find(seen.begin(), seen.end(), canon) != seen.end()) continue;
                seen.push_back(canon);
                for (const auto& f : fs::directory_iterator(dir, ec)) {
                    if (f.path().filename().string().rfind("inputprofile_", 0) != 0) continue;
                    std::ifstream in(f.path(), std::ios::binary);
                    std::stringstream buf;
                    buf << in.rdbuf();
                    MsfsControlProfile p;
                    if (!parseMsfsControlProfile(buf.str(), p)) continue;
                    p.file = f.path().string();
                    p.edition = std::string(app) == "2537590" ? 2024 : 2020;
                    p.modified = static_cast<long long>(fs::last_write_time(f.path(), ec).time_since_epoch().count());
                    out.push_back(std::move(p));
                }
            }
    }
    return out;
}

std::vector<DoubleBinding> findDoubleBindings(const Profile& p, const std::vector<DeviceButtons>& devices, const std::vector<MsfsControlProfile>& sim, int edition) {
    std::vector<DoubleBinding> out;
    for (size_t i = 0; i < p.bindings.size(); ++i) {
        const Binding& b = p.bindings[i];
        if (b.input.kind != input::InputKind::Button || !b.enabled) continue;
        const DeviceButtons* dev = nullptr;
        for (const auto& d : devices) if (d.key == b.input.device) dev = &d;
        if (!dev) continue;
        const auto at = std::find(dev->codes.begin(), dev->codes.end(), b.input.code);
        if (at == dev->codes.end()) continue;
        const int button = static_cast<int>(at - dev->codes.begin()) + 1;
        // the newest airplane (or any-aircraft) profile of this device
        const MsfsControlProfile* best = nullptr;
        for (const auto& s : sim) {
            if (squash(s.deviceName) != squash(dev->name) && squash(dev->name).find(squash(s.deviceName)) == std::string::npos) continue;
            if (!s.category.empty() && s.category != "AIRPLANE") continue;
            // the connected sim's own profiles first, then the newest
            auto rank = [edition](const MsfsControlProfile& x) { return edition != 0 && x.edition == edition ? 1 : 0; };
            if (!best || rank(s) > rank(*best) || (rank(s) == rank(*best) && s.modified > best->modified)) best = &s;
        }
        if (!best) continue;
        for (const auto& [n, action] : best->buttons)
            if (n == button) {
                out.push_back({i, button, action, best->friendlyName});
                break;
            }
    }
    return out;
}

}  // namespace lr::app
