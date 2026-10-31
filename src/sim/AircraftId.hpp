// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kyr4l
#pragma once
// Turns the aircraft.cfg path the sim reports ("...\SimObjects\Airplanes\Asobo_CJ4\aircraft.CFG") into the id used to
// pick a catalog and a profile. Header-only so the sim layer, the UI and the tests share one definition.
#include <algorithm>
#include <cctype>
#include <string>

namespace lr::sim {

struct AircraftId {
    std::string id;     // catalog / profile key: "A32NX", "A380X", or the aircraft folder name ("Asobo_CJ4")
    std::string label;  // human readable
    bool empty() const { return id.empty(); }
};

inline std::string lowerAscii(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

// "Asobo_CJ4", "asobo_cj4", "Asobo CJ4" and "ASOBO-CJ4" are one aircraft: how the folder is spelled depends on who reports it
// (the sim, a livery, an older config, a label typed by hand). Ids are compared by their letters and digits only.
inline std::string aircraftKey(const std::string& s) {
    std::string k;
    for (unsigned char c : s)
        if (std::isalnum(c)) k += static_cast<char>(std::tolower(c));
    return k;
}

inline bool sameAircraft(const std::string& a, const std::string& b) {
    const std::string ka = aircraftKey(a);
    return !ka.empty() && ka == aircraftKey(b);
}

// Folders inside an aircraft package that are not the aircraft itself.
inline bool genericFolderName(const std::string& name) {
    const std::string f = lowerAscii(name);
    return f.empty() || f == "config" || f == "common" || f == "model" || f == "panel" || f == "presets" || f == "attachments" || f == "sound" ||
           f == "simobjects" || f == "airplanes";
}

inline AircraftId identifyAircraft(const std::string& cfgPath) {
    std::string p = cfgPath;
    std::replace(p.begin(), p.end(), '\\', '/');
    while (!p.empty() && p.back() == '/') p.pop_back();
    // The aircraft is the folder right below "Airplanes" ("...\SimObjects\Airplanes\Asobo_CJ4\aircraft.CFG"). MSFS 2024 reports a path
    // that goes deeper ("...\airplanes\asobo_c172sp_as1000\...\config\..."), so the folder holding the file is not the aircraft there.
    std::string folder;
    const std::string lowPath = lowerAscii(p);
    const std::string marker = "/airplanes/";
    const size_t at = lowPath.find(marker);
    if (at != std::string::npos) {
        const size_t start = at + marker.size();
        const size_t end = p.find('/', start);
        folder = p.substr(start, end == std::string::npos ? std::string::npos : end - start);
    }
    if (folder.empty()) {
        // no "Airplanes" in the path: the folder that holds aircraft.cfg = the last path component before the file name; the sub-folders
        // of a package are not aircraft ("config" came out as an aircraft once), so walk up past them
        size_t slash = p.find_last_of('/');
        if (slash == std::string::npos) return {};
        std::string dir = p.substr(0, slash);
        for (;;) {
            const size_t up = dir.find_last_of('/');
            std::string part = up == std::string::npos ? dir : dir.substr(up + 1);
            if (!genericFolderName(part)) { folder = part; break; }
            if (up == std::string::npos) return {};
            dir = dir.substr(0, up);
        }
    }
    // liveries live in their own folder ("..._LIVERY_01", "_FlyByWire_A320_NEO-LIVERY"): use the base aircraft
    const std::string low = lowerAscii(folder);
    size_t cut = low.find("livery");
    if (cut != std::string::npos) {
        folder = folder.substr(0, cut);
        while (!folder.empty() && (folder.back() == '_' || folder.back() == '-' || folder.back() == ' ')) folder.pop_back();
    }
    while (!folder.empty() && folder.front() == '_') folder.erase(0, 1);
    if (folder.empty()) return {};
    const std::string l = lowerAscii(folder);
    // FlyByWire aircraft have curated catalogs; Asobo's own "A320_NEO" is a different cockpit and must not match them
    if (l.find("flybywire") != std::string::npos) {
        if (l.find("a320") != std::string::npos) return {"A32NX", "FlyByWire A320neo (A32NX)"};
        if (l.find("a380") != std::string::npos) return {"A380X", "FlyByWire A380-842 (A380X)"};
    }
    std::string label = folder;
    std::replace(label.begin(), label.end(), '_', ' ');
    return {folder, label};
}

}  // namespace lr::sim
