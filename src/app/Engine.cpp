// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kyr4l
#include "app/Engine.hpp"

#include <algorithm>
#include <cmath>

#include "util/Log.hpp"

namespace lr::app {

using clock = std::chrono::steady_clock;

Engine::Engine(sim::SimBridge& bridge) : bridge_(bridge) {}

void Engine::setProfile(std::shared_ptr<const Profile> profile) {
    std::lock_guard<std::mutex> lock(m_);
    profile_ = std::move(profile);
    held_.clear();
    axisState_.clear();
    scheduled_.erase(std::remove_if(scheduled_.begin(), scheduled_.end(), [](const Scheduled& x) { return x.binding >= 0; }), scheduled_.end());
}

void Engine::setCalibrations(std::vector<AxisCalibration> calibrations) {
    auto list = std::make_shared<const std::vector<AxisCalibration>>(std::move(calibrations));
    std::lock_guard<std::mutex> lock(m_);
    calibrations_ = std::move(list);
    for (auto& [idx, st] : axisState_) st.zoneKnown = false;  // the gates moved: the zone is looked at afresh
}

void Engine::setProbes(std::function<bool(const std::string&, int)> buttonDown, std::function<float(const std::string&, int)> axisValue) {
    std::lock_guard<std::mutex> lock(m_);
    buttonProbe_ = std::move(buttonDown);
    axisProbe_ = std::move(axisValue);
}

void Engine::beginGateLearning(const AxisCalibration& cal) {
    std::lock_guard<std::mutex> lock(m_);
    learn_ = GateLearning{};
    learn_.active = true;
    learn_.device = cal.device;
    learn_.axisCode = cal.code;
    learn_.button[0] = cal.cutoffBtn;
    learn_.button[1] = cal.idleBtn;
    learn_.button[2] = cal.milBtn;
    learn_.button[3] = cal.abBtn;
}

void Engine::endGateLearning() {
    std::lock_guard<std::mutex> lock(m_);
    learn_ = GateLearning{};
}

std::array<std::optional<double>, 4> Engine::takeGateLearned() {
    std::lock_guard<std::mutex> lock(m_);
    const auto raw = learn_.raw;
    learn_.raw = {};
    return raw;
}

bool Engine::buttonIsDown(const std::string& device, int code) {
    const std::string key = device + "#" + std::to_string(code);
    std::function<bool(const std::string&, int)> probe;
    {
        std::lock_guard<std::mutex> lock(m_);
        auto it = buttonState_.find(key);
        if (it != buttonState_.end()) return it->second;
        probe = buttonProbe_;
    }
    const bool down = probe ? probe(device, code) : false;  // not seen move yet: ask the input manager once
    std::lock_guard<std::mutex> lock(m_);
    return buttonState_.emplace(key, down).first->second;
}

Engine::GateState Engine::gatesOf(const AxisCalibration& cal) {
    GateState g;
    if (cal.cutoffBtn >= 0) g.cutoff = buttonIsDown(cal.device, cal.cutoffBtn);
    if (cal.idleBtn >= 0) g.idle = buttonIsDown(cal.device, cal.idleBtn);
    if (cal.milBtn >= 0) g.mil = buttonIsDown(cal.device, cal.milBtn);
    if (cal.abBtn >= 0) g.ab = buttonIsDown(cal.device, cal.abBtn);
    return g;
}

double Engine::axisRawOf(const AxisCalibration& cal) {
    const std::string key = cal.device + "#" + std::to_string(cal.code);
    std::function<float(const std::string&, int)> probe;
    {
        std::lock_guard<std::mutex> lock(m_);
        auto it = axisRaw_.find(key);
        if (it != axisRaw_.end()) return it->second;
        probe = axisProbe_;
    }
    const float v = probe ? probe(cal.device, cal.code) : -1.0f;
    return v >= 0 ? v : cal.idle;  // never moved and unknown: where the lever rests at idle
}

void Engine::setEnabled(bool on) {
    std::lock_guard<std::mutex> lock(m_);
    enabled_ = on;
    if (!on) held_.clear();
}

void Engine::beginCapture(bool buttonsOnly) {
    std::lock_guard<std::mutex> lock(m_);
    capturing_ = true;
    captureButtonsOnly_ = buttonsOnly;
    captured_.reset();
    captureBase_.clear();
}

void Engine::cancelCapture() {
    std::lock_guard<std::mutex> lock(m_);
    capturing_ = false;
    captureButtonsOnly_ = false;
    captured_.reset();
}

bool Engine::capturing() const {
    std::lock_guard<std::mutex> lock(m_);
    return capturing_;
}

std::optional<input::InputId> Engine::takeCaptured() {
    std::lock_guard<std::mutex> lock(m_);
    auto r = captured_;
    captured_.reset();
    return r;
}

std::optional<input::InputEvent> Engine::lastInput() const {
    std::lock_guard<std::mutex> lock(m_);
    return lastInput_;
}

std::vector<FiredRecord> Engine::recentFires() const {
    std::lock_guard<std::mutex> lock(m_);
    return {fires_.begin(), fires_.end()};
}

void Engine::record(const std::string& text) {
    fires_.push_back({clock::now(), text});
    while (fires_.size() > 50) fires_.pop_front();
}

void Engine::recordLatest(const std::string& key, const std::string& text) {
    if (!fires_.empty() && fires_.back().text.rfind(key, 0) == 0) {
        fires_.back() = {clock::now(), text};
        return;
    }
    record(text);
}

double Engine::axisPosition(const AxisConfig& c, double raw01) {
    const double span = c.inMax - c.inMin;
    double t = span > 1e-6 ? (raw01 - c.inMin) / span : raw01;
    t = std::clamp(t, 0.0, 1.0);
    return c.invert ? 1.0 - t : t;
}

double Engine::axisOutput(const AxisConfig& c, double t) {
    const double v = c.outMin + t * (c.outMax - c.outMin);
    return c.integer ? std::round(v) : v;
}

Engine::DetentResult Engine::detentOutput(const AxisDetents& d, const AxisCalibration& cal, double raw01, AxisZone previous, const GateState& gates) {
    // mirrored so that the cut-off is always the low end
    const bool reversed = cal.idle > cal.mil;
    auto n = [reversed](double x) { return reversed ? 1.0 - x : x; };
    const double r = n(raw01), cutoff = n(cal.cutoff), idle = n(cal.idle), mil = n(cal.mil), ab = n(cal.ab);
    const double cutLimit = (cutoff + idle) / 2, abLimit = (mil + ab) / 2, hysteresis = 0.01;
    // A gate with a button is decided by the button; one without by the position, with a little hysteresis.
    AxisZone zone = AxisZone::Dry;
    const bool inCutoff = cal.cutoffBtn >= 0 ? gates.cutoff : (previous == AxisZone::Cutoff ? r < cutLimit + hysteresis : r < cutLimit - hysteresis);
    const bool inAfterburner = cal.abBtn >= 0 ? gates.ab : (previous == AxisZone::Afterburner ? r > abLimit - hysteresis : r > abLimit + hysteresis);
    if (inCutoff) zone = AxisZone::Cutoff;
    else if (inAfterburner) zone = AxisZone::Afterburner;
    const double dryStart = cal.idleBtn >= 0 ? idle : idle + cal.snap;  // above this the output leaves outIdle
    double value;
    if (zone == AxisZone::Cutoff) value = d.outCutoff;
    else if (cal.idleBtn >= 0 && gates.idle) value = d.outIdle;
    else if (zone == AxisZone::Afterburner || (cal.milBtn >= 0 && gates.mil)) value = d.outMil;
    else if (r <= dryStart) value = d.outIdle;
    else if (r >= mil) value = d.outMil;
    else value = d.outIdle + (r - dryStart) / (mil - dryStart) * (d.outMil - d.outIdle);
    return {value, zone};
}

Engine::DetentResult Engine::detentOutput(const AxisDetents& d, const AxisCalibration& cal, double raw01, AxisZone previous) {
    return detentOutput(d, cal, raw01, previous, GateState{});
}

std::string Engine::formatValue(double v, bool integer) {
    char buf[64];
    if (integer) snprintf(buf, sizeof buf, "%lld", static_cast<long long>(std::llround(v)));
    else snprintf(buf, sizeof buf, "%.10g", v);  // enough digits for the value to reach the calculator code as it is (%g keeps six)
    return buf;
}

std::vector<Action> Engine::withValue(const std::vector<Action>& actions, double v, bool integer) {
    std::vector<Action> out = actions;
    const std::string text = formatValue(v, integer);
    for (auto& a : out) {
        switch (a.kind) {
            case ActionKind::SimEvent:
            case ActionKind::InputEvent:
            case ActionKind::LVarSet: a.value = v; break;
            case ActionKind::CalcCode: {
                size_t pos = 0;
                while ((pos = a.name.find("{v}", pos)) != std::string::npos) { a.name.replace(pos, 3, text); pos += text.size(); }
                break;
            }
            case ActionKind::HEvent:
            case ActionKind::InputEventToggle:
            case ActionKind::Delay:
            case ActionKind::LVarToggle: break;
        }
    }
    return out;
}

void Engine::sendAxisValue(const Binding& b, double out) {
    const std::string origin = b.label.empty() ? std::string("axis") : b.label;
    fire(withValue(b.onPress, out, b.axis.integer), origin);
    std::lock_guard<std::mutex> lock(m_);
    recordLatest(origin + " =", origin + " = " + formatValue(out, b.axis.integer));
}

void Engine::onAxis(const input::InputEvent& ev) {
    std::shared_ptr<const Profile> profile;
    std::shared_ptr<const std::vector<AxisCalibration>> calibrations;
    {
        std::lock_guard<std::mutex> lock(m_);
        lastInput_ = ev;
        axisRaw_[ev.id.device + "#" + std::to_string(ev.id.code)] = ev.axis;  // also while bindings are paused: a gate button needs it
        if (!enabled_) return;
        profile = profile_;
        calibrations = calibrations_;
    }
    if (!profile) return;
    const auto now = clock::now();
    for (size_t i = 0; i < profile->bindings.size(); ++i) {
        const Binding& b = profile->bindings[i];
        if (!b.enabled || !(b.input == ev.id)) continue;
        const AxisConfig& c = b.axis;
        const double t = axisPosition(c, ev.axis);
        if (c.mode == AxisMode::Value) {
            // a lever with gates needs its calibration; without one it is a plain linear axis
            const AxisCalibration* cal = nullptr;
            if (c.detents.enabled)
                for (const auto& k : *calibrations)
                    if (k.device == ev.id.device && k.code == ev.id.code && k.valid()) cal = &k;
            if (cal) {
                applyLever(b, i, *cal, ev.axis);
                continue;
            }
            const double out = axisOutput(c, t);
            bool sendNow = false;
            {
                std::lock_guard<std::mutex> lock(m_);
                AxisState& st = axisState_[i];
                const bool changed = !st.hasSent || (c.integer ? std::llround(out) != std::llround(st.lastSent)
                                                                : std::fabs(out - st.lastSent) > 1e-6 * (std::fabs(c.outMax - c.outMin) + 1));
                if (!changed) st.hasPending = false;
                else if (st.hasSent && now - st.lastTime < std::chrono::milliseconds(c.minIntervalMs)) {
                    st.pending = out;
                    st.hasPending = true;  // tick() sends the latest value once the interval has passed
                } else {
                    st.hasSent = true;
                    st.lastSent = out;
                    st.lastTime = now;
                    st.hasPending = false;
                    sendNow = true;
                }
            }
            if (sendNow) sendAxisValue(b, out);
        } else {
            int steps = 0, dir = 0;
            const double step = std::max(0.005, c.stepPercent / 100.0);
            {
                std::lock_guard<std::mutex> lock(m_);
                AxisState& st = axisState_[i];
                if (!st.anchorValid) { st.anchor = axisPosition(c, ev.axisPrev); st.anchorValid = true; }
                const double delta = t - st.anchor;
                const int n = static_cast<int>(std::fabs(delta) / step);
                if (n < 1) continue;
                dir = delta > 0 ? 1 : -1;
                steps = std::min(n, 10);
                st.anchor = n > 10 ? t : st.anchor + dir * n * step;  // a huge jump resynchronises instead of replaying
            }
            for (int k = 0; k < steps; ++k) dispatch(b, dir > 0, dir > 0 ? " (+)" : " (-)");
        }
    }
}

void Engine::fire(const std::vector<Action>& actions, const std::string& origin) { runActions(actions, 0, origin, -1, 0); }

bool Engine::hasScheduled() const {
    std::lock_guard<std::mutex> lock(m_);
    return !scheduled_.empty();
}

void Engine::setActionObserver(std::function<void(const Action&, const std::string&)> observer) {
    std::lock_guard<std::mutex> lock(m_);
    observer_ = std::move(observer);
}

void Engine::runActions(const std::vector<Action>& actions, size_t from, const std::string& origin, int binding, uint64_t generation) {
    std::function<void(const Action&, const std::string&)> observer;
    {
        std::lock_guard<std::mutex> lock(m_);
        observer = observer_;
    }
    for (size_t k = from; k < actions.size(); ++k) {
        const Action& a = actions[k];
        if (observer && a.kind != ActionKind::Delay) observer(a, origin);
        switch (a.kind) {
            case ActionKind::SimEvent: bridge_.sendEvent(a.name, a.value, origin); break;
            case ActionKind::HEvent: bridge_.sendHEvent(a.name, origin); break;
            case ActionKind::LVarSet: bridge_.setLVar(a.name, a.value, origin); break;
            case ActionKind::LVarToggle: bridge_.toggleLVar(a.name, origin); break;
            case ActionKind::InputEvent: bridge_.setInputEvent(a.name, a.value, origin); break;
            case ActionKind::InputEventToggle: bridge_.toggleInputEvent(a.name, a.value >= 2 ? static_cast<int>(a.value) : 2, origin); break;
            case ActionKind::CalcCode: bridge_.execCalculatorCode(a.name, origin); break;
            case ActionKind::Delay: {
                // the rest of the list runs later, from tick(); nothing here ever sleeps
                Scheduled item;
                item.due = clock::now() + std::chrono::milliseconds(static_cast<long long>(std::clamp(a.value, 0.0, 10000.0)));
                item.rest.assign(actions.begin() + static_cast<std::ptrdiff_t>(k) + 1, actions.end());
                item.origin = origin;
                item.binding = binding;
                item.generation = generation;
                if (item.rest.empty()) return;
                std::lock_guard<std::mutex> lock(m_);
                scheduled_.push_back(std::move(item));
                return;
            }
        }
    }
}

int Engine::nextScheduledMs() const {
    const auto now = clock::now();
    int wait = -1;
    for (const auto& s : scheduled_) {
        const int ms = std::max<int>(1, static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(s.due - now).count()) + 1);
        if (wait < 0 || ms < wait) wait = ms;
    }
    return wait;
}

