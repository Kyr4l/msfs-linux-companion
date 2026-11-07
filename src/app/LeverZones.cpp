// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kyr4l
#include "app/LeverZones.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>

#include <nlohmann/json.hpp>

#include "util/AtomicFile.hpp"

namespace lr::app {

using nlohmann::json;

namespace {

std::string replaceAll(std::string s, const std::string& from, const std::string& to) {
    if (from.empty()) return s;
    for (size_t at = s.find(from); at != std::string::npos; at = s.find(from, at + to.size())) s.replace(at, from.size(), to);
    return s;
}

std::string withEngine(const std::string& s, int engine) { return replaceAll(s, "{n}", std::to_string(engine)); }

// Engine numbers in the usual places of event and variable names, back to {n}: THROTTLE1_, Throttle_1, :1, (one digit only, so a
// value such as 16384 is never touched)
std::string generalize(std::string s, int engine) {
    const std::string e = std::to_string(engine);
    for (const char* stem : {"THROTTLE", "MIXTURE", "PROP_PITCH", "LastAB", "MIXTURE_RATIO", "ENGINE_Throttle_", "ENGINE_THROTTLE_", "MLC_REV_LEVER_"}) {
        const std::string from = std::string(stem) + e;
        for (size_t at = s.find(from); at != std::string::npos; at = s.find(from, at + 1)) {
            const size_t after = at + from.size();
            if (after < s.size() && std::isdigit(static_cast<unsigned char>(s[after]))) continue;
            s.replace(at, from.size(), std::string(stem) + "{n}");
        }
    }
    for (const char* tail : {",", ")"}) s = replaceAll(s, ":" + e + tail, std::string(":{n}") + tail);
    return s;
}

Action makeAction(ActionKind kind, const std::string& name, double value = 0) {
    Action a;
    a.kind = kind;
    a.name = name;
    a.value = value;
    return a;
}

std::string number(double v) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.10g", v);
    return buf;
}

json actionsJson(const std::vector<Action>& list) {
    json a = json::array();
    for (const auto& x : list) a.push_back(json{{"kind", actionKindName(x.kind)}, {"name", x.name}, {"value", std::isfinite(x.value) ? x.value : 0.0}});
    return a;
}

void readActions(const json& j, std::vector<Action>& out) {
    out.clear();
    if (!j.is_array()) return;
    for (const auto& x : j) {
        if (!x.is_object()) continue;
        Action a;
        if (!actionKindFromName(x.value("kind", ""), a.kind)) continue;
        a.name = x.value("name", "");
        a.value = x.contains("value") && x["value"].is_number() ? x["value"].get<double>() : 0.0;
        out.push_back(a);
    }
}

std::vector<Action> substituted(const std::vector<Action>& list, int engine) {
    std::vector<Action> out = list;
    for (auto& a : out) a.name = withEngine(a.name, engine);
    return out;
}

}  // namespace

std::string reverseFlagName(int engine) { return "MLC_REV_LEVER_" + std::to_string(engine); }

