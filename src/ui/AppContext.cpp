// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kyr4l
#include "ui/Ui.hpp"
#include "app/AxisTargets.hpp"
#include "ui/UiCommon.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <ctime>
#include <filesystem>

#include <imgui.h>
#include <imgui_stdlib.h>

#include "app/AutoCatalog.hpp"
#include "app/Paths.hpp"
#include "sim/AircraftId.hpp"
#include "util/Log.hpp"

namespace lr::app {

using namespace uidetail;

// ---------------------------------------------------------------- AppContext

void AppContext::pushProfileToEngine() {
    // Every edit of anything calls this; the engine forgets where each lever is when it is given a profile, so a lever resting in a gate
    // would fire that gate's actions again at its next movement. Only a change of what the engine uses is passed on.
    const std::string fingerprint = engineFingerprint(settings.ensureActive(), settings.calibrations);
    if (fingerprint != pushedFingerprint_) {
        pushedFingerprint_ = fingerprint;
        engine.setCalibrations(settings.calibrations);
        engine.setProfile(std::make_shared<Profile>(settings.ensureActive()));
    }
    refreshWatches();
}

const Guide* AppContext::guideFor(const std::string& aircraft) const {
    for (const auto& g : guides)
        if (sim::sameAircraft(g.aircraft, aircraft)) return &g;
    return nullptr;
}

void AppContext::refreshWatches() {
    const Profile& p = settings.ensureActive();
    std::vector<std::string> vars;
    for (const auto& b : p.bindings)
        for (const auto* list : {&b.onPress, &b.onRelease})
            for (const auto& a : *list)
                if ((a.kind == ActionKind::LVarSet || a.kind == ActionKind::LVarToggle) && !a.name.empty()) vars.push_back(a.name);
    std::vector<std::string> inputs;
    for (const auto& b : p.bindings)
        for (const auto* list : {&b.onPress, &b.onRelease})
            for (const auto& a : *list)
                if ((a.kind == ActionKind::InputEvent || a.kind == ActionKind::InputEventToggle) && !a.name.empty()) inputs.push_back(a.name);
    for (const auto& pr : guideProbes) {
        if (!pr.inputEvent.empty()) for (auto& n : pr.inputEvents()) inputs.push_back(std::move(n));
        else if (!pr.code.empty()) vars.push_back(pr.code);
    }
    bridge.watchLVars(vars);
    bridge.watchInputEvents(inputs);
}

bool AppContext::suggestedAxisRange(const Binding& b, double& lo, double& hi) const {
    // the active profile's own catalog speaks first (the same event takes other ranges in other cockpits), then the controls every
    // aircraft has, then any catalog, then the name
    const Profile* active = settings.active();
    const Catalog* own = active ? catalogs.forAircraft(active->aircraft) : nullptr;
    for (const auto& a : b.onPress) {
        const bool input = a.kind == ActionKind::InputEvent || a.kind == ActionKind::InputEventToggle;
        if (a.kind != ActionKind::SimEvent && !input) continue;
        auto fromCatalog = [&](const Catalog& cat) {
            for (const auto& e : cat.entries)
                if (e.hasAxisRange && e.name == a.name && (e.kind == "input_event") == input) { lo = e.axisMin; hi = e.axisMax; return true; }
            return false;
        };
        if (own && fromCatalog(*own)) return true;
        if (!input) {
            if (const AxisTarget* t = findAxisTarget(a.name)) { lo = t->lo; hi = t->hi; return true; }
        }
        for (const auto& cat : catalogs)
            if (fromCatalog(cat)) return true;
        if (!input && axisEventRange(a.name, lo, hi)) return true;
    }
    return false;
}

int AppContext::fixAxisRanges(Profile& p) {
    int n = 0;
    for (auto& b : p.bindings) {
        double lo, hi;
        if (b.input.kind != input::InputKind::Axis || !suggestedAxisRange(b, lo, hi) || !axisRangeLooksWrong(b.axis, lo, hi)) continue;
        b.axis.outMin = lo;
        b.axis.outMax = hi;
        b.axis.integer = true;
        ++n;
    }
    if (n) markDirty();
    return n;
}

void AppContext::warnAxisRanges() const {
    for (const auto& p : settings.profiles)
        for (const auto& b : p.bindings) {
            double lo, hi;
            if (b.input.kind == input::InputKind::Axis && suggestedAxisRange(b, lo, hi) && axisRangeLooksWrong(b.axis, lo, hi))
                LOG_WARN("Profile \"%s\", binding \"%s\": the axis event expects %g..%g but the output range is %g..%g, so the control barely moves "
                         "(open the binding and press 'Set range', or use the banner in the Bindings tab)",
                         p.name.c_str(), b.label.c_str(), lo, hi, b.axis.outMin, b.axis.outMax);
        }
}

const Catalog* AppContext::catalogFor(const std::string& aircraft) const { return catalogs.forAircraft(aircraft); }

static std::string liveCacheFile(const std::string& aircraft) {
    std::string safe;
    for (char c : aircraft) safe += (std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_') ? c : '_';
    return (std::filesystem::path(configDir()) / "live" / (safe + ".json")).string();
}

void AppContext::loadLiveCaches() {
    std::error_code ec;
    const std::filesystem::path dir = std::filesystem::path(configDir()) / "live";
    if (!std::filesystem::is_directory(dir, ec)) return;
    std::vector<std::filesystem::path> files;
    for (const auto& f : std::filesystem::directory_iterator(dir, ec))
        if (f.path().extension() == ".json") files.push_back(f.path());
    std::sort(files.begin(), files.end());  // the directory has no order of its own: the same files give the same catalogs every start
    for (const auto& file : files) {
        Catalog cached;
        std::string err;
        if (!Catalog::loadLive(file.string(), cached, err)) { LOG_WARN("Ignoring %s: %s", file.string().c_str(), err.c_str()); continue; }
        if (sim::genericFolderName(cached.aircraft)) {
            // MSFS 2024 reports a path that older versions turned into the aircraft "config"; the file can be deleted
            LOG_WARN("Ignoring %s: \"%s\" is not an aircraft (written by an older version on MSFS 2024)", file.string().c_str(), cached.aircraft.c_str());
            continue;
        }
        catalogs.applyLive(cached.aircraft, cached.title, cached.entries);
        autoLabel(cached.aircraft);
        LOG_INFO("Cached controls for %s: %zu entries", cached.aircraft.c_str(), cached.entries.size());
    }
    catalogs.refreshStandard();
}

void AppContext::syncLive() {
    const uint64_t rev = bridge.liveRevision();
    if (rev == liveSeen) return;
    liveSeen = rev;
    const sim::LiveSnapshot snap = bridge.liveSnapshot();
    if (snap.aircraftId.empty()) return;
    std::vector<std::pair<std::string, bool>> events;
    for (const auto& e : snap.inputEvents) events.emplace_back(e.name, e.isString);
    auto fresh = CatalogSet::liveEntriesFor(events, snap.lvars, snap.aircraftId);
    const bool changed = catalogs.applyLive(snap.aircraftId, snap.aircraftLabel, fresh);
    if (!changed) return;
    autoLabel(snap.aircraftId);
    if (const Catalog* c = catalogs.find(snap.aircraftId)) {
        std::string err;
        if (!c->saveLive(liveCacheFile(c->aircraft), err)) LOG_WARN("Could not cache the controls of %s: %s", c->aircraft.c_str(), err.c_str());
    }
}

void AppContext::autoLabel(const std::string& aircraft) {
    Catalog* cat = catalogs.find(aircraft);
    // a curated catalog keeps its own entries; the live input events it does not list are labelled like any other aircraft's
    if (!cat || cat->aircraft == kStandardCatalogId) return;
    const std::string key = sim::aircraftKey(aircraft);
    auto it = packageTexts.find(key);
    if (it == packageTexts.end()) {
        if (!packageRoots) packageRoots = simPackageRoots();
        std::shared_ptr<const PackageText> text;
        if (const auto pkg = findAircraftPackage(*packageRoots, aircraft)) {
            auto t = std::make_shared<PackageText>(readPackageText(*pkg));
            LOG_INFO("Package of %s: %s (%zu tooltip strings%s)", aircraft.c_str(), pkg->dir.c_str(), t->strings.size(), t->checklistXml.empty() ? "" : ", checklist");
            text = std::move(t);
        }
        it = packageTexts.emplace(key, std::move(text)).first;
    }
    const PackageText* text = it->second.get();
    applyAutoLabels(*cat, text, text && !cat->curated ? text->title : std::string());
    if (text && !text->checklistXml.empty() && !guideFor(aircraft)) {
        Guide g;
        std::string err;
        if (buildAutoGuide(cat->aircraft, text->title.empty() ? cat->label() : text->title, *text, *cat, g, err)) {
            LOG_INFO("Guide %s: %zu topics from the sim's checklist", g.aircraft.c_str(), g.topics.size());
            guides.push_back(std::move(g));
        } else {
            LOG_WARN("No guide from the checklist of %s: %s", aircraft.c_str(), err.c_str());
        }
    }
}

void AppContext::forgetLive(const std::string& aircraft) {
    catalogs.forgetLive(aircraft);
    // every file of that aircraft, whatever the spelling of its name ("asobo_cj4.json" and "Asobo_CJ4.json" are the same aircraft)
    std::error_code ec;
    const std::filesystem::path dir = std::filesystem::path(configDir()) / "live";
    std::vector<std::filesystem::path> files;
    for (const auto& f : std::filesystem::directory_iterator(dir, ec))
        if (f.path().extension() == ".json" && sim::sameAircraft(f.path().stem().string(), aircraft)) files.push_back(f.path());
    for (const auto& f : files) std::filesystem::remove(f, ec);
}

void AppContext::pushHapticsConfig() {
    haptics.setConfig(settings.haptics);
}

void AppContext::markDirty() {
    dirty = true;
    pushProfileToEngine();
}

bool AppContext::save() {
    std::string err;
    if (!saveSettings(configPath, settings, err)) {
        lastSaveError = err;
        LOG_ERROR("Saving %s failed: %s", configPath.c_str(), err.c_str());
        return false;
    }
    lastSaveError.clear();
    dirty = false;
    return true;
}

}  // namespace lr::app
