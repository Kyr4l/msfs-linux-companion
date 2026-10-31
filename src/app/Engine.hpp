// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kyr4l
#pragma once
// Turns joystick events into simulator commands according to the active profile.
// Called from the input thread; the UI thread swaps in new profiles and reads status.
#include <array>
#include <chrono>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "app/Profile.hpp"
#include "input/Evdev.hpp"
#include "sim/SimBridge.hpp"

namespace lr::app {

struct FiredRecord {
    std::chrono::steady_clock::time_point time;
    std::string text;
};

class Engine {
public:
    explicit Engine(sim::SimBridge& bridge);

    void setProfile(std::shared_ptr<const Profile> profile);
    void setCalibrations(std::vector<AxisCalibration> calibrations);  // the gates of the calibrated levers (Settings::calibrations)
    // How to ask the input manager for the state of a button or axis the engine has not seen move yet (a lever resting in its cut-off
    // gate when the program starts). Without them such a gate counts as open.
    void setProbes(std::function<bool(const std::string&, int)> buttonDown, std::function<float(const std::string&, int)> axisValue);
    // While a calibration is open: remember the lever's position at the moment each gate button closes or opens (read by takeGateLearned()).
    void beginGateLearning(const AxisCalibration& cal);
    void endGateLearning();
    std::array<std::optional<double>, 4> takeGateLearned();  // cut-off closes, idle opens, end of the dry range closes, afterburner closes; each is returned once
    void setEnabled(bool on);
    bool enabled() const { return enabled_; }

    // input-thread entry points
    void onInput(const input::InputEvent& ev);
    // Runs what is due (key repeats, axis values held back by their minimum interval). Returns the milliseconds until it
    // has to run again, or -1 when nothing is waiting: the input thread sleeps exactly that long.
    int tick();

    // UI helpers
    // Pure helpers (also used by the UI and the tests)
    static double axisPosition(const AxisConfig& c, double raw01);            // 0..1 inside the used travel, inverted if asked
    static double axisOutput(const AxisConfig& c, double position01);        // rescaled (and rounded if integer)
    // A lever with gates: where it is (raw 0..1 of the device travel) -> the value to send and the zone it is in. `previous` gives the
    // zone a little hysteresis, so a lever resting at a zone boundary does not chatter.
    // Which gate buttons are closed (only meaningful for the gates of the calibration that have a button).
    struct GateState { bool cutoff = false, idle = false, mil = false, ab = false; };
    struct DetentResult { double value; AxisZone zone; };
    static DetentResult detentOutput(const AxisDetents& d, const AxisCalibration& cal, double raw01, AxisZone previous, const GateState& gates);
    static DetentResult detentOutput(const AxisDetents& d, const AxisCalibration& cal, double raw01, AxisZone previous);  // no gate button closed
    static std::vector<Action> withValue(const std::vector<Action>& actions, double v, bool integer);  // substitute the axis value
    static std::string formatValue(double v, bool integer);

    void beginCapture(bool buttonsOnly = false);  // buttonsOnly: ignore axes and hats (learning the gate buttons of a lever)
    void cancelCapture();
    bool capturing() const;
    std::optional<input::InputId> takeCaptured();
    std::optional<input::InputEvent> lastInput() const;
    std::vector<FiredRecord> recentFires() const;
    void fire(const std::vector<Action>& actions, const std::string& origin);  // a Delay action holds back what follows it in the list
    bool hasScheduled() const;  // actions are waiting for a Delay: call tick() when they are due
    // Called with every action the engine carries out (after its Delay), for the tests.
    void setActionObserver(std::function<void(const Action&, const std::string& origin)> observer);

private:
    struct Held {
        input::InputId id;
        size_t bindingIndex;
        std::chrono::steady_clock::time_point nextRepeat;
    };
    void dispatch(const Binding& b, bool press, const char* why);
    void record(const std::string& text);
    void recordLatest(const std::string& key, const std::string& text);  // replaces the previous entry of the same key
    void onAxis(const input::InputEvent& ev);
    void sendAxisValue(const Binding& b, double out);
    void dispatchZone(const Binding& b, size_t index, const std::vector<Action>& actions, const char* what);  // runs and records the actions of a gate zone
    void applyLever(const Binding& b, size_t index, const AxisCalibration& cal, double raw);  // value and gate actions of a lever with gates
    bool buttonIsDown(const std::string& device, int code);
    GateState gatesOf(const AxisCalibration& cal);
    double axisRawOf(const AxisCalibration& cal);

    struct AxisState {
        bool hasSent = false;
        double lastSent = 0;
        bool hasPending = false;
        double pending = 0;
        std::chrono::steady_clock::time_point lastTime{};
        bool anchorValid = false;
        double anchor = 0;
        bool zoneKnown = false;  // lever with gates: the zone seen last
        AxisZone zone = AxisZone::Dry;
        uint64_t generation = 0;  // counts the changes of zone: what a Delay held back for an earlier zone is dropped
        uint64_t sendSerial = 0;  // counts the values sent: a repeat held back for the last one is dropped when a newer one goes out
    };
    std::unordered_map<size_t, AxisState> axisState_;           // by binding index in the current profile
    std::unordered_map<std::string, float> captureBase_;        // axis resting values seen since beginCapture()
    std::unordered_map<std::string, float> axisRaw_;            // last position seen of every axis ("device#code"), also while bindings are paused
    std::unordered_map<std::string, bool> buttonState_;         // last state seen of every button ("device#code")
    std::function<bool(const std::string&, int)> buttonProbe_;
    std::function<float(const std::string&, int)> axisProbe_;
    struct GateLearning {
        bool active = false;
        std::string device;
        int axisCode = 0;
        int button[4] = {-1, -1, -1, -1};
        std::array<std::optional<double>, 4> raw;
    } learn_;
    bool captureButtonsOnly_ = false;

    // The rest of an action list after a Delay. One tied to a lever with gates (binding >= 0) is dropped when the lever changes zone
    // before it is due: a cut-off held back for 100 ms must not fire for a lever that has meanwhile left the gate.
    struct Scheduled {
        std::chrono::steady_clock::time_point due;
        std::vector<Action> rest;
        std::string origin;
        int binding = -1;
        uint64_t generation = 0;
        bool useSerial = false;  // also dropped when a newer value of the lever has been sent since
        uint64_t serial = 0;
    };
    std::vector<Scheduled> scheduled_;
    std::function<void(const Action&, const std::string&)> observer_;
    void runActions(const std::vector<Action>& actions, size_t from, const std::string& origin, int binding, uint64_t generation);
    std::vector<Scheduled> takeDue(int& waitMs);  // under m_: what is due now (stale ones dropped), and the wait until the next

    sim::SimBridge& bridge_;
    mutable std::mutex m_;
    std::shared_ptr<const Profile> profile_;
    std::shared_ptr<const std::vector<AxisCalibration>> calibrations_ = std::make_shared<const std::vector<AxisCalibration>>();
    std::vector<Held> held_;
    bool enabled_ = true;
    bool capturing_ = false;
    std::optional<input::InputId> captured_;
    std::optional<input::InputEvent> lastInput_;
    std::deque<FiredRecord> fires_;
};

}  // namespace lr::app
