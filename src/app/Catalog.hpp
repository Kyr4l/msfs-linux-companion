// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kyr4l
#pragma once
// The browsable list of aircraft functions (generated from the FlyByWire docs, see tools/build_catalog.py).
#include <string>
#include <vector>

#include "app/Profile.hpp"
#include <deque>
#include <unordered_set>

namespace lr::app {

struct CatalogEntry {
    std::string panel, section, function, api, name, kind, values, rw, type, remark, description;
    bool hasValue = false;
    double value = 0;
    std::vector<std::string> codes;  // recipes with several RPN snippets (one action each)
    std::string reference;           // page with real photos of the panel (FlyByWire docs), may be empty
    bool live = false;               // discovered from the running sim (input events, L: vars), cached on disk
    bool standard = false;           // from the shared standard-events catalog, merged into aircraft without a curated catalog
    bool cockpitOnly = false;        // standard events: used by cockpit behaviours but not offered by the MSFS controls menu
    bool hasAxisRange = false;       // an axis-style event: the docs give the parameter range it expects
    double axisMin = 0, axisMax = 0;
    std::string searchKey;  // lower-cased concatenation for filtering
    Action toAction() const;
    std::vector<Action> toActions() const;  // all actions of the entry (recipes may have several)
    bool isCaptainSide() const;            // api names the L (captain) side, e.g. A32NX.FCU_EFIS_L_BARO_INC
    std::string firstOfficerApi() const;   // same api with the side swapped to R
    bool isIncDec() const;                 // +/- style entry, best bound with repeat-while-held
};

struct Catalog {
    std::string aircraft;   // id, e.g. "A32NX", or an aircraft folder name such as "Asobo_CJ4"
    std::string title;      // display name
    bool curated = false;   // has hand-checked entries from a shipped catalog file (not just live / standard ones)
    unsigned revision = 0;  // bumped whenever the entries change so views know to rebuild
    std::string label() const { return title.empty() ? aircraft : title; }
    std::string sourcePath;
    std::vector<CatalogEntry> entries;
    bool load(const std::string& path, std::string& err);

    // Replaces the live entries (flag live) with `fresh`, keeping every other entry. Returns true when something changed.
    bool setLiveEntries(std::vector<CatalogEntry> fresh);
    // Adds the standard entries (flag standard) unless an entry with the same api name is already there.
    void addStandardEntries(const std::vector<CatalogEntry>& standard);
    void removeStandardEntries();
    // Cache file with only the live entries ("live": true) and the aircraft id/title.
    bool saveLive(const std::string& path, std::string& err) const;
    static bool loadLive(const std::string& path, Catalog& out, std::string& err);
};

constexpr const char* kStandardCatalogId = "STANDARD";

// Every catalog the app knows, one per aircraft id, plus the shared standard-events catalog:
//   curated aircraft (FlyByWire A32NX / A380X): the shipped entries, plus whatever the sim reports that they do not list
//   any other aircraft (CJ4, F/A-18E, Cessna ...): what the sim reports (input events, L: vars), plus the standard events
class CatalogSet {
public:
    std::deque<Catalog> items;  // deque: references stay valid when aircraft are added

    // Adds a shipped catalog file's content (replaces a catalog with the same aircraft id).
    void addShipped(Catalog c);
    Catalog* find(const std::string& id);
    const Catalog* find(const std::string& id) const;
    const Catalog* standard() const { return find(kStandardCatalogId); }
    // Catalog to use for a profile's aircraft: exact match, else the standard events, else the first one.
    const Catalog* forAircraft(const std::string& id) const;
    // Creates an empty live catalog for an aircraft the sim reported (no-op when it exists).
    Catalog& ensure(const std::string& id, const std::string& title);
    // Replaces what the sim reports for `id` with the union of the cached and the fresh names. True when entries changed.
    bool applyLive(const std::string& id, const std::string& title, const std::vector<CatalogEntry>& fresh);
    bool forgetLive(const std::string& id);
    // (Re)adds the standard events to every catalog that has no curated file. Call after loading or adding catalogs.
    void refreshStandard();
    // Live entries for a snapshot of the sim: input events (not the text ones) and L: variables.
    static std::vector<CatalogEntry> liveEntriesFor(const std::vector<std::pair<std::string, bool>>& inputEvents, const std::vector<std::string>& lvars);

    auto begin() { return items.begin(); }
    auto end() { return items.end(); }
    auto begin() const { return items.begin(); }
    auto end() const { return items.end(); }
    bool empty() const { return items.empty(); }
    size_t size() const { return items.size(); }
};

// Builds catalog entries for what the sim reports about the loaded aircraft.
CatalogEntry makeInputEventEntry(const std::string& name, bool isString);
CatalogEntry makeLiveLVarEntry(const std::string& name);

}  // namespace lr::app
