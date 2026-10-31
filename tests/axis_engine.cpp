// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kyr4l
// Axis binding logic: mapping, value substitution, rate limiting, step counting, capture, config round trip.
#include <chrono>
#include <cmath>
#include <fstream>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <thread>

#include "app/Engine.hpp"
#include "app/Paths.hpp"
#include "app/Profile.hpp"
#include "sim/SimBridge.hpp"

using namespace lr;
using namespace lr::app;
using lr::input::InputEvent;
using lr::input::InputId;
using lr::input::InputKind;

static int failures = 0;
#define CHECK(cond)                                                                         \
    do {                                                                                    \
        if (!(cond)) { std::printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); ++failures; } \
    } while (0)

static InputEvent axisEvent(const char* dev, int code, float prev, float now) {
    InputEvent e;
    e.id = {dev, InputKind::Axis, code, 0};
    e.axis = now;
    e.axisPrev = prev;
    e.time = std::chrono::steady_clock::now();
    return e;
}

static size_t firesNamed(Engine& eng, const std::string& needle) {
    size_t n = 0;
    for (const auto& f : eng.recentFires())
        if (f.text.find(needle) != std::string::npos) ++n;
    return n;
}

static std::string lastFire(Engine& eng) {
    auto f = eng.recentFires();
    return f.empty() ? std::string() : f.back().text;
}

