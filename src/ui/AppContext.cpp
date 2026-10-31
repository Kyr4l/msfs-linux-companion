// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kyr4l
#include "ui/Ui.hpp"
#include "ui/UiCommon.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <ctime>
#include <filesystem>

#include <imgui.h>
#include <imgui_stdlib.h>

#include "app/Paths.hpp"
#include "util/Log.hpp"

namespace lr::app {

using namespace uidetail;

// ---------------------------------------------------------------- AppContext

void AppContext::pushProfileToEngine() {
    engine.setProfile(std::make_shared<Profile>(settings.ensureActive()));
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
        if (!pr.inputEvent.empty()) inputs.push_back(pr.inputEvent);
        else if (!pr.code.empty()) vars.push_back(pr.code);
    }
    bridge.watchLVars(vars);
    bridge.watchInputEvents(inputs);
}

bool AppContext::suggestedAxisRange(const Binding& b, double& lo, double& hi) const {
    for (const auto& a : b.onPress) {
        const bool input = a.kind == ActionKind::InputEvent || a.kind == ActionKind::InputEventToggle;
        if (a.kind != ActionKind::SimEvent && !input) continue;
        for (const auto& cat : catalogs)
            for (const auto& e : cat.entries)
                if (e.hasAxisRange && e.name == a.name && (e.kind == "input_event") == input) { lo = e.axisMin; hi = e.axisMax; return true; }
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
    for (const auto& f : std::filesystem::directory_iterator(dir, ec)) {
        if (f.path().extension() != ".json") continue;
        Catalog cached;
        std::string err;
        if (!Catalog::loadLive(f.path().string(), cached, err)) { LOG_WARN("Ignoring %s: %s", f.path().string().c_str(), err.c_str()); continue; }
        catalogs.applyLive(cached.aircraft, cached.title, cached.entries);
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
    if (const Catalog* c = catalogs.find(snap.aircraftId)) {
        std::string err;
        if (!c->saveLive(liveCacheFile(c->aircraft), err)) LOG_WARN("Could not cache the controls of %s: %s", c->aircraft.c_str(), err.c_str());
    }
}

void AppContext::forgetLive(const std::string& aircraft) {
    const std::string canon = catalogs.canonicalId(aircraft);  // the spelling that names the cache file
    catalogs.forgetLive(aircraft);
    std::error_code ec;
    std::filesystem::remove(liveCacheFile(canon), ec);
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