Engine::Scheduled Engine::idleAgain(const Binding& b, size_t index, double value, const AxisState& st) const {
    // The aircraft stops a throttle that is set to 0 from above at its idle gate (10 %); it takes a second 0 to reach 0, which is the
    // sim's own idle. Sent once more shortly after, unless the lever has moved on by then (a newer value or another zone).
    Scheduled again;
    again.due = clock::now() + std::chrono::milliseconds(100);
    again.rest = withValue(b.onPress, value, b.axis.integer);
    again.origin = (b.label.empty() ? std::string("axis") : b.label) + " (idle again)";
    again.binding = static_cast<int>(index);
    again.useSerial = true;
    again.serial = st.sendSerial;
    again.generation = st.generation;
    return again;
}

std::vector<Engine::Scheduled> Engine::takeDue(int& waitMs) {
    std::vector<Scheduled> ready;
    const auto now = clock::now();
    for (auto it = scheduled_.begin(); it != scheduled_.end();) {
        // what belongs to a lever is dropped when the bindings are paused (the calibration window, the "Bindings active" box): the lever is not to be
        // heard from until they are on again. A Test button (binding -1) carries on.
        const bool stale = it->binding >= 0 && (!enabled_ || axisState_.find(static_cast<size_t>(it->binding)) == axisState_.end() ||
                                                axisState_[static_cast<size_t>(it->binding)].generation != it->generation ||
                                                (it->useSerial && axisState_[static_cast<size_t>(it->binding)].sendSerial != it->serial));
        if (stale) { it = scheduled_.erase(it); continue; }
        if (it->due <= now) {
            ready.push_back(std::move(*it));
            it = scheduled_.erase(it);
            continue;
        }
        const int ms = std::max<int>(1, static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(it->due - now).count()) + 1);
        if (waitMs < 0 || ms < waitMs) waitMs = ms;
        ++it;
    }
    return ready;
}

