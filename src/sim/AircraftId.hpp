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

inline AircraftId identifyAircraft(const std::string& cfgPath) {
    // folder that holds aircraft.cfg = last path component before the file name
    std::string p = cfgPath;
    std::replace(p.begin(), p.end(), '\\', '/');
    while (!p.empty() && p.back() == '/') p.pop_back();
    size_t slash = p.find_last_of('/');
    if (slash == std::string::npos) return {};
    std::string dir = p.substr(0, slash);
    size_t slash2 = dir.find_last_of('/');
    std::string folder = slash2 == std::string::npos ? dir : dir.substr(slash2 + 1);
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
