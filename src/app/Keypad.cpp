// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kyr4l
#include "app/Keypad.hpp"

#include <fstream>

#include <nlohmann/json.hpp>

namespace lr::app {

using json = nlohmann::json;

const KeypadKey* Keypad::key(const std::string& keyId) const {
    for (const auto& k : keys)
        if (k.id == keyId) return &k;
    return nullptr;
}

bool KeypadSet::load(const std::string& path, std::string& err) {
    std::ifstream in(path);
    if (!in) { err = "cannot open " + path; return false; }
    try {
        json j;
        in >> j;
        std::vector<Keypad> loaded;
        for (const auto& pj : j.value("pads", json::array())) {
            Keypad p;
            p.id = pj.value("id", "");
            p.aircraft = pj.value("aircraft", "");
            p.title = pj.value("title", "");
            p.hEventPrefix = pj.value("h_event_prefix", "");
            p.width = pj.value("width", 0.0f);
            p.height = pj.value("height", 0.0f);
            for (const auto& s : pj.value("sources", json::array())) p.sources.push_back(s.get<std::string>());
            for (const auto& kj : pj.value("keys", json::array()))
                p.keys.push_back({kj.value("id", ""), kj.value("label", ""), kj.value("group", ""), kj.value("x", 0.0f), kj.value("y", 0.0f),
                                  kj.value("w", 0.0f), kj.value("h", 0.0f)});
            if (p.id.empty() || p.keys.empty() || p.width <= 0 || p.height <= 0) { err = "a pad without an id, keys or size in " + path; return false; }
            loaded.push_back(std::move(p));
        }
        for (auto& p : loaded) pads.push_back(std::move(p));
    } catch (const std::exception& e) {
        err = std::string("parse error: ") + e.what();
        return false;
    }
    return true;
}

const Keypad* KeypadSet::find(const std::string& id) const {
    for (const auto& p : pads)
        if (p.id == id) return &p;
    return nullptr;
}

std::vector<std::string> splitKeyChoices(const std::string& entry) {
    std::vector<std::string> out;
    size_t pos = 0;
    while (pos <= entry.size()) {
        size_t slash = entry.find('/', pos);
        if (slash == std::string::npos) slash = entry.size();
        if (slash > pos) out.push_back(entry.substr(pos, slash - pos));
        pos = slash + 1;
    }
    return out;
}

}  // namespace lr::app
