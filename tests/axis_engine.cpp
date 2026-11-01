// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kyr4l
// Axis binding logic: mapping, value substitution, rate limiting, step counting, capture, config round trip.
#include <chrono>
#include <cmath>
#include <fstream>
#include <cstdio>
#include <filesystem>
#include <map>
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

static InputEvent buttonEvent(const char* dev, int code, bool pressed) {
    InputEvent e;
    e.id = {dev, InputKind::Button, code, 0};
    e.pressed = pressed;
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
        // a lever that stops a little short of the end of the device's range (the URSA MINOR reads 99.7 %) still sends the full value, and the
        // same at the other end; a little further in is left as it is
        AxisConfig full;
        full.outMin = 0;
        full.outMax = 16383;
        CHECK(Engine::axisOutput(full, Engine::axisPosition(full, 0.9967)) == 16383.0);
        CHECK(Engine::axisOutput(full, Engine::axisPosition(full, 0.004)) == 0.0);
        CHECK(Engine::axisPosition(full, 0.985) == 0.985 && Engine::axisPosition(full, 0.02) == 0.02);
        full.invert = true;
        CHECK(Engine::axisPosition(full, 0.9967) == 0.0 && Engine::axisPosition(full, 0.004) == 1.0);
        full.invert = false;
        full.inMin = 0.1;
        full.inMax = 0.9;  // the end is the end of the used travel
        CHECK(Engine::axisPosition(full, 0.895) == 1.0 && Engine::axisPosition(full, 0.104) == 0.0);
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
    eng.onInput(axisEvent(dev, 4, 0.39f, 0.985f));      // anchor 0.40 -> 0.985 = 5.85 steps = five (from 0.99 on, the position is the end: six)
    CHECK(firesNamed(eng, "Knob (+)") == 2 + 5);
    eng.onInput(axisEvent(dev, 4, 0.985f, 0.0f));       // a huge sweep sends at most ten steps, then resynchronises
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

    // ---- a lever with a switch at each gate (the URSA MINOR throttle): the buttons decide the zones, the axis only the part between
    {
        // the gates sit a centimetre apart: the positions of cut-off and idle overlap, which no zones from the position can separate
        AxisCalibration cal;
        cal.device = dev;
        cal.code = 3;
        cal.cutoff = 0.03; cal.idle = 0.03; cal.mil = 0.75; cal.ab = 0.90;
        cal.cutoffBtn = 300; cal.idleBtn = 301; cal.milBtn = 302; cal.abBtn = 303;
        CHECK(cal.valid());
        AxisCalibration noButton = cal;
        noButton.cutoffBtn = -1;
        CHECK(!noButton.valid());  // judged from the position, cut-off and idle at the same place cannot work
        noButton = cal;
        noButton.abBtn = -1;
        noButton.ab = noButton.mil;
        CHECK(!noButton.valid());
        AxisDetents d;
        d.enabled = true; d.outCutoff = 0.0; d.outIdle = 0.1; d.outMil = 0.8;
        using G = Engine::GateState;
        auto at = [&](double raw, G g, AxisZone prev = AxisZone::Dry) { return Engine::detentOutput(d, cal, raw, prev, g); };
        G none, cut, idle, mil, ab;
        cut.cutoff = true; idle.idle = true; mil.mil = true; ab.ab = true;

        CHECK(at(0.02, none).zone == AxisZone::Dry && at(0.02, none).value == 0.1);  // below the top of idle, no button: idle
        CHECK(at(0.03, cut).zone == AxisZone::Cutoff && at(0.03, cut).value == 0.0);  // the button, though the position is the idle one
        CHECK(at(0.50, cut).zone == AxisZone::Cutoff);                                // and wins over any position
        CHECK(at(0.04, idle).value == 0.1 && at(0.04, idle).zone == AxisZone::Dry);
        CHECK(std::fabs(at(0.40, none).value - (0.1 + (0.40 - 0.03) / (0.75 - 0.03) * 0.7)) < 1e-9);  // between the gates the axis decides
        CHECK(at(0.70, mil).value == 0.8 && at(0.70, mil).zone == AxisZone::Dry);    // the end switch is exact even before the position gets there
        CHECK(at(0.80, ab).zone == AxisZone::Afterburner && at(0.80, ab).value == 0.8);
        G both = cut; both.ab = true;
        CHECK(at(0.5, both).zone == AxisZone::Cutoff);
        CHECK(at(0.99, none).zone == AxisZone::Dry);  // a gate with a button ignores the position: no button, no afterburner

        // the engine: gate buttons move the zone without any axis event, and give the zone the lever starts in
        Engine lever(bridge);
        std::map<int, bool> pressed;
        float rawNow = -1;
        lever.setProbes([&](const std::string&, int code) { return pressed[code]; }, [&](const std::string&, int) { return rawNow; });
        auto lp = std::make_shared<Profile>();
        Binding t;
        t.label = "Right throttle";
        t.input = {dev, InputKind::Axis, 3, 0};
        t.axis.mode = AxisMode::Value; t.axis.integer = false; t.axis.minIntervalMs = 5; t.axis.detents = d;
        Action thr, cut1, cut0, burn1, burn0;
        thr.kind = ActionKind::InputEvent; thr.name = "ENGINE_Throttle_2";
        cut1.kind = ActionKind::InputEvent; cut1.name = "ENG_CUT"; cut1.value = 1;
        cut0 = cut1; cut0.value = 0;
        burn1.kind = ActionKind::InputEvent; burn1.name = "AB"; burn1.value = 1;
        burn0 = burn1; burn0.value = 0;
        t.onPress = {thr};
        t.onCutoff = {cut1}; t.onCutoffLeave = {cut0}; t.onAfterburner = {burn1}; t.onAfterburnerLeave = {burn0};
        lp->bindings.push_back(t);
        lever.setProfile(lp);
        lever.setCalibrations({cal});
        auto pause = [] { std::this_thread::sleep_for(std::chrono::milliseconds(12)); };

        lever.onInput(axisEvent(dev, 3, 0.0f, 0.40f));  // somewhere in the dry range
        CHECK(firesNamed(lever, "Right throttle = 0.4") == 1 && firesNamed(lever, "(cut-off)") == 0);
        pause();
        lever.onInput(axisEvent(dev, 3, 0.40f, 0.02f));  // pulled back: the position alone says idle, the cut-off switch has not closed
        CHECK(firesNamed(lever, "(cut-off)") == 0 && firesNamed(lever, "Right throttle = 0.1") == 1);
        pause();
        pressed[300] = true;
        lever.onInput(buttonEvent(dev, 300, true));  // over the gate: the switch closes, the axis does not move
        CHECK(firesNamed(lever, "Right throttle (cut-off): ") == 1 && firesNamed(lever, "Right throttle = 0") >= 1);
        pause();
        lever.onInput(axisEvent(dev, 3, 0.02f, 0.01f));  // still in the cut-off: nothing new
        CHECK(firesNamed(lever, "(cut-off)") == 1 && firesNamed(lever, "(leaves the cut-off)") == 0);
        pause();
        pressed[300] = false;
        lever.onInput(buttonEvent(dev, 300, false));
        CHECK(firesNamed(lever, "(leaves the cut-off)") == 1);
        pause();
        pressed[303] = true;
        lever.onInput(buttonEvent(dev, 303, true));  // the afterburner switch closes
        CHECK(firesNamed(lever, "Right throttle (afterburner): ") == 1 && firesNamed(lever, "Right throttle = 0.8") >= 1);
        pause();
        pressed[303] = false;
        lever.onInput(buttonEvent(dev, 303, false));
        CHECK(firesNamed(lever, "(leaves the afterburner)") == 1);
        pause();
        lever.onInput(buttonEvent(dev, 304, true));  // some other button of the device: nothing happens
        const size_t before = lever.recentFires().size();
        lever.onInput(buttonEvent(dev, 304, false));
        CHECK(lever.recentFires().size() == before);

        // started with the lever already in the cut-off gate: the first position seen finds it there
        Engine resting(bridge);
        std::map<int, bool> held{{300, true}};
        resting.setProbes([&](const std::string&, int code) { return held[code]; }, nullptr);
        resting.setProfile(lp);
        resting.setCalibrations({cal});
        resting.onInput(axisEvent(dev, 3, 0.01f, 0.02f));
        CHECK(firesNamed(resting, "(cut-off)") == 1);

        // a calibration open: the position at each gate switch is remembered, the idle one when it opens
        Engine learning(bridge);
        learning.setProfile(lp);
        learning.beginGateLearning(cal);
        learning.onInput(axisEvent(dev, 3, 0.0f, 0.01f));
        learning.onInput(buttonEvent(dev, 300, true));
        learning.onInput(buttonEvent(dev, 300, false));
        learning.onInput(axisEvent(dev, 3, 0.01f, 0.025f));
        learning.onInput(buttonEvent(dev, 301, true));    // idle closes: not what is wanted
        learning.onInput(axisEvent(dev, 3, 0.025f, 0.04f));
        learning.onInput(buttonEvent(dev, 301, false));   // idle opens: the lever leaves it here
        learning.onInput(axisEvent(dev, 3, 0.04f, 0.74f));
        learning.onInput(buttonEvent(dev, 302, true));
        learning.onInput(axisEvent(dev, 3, 0.74f, 0.88f));
        learning.onInput(buttonEvent(dev, 303, true));
        const auto got = learning.takeGateLearned();
        CHECK(got[0] && std::fabs(*got[0] - 0.01) < 1e-6 && got[1] && std::fabs(*got[1] - 0.04) < 1e-6 &&
              got[2] && std::fabs(*got[2] - 0.74) < 1e-6 && got[3] && std::fabs(*got[3] - 0.88) < 1e-6);
        const auto again = learning.takeGateLearned();
        CHECK(!again[0] && !again[1] && !again[2] && !again[3]);  // each reading is handed over once
        learning.onInput(buttonEvent(dev, 303, false));
        learning.onInput(buttonEvent(dev, 303, true));
        CHECK(learning.takeGateLearned()[3]);  // and the next pass is recorded afresh
        learning.endGateLearning();
        learning.onInput(buttonEvent(dev, 303, false));
        learning.onInput(buttonEvent(dev, 303, true));
        CHECK(!learning.takeGateLearned()[3]);  // nothing is recorded once the calibration is closed

        // listening for a gate button ignores the axis the lever moves
        Engine listening(bridge);
        listening.beginCapture(true);
        listening.onInput(axisEvent(dev, 3, 0.0f, 0.9f));
        CHECK(!listening.takeCaptured() && listening.capturing());
        listening.onInput(buttonEvent(dev, 301, true));
        const auto cap = listening.takeCaptured();
        CHECK(cap && cap->kind == InputKind::Button && cap->code == 301 && !listening.capturing());

        // saved with the config; an older calibration without buttons reads as before
        Settings st;
        st.setCalibration(cal);
        AxisCalibration plain;
        plain.device = dev; plain.code = 4;
        st.setCalibration(plain);
        const auto path = (std::filesystem::temp_directory_path() / "msfs-linux-companion-gatebtn-test.json").string();
        std::string err;
        CHECK(saveSettings(path, st, err));
        Settings back;
        CHECK(loadSettings(path, back, err));
        const AxisCalibration* c3 = back.calibrationFor({dev, InputKind::Axis, 3, 0});
        const AxisCalibration* c4 = back.calibrationFor({dev, InputKind::Axis, 4, 0});
        CHECK(c3 && c3->cutoffBtn == 300 && c3->idleBtn == 301 && c3->milBtn == 302 && c3->abBtn == 303 && c3->valid());
        CHECK(c4 && c4->cutoffBtn == -1 && c4->idleBtn == -1 && c4->milBtn == -1 && c4->abBtn == -1);
        std::filesystem::remove(path);
    }

    // ---- Delay: the rest of a list waits, and what a lever with gates held back is dropped when it changes zone
    {
        AxisCalibration cal;
        cal.device = dev; cal.code = 3;
        cal.cutoff = 0.03; cal.idle = 0.03; cal.mil = 0.75; cal.ab = 0.90;
        cal.cutoffBtn = 300; cal.idleBtn = 301; cal.milBtn = 302; cal.abBtn = 303;
        AxisDetents d;
        d.enabled = true; d.outCutoff = 0.0; d.outIdle = 0.1; d.outMil = 0.8;

        struct Seen { std::string name; double value; std::chrono::steady_clock::time_point at; };
        auto watch = [](Engine& e, std::vector<Seen>& log) {
            e.setActionObserver([&log](const Action& a, const std::string&) { log.push_back({a.name, a.value, std::chrono::steady_clock::now()}); });
        };
        auto count = [](const std::vector<Seen>& log, const std::string& name, double value) {
            size_t n = 0;
            for (const auto& x : log) if (x.name == name && std::fabs(x.value - value) < 1e-9) ++n;
            return n;
        };
        auto act = [](ActionKind k, const char* name, double v) { Action a; a.kind = k; a.name = name; a.value = v; return a; };
        auto wait = [](int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); };

        {   // a plain list: what follows the Delay comes later, from tick(), and nothing sleeps
            Engine e(bridge);
            std::vector<Seen> log;
            watch(e, log);
            const auto t0 = std::chrono::steady_clock::now();
            e.fire({act(ActionKind::InputEvent, "A", 1), act(ActionKind::Delay, "", 60), act(ActionKind::InputEvent, "B", 2), act(ActionKind::Delay, "", 30), act(ActionKind::InputEvent, "C", 3)}, "test");
            CHECK(std::chrono::steady_clock::now() - t0 < std::chrono::milliseconds(40));
            CHECK(log.size() == 1 && log[0].name == "A" && e.hasScheduled());
            CHECK(e.tick() > 0 && log.size() == 1);  // not due yet, and the engine says when to look again
            wait(80);
            CHECK(e.tick() > 0);  // B ran and scheduled the rest of its list: the next look is due when that is, not at the default poll
            CHECK(log.size() == 2 && log[1].name == "B" && e.hasScheduled());
            wait(50);
            e.tick();
            CHECK(log.size() == 3 && log[2].name == "C" && !e.hasScheduled() && e.tick() == -1);
            CHECK(log[1].at - log[0].at >= std::chrono::milliseconds(55));
            Action w = act(ActionKind::Delay, "", 120);
            CHECK(w.describe() == "wait 120 ms" && !actionNeedsMobiFlight(ActionKind::Delay));
            ActionKind k = ActionKind::SimEvent;
            CHECK(actionKindFromName(actionKindName(ActionKind::Delay), k) && k == ActionKind::Delay);
        }

        auto lever = [&](Engine& e, const std::vector<Action>& onCutoff, const std::vector<Action>& onAb, std::map<int, bool>& pressed) {
            auto lp = std::make_shared<Profile>();
            Binding t;
            t.label = "Left throttle";
            t.input = {dev, InputKind::Axis, 3, 0};
            t.axis.mode = AxisMode::Value; t.axis.integer = false; t.axis.minIntervalMs = 5; t.axis.detents = d;
            t.onPress = {act(ActionKind::InputEvent, "THR", 0)};
            t.onCutoff = onCutoff;
            t.onAfterburner = onAb;
            t.onAfterburnerLeave = {act(ActionKind::InputEvent, "AB", 0)};
            lp->bindings.push_back(t);
            e.setProbes([&pressed](const std::string&, int code) { return pressed[code]; }, [](const std::string&, int) { return -1.0f; });
            e.setProfile(lp);
            e.setCalibrations({cal});
        };
        const std::vector<Action> cutSeq = {act(ActionKind::Delay, "", 40), act(ActionKind::InputEvent, "THR", 0), act(ActionKind::Delay, "", 40), act(ActionKind::InputEvent, "STOP", 1)};
        const std::vector<Action> abSeq = {act(ActionKind::LVarSet, "LASTAB", 1), act(ActionKind::Delay, "", 40), act(ActionKind::InputEvent, "AB", 1)};

        {   // the lever stays in the cut-off gate: the whole sequence runs, in order
            Engine e(bridge);
            std::vector<Seen> log;
            std::map<int, bool> pressed;
            lever(e, cutSeq, abSeq, pressed);
            watch(e, log);
            e.onInput(axisEvent(dev, 3, 0.0f, 0.5f));
            wait(8);
            pressed[300] = true;
            e.onInput(buttonEvent(dev, 300, true));
            CHECK(count(log, "THR", 0) == 1 && count(log, "STOP", 1) == 0);  // the value went at once, the sequence waits
            wait(60); e.tick();
            CHECK(count(log, "THR", 0) == 2 && count(log, "STOP", 1) == 0);
            wait(60); e.tick();
            CHECK(count(log, "STOP", 1) == 1 && !e.hasScheduled());
        }
        {   // the lever leaves the gate before the sequence is done: the rest is dropped, STOP is never sent for a lever at idle
            Engine e(bridge);
            std::vector<Seen> log;
            std::map<int, bool> pressed;
            lever(e, cutSeq, abSeq, pressed);
            watch(e, log);
            e.onInput(axisEvent(dev, 3, 0.0f, 0.5f));
            wait(8);
            pressed[300] = true;
            e.onInput(buttonEvent(dev, 300, true));
            wait(60); e.tick();                      // the second throttle 0 is out, STOP waits
            pressed[300] = false;
            e.onInput(buttonEvent(dev, 300, false));  // the lever goes back up
            wait(60); e.tick();
            CHECK(count(log, "STOP", 1) == 0);
        }
        {   // afterburner: the value is sent again when the zone changes, the L: var goes before the event, and leaving sends the value back
            Engine e(bridge);
            std::vector<Seen> log;
            std::map<int, bool> pressed;
            lever(e, cutSeq, abSeq, pressed);
            watch(e, log);
            pressed[302] = true;
            e.onInput(axisEvent(dev, 3, 0.0f, 0.76f));  // end of the dry range: 0.8
            const size_t at80 = count(log, "THR", 0.8);
            CHECK(at80 == 1);
            wait(8);
            pressed[303] = true;
            e.onInput(buttonEvent(dev, 303, true));    // the afterburner switch closes
            CHECK(count(log, "THR", 0.8) == 2);        // the same value again, whatever the simulator did to the lever meanwhile
            CHECK(count(log, "LASTAB", 1) == 1 && count(log, "AB", 1) == 0);
            wait(60); e.tick();
            CHECK(count(log, "AB", 1) == 1);
            size_t iLast = 0, iAb = 0;
            for (size_t i = 0; i < log.size(); ++i) { if (log[i].name == "LASTAB") iLast = i; if (log[i].name == "AB" && log[i].value == 1) iAb = i; }
            CHECK(iLast < iAb);
            wait(8);
            pressed[303] = false;
            e.onInput(buttonEvent(dev, 303, false));   // leaves the afterburner: AB off, then the throttle back to 0.8
            CHECK(count(log, "AB", 0) == 1 && count(log, "THR", 0.8) == 3);
            size_t iOff = 0, iBack = 0;
            for (size_t i = 0; i < log.size(); ++i) { if (log[i].name == "AB" && log[i].value == 0) iOff = i; if (i > iOff && log[i].name == "THR" && log[i].value == 0.8) iBack = i; }
            CHECK(iOff > 0 && iBack > iOff);
        }
        {   // a new profile drops what a lever held back
            Engine e(bridge);
            std::vector<Seen> log;
            std::map<int, bool> pressed;
            lever(e, cutSeq, abSeq, pressed);
            watch(e, log);
            e.onInput(axisEvent(dev, 3, 0.0f, 0.5f));
            wait(8);
            pressed[300] = true;
            e.onInput(buttonEvent(dev, 300, true));
            e.setProfile(std::make_shared<Profile>());
            wait(100); e.tick();
            CHECK(count(log, "STOP", 1) == 0);
        }
    }

    // ---- the sim's own idle is 0, and a 0 set from above stops at the idle gate: landing on idle sends it once more
    {
        AxisCalibration cal;
        cal.device = dev; cal.code = 3;
        cal.cutoff = 0.03; cal.idle = 0.03; cal.mil = 0.75; cal.ab = 0.90;
        cal.cutoffBtn = 300; cal.idleBtn = 301; cal.milBtn = 302; cal.abBtn = 303;
        AxisDetents d;
        d.enabled = true; d.outCutoff = 0.0; d.outIdle = 0.0; d.outMil = 0.8;
        auto act = [](ActionKind k, const char* name, double v) { Action a; a.kind = k; a.name = name; a.value = v; return a; };
        auto wait = [](int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); };
        struct Seen { std::string name; double value; };
        auto setup = [&](Engine& e, std::vector<Seen>& log, std::map<int, bool>& pressed, int minInterval = 5) {
            auto lp = std::make_shared<Profile>();
            Binding t;
            t.label = "Left throttle";
            t.input = {dev, InputKind::Axis, 3, 0};
            t.axis.mode = AxisMode::Value; t.axis.integer = false; t.axis.minIntervalMs = minInterval; t.axis.detents = d;
            t.onPress = {act(ActionKind::InputEvent, "THR", 0)};
            t.onCutoff = {act(ActionKind::InputEvent, "STOP", 1)};
            t.onCutoffLeave = {act(ActionKind::InputEvent, "STOP", 0)};
            lp->bindings.push_back(t);
            e.setProbes([&pressed](const std::string&, int code) { return pressed[code]; }, [](const std::string&, int) { return -1.0f; });
            e.setProfile(lp);
            e.setCalibrations({cal});
            e.setActionObserver([&log](const Action& a, const std::string&) { log.push_back({a.name, a.value}); });
        };
        auto count = [](const std::vector<Seen>& log, const std::string& name, double value) {
            size_t n = 0;
            for (const auto& x : log) if (x.name == name && std::fabs(x.value - value) < 1e-9) ++n;
            return n;
        };

        {   // pulled back from the dry range onto idle: 0, and 0 again 100 ms later
            Engine e(bridge);
            std::vector<Seen> log;
            std::map<int, bool> pressed;
            setup(e, log, pressed);
            e.onInput(axisEvent(dev, 3, 0.0f, 0.40f));
            wait(8);
            pressed[301] = true;
            e.onInput(buttonEvent(dev, 301, true));  // the idle switch closes
            CHECK(count(log, "THR", 0) == 1);
            e.tick();
            CHECK(count(log, "THR", 0) == 1);  // not yet
            wait(130);
            e.tick();
            CHECK(count(log, "THR", 0) == 2 && !e.hasScheduled());
        }
        {   // the lever moves on before the repeat is due: nothing is sent for the lever that is no longer at idle
            Engine e(bridge);
            std::vector<Seen> log;
            std::map<int, bool> pressed;
            setup(e, log, pressed);
            e.onInput(axisEvent(dev, 3, 0.0f, 0.40f));
            wait(8);
            pressed[301] = true;
            e.onInput(buttonEvent(dev, 301, true));
            wait(30);
            pressed[301] = false;
            e.onInput(buttonEvent(dev, 301, false));
            e.onInput(axisEvent(dev, 3, 0.40f, 0.45f));  // up again: a newer value
            wait(130);
            e.tick();
            CHECK(count(log, "THR", 0) == 1);
        }
        {   // a lever that starts at idle, and one that comes out of the cut-off, send no repeat; leaving the cut-off clears STOP
            Engine e(bridge);
            std::vector<Seen> log;
            std::map<int, bool> pressed;
            pressed[301] = true;
            setup(e, log, pressed);
            e.onInput(axisEvent(dev, 3, 0.0f, 0.02f));
            wait(130);
            e.tick();
            CHECK(count(log, "THR", 0) == 1);
            pressed[300] = true;
            e.onInput(buttonEvent(dev, 300, true));    // cut-off
            CHECK(count(log, "STOP", 1) == 1);
            wait(8);
            pressed[300] = false;
            e.onInput(buttonEvent(dev, 300, false));   // out of it, back to idle
            CHECK(count(log, "STOP", 0) == 1);
            const size_t zeros = count(log, "THR", 0);
            wait(130);
            e.tick();
            CHECK(count(log, "THR", 0) == zeros);       // no repeat for a change of zone
        }
    }

    // ---- a value held back by the minimum interval: it is sent from tick(), and a lever that has come down onto idle gets its second 0 then too
    {
        AxisCalibration cal;
        cal.device = dev; cal.code = 3;
        cal.cutoff = 0.03; cal.idle = 0.03; cal.mil = 0.75; cal.ab = 0.90;
        cal.cutoffBtn = 300; cal.idleBtn = 301; cal.milBtn = 302; cal.abBtn = 303;
        AxisDetents d;
        d.enabled = true; d.outCutoff = 0.0; d.outIdle = 0.0; d.outMil = 0.8;
        struct Seen { std::string name; double value; };
        auto act = [](ActionKind k, const char* name, double v) { Action a; a.kind = k; a.name = name; a.value = v; return a; };
        auto wait = [](int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); };
        auto count = [](const std::vector<Seen>& log, const std::string& name, double value) {
            size_t n = 0;
            for (const auto& x : log) if (x.name == name && std::fabs(x.value - value) < 1e-9) ++n;
            return n;
        };
        auto setup = [&](Engine& e, std::vector<Seen>& log, std::map<int, bool>& pressed) {
            auto lp = std::make_shared<Profile>();
            Binding t;
            t.label = "Left throttle";
            t.input = {dev, InputKind::Axis, 3, 0};
            t.axis.mode = AxisMode::Value; t.axis.integer = false; t.axis.minIntervalMs = 60; t.axis.detents = d;
            t.onPress = {act(ActionKind::InputEvent, "THR", 0)};
            lp->bindings.push_back(t);
            e.setProbes([&pressed](const std::string&, int code) { return pressed[code]; }, [](const std::string&, int) { return -1.0f; });
            e.setProfile(lp);
            e.setCalibrations({cal});
            e.setActionObserver([&log](const Action& a, const std::string&) { log.push_back({a.name, a.value}); });
        };
        {
            Engine e(bridge);
            std::vector<Seen> log;
            std::map<int, bool> pressed;
            setup(e, log, pressed);
            e.onInput(axisEvent(dev, 3, 0.0f, 0.40f));  // sent at once
            pressed[301] = true;
            e.onInput(buttonEvent(dev, 301, true));     // the idle switch closes inside the 60 ms interval: the 0 is held back
            CHECK(count(log, "THR", 0) == 0);
            wait(70);
            e.tick();
            CHECK(count(log, "THR", 0) == 1 && e.hasScheduled());  // the held value is out, and the second 0 is on its way
            wait(130);
            e.tick();
            CHECK(count(log, "THR", 0) == 2 && !e.hasScheduled());
        }
        {   // paused: what was held back is dropped, not sent when the interval is over
            Engine e(bridge);
            std::vector<Seen> log;
            std::map<int, bool> pressed;
            setup(e, log, pressed);
            e.onInput(axisEvent(dev, 3, 0.0f, 0.40f));
            e.onInput(axisEvent(dev, 3, 0.40f, 0.60f));  // inside the interval: held back
            const size_t sent = log.size();
            e.setEnabled(false);
            wait(70);
            e.tick();
            CHECK(log.size() == sent);
        }
    }

    std::printf(failures ? "\n%d check(s) FAILED\n" : "all axis checks passed\n", failures);
    return failures ? 1 : 0;
}