void Engine::dispatch(const Binding& b, bool press, const char* why) {
    const auto& actions = press ? b.onPress : b.onRelease;
    if (actions.empty()) return;
    std::string origin = b.label.empty() ? std::string("binding") : b.label;
    origin += why;
    fire(actions, origin);
    std::string text = origin + ": ";
    for (size_t i = 0; i < actions.size(); ++i) text += (i ? ", " : "") + actions[i].describe();
    std::lock_guard<std::mutex> lock(m_);
    record(text);
}

// The value and the gate actions of a lever with gates, for a new position or a gate button that closed or opened.
void Engine::applyLever(const Binding& b, size_t i, const AxisCalibration& cal, double raw) {
    const AxisConfig& c = b.axis;
    const GateState gates = gatesOf(cal);
    AxisZone zoneBefore = AxisZone::Dry;
    bool zoneKnown = false;
    {
        std::lock_guard<std::mutex> lock(m_);
        const AxisState& st = axisState_[i];
        zoneKnown = st.zoneKnown;
        zoneBefore = st.zone;
    }
    const DetentResult r = detentOutput(c.detents, cal, raw, zoneBefore, gates);
    const double out = c.integer ? std::round(r.value) : r.value;
    const bool zoneChanged = !zoneKnown || r.zone != zoneBefore;
    if (zoneChanged) {
        {
            std::lock_guard<std::mutex> lock(m_);
            axisState_[i].zone = r.zone;
            axisState_[i].zoneKnown = true;
            ++axisState_[i].generation;  // whatever an earlier zone still held back is dropped
        }
        // leaving a gate zone first, then the value, then entering the next one
        if (zoneKnown && zoneBefore == AxisZone::Cutoff) dispatchZone(b, i, b.onCutoffLeave, " (leaves the cut-off)");
        if (zoneKnown && zoneBefore == AxisZone::Afterburner) dispatchZone(b, i, b.onAfterburnerLeave, " (leaves the afterburner)");
    }
    const auto now = clock::now();
    bool sendNow = false;
    bool landedOnIdle = false;
    uint64_t serial = 0;
    {
        std::lock_guard<std::mutex> lock(m_);
        AxisState& st = axisState_[i];
        const double scale = std::fabs(c.detents.outMil - c.detents.outCutoff);
        // a change of zone sends the value again even when it is the same: the simulator may have moved the lever itself (an afterburner
        // that sets its own throttle, a cut-off that stops at a gate), and the gate actions expect the throttle where the zone puts it
        const bool changed = zoneChanged || !st.hasSent || (c.integer ? std::llround(out) != std::llround(st.lastSent) : std::fabs(out - st.lastSent) > 1e-6 * (scale + 1));
        if (!changed) st.hasPending = false;
        else if (st.hasSent && !zoneChanged && now - st.lastTime < std::chrono::milliseconds(c.minIntervalMs)) {
            st.pending = out;
            st.hasPending = true;  // tick() sends the latest value once the interval has passed
        } else {
            // the lever has come down onto idle from higher up (not just changed zone): see below
            landedOnIdle = st.hasSent && r.zone == AxisZone::Dry && !zoneChanged && st.lastSent > out + 1e-9 && std::fabs(out - c.detents.outIdle) < 1e-9;
            st.hasSent = true;  // a change of zone is never held back: the gate and its value go together
            st.lastSent = out;
            st.lastTime = now;
            st.hasPending = false;
            serial = ++st.sendSerial;
            sendNow = true;
        }
    }
    if (sendNow) sendAxisValue(b, out);
    if (landedOnIdle) {
        std::lock_guard<std::mutex> lock(m_);
        Scheduled again = idleAgain(b, i, out, axisState_[i]);
        again.serial = serial;  // the value that was just sent, not one sent since
        scheduled_.push_back(std::move(again));
    }
    if (zoneChanged) {
        if (r.zone == AxisZone::Cutoff) dispatchZone(b, i, b.onCutoff, " (cut-off)");
        if (r.zone == AxisZone::Afterburner) dispatchZone(b, i, b.onAfterburner, " (afterburner)");
    }
}