std::vector<Action> reverseZoneActions(const ReverseZone& r, const std::string& bindingAxisEvent, bool entering) {
    const std::string n = std::to_string(r.engine);
    const std::string reversed = withEngine(r.reversedCode, r.engine);
    const std::string axis = r.axisEvent.empty() ? bindingAxisEvent : withEngine(r.axisEvent, r.engine);
    const bool shared = r.otherEngine > 0 && r.otherEngine != r.engine;
    // the lever with the higher engine number goes second, so two levers pulled together switch reverse once
    const double second = shared && r.engine > r.otherEngine ? 300 : 0;
    const std::string toggle = "0 (>K:" + r.toggleEvent + ")";
    std::vector<Action> out;
    if (reversed.empty() || axis.empty() || r.toggleEvent.empty()) return out;  // not set up: do nothing rather than guess
    if (entering) {
        if (shared) out.push_back(makeAction(ActionKind::LVarSet, reverseFlagName(r.engine), 1));
        if (second > 0) out.push_back(makeAction(ActionKind::Delay, "", second));
        const std::string when = r.groundOnly ? "(A:SIM ON GROUND, Bool) " + reversed + " ! and" : reversed + " !";
        out.push_back(makeAction(ActionKind::CalcCode, when + " if{ " + toggle + " }"));
        out.push_back(makeAction(ActionKind::Delay, "", 250));
        // the amount only once the aircraft reports reverse: without it the same value is forward thrust
        out.push_back(makeAction(ActionKind::CalcCode, reversed + " if{ " + number(r.amount) + " (>K:" + axis + ") }"));
    } else {
        if (shared) out.push_back(makeAction(ActionKind::LVarSet, reverseFlagName(r.engine), 0));
        out.push_back(makeAction(ActionKind::CalcCode, number(r.idle) + " (>K:" + axis + ")"));
        out.push_back(makeAction(ActionKind::Delay, "", 150 + second));
        const std::string when = shared ? "(L:" + reverseFlagName(r.otherEngine) + ", Number) 0 == " + reversed + " and" : reversed;
        out.push_back(makeAction(ActionKind::CalcCode, when + " if{ " + toggle + " }"));
    }
    return out;
}

std::string leverAxisEvent(const Binding& b) {
    for (const auto& a : b.onPress)
        if (a.kind == ActionKind::SimEvent && !a.name.empty()) return a.name;
    return {};
}

int leverEngine(const Binding& b) {
    // THROTTLE2_AXIS_SET_EX1 (K: event) or ENGINE_Throttle_2 (the F/A-18E's cockpit event): the digit after THROTTLE, with or without "_"
    for (const auto& a : b.onPress) {
        std::string upper = a.name;
        for (char& c : upper) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        const size_t at = upper.find("THROTTLE");
        if (at == std::string::npos) continue;
        size_t d = at + 8;
        if (d < upper.size() && upper[d] == '_') ++d;
        if (d < upper.size() && std::isdigit(static_cast<unsigned char>(upper[d])) &&
            (d + 1 >= upper.size() || !std::isdigit(static_cast<unsigned char>(upper[d + 1]))))
            return upper[d] - '0';
    }
    if (b.cutoffMode == ZoneMode::Reverse) return b.reverse.engine;
    return 1;
}

bool loadLeverRecipes(const std::string& path, bool userMade, std::vector<LeverRecipe>& out, std::string& err) {
    std::ifstream in(path);
    if (!in) { err = "cannot open " + path; return false; }
    json j;
    try { j = json::parse(in); } catch (const std::exception& e) { err = path + ": " + e.what(); return false; }
    if (!j.contains("recipes") || !j["recipes"].is_array()) { err = path + ": no \"recipes\" list"; return false; }
    for (const auto& rj : j["recipes"]) {
        if (!rj.is_object() || !rj.contains("id") || !rj.contains("title")) continue;
        LeverRecipe r;
        r.userMade = userMade;
        r.id = rj.value("id", "");
        r.title = rj.value("title", "");
        r.checked = rj.value("checked", "");
        r.verified = rj.value("verified", false);
        if (rj.contains("aircraft") && rj["aircraft"].is_array())
            for (const auto& a : rj["aircraft"]) if (a.is_string()) r.aircraft.push_back(a.get<std::string>());
        readActions(rj.value("press", json::array()), r.press);
        r.integer = rj.value("integer", true);
        const json lever = rj.value("lever", json::object());
        AxisDetents& d = r.detents;
        d.top = lever.value("top", "full") == "full" ? LeverTop::Full : LeverTop::Max;
        d.outCutoff = lever.value("out_cutoff", 0.0);
        d.outIdle = lever.value("out_idle", 0.0);
        d.outMil = lever.value("out_mil", 1.0);
        if (lever.contains("stages") && lever["stages"].is_array())
            for (const auto& sj : lever["stages"]) {
                if (!sj.is_object()) continue;
                LeverStage st;
                st.name = sj.value("name", "");
                st.at = std::clamp(sj.value("at", 0.5), 0.0, 1.0);
                st.value = sj.value("value", 0.0);
                st.band = std::clamp(sj.value("band", 0.03), 0.0, 0.5);
                d.stages.push_back(st);
                r.atGate.push_back(std::max(0.0, sj.value("at_gate", 0.0)));
            }
        if (rj.contains("cutoff") && rj["cutoff"].is_object()) {
            const json& z = rj["cutoff"];
            if (z.value("mode", "") == "reverse") {
                r.cutoffMode = ZoneMode::Reverse;
                r.reverse.toggleEvent = z.value("toggle_event", r.reverse.toggleEvent);
                r.reverse.reversedCode = z.value("reversed", "");
                r.reverse.axisEvent = z.value("axis_event", "");
                r.reverse.amount = z.value("amount", r.reverse.amount);
                r.reverse.idle = z.value("idle", r.reverse.idle);
                r.reverse.groundOnly = z.value("ground_only", true);
                r.reverseShared = z.value("shared", false);
            } else {
                readActions(z.value("enter", json::array()), r.onCutoff);
                readActions(z.value("leave", json::array()), r.onCutoffLeave);
            }
        }
        if (rj.contains("afterburner") && rj["afterburner"].is_object()) {
            readActions(rj["afterburner"].value("enter", json::array()), r.onAfterburner);
            readActions(rj["afterburner"].value("leave", json::array()), r.onAfterburnerLeave);
        }
        out.push_back(std::move(r));
    }
    return true;
}

