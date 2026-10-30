// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kyr4l
#include "app/Catalog.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>

#include <nlohmann/json.hpp>

namespace lr::app {

static std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

Action CatalogEntry::toAction() const {
    Action a;
    if (kind == "h_event") {
        a.kind = ActionKind::HEvent;
        a.name = name;
    } else if (kind == "lvar") {
        a.name = name;
        if (values == "0|1" && !hasValue) {
            a.kind = ActionKind::LVarToggle;  // push buttons: flip the state each press
        } else {
            a.kind = ActionKind::LVarSet;
            a.value = hasValue ? value : 1;
        }
    } else if (kind == "avar") {
        a.kind = ActionKind::CalcCode;
        a.name = "1 (>A:" + name + ", Bool)";
    } else if (kind == "input_event") {
        a.kind = ActionKind::InputEventToggle;  // most cockpit switches: flip between positions; edit the kind/value for momentary ones
        a.name = name;
        a.value = 2;
    } else if (kind == "calc_code") {
        a.kind = ActionKind::CalcCode;
        a.name = name;
    } else {
        a.kind = ActionKind::SimEvent;
        a.name = name;
        a.value = hasValue ? value : 0;
    }
    return a;
}

std::vector<Action> CatalogEntry::toActions() const {
    if (codes.size() <= 1) return {toAction()};
    std::vector<Action> out;
    for (const auto& c : codes) {
        Action a;
        a.kind = ActionKind::CalcCode;
        a.name = c;
        out.push_back(a);
    }
    return out;
}

bool CatalogEntry::isCaptainSide() const {
    return api.find("_L_") != std::string::npos || api.find("EFIS_L_") != std::string::npos;
}

std::string CatalogEntry::firstOfficerApi() const {
    std::string r = api;
    size_t pos = r.find("EFIS_L_");
    if (pos != std::string::npos) { r[pos + 5] = 'R'; return r; }
    pos = r.find("_L_");
    if (pos != std::string::npos) { r[pos + 1] = 'R'; return r; }
    return {};
}

bool CatalogEntry::isIncDec() const {
    for (const char* suffix : {"_INC", "_DEC", "_INCREMENT", "_DECREMENT"})
        if (api.size() >= strlen(suffix) && api.compare(api.size() - strlen(suffix), strlen(suffix), suffix) == 0) return true;
    return api.find("BARO_INC") != std::string::npos || api.find("BARO_DEC") != std::string::npos;
}

bool Catalog::load(const std::string& path, std::string& err) {
    std::ifstream in(path);
    if (!in) { err = "cannot open " + path; return false; }
    try {
        nlohmann::json j;
        in >> j;
        aircraft = j.value("aircraft", "");
        title = j.value("title", "");
        entries.clear();
        for (const auto& e : j.value("entries", nlohmann::json::array())) {
            CatalogEntry c;
            c.panel = e.value("panel", "");
            c.section = e.value("section", "");
            c.function = e.value("function", "");
            c.api = e.value("api", "");
            c.name = e.value("name", c.api);
            c.kind = e.value("kind", "sim_event");
            c.values = e.value("values", "");
            c.rw = e.value("rw", "");
            c.type = e.value("type", "");
            c.remark = e.value("remark", "");
            c.description = e.value("description", "");
            c.reference = e.value("reference", "");
            c.live = e.value("live", false);
            c.standard = e.value("standard", false);
            c.cockpitOnly = e.value("cockpit_only", false);
            if (e.contains("value") && e["value"].is_number()) { c.hasValue = true; c.value = e["value"].get<double>(); }
            for (const auto& code : e.value("codes", nlohmann::json::array())) c.codes.push_back(code.get<std::string>());
            if (e.contains("axis_range") && e["axis_range"].is_array() && e["axis_range"].size() == 2) {
                c.hasAxisRange = true;
                c.axisMin = e["axis_range"][0].get<double>();
                c.axisMax = e["axis_range"][1].get<double>();
            }
            c.searchKey = lower(c.panel + " " + c.section + " " + c.function + " " + c.api + " " + c.type + " " + c.remark + " " + c.description);
            entries.push_back(std::move(c));
        }
        sourcePath = path;
        curated = true;
        ++revision;
    } catch (const std::exception& e) {
        err = std::string("parse error: ") + e.what();
        return false;
    }
    return true;
}

}  // namespace lr::app