void Engine::dispatchZone(const Binding& b, size_t index, const std::vector<Action>& actions, const char* what) {
    std::string origin = b.label.empty() ? std::string("axis") : b.label;
    origin += what;
    std::string text = origin + ": ";
    for (size_t i = 0; i < actions.size(); ++i) text += (i ? ", " : "") + actions[i].describe();
    if (actions.empty()) text += "(no actions)";
    else {
        uint64_t generation;
        {
            std::lock_guard<std::mutex> lock(m_);
            generation = axisState_[index].generation;
        }
        runActions(actions, 0, origin, static_cast<int>(index), generation);
    }
    std::lock_guard<std::mutex> lock(m_);
    record(text);
}

void Engine::onInput(const input::InputEvent& ev) {
    if (ev.id.kind == input::InputKind::Axis) {
        bool capturing;
        {
            std::lock_guard<std::mutex> lock(m_);
            capturing = capturing_;
            if (capturing) axisRaw_[ev.id.device + "#" + std::to_string(ev.id.code)] = ev.axis;  // onAxis records it otherwise
        }
        if (!capturing) {
            onAxis(ev);
            return;
        }
    }
    if (ev.id.kind == input::InputKind::Button) {  // the state of every button, before anything below can return
        std::lock_guard<std::mutex> lock(m_);
        buttonState_[ev.id.device + "#" + std::to_string(ev.id.code)] = ev.pressed;
        if (learn_.active && learn_.device == ev.id.device) {
            // a calibration is open: where was the lever when a gate button closed (or, for idle, opened)?
            const auto at = axisRaw_.find(ev.id.device + "#" + std::to_string(learn_.axisCode));
            for (int g = 0; g < 4 && at != axisRaw_.end(); ++g)
                if (learn_.button[g] == ev.id.code && ev.pressed == (g != 1)) learn_.raw[static_cast<size_t>(g)] = at->second;
        }
    }
    std::shared_ptr<const Profile> profile;
    std::shared_ptr<const std::vector<AxisCalibration>> calibrations;
    {
        std::lock_guard<std::mutex> lock(m_);
        if (capturing_) {
            lastInput_ = ev;
            if (captureButtonsOnly_ && ev.id.kind != input::InputKind::Button) return;
            if (ev.id.kind == input::InputKind::Axis) {
                // An axis counts when it is moved a good part of its travel from where it rested, so a noisy or
                // slowly drifting axis is never picked up by accident.
                const std::string key = ev.id.device + "#" + std::to_string(ev.id.code);
                auto it = captureBase_.find(key);
                if (it == captureBase_.end()) it = captureBase_.emplace(key, ev.axisPrev).first;
                if (std::fabs(ev.axis - it->second) >= 0.20f) {
                    captured_ = ev.id;
                    capturing_ = false;
                }
            } else if (ev.pressed) {
                captured_ = ev.id;
                capturing_ = false;
            }
            return;  // never fire bindings while learning a new input
        }
        lastInput_ = ev;
        if (!enabled_) return;
        profile = profile_;
        calibrations = calibrations_;
        if (!ev.pressed) {
            held_.erase(std::remove_if(held_.begin(), held_.end(), [&](const Held& h) { return h.id == ev.id; }), held_.end());
        }
    }
    if (!profile) return;
    for (size_t i = 0; i < profile->bindings.size(); ++i) {
        const Binding& b = profile->bindings[i];
        if (!b.enabled || !(b.input == ev.id)) continue;
        dispatch(b, ev.pressed, ev.pressed ? "" : " (release)");
        if (ev.pressed && b.repeat) {
            std::lock_guard<std::mutex> lock(m_);
            held_.push_back({ev.id, i, ev.time + std::chrono::milliseconds(b.repeatDelayMs)});
        }
    }
    if (ev.id.kind != input::InputKind::Button) return;
    // a gate button of a lever with gates closed or opened: the lever is judged again, whether or not its axis moved
    for (size_t i = 0; i < profile->bindings.size(); ++i) {
        const Binding& b = profile->bindings[i];
        if (!b.enabled || b.input.kind != input::InputKind::Axis || b.axis.mode != AxisMode::Value || !b.axis.detents.enabled) continue;
        for (const auto& cal : *calibrations)
            if (cal.device == b.input.device && cal.code == b.input.code && cal.device == ev.id.device && cal.valid() &&
                (cal.cutoffBtn == ev.id.code || cal.idleBtn == ev.id.code || cal.milBtn == ev.id.code || cal.abBtn == ev.id.code))
                applyLever(b, i, cal, axisRawOf(cal));
    }
}

