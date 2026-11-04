// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kyr4l
#pragma once
// Catalogs and guides made by the program for an aircraft that has no hand-made one. The cockpit input events the sim reports get a panel,
// a section, a readable name and a behaviour (switch / knob / push button) guessed from their names (MSFS names them CATEGORY_SUBJECT_...,
// ENGINE_THROTTLE_1, AS1000_COM_LARGE_PFD) and, when the aircraft's package is on disk, from its English tooltips (SimPackages.hpp). The
// guide follows the sim's own checklist when the package has a readable one. Nothing is tried on the aircraft: every entry says so.
#include <string>
#include <vector>

#include "app/Catalog.hpp"
#include "app/Guide.hpp"
#include "app/SimPackages.hpp"

namespace lr::app {

struct AutoLabel {
    std::string panel, section, function, behavior, note;
    int positions = 2;
};

// Labels for each name, in order: one per name, two for a knob (up, then down). `text` may be null (no package found).
std::vector<std::vector<AutoLabel>> autoLabels(const std::vector<std::string>& names, const PackageText* text);

// Relabels the cockpit input events the sim reported for `cat` (call after every CatalogSet::applyLive of a catalog without a hand-made
// file). A knob gets a second entry (autoTwin) for its other direction; `title`, when not empty, becomes the catalog's name.
void applyAutoLabels(Catalog& cat, const PackageText* text, const std::string& title);

// A guide that walks through the sim's own checklist (one topic per list), each step pointing at the catalog control whose name matches it.
// False when the package has no readable checklist.
bool buildAutoGuide(const std::string& aircraft, const std::string& title, const PackageText& text, const Catalog& cat, Guide& out, std::string& err);

}  // namespace lr::app
