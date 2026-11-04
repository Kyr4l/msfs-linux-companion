// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kyr4l
#pragma once
// Finds the package of an aircraft on disk and reads what it says about itself in plain text: the English tooltips of its controls
// (en-us.locpak) and, when the package has one, the sim's own checklist (WT format: <Checklist><Group><List><Item>). MSFS 2024 keeps these
// in the unencrypted minimal.fsarchive and html_ui/html_ui.fsarchive (sim/FsArchive.hpp); MSFS 2020 and add-ons keep them as loose files.
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace lr::app {

// The folders the sim installs packages to: $MSFS_PACKAGES_DIRS (':'-separated) when set, else the InstalledPackagesPath of each MSFS
// (2024 and 2020) found in the Steam libraries under Proton. Each folder holds Official*/<store>/<package>, Community*/<package> ...
std::vector<std::string> simPackageRoots();
// InstalledPackagesPath of a UserCfg.opt ("C:\users\...\Packages") as a Linux path inside the Proton prefix `pfx`; empty when not found.
std::string packagesPathFromUserCfg(const std::string& userCfgText, const std::string& pfx);

struct AircraftPackage {
    std::string dir;     // the package folder
    std::string name;    // its folder name ("microsoft-aircraft-sf50")
    std::string folder;  // the aircraft folder inside simobjects/<kind>/ ("microsoft_sf50")
};

// The package that holds simobjects/<kind>/<aircraft>. A Community package wins over an Official one (as in the sim).
std::optional<AircraftPackage> findAircraftPackage(const std::vector<std::string>& roots, const std::string& aircraftId);

struct PackageText {
    std::map<std::string, std::string> strings;  // en-us.locpak: key ("SF50.ENGINE_KNOB_TITLE") -> English text
    std::string checklistXml;                    // empty when the package has no readable checklist
    std::string checklistSource;                 // where the checklist came from, for the guide's sources
    std::vector<std::string> sources;            // what was read, in words
    std::string title;                           // "Cirrus Vision Jet G2" (AIRCRAFT.UI_MANUFACTURER + UI_MODEL), may be empty
    bool empty() const { return strings.empty() && checklistXml.empty(); }
};

PackageText readPackageText(const AircraftPackage& pkg);
// The strings of a .locpak file ({"LocalisationPackage":{"Strings":{...}}}); false when it is not one.
bool parseLocpak(const std::string& text, std::map<std::string, std::string>& out);

}  // namespace lr::app