bool saveUserLeverRecipes(const std::string& path, const std::vector<LeverRecipe>& recipes, std::string& err) {
    json list = json::array();
    for (const auto& r : recipes) {
        if (!r.userMade) continue;
        json stages = json::array();
        for (size_t i = 0; i < r.detents.stages.size(); ++i) {
            const auto& st = r.detents.stages[i];
            json sj{{"name", st.name}, {"at", st.at}, {"value", st.value}, {"band", st.band}};
            if (i < r.atGate.size() && r.atGate[i] > 0) sj["at_gate"] = r.atGate[i];
            stages.push_back(sj);
        }
        json rj{{"id", r.id}, {"title", r.title}, {"checked", r.checked}, {"verified", r.verified}, {"aircraft", r.aircraft},
                {"press", actionsJson(r.press)}, {"integer", r.integer},
                {"lever", {{"top", r.detents.top == LeverTop::Full ? "full" : "max"}, {"out_cutoff", r.detents.outCutoff},
                           {"out_idle", r.detents.outIdle}, {"out_mil", r.detents.outMil}, {"stages", stages}}}};
        if (r.cutoffMode == ZoneMode::Reverse)
            rj["cutoff"] = json{{"mode", "reverse"}, {"toggle_event", r.reverse.toggleEvent}, {"reversed", r.reverse.reversedCode},
                                {"axis_event", r.reverse.axisEvent}, {"amount", r.reverse.amount}, {"idle", r.reverse.idle},
                                {"ground_only", r.reverse.groundOnly}, {"shared", r.reverseShared}};
        else if (!r.onCutoff.empty() || !r.onCutoffLeave.empty())
            rj["cutoff"] = json{{"enter", actionsJson(r.onCutoff)}, {"leave", actionsJson(r.onCutoffLeave)}};
        if (!r.onAfterburner.empty() || !r.onAfterburnerLeave.empty())
            rj["afterburner"] = json{{"enter", actionsJson(r.onAfterburner)}, {"leave", actionsJson(r.onAfterburnerLeave)}};
        list.push_back(rj);
    }
    return writeFileAtomically(path, json{{"recipes", list}}.dump(1) + "\n", err);
}