int Engine::tick() {
    std::vector<std::pair<std::shared_ptr<const Profile>, size_t>> due;
    std::vector<std::pair<std::shared_ptr<const Profile>, std::pair<size_t, double>>> axisDue;
    std::vector<Scheduled> ready;
    int wait = -1;
    auto soonest = [&wait](std::chrono::steady_clock::duration d) {
        const int ms = std::max<int>(1, static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(d).count()) + 1);
        if (wait < 0 || ms < wait) wait = ms;
    };
    {
        std::lock_guard<std::mutex> lock(m_);
        ready = takeDue(wait);  // the rest of an action list that was waiting for its Delay
        if (!enabled_)  // paused (the calibration window, the "Bindings active" box): a value held back for the interval is not sent after all
            for (auto& [idx, st] : axisState_) st.hasPending = false;
        if (profile_ && enabled_ && !(held_.empty() && axisState_.empty())) {
            const auto now = clock::now();
            // axis values held back by the minimum interval: send the latest one once it has passed
            for (auto& [idx, st] : axisState_) {
                if (!st.hasPending || idx >= profile_->bindings.size()) continue;
                const AxisConfig& c = profile_->bindings[idx].axis;
                const auto interval = std::chrono::milliseconds(c.minIntervalMs);
                if (now - st.lastTime < interval) { soonest(interval - (now - st.lastTime)); continue; }
                const Binding& lever = profile_->bindings[idx];
                const bool landedOnIdle = lever.axis.detents.enabled && st.hasSent && st.zone == AxisZone::Dry && st.lastSent > st.pending + 1e-9 &&
                                          std::fabs(st.pending - lever.axis.detents.outIdle) < 1e-9;
                st.hasSent = true;
                st.lastSent = st.pending;
                st.lastTime = now;
                st.hasPending = false;
                ++st.sendSerial;
                axisDue.push_back({profile_, {idx, st.pending}});
                if (landedOnIdle) scheduled_.push_back(idleAgain(lever, idx, st.pending, st));
            }
            for (auto& h : held_) {
                if (h.bindingIndex >= profile_->bindings.size()) continue;
                if (now < h.nextRepeat) { soonest(h.nextRepeat - now); continue; }
                const Binding& b = profile_->bindings[h.bindingIndex];
                const auto interval = std::chrono::milliseconds(b.repeatIntervalMs < 10 ? 10 : b.repeatIntervalMs);
                h.nextRepeat = now + interval;
                soonest(interval);
                due.emplace_back(profile_, h.bindingIndex);
            }
        }
    }
    for (const auto& item : ready) runActions(item.rest, 0, item.origin, item.binding, item.generation);
    for (const auto& [profile, idx] : due) dispatch(profile->bindings[idx], true, " (repeat)");
    for (const auto& [profile, item] : axisDue) sendAxisValue(profile->bindings[item.first], item.second);
    if (!ready.empty() || !axisDue.empty()) {
        // the tails that ran may have scheduled the next part of their list: the next tick is due when that is, not at the default poll
        std::lock_guard<std::mutex> lock(m_);
        const int next = nextScheduledMs();
        if (next >= 0 && (wait < 0 || next < wait)) wait = next;
    }
    return wait;
}

}  // namespace lr::app