int main() {
    // ---- pure helpers
    {
        AxisConfig c;  // value mode 0..100, integer
        CHECK(Engine::axisPosition(c, 0.25) == 0.25);
        CHECK(Engine::axisOutput(c, 0.256) == 26.0);
        c.invert = true;
        CHECK(Engine::axisPosition(c, 0.25) == 0.75);
        c.invert = false;
        c.inMin = 0.1;
        c.inMax = 0.9;  // 10% dead zone at each end
        CHECK(Engine::axisPosition(c, 0.05) == 0.0);
        CHECK(Engine::axisPosition(c, 0.95) == 1.0);
        CHECK(std::fabs(Engine::axisPosition(c, 0.5) - 0.5) < 1e-9);
        c.outMin = 100;
        c.outMax = 0;  // reversed output range
        CHECK(Engine::axisOutput(c, 0.25) == 75.0);
        CHECK(Engine::formatValue(12.0, true) == "12");
        CHECK(Engine::formatValue(0.5, false) == "0.5");
    }
    {
        std::vector<Action> acts(5);
        acts[0].kind = ActionKind::SimEvent; acts[0].name = "X";
        acts[1].kind = ActionKind::LVarSet; acts[1].name = "Y";
        acts[2].kind = ActionKind::CalcCode; acts[2].name = "{v} (>L:Z) {v} (>L:W)";
        acts[3].kind = ActionKind::HEvent; acts[3].name = "H";
        acts[4].kind = ActionKind::LVarToggle; acts[4].name = "T"; acts[4].value = 7;
        auto out = Engine::withValue(acts, 42.0, true);
        CHECK(out[0].value == 42.0 && out[1].value == 42.0);
        CHECK(out[2].name == "42 (>L:Z) 42 (>L:W)");
        CHECK(out[3].name == "H" && out[4].value == 7);
        CHECK(acts[2].name == "{v} (>L:Z) {v} (>L:W)");  // the original is untouched
    }

    // ---- axis event ranges (the 0..100 default on a -16384..16383 event is what left the throttle levers motionless)
    {
        double lo = 0, hi = 0;
        CHECK(axisEventRange("THROTTLE1_AXIS_SET_EX1", lo, hi) && lo == -16384 && hi == 16383);
        CHECK(axisEventRange("K:THROTTLE4_AXIS_SET_EX1", lo, hi));
        CHECK(axisEventRange("AXIS_LEFT_BRAKE_SET", lo, hi) && lo == -16383 && hi == 16383);
        CHECK(!axisEventRange("GEAR_UP", lo, hi));
        CHECK(!axisEventRange("A32NX.FCU_HDG_SET", lo, hi));
        AxisConfig c;  // default 0..100
        CHECK(axisRangeLooksWrong(c, -16384, 16383));
        CHECK(!axisRangeLooksWrong(c, 100, 49000 - 48000));  // span below 1000: not an axis event
        c.outMin = -16384;
        c.outMax = 16383;
        CHECK(!axisRangeLooksWrong(c, -16384, 16383));
        c.mode = AxisMode::Steps;
        c.outMin = 0;
        c.outMax = 100;
        CHECK(!axisRangeLooksWrong(c, -16384, 16383));  // steps mode has no output range
    }

    // ---- the panel "photos >" link only ever opens the FlyByWire docs site and never goes through a shell
    {
        CHECK(!openDocsUrl("https://example.com/"));
        CHECK(!openDocsUrl("http://docs.flybywiresim.com/pilots-corner/"));
        CHECK(!openDocsUrl("https://docs.flybywiresim.com.evil.example/x"));
        CHECK(!openDocsUrl("https://docs.flybywiresim.com/a;touch /tmp/pwned"));
        CHECK(!openDocsUrl("https://docs.flybywiresim.com/a$(id)"));
        CHECK(!openDocsUrl("https://docs.flybywiresim.com/a b"));
        CHECK(!openDocsUrl("https://docs.flybywiresim.com/a?x=1"));
        CHECK(!openDocsUrl(""));
    }

    // ---- engine behaviour
    sim::SimBridge bridge;  // never started: commands are queued/dropped, only the engine's decisions are checked
    Engine eng(bridge);
    const char* dev = "4098:b970:test";

    auto prof = std::make_shared<Profile>();
    {
        Binding v;  // value mode
        v.label = "Brightness";
        v.input = {dev, InputKind::Axis, 3, 0};
        v.axis.mode = AxisMode::Value;
        v.axis.minIntervalMs = 40;
        Action a;
        a.kind = ActionKind::LVarSet;
        a.name = "BRT";
        v.onPress = {a};
        prof->bindings.push_back(v);

        Binding s;  // steps mode
        s.label = "Knob";
        s.input = {dev, InputKind::Axis, 4, 0};
        s.axis.mode = AxisMode::Steps;
        s.axis.stepPercent = 10;
        Action up, down;
        up.kind = ActionKind::SimEvent; up.name = "INC";
        down.kind = ActionKind::SimEvent; down.name = "DEC";
        s.onPress = {up};
        s.onRelease = {down};
        prof->bindings.push_back(s);
    }
    eng.setProfile(prof);

    // value: first move sends at once with the mapped value
    eng.onInput(axisEvent(dev, 3, 0.0f, 0.50f));
    CHECK(firesNamed(eng, "Brightness = 50") == 1);
    // quick follow-ups are held back, only the latest is sent after the interval (and replaces the entry)
    eng.onInput(axisEvent(dev, 3, 0.50f, 0.60f));
    eng.onInput(axisEvent(dev, 3, 0.60f, 0.70f));
    CHECK(lastFire(eng).find("= 50") != std::string::npos);
    // the input thread sleeps for what tick() reports: a held-back value is due within the 40 ms interval, nothing waits afterwards
    {
        const int wait = eng.tick();
        CHECK(wait >= 1 && wait <= 41);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(60));
    CHECK(eng.tick() == -1);                                   // the value went out and nothing else is waiting
    CHECK(lastFire(eng).find("Brightness = 70") != std::string::npos);
    // a change too small to change the rounded output sends nothing
    const size_t before = eng.recentFires().size();
    std::this_thread::sleep_for(std::chrono::milliseconds(60));
    eng.onInput(axisEvent(dev, 3, 0.70f, 0.701f));
    eng.tick();
    CHECK(eng.recentFires().size() == before);

    // steps: 10% per step
    eng.onInput(axisEvent(dev, 4, 0.50f, 0.55f));
    CHECK(firesNamed(eng, "Knob (+)") == 0);            // 5% < one step
    eng.onInput(axisEvent(dev, 4, 0.55f, 0.72f));
    CHECK(firesNamed(eng, "Knob (+)") == 2);            // 22% travelled from the anchor = two steps
    // the anchor moved to 0.70 after the two steps up; 0.70 -> 0.51 is 1.9 steps, which counts as one (no rounding up)
    eng.onInput(axisEvent(dev, 4, 0.72f, 0.51f));
    CHECK(firesNamed(eng, "Knob (-)") == 1);
    eng.onInput(axisEvent(dev, 4, 0.51f, 0.39f));       // anchor 0.60 -> 0.39 = 2.1 steps = two more
    CHECK(firesNamed(eng, "Knob (-)") == 3);
    eng.onInput(axisEvent(dev, 4, 0.39f, 0.99f));       // anchor 0.40 -> 0.99 = 5.9 steps = five
    CHECK(firesNamed(eng, "Knob (+)") == 2 + 5);
    eng.onInput(axisEvent(dev, 4, 0.99f, 0.0f));        // a huge sweep sends at most ten steps, then resynchronises
    CHECK(firesNamed(eng, "Knob (-)") <= 3 + 10);
    CHECK(firesNamed(eng, "Knob (-)") >= 3 + 8);  // 0.9 of travel is 8-9 steps depending on float rounding

    // a different axis / device does not trigger anything
    const size_t n = eng.recentFires().size();
    eng.onInput(axisEvent(dev, 5, 0.0f, 1.0f));
    eng.onInput(axisEvent("other", 3, 0.0f, 1.0f));
    CHECK(eng.recentFires().size() == n);

    // capture: small drift is ignored, a real move is captured
    eng.beginCapture();
    eng.onInput(axisEvent(dev, 6, 0.50f, 0.54f));
    CHECK(eng.capturing());
    eng.onInput(axisEvent(dev, 6, 0.54f, 0.80f));
    CHECK(!eng.capturing());
    auto got = eng.takeCaptured();
    CHECK(got && got->kind == InputKind::Axis && got->code == 6);
    // capturing never fires bindings
    eng.beginCapture();
    const size_t m = eng.recentFires().size();
    eng.onInput(axisEvent(dev, 3, 0.0f, 1.0f));
    CHECK(eng.recentFires().size() == m);
    eng.cancelCapture();

    // ---- config round trip with an axis binding; plain bindings stay unchanged on disk
    {
        Settings st;
        st.profiles.push_back(*prof);
        st.activeProfile = prof->name;
        Binding btn;
        btn.label = "plain";
        btn.input = {dev, InputKind::Button, 288, 0};
        st.profiles[0].bindings.push_back(btn);
        st.profiles[0].bindings[0].axis.invert = true;
        st.profiles[0].bindings[0].axis.outMax = 255;
        st.profiles[0].bindings[1].axis.stepPercent = 7.5;
        const auto path = (std::filesystem::temp_directory_path() / "msfs-linux-companion-axis-test.json").string();
        std::string err;
        CHECK(saveSettings(path, st, err));
        Settings back;
        CHECK(loadSettings(path, back, err));
        const auto& b = back.profiles[0].bindings;
        CHECK(b.size() == 3);
        CHECK(b[0].input.kind == InputKind::Axis && b[0].axis.invert && b[0].axis.outMax == 255 && b[0].axis.mode == AxisMode::Value);
        CHECK(b[1].axis.mode == AxisMode::Steps && b[1].axis.stepPercent == 7.5 && b[1].onRelease.size() == 1);
        CHECK(b[2].input.kind == InputKind::Button);
        std::ifstream in(path);
        std::string text((std::istreambuf_iterator<char>(in)), {});
        // exactly two "axis" objects are written (the two axis bindings), none for the plain button
        size_t count = 0;
        for (size_t pos = 0; (pos = text.find("\"step_percent\"", pos)) != std::string::npos; ++pos) ++count;
        CHECK(count == 2);
        std::filesystem::remove(path);
    }

    // ---- a lever with gates (the URSA MINOR throttle): cut-off, idle detent, end of the dry range, afterburner gate
    {
        AxisCalibration cal;
        cal.device = dev;
        cal.code = 0;
        cal.cutoff = 0.0; cal.idle = 0.12; cal.mil = 0.80; cal.ab = 0.95; cal.snap = 0.015;
        CHECK(cal.valid());
        AxisDetents d;
        d.enabled = true; d.outCutoff = 0.0; d.outIdle = 0.1; d.outMil = 0.8;
        auto at = [&](double raw, AxisZone prev) { return Engine::detentOutput(d, cal, raw, prev); };

        CHECK(at(0.0, AxisZone::Dry).zone == AxisZone::Cutoff && at(0.0, AxisZone::Dry).value == 0.0);
        CHECK(at(0.04, AxisZone::Dry).zone == AxisZone::Cutoff);
        // the boundary between cut-off and idle is at 0.06; 0.01 either way keeps the zone the lever came from
        CHECK(at(0.065, AxisZone::Dry).zone == AxisZone::Dry && at(0.065, AxisZone::Cutoff).zone == AxisZone::Cutoff);
        CHECK(at(0.12, AxisZone::Dry).zone == AxisZone::Dry && at(0.12, AxisZone::Dry).value == 0.1);
        CHECK(at(0.134, AxisZone::Dry).value == 0.1);  // inside the idle band
        CHECK(std::fabs(at(0.5, AxisZone::Dry).value - (0.1 + (0.5 - 0.135) / (0.8 - 0.135) * 0.7)) < 1e-9);
        CHECK(std::fabs(at(0.8, AxisZone::Dry).value - 0.8) < 1e-9 && at(0.8, AxisZone::Dry).zone == AxisZone::Dry);
        // the boundary dry range / afterburner is at 0.875
        CHECK(at(0.88, AxisZone::Dry).zone == AxisZone::Dry && at(0.88, AxisZone::Afterburner).zone == AxisZone::Afterburner);
        CHECK(at(0.9, AxisZone::Dry).zone == AxisZone::Afterburner && at(0.9, AxisZone::Dry).value == 0.8);
        CHECK(at(1.0, AxisZone::Dry).zone == AxisZone::Afterburner);
        {   // sweeping the lever up never makes the output go down
            AxisZone z = AxisZone::Dry;
            double last = -1;
            bool monotonic = true;
            for (int i = 0; i <= 1000; ++i) {
                const auto r = at(i / 1000.0, z);
                z = r.zone;
                monotonic = monotonic && r.value >= last - 1e-12;
                last = r.value;
            }
            CHECK(monotonic);
        }
        {   // a lever mounted the other way round (reads 1 at the cut-off) is mirrored
            AxisCalibration rev = cal;
            rev.cutoff = 1.0; rev.idle = 0.88; rev.mil = 0.20; rev.ab = 0.05;
            CHECK(rev.valid());
            CHECK(Engine::detentOutput(d, rev, 1.0, AxisZone::Dry).zone == AxisZone::Cutoff);
            CHECK(Engine::detentOutput(d, rev, 0.0, AxisZone::Dry).zone == AxisZone::Afterburner);
            CHECK(std::fabs(Engine::detentOutput(d, rev, 0.5, AxisZone::Dry).value - at(0.5, AxisZone::Dry).value) < 1e-9);
        }
        {   // calibrations that cannot work are refused
            AxisCalibration bad = cal;
            bad.idle = bad.cutoff;               CHECK(!bad.valid());
            bad = cal; bad.mil = bad.idle + 0.05; CHECK(!bad.valid());
            bad = cal; bad.ab = bad.mil;          CHECK(!bad.valid());
            bad = cal; bad.snap = 0.5;            CHECK(!bad.valid());
            bad = cal; bad.idle = 0.9;            CHECK(!bad.valid());
        }

        // the engine: zone actions fire once per entry and per exit, the value follows
        Engine lever(bridge);
        auto lp = std::make_shared<Profile>();
        Binding t;
        t.label = "Left throttle";
        t.input = {dev, InputKind::Axis, 0, 0};
        t.axis.mode = AxisMode::Value;
        t.axis.integer = false;
        t.axis.minIntervalMs = 5;
        t.axis.detents = d;
        Action thr, cut, cutBack, burn, burnOff;
        thr.kind = ActionKind::InputEvent; thr.name = "ENGINE_Throttle_1";
        cut.kind = ActionKind::InputEvent; cut.name = "ENG_CUT"; cut.value = 1;
        cutBack.kind = ActionKind::InputEvent; cutBack.name = "ENG_CUT"; cutBack.value = 0;
        burn.kind = ActionKind::InputEvent; burn.name = "ENGINE_Throttle_1_Afterburner"; burn.value = 1;
        burnOff.kind = ActionKind::InputEvent; burnOff.name = "ENGINE_Throttle_1_Afterburner"; burnOff.value = 0;
        t.onPress = {thr};
        t.onCutoff = {cut}; t.onCutoffLeave = {cutBack}; t.onAfterburner = {burn}; t.onAfterburnerLeave = {burnOff};
        lp->bindings.push_back(t);
        lever.setProfile(lp);
        lever.setCalibrations({cal});
        auto pause = [] { std::this_thread::sleep_for(std::chrono::milliseconds(12)); };

        lever.onInput(axisEvent(dev, 0, 0.5f, 0.0f));  // slammed to the cut-off stop
        CHECK(firesNamed(lever, "Left throttle (cut-off): ") == 1 && firesNamed(lever, "ENG_CUT") == 1);
        CHECK(firesNamed(lever, "Left throttle = 0") == 1);
        pause();
        lever.onInput(axisEvent(dev, 0, 0.0f, 0.02f));  // still in the cut-off zone: nothing new
        CHECK(firesNamed(lever, "(cut-off)") == 1 && firesNamed(lever, "(leaves the cut-off)") == 0);
        pause();
        lever.onInput(axisEvent(dev, 0, 0.02f, 0.12f));  // over the idle gate
        CHECK(firesNamed(lever, "(leaves the cut-off)") == 1 && firesNamed(lever, "(cut-off)") == 1);
        CHECK(firesNamed(lever, "Left throttle = 0.1") == 1);
        pause();
        lever.onInput(axisEvent(dev, 0, 0.12f, 0.50f));
        CHECK(firesNamed(lever, "(afterburner)") == 0);
        pause();
        lever.onInput(axisEvent(dev, 0, 0.50f, 0.97f));  // through the afterburner gate
        CHECK(firesNamed(lever, "Left throttle (afterburner): ") == 1 && firesNamed(lever, "ENGINE_Throttle_1_Afterburner") == 1);
        CHECK(firesNamed(lever, "Left throttle = 0.8") == 1);
        pause();
        lever.onInput(axisEvent(dev, 0, 0.97f, 0.99f));  // inside the zone: nothing new
        CHECK(firesNamed(lever, "(afterburner)") == 1);
        pause();
        lever.onInput(axisEvent(dev, 0, 0.99f, 0.40f));  // pulled back out
        CHECK(firesNamed(lever, "(leaves the afterburner)") == 1);
        pause();
        lever.onInput(axisEvent(dev, 0, 0.40f, 0.95f));  // and in again
        CHECK(firesNamed(lever, "(afterburner)") == 2);

        // calibrations pushed to the engine are used; a lever without one is a plain linear axis (0 to 100 here)
        Engine uncal(bridge);
        uncal.setProfile(lp);
        uncal.onInput(axisEvent(dev, 0, 0.0f, 0.5f));
        CHECK(firesNamed(uncal, "(cut-off)") == 0 && firesNamed(uncal, "Left throttle = 50") == 1);
        uncal.setCalibrations({cal});
        pause();
        uncal.onInput(axisEvent(dev, 0, 0.5f, 0.0f));
        CHECK(firesNamed(uncal, "(cut-off)") == 1);
        AxisCalibration broken = cal;
        broken.idle = broken.cutoff;
        Engine nobody(bridge);
        nobody.setProfile(lp);
        nobody.setCalibrations({broken});
        nobody.onInput(axisEvent(dev, 0, 0.5f, 0.0f));
        CHECK(firesNamed(nobody, "(cut-off)") == 0);  // an unusable calibration is ignored, not guessed at

        // settings: calibrations and the lever's gates round-trip; a plain axis writes nothing new
        Settings st;
        st.profiles.push_back(*lp);
        st.activeProfile = st.profiles[0].name;
        st.setCalibration(cal);
        AxisCalibration other = cal;
        other.code = 1; other.idle = 0.2;
        st.setCalibration(other);
        st.setCalibration(cal);  // same axis again: replaced, not added
        CHECK(st.calibrations.size() == 2 && st.calibrationFor({dev, InputKind::Axis, 1, 0}) && st.calibrationFor({dev, InputKind::Axis, 1, 0})->idle == 0.2);
        const auto path = (std::filesystem::temp_directory_path() / "msfs-linux-companion-gates-test.json").string();
        std::string err;
        CHECK(saveSettings(path, st, err));
        Settings back;
        CHECK(loadSettings(path, back, err));
        CHECK(back.calibrations.size() == 2);
        const AxisCalibration* c0 = back.calibrationFor({dev, InputKind::Axis, 0, 0});
        CHECK(c0 && c0->cutoff == 0.0 && c0->idle == 0.12 && c0->mil == 0.80 && c0->ab == 0.95 && c0->snap == 0.015);
        const Binding& rb = back.profiles[0].bindings[0];
        CHECK(rb.axis.detents.enabled && rb.axis.detents.outIdle == 0.1 && rb.axis.detents.outMil == 0.8);
        CHECK(rb.onCutoff.size() == 1 && rb.onCutoff[0].name == "ENG_CUT" && rb.onCutoff[0].value == 1);
        CHECK(rb.onCutoffLeave.size() == 1 && rb.onCutoffLeave[0].value == 0);
        CHECK(rb.onAfterburner.size() == 1 && rb.onAfterburnerLeave.size() == 1 && rb.onAfterburnerLeave[0].name == "ENGINE_Throttle_1_Afterburner");
        st.clearCalibration({dev, InputKind::Axis, 1, 0});
        CHECK(st.calibrations.size() == 1 && !st.calibrationFor({dev, InputKind::Axis, 1, 0}));
        std::filesystem::remove(path);
    }

    std::printf(failures ? "\n%d check(s) FAILED\n" : "all axis checks passed\n", failures);
    return failures ? 1 : 0;
}