namespace {

// The lever part of a binding as the recipe would make it (the parts a recipe does not set are copied from `b`).
Binding recipeResult(const LeverRecipe& r, int engine, double gateAt, const Binding& b) {
    Binding out = b;
    AxisConfig& c = out.axis;
    AxisDetents& d = c.detents;
    d.enabled = true;
    d.top = r.detents.top;
    d.outCutoff = r.detents.outCutoff;
    d.outIdle = r.detents.outIdle;
    d.outMil = r.detents.outMil;
    d.stages = r.detents.stages;
    for (size_t i = 0; i < d.stages.size() && i < r.atGate.size(); ++i)
        if (r.atGate[i] > 0) d.stages[i].at = std::clamp(r.atGate[i] * (gateAt > 0 ? gateAt : 0.75), 0.0, 1.0);
    c.integer = r.integer;
    if (!r.press.empty()) out.onPress = substituted(r.press, engine);
    out.cutoffMode = r.cutoffMode;
    out.reverse = ReverseZone{};
    if (r.cutoffMode == ZoneMode::Reverse) {
        out.reverse = r.reverse;
        out.reverse.engine = engine;
        out.reverse.otherEngine = r.reverseShared ? (engine == 1 ? 2 : 1) : 0;
        out.onCutoff.clear();
        out.onCutoffLeave.clear();
    } else {
        out.onCutoff = substituted(r.onCutoff, engine);
        out.onCutoffLeave = substituted(r.onCutoffLeave, engine);
    }
    out.onAfterburner = substituted(r.onAfterburner, engine);
    out.onAfterburnerLeave = substituted(r.onAfterburnerLeave, engine);
    out.recipe = r.id;
    return out;
}

bool sameActions(const std::vector<Action>& a, const std::vector<Action>& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (a[i].kind != b[i].kind || a[i].name != b[i].name || std::fabs(a[i].value - b[i].value) > 1e-9) return false;
    return true;
}

}  // namespace

void applyLeverRecipe(const LeverRecipe& r, int engine, double gateAt, Binding& b) { b = recipeResult(r, engine, gateAt, b); }

bool leverMatchesRecipe(const LeverRecipe& r, int engine, double gateAt, const Binding& b) {
    const Binding want = recipeResult(r, engine, gateAt, b);
    const AxisDetents &x = want.axis.detents, &y = b.axis.detents;
    if (x.top != y.top || x.outCutoff != y.outCutoff || x.outIdle != y.outIdle || x.outMil != y.outMil || x.stages.size() != y.stages.size()) return false;
    for (size_t i = 0; i < x.stages.size(); ++i)
        if (x.stages[i].name != y.stages[i].name || std::fabs(x.stages[i].at - y.stages[i].at) > 1e-6 || x.stages[i].value != y.stages[i].value ||
            std::fabs(x.stages[i].band - y.stages[i].band) > 1e-6)
            return false;
    if (!r.press.empty() && !sameActions(want.onPress, b.onPress)) return false;
    return want.cutoffMode == b.cutoffMode && want.reverse == b.reverse && sameActions(want.onCutoff, b.onCutoff) &&
           sameActions(want.onCutoffLeave, b.onCutoffLeave) && sameActions(want.onAfterburner, b.onAfterburner) &&
           sameActions(want.onAfterburnerLeave, b.onAfterburnerLeave);
}

LeverRecipe recipeFromBinding(const Binding& b, const std::string& id, const std::string& title) {
    const int engine = leverEngine(b);
    LeverRecipe r;
    r.id = id;
    r.title = title;
    r.userMade = true;
    r.checked = "Saved from your own lever";
    r.detents = b.axis.detents;
    r.integer = b.axis.integer;
    r.cutoffMode = b.cutoffMode;
    if (b.cutoffMode == ZoneMode::Reverse) {
        r.reverse = b.reverse;
        r.reverse.reversedCode = generalize(b.reverse.reversedCode, engine);
        r.reverse.axisEvent = generalize(b.reverse.axisEvent, engine);
        r.reverseShared = b.reverse.otherEngine > 0;
    }
    auto gen = [engine](std::vector<Action> list) { for (auto& a : list) a.name = generalize(a.name, engine); return list; };
    r.press = gen(b.onPress);
    r.onCutoff = gen(b.onCutoff);
    r.onCutoffLeave = gen(b.onCutoffLeave);
    r.onAfterburner = gen(b.onAfterburner);
    r.onAfterburnerLeave = gen(b.onAfterburnerLeave);
    return r;
}

}  // namespace lr::app