namespace lr::app {

// ---------------------------------------------------------------- live / standard entries

namespace {
std::string prettify(std::string s) {
    std::replace(s.begin(), s.end(), '_', ' ');
    return s;
}

// Splits "AS1000_PFD_SOFTKEYS_1" into section "AS1000 PFD" and function "SOFTKEYS 1": the first token names the
// system; short or digit-bearing first tokens (AS1000, G1000, FMS) are joined with the second one.
void splitName(const std::string& name, std::string& section, std::string& function) {
    std::vector<std::string> t;
    size_t pos = 0;
    while (pos <= name.size()) {
        size_t u = name.find('_', pos);
        if (u == std::string::npos) u = name.size();
        if (u > pos) t.push_back(name.substr(pos, u - pos));
        pos = u + 1;
    }
    if (t.empty()) { section = "Other"; function = name; return; }
    size_t take = 1;
    const bool digit = std::any_of(t[0].begin(), t[0].end(), [](unsigned char c) { return std::isdigit(c); });
    if (t.size() > 2 && (t[0].size() <= 3 || digit || t[0] == "ANTI")) take = 2;
    if (t.size() <= take) take = t.size() > 1 ? 1 : 0;
    for (size_t i = 0; i < take; ++i) section += (i ? " " : "") + t[i];
    for (size_t i = take; i < t.size(); ++i) function += (i > take ? " " : "") + t[i];
    if (section.empty()) { section = "Other"; }
    if (function.empty()) function = section;
}

std::string lowerKey(const CatalogEntry& c) {
    return lower(c.panel + " " + c.section + " " + c.function + " " + c.api + " " + c.type + " " + c.remark + " " + c.description);
}
}  // namespace

CatalogEntry makeInputEventEntry(const std::string& name, bool isString) {
    CatalogEntry c;
    c.panel = "Cockpit input events (from the sim)";
    splitName(name, c.section, c.function);
    c.api = c.name = name;
    c.kind = "input_event";
    c.type = isString ? "INPUT EVENT (text)" : "INPUT EVENT";
    c.rw = "R/W";
    c.live = true;
    c.searchKey = lowerKey(c);
    return c;
}

CatalogEntry makeLiveLVarEntry(const std::string& name) {
    CatalogEntry c;
    c.panel = "L: variables (from the sim)";
    splitName(name, c.section, c.function);
    c.api = c.name = name;
    c.kind = "lvar";
    c.type = "LVAR";
    c.rw = "R/W";
    c.live = true;
    c.searchKey = lowerKey(c);
    return c;
}

bool Catalog::setLiveEntries(std::vector<CatalogEntry> fresh) {
    // names the curated part already lists stay out of the live part (no duplicates in the panels)
    std::unordered_set<std::string> have;
    for (const auto& e : entries)
        if (!e.live) have.insert(e.api);
    std::vector<CatalogEntry> wanted;
    for (auto& e : fresh)
        if (have.insert(e.api).second) wanted.push_back(std::move(e));

    std::vector<std::string> before, after;
    for (const auto& e : entries) if (e.live) before.push_back(e.api);
    for (const auto& e : wanted) after.push_back(e.api);
    std::sort(before.begin(), before.end());
    std::sort(after.begin(), after.end());
    if (before == after) return false;
    entries.erase(std::remove_if(entries.begin(), entries.end(), [](const CatalogEntry& e) { return e.live; }), entries.end());
    for (auto& e : wanted) entries.push_back(std::move(e));
    ++revision;
    return true;
}

void Catalog::addStandardEntries(const std::vector<CatalogEntry>& standard) {
    removeStandardEntries();
    std::unordered_set<std::string> have;
    for (const auto& e : entries) have.insert(e.api);
    for (const auto& e : standard)
        if (have.insert(e.api).second) entries.push_back(e);
    ++revision;
}

void Catalog::removeStandardEntries() {
    entries.erase(std::remove_if(entries.begin(), entries.end(), [](const CatalogEntry& e) { return e.standard; }), entries.end());
}

bool Catalog::saveLive(const std::string& path, std::string& err) const {
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& e : entries) {
        if (!e.live) continue;
        arr.push_back({{"panel", e.panel}, {"section", e.section}, {"function", e.function}, {"api", e.api}, {"name", e.name},
                       {"kind", e.kind}, {"type", e.type}, {"rw", e.rw}, {"live", true}});
    }
    nlohmann::json j{{"aircraft", aircraft}, {"title", title}, {"live_cache", true}, {"entries", arr}};
    try {
        std::filesystem::create_directories(std::filesystem::path(path).parent_path());
        const std::string tmp = path + ".tmp";
        {
            std::ofstream out(tmp);
            if (!out) { err = "cannot write " + tmp; return false; }
            out << j.dump(1) << "\n";
        }
        std::filesystem::rename(tmp, path);
    } catch (const std::exception& e) {
        err = e.what();
        return false;
    }
    return true;
}

