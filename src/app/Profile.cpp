// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kyr4l
#include "app/Profile.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>

#include <nlohmann/json.hpp>

namespace lr::app {

using nlohmann::json;

const char* actionKindName(ActionKind k) {
    switch (k) {
        case ActionKind::SimEvent: return "sim_event";
        case ActionKind::HEvent: return "h_event";
        case ActionKind::LVarSet: return "lvar_set";
        case ActionKind::LVarToggle: return "lvar_toggle";
        case ActionKind::CalcCode: return "calc_code";
        case ActionKind::InputEvent: return "input_event";
        case ActionKind::InputEventToggle: return "input_event_toggle";
    }
    return "sim_event";
}

const char* actionKindLabel(ActionKind k) {
    switch (k) {
        case ActionKind::SimEvent: return "K: event (SimConnect / custom)";
        case ActionKind::HEvent: return "H: event (needs MobiFlight)";
        case ActionKind::LVarSet: return "L: var set (needs MobiFlight)";
        case ActionKind::LVarToggle: return "L: var toggle 0/1 (needs MobiFlight)";
        case ActionKind::CalcCode: return "RPN calculator code (needs MobiFlight)";
        case ActionKind::InputEvent: return "B: input event, set value (cockpit switch)";
        case ActionKind::InputEventToggle: return "B: input event, toggle / cycle positions";
    }
    return "?";
}

bool actionKindFromName(const std::string& s, ActionKind& out) {
    for (ActionKind k : {ActionKind::SimEvent, ActionKind::HEvent, ActionKind::LVarSet, ActionKind::LVarToggle, ActionKind::CalcCode,
                         ActionKind::InputEvent, ActionKind::InputEventToggle}) {
        if (s == actionKindName(k)) { out = k; return true; }
    }
    return false;
}

bool actionNeedsMobiFlight(ActionKind k) { return k != ActionKind::SimEvent && k != ActionKind::InputEvent && k != ActionKind::InputEventToggle; }

static std::string num(double v) {
    char buf[64];
    if (std::fabs(v - std::round(v)) < 1e-9) snprintf(buf, sizeof buf, "%lld", static_cast<long long>(std::llround(v)));
    else snprintf(buf, sizeof buf, "%g", v);
    return buf;
}

std::string Action::describe() const {
    switch (kind) {
        case ActionKind::SimEvent: return "K:" + name + (value != 0 ? " " + num(value) : "");
        case ActionKind::HEvent: return "H:" + name;
        case ActionKind::LVarSet: return "L:" + name + " = " + num(value);
        case ActionKind::LVarToggle: return "L:" + name + " toggle";
        case ActionKind::CalcCode: return "calc: " + name;
        case ActionKind::InputEvent: return "B:" + name + " = " + num(value);
        case ActionKind::InputEventToggle: return "B:" + name + (value > 2 ? " cycle " + num(value) : std::string(" toggle"));
    }
    return name;
}

bool AxisCalibration::valid() const {
    const bool rev = cutoff > ab;
    auto n = [rev](double x) { return rev ? 1.0 - x : x; };
    const double c = n(cutoff), i = n(idle), m = n(mil), a = n(ab);
    return c >= 0.0 && a <= 1.0 && i - c >= 0.01 && m - i >= 0.1 && a - m >= 0.01 && snap >= 0.0 && snap < (m - i) / 2;
}

const AxisCalibration* Settings::calibrationFor(const input::InputId& axis) const {
    for (const auto& c : calibrations)
        if (c.device == axis.device && c.code == axis.code) return &c;
    return nullptr;
}

void Settings::setCalibration(const AxisCalibration& c) {
    for (auto& x : calibrations)
        if (x.device == c.device && x.code == c.code) { x = c; return; }
    calibrations.push_back(c);
}

void Settings::clearCalibration(const input::InputId& axis) {
    calibrations.erase(std::remove_if(calibrations.begin(), calibrations.end(),
                                      [&](const AxisCalibration& c) { return c.device == axis.device && c.code == axis.code; }),
                       calibrations.end());
}

Profile* Settings::active() {
    for (auto& p : profiles)
        if (p.name == activeProfile) return &p;
    return profiles.empty() ? nullptr : &profiles.front();
}

const Profile* Settings::active() const {
    for (const auto& p : profiles)
        if (p.name == activeProfile) return &p;
    return profiles.empty() ? nullptr : &profiles.front();
}

Profile& Settings::ensureActive() {
    if (profiles.empty()) { Profile first; first.name = "A32NX"; profiles.push_back(first); }
    Profile* p = active();
    if (!p) p = &profiles.front();
    activeProfile = p->name;
    return *p;
}

static json toJson(const Action& a) {
    return json{{"kind", actionKindName(a.kind)}, {"name", a.name}, {"value", a.value}};
}

static Action actionFromJson(const json& j) {
    Action a;
    actionKindFromName(j.value("kind", "sim_event"), a.kind);
    a.name = j.value("name", "");
    a.value = j.value("value", 0.0);
    return a;
}

static json toJson(const AxisConfig& c) {
    json j{{"mode", c.mode == AxisMode::Steps ? "steps" : "value"}, {"invert", c.invert},
           {"in_min", c.inMin}, {"in_max", c.inMax}, {"out_min", c.outMin}, {"out_max", c.outMax},
           {"integer", c.integer}, {"min_interval_ms", c.minIntervalMs}, {"step_percent", c.stepPercent}};
    if (c.detents.enabled)  // omitted otherwise: files of plain axes stay as before
        j["detents"] = json{{"enabled", true}, {"out_cutoff", c.detents.outCutoff}, {"out_idle", c.detents.outIdle}, {"out_mil", c.detents.outMil}};
    return j;
}

static AxisConfig axisFromJson(const json& j) {
    AxisConfig c;
    c.mode = j.value("mode", "value") == "steps" ? AxisMode::Steps : AxisMode::Value;
    c.invert = j.value("invert", false);
    c.inMin = j.value("in_min", 0.0);
    c.inMax = j.value("in_max", 1.0);
    c.outMin = j.value("out_min", 0.0);
    c.outMax = j.value("out_max", 100.0);
    c.integer = j.value("integer", true);
    c.minIntervalMs = j.value("min_interval_ms", 40);
    c.stepPercent = j.value("step_percent", 5.0);
    if (j.contains("detents") && j["detents"].is_object()) {
        const json& d = j["detents"];
        c.detents.enabled = d.value("enabled", false);
        c.detents.outCutoff = d.value("out_cutoff", c.detents.outCutoff);
        c.detents.outIdle = d.value("out_idle", c.detents.outIdle);
        c.detents.outMil = d.value("out_mil", c.detents.outMil);
    }
    if (c.inMax <= c.inMin) { c.inMin = 0.0; c.inMax = 1.0; }
    if (c.stepPercent < 0.5) c.stepPercent = 0.5;
    return c;
}

static json toJson(const Binding& b) {
    json press = json::array(), release = json::array();
    for (const auto& a : b.onPress) press.push_back(toJson(a));
    for (const auto& a : b.onRelease) release.push_back(toJson(a));
    json out = json{
        {"label", b.label},
        {"input", {{"device", b.input.device}, {"kind", input::kindName(b.input.kind)}, {"code", b.input.code}, {"dir", b.input.dir}}},
        {"on_press", press},
        {"on_release", release},
        {"repeat", b.repeat},
        {"repeat_delay_ms", b.repeatDelayMs},
        {"repeat_interval_ms", b.repeatIntervalMs},
        {"enabled", b.enabled},
    };
    if (b.input.kind == input::InputKind::Axis) out["axis"] = toJson(b.axis);  // omitted for buttons/hats: files stay as before
    if (b.axis.detents.enabled && b.input.kind == input::InputKind::Axis) {
        auto list = [](const std::vector<Action>& v) { json a = json::array(); for (const auto& x : v) a.push_back(toJson(x)); return a; };
        out["zone_actions"] = json{{"cutoff", list(b.onCutoff)}, {"cutoff_leave", list(b.onCutoffLeave)},
                                   {"afterburner", list(b.onAfterburner)}, {"afterburner_leave", list(b.onAfterburnerLeave)}};
    }
    return out;
}

static Binding bindingFromJson(const json& j) {
    Binding b;
    b.label = j.value("label", "");
    if (j.contains("input")) {
        const json& i = j["input"];
        b.input.device = i.value("device", "");
        b.input.kind = input::kindFromName(i.value("kind", "button"));
        b.input.code = i.value("code", 0);
        b.input.dir = i.value("dir", 0);
    }
    for (const auto& a : j.value("on_press", json::array())) b.onPress.push_back(actionFromJson(a));
    for (const auto& a : j.value("on_release", json::array())) b.onRelease.push_back(actionFromJson(a));
    if (j.contains("axis")) b.axis = axisFromJson(j["axis"]);
    if (j.contains("zone_actions") && j["zone_actions"].is_object()) {
        const json& z = j["zone_actions"];
        auto read = [&z](const char* key, std::vector<Action>& into) { for (const auto& a : z.value(key, json::array())) into.push_back(actionFromJson(a)); };
        read("cutoff", b.onCutoff);
        read("cutoff_leave", b.onCutoffLeave);
        read("afterburner", b.onAfterburner);
        read("afterburner_leave", b.onAfterburnerLeave);
    }
    b.repeat = j.value("repeat", false);
    b.repeatDelayMs = j.value("repeat_delay_ms", 400);
    b.repeatIntervalMs = j.value("repeat_interval_ms", 100);
    b.enabled = j.value("enabled", true);
    return b;
}

bool axisEventRange(const std::string& eventName, double& lo, double& hi) {
    std::string n = eventName;
    if (n.rfind("K:", 0) == 0) n.erase(0, 2);
    auto ends = [&](const char* suffix) { return n.size() >= strlen(suffix) && n.compare(n.size() - strlen(suffix), strlen(suffix), suffix) == 0; };
    if (ends("_AXIS_SET_EX1")) { lo = -16384; hi = 16383; return true; }
    if (n.rfind("AXIS_", 0) == 0 && ends("_SET")) { lo = -16383; hi = 16383; return true; }
    return false;
}

bool axisRangeLooksWrong(const AxisConfig& axis, double lo, double hi) {
    if (axis.mode != AxisMode::Value) return false;
    return (hi - lo) >= 1000 && std::fabs(axis.outMax - axis.outMin) < 1000;
}

bool loadSettings(const std::string& path, Settings& out, std::string& err) {
    std::ifstream in(path);
    if (!in) { err = "cannot open " + path; return false; }
    json j;
    try {
        in >> j;
        Settings s;
        s.loadedVersion = j.value("version", 1);
        s.host = j.value("host", s.host);
        s.port = j.value("port", s.port);
        s.autoConnect = j.value("auto_connect", s.autoConnect);
        s.guideAutodetect = j.value("guide_autodetect", s.guideAutodetect);
        s.activeProfile = j.value("active_profile", "");
        for (const auto& cj : j.value("calibrations", json::array())) {
            AxisCalibration c;
            c.device = cj.value("device", "");
            c.code = cj.value("code", 0);
            c.cutoff = cj.value("cutoff", c.cutoff);
            c.idle = cj.value("idle", c.idle);
            c.mil = cj.value("mil", c.mil);
            c.ab = cj.value("ab", c.ab);
            c.snap = cj.value("snap", c.snap);
            if (!c.device.empty()) s.calibrations.push_back(c);
        }
        if (j.contains("haptics")) {
            const json& h = j["haptics"];
            s.haptics.enabled = h.value("enabled", false);
            s.haptics.master = h.value("master", 1.0f);
            const json rulesJ = h.value("rules", json::object());
            for (const auto& [id, rj] : rulesJ.items()) {
                haptics::RuleConfig rc;
                rc.enabled = rj.value("enabled", true);
                rc.gain = rj.value("gain", 1.0f);
                s.haptics.rules[id] = rc;
            }
            const json devicesJ = h.value("devices", json::object());
            for (const auto& [key, dj] : devicesJ.items()) {
                haptics::DeviceConfig dc;
                dc.enabled = dj.value("enabled", true);
                dc.identity.family = dj.value("family", -1);
                dc.identity.left.device = dj.value("left_device", -1);
                dc.identity.left.index = dj.value("left_index", 0);
                dc.identity.right.device = dj.value("right_device", -1);
                dc.identity.right.index = dj.value("right_index", 0);
                s.haptics.devices[key] = dc;
            }
        }
        for (const auto& pj : j.value("profiles", json::array())) {
            Profile p;
            p.name = pj.value("name", "Profile");
            p.aircraft = pj.value("aircraft", std::string(kDefaultAircraft));
            if (p.aircraft.empty()) p.aircraft = kDefaultAircraft;
            for (const auto& bj : pj.value("bindings", json::array())) p.bindings.push_back(bindingFromJson(bj));
            s.profiles.push_back(std::move(p));
        }
        out = std::move(s);
    } catch (const std::exception& e) {
        err = std::string("parse error: ") + e.what();
        return false;
    }
    return true;
}

bool saveSettings(const std::string& path, const Settings& s, std::string& err) {
    json profiles = json::array();
    for (const auto& p : s.profiles) {
        json bindings = json::array();
        for (const auto& b : p.bindings) bindings.push_back(toJson(b));
        profiles.push_back(json{{"name", p.name}, {"aircraft", p.aircraft}, {"bindings", bindings}});
    }
    json rules = json::object(), devices = json::object();
    for (const auto& [id, rc] : s.haptics.rules) rules[id] = json{{"enabled", rc.enabled}, {"gain", rc.gain}};
    for (const auto& [key, dc] : s.haptics.devices)
        devices[key] = json{{"enabled", dc.enabled}, {"family", dc.identity.family},
                            {"left_device", dc.identity.left.device}, {"left_index", dc.identity.left.index},
                            {"right_device", dc.identity.right.device}, {"right_index", dc.identity.right.index}};
    json calibrations = json::array();
    for (const auto& c : s.calibrations)
        calibrations.push_back(json{{"device", c.device}, {"code", c.code}, {"cutoff", c.cutoff}, {"idle", c.idle}, {"mil", c.mil}, {"ab", c.ab}, {"snap", c.snap}});
    json j{{"version", kConfigVersion}, {"host", s.host}, {"port", s.port}, {"auto_connect", s.autoConnect}, {"guide_autodetect", s.guideAutodetect},
           {"active_profile", s.activeProfile}, {"profiles", profiles}, {"calibrations", calibrations},
           {"haptics", {{"enabled", s.haptics.enabled}, {"master", s.haptics.master}, {"rules", rules}, {"devices", devices}}}};
    try {
        std::filesystem::create_directories(std::filesystem::path(path).parent_path());
        // First save over a file written by an older schema: keep a copy of it once, untouched, so an
        // upgrade can always be rolled back (config.json.v1.bak).
        if (std::filesystem::exists(path)) {
            int onDisk = 1;
            try {
                std::ifstream old(path);
                json oj;
                old >> oj;
                onDisk = oj.value("version", 1);
            } catch (...) {
            }
            const std::string bak = path + ".v" + std::to_string(onDisk) + ".bak";
            if (onDisk < kConfigVersion && !std::filesystem::exists(bak)) std::filesystem::copy_file(path, bak);
        }
        const std::string tmp = path + ".tmp";
        {
            std::ofstream outFile(tmp);
            if (!outFile) { err = "cannot write " + tmp; return false; }
            outFile << j.dump(2) << "\n";
        }
        std::filesystem::rename(tmp, path);
    } catch (const std::exception& e) {
        err = e.what();
        return false;
    }
    return true;
}

}  // namespace lr::app