bool Catalog::loadLive(const std::string& path, Catalog& out, std::string& err) {
    std::ifstream in(path);
    if (!in) { err = "cannot open " + path; return false; }
    try {
        nlohmann::json j;
        in >> j;
        out.aircraft = j.value("aircraft", "");
        out.title = j.value("title", "");
        if (out.aircraft.empty()) { err = "no aircraft id"; return false; }
        out.entries.clear();
        for (const auto& e : j.value("entries", nlohmann::json::array())) {
            CatalogEntry c;
            c.panel = e.value("panel", "");
            c.section = e.value("section", "");
            c.function = e.value("function", "");
            c.api = e.value("api", "");
            c.name = e.value("name", c.api);
            c.kind = e.value("kind", "input_event");
            c.type = e.value("type", "");
            c.rw = e.value("rw", "R/W");
            c.live = true;
            if (c.api.empty() || (c.kind != "input_event" && c.kind != "lvar")) continue;
            c.searchKey = lowerKey(c);
            out.entries.push_back(std::move(c));
        }
    } catch (const std::exception& e) {
        err = std::string("parse error: ") + e.what();
        return false;
    }
    return true;
}


// ---------------------------------------------------------------- CatalogSet

Catalog* CatalogSet::find(const std::string& id) {
    for (auto& c : items)
        if (c.aircraft == id) return &c;
    return nullptr;
}

const Catalog* CatalogSet::find(const std::string& id) const {
    for (const auto& c : items)
        if (c.aircraft == id) return &c;
    return nullptr;
}

const Catalog* CatalogSet::forAircraft(const std::string& id) const {
    if (const Catalog* c = find(id)) return c;
    if (const Catalog* s = standard()) return s;
    return items.empty() ? nullptr : &items.front();
}

void CatalogSet::addShipped(Catalog c) {
    if (Catalog* existing = find(c.aircraft)) {
        *existing = std::move(c);
        ++existing->revision;
    } else {
        items.push_back(std::move(c));
    }
}

Catalog& CatalogSet::ensure(const std::string& id, const std::string& title) {
    if (Catalog* c = find(id)) {
        if (c->title.empty()) c->title = title;
        return *c;
    }
    Catalog c;
    c.aircraft = id;
    // cached titles already carry the suffix: never add it twice
    c.title = title.find("(from the sim)") != std::string::npos ? title : title + " (from the sim)";
    items.push_back(std::move(c));
    return items.back();
}

std::vector<CatalogEntry> CatalogSet::liveEntriesFor(const std::vector<std::pair<std::string, bool>>& inputEvents, const std::vector<std::string>& lvars) {
    std::vector<CatalogEntry> out;
    out.reserve(inputEvents.size() + lvars.size());
    for (const auto& [name, isString] : inputEvents)
        if (!isString) out.push_back(makeInputEventEntry(name, false));  // text-valued events cannot be toggled or set from a number
    for (const auto& n : lvars) out.push_back(makeLiveLVarEntry(n));
    return out;
}

bool CatalogSet::applyLive(const std::string& id, const std::string& title, const std::vector<CatalogEntry>& fresh) {
    Catalog& cat = ensure(id, title);
    // union with what an earlier session cached: lists grow as more of the cockpit gets exercised (L: vars appear lazily)
    std::vector<CatalogEntry> merged = fresh;
    std::unordered_set<std::string> have;
    for (const auto& e : merged) have.insert(e.kind + ":" + e.api);
    for (const auto& e : cat.entries)
        if (e.live && have.insert(e.kind + ":" + e.api).second) merged.push_back(e);
    const bool changed = cat.setLiveEntries(std::move(merged));
    if (changed && !cat.curated) {
        if (const Catalog* s = standard()) cat.addStandardEntries(s->entries);
    }
    return changed;
}

bool CatalogSet::forgetLive(const std::string& id) {
    Catalog* cat = find(id);
    if (!cat) return false;
    const bool changed = cat->setLiveEntries({});
    return changed;
}

void CatalogSet::refreshStandard() {
    const Catalog* s = standard();
    for (auto& c : items) {
        if (c.aircraft == kStandardCatalogId || c.curated) continue;
        if (s) c.addStandardEntries(s->entries);
        else c.removeStandardEntries();
    }
}

}  // namespace lr::app
