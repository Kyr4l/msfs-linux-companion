// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kyr4l
#pragma once
// Turns joystick input into mouse and keyboard output, the way antiMicroX does (same dead zones, speeds and mouse curve).
// onInput() and tick() run on the input thread, the rest on any thread. It costs nothing while no output is moving: tick() then asks for no wake-up.
#include <chrono>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "desktop/DesktopTypes.hpp"

namespace lr::desktop {

// The wheel is counted in high-resolution units, as Linux does: 120 make one notch of an ordinary wheel.
inline constexpr int kWheelNotch = 120;

// Programs that only understand whole notches get one for every 120 units. Turning back starts the count again, as a real high-resolution wheel does.
class WheelNotches {
public:
    int feed(int units) {
        if ((units > 0 && rest_ < 0) || (units < 0 && rest_ > 0)) rest_ = 0;
        rest_ += units;
        const int notches = rest_ / kWheelNotch;
        rest_ -= notches * kWheelNotch;
        return notches;
    }
    void reset() { rest_ = 0; }

private:
    int rest_ = 0;
};

class OutputSink {
public:
    virtual ~OutputSink() = default;
    virtual bool open(std::string& err) = 0;
    virtual void close() = 0;
    virtual void key(int code, bool down) = 0;
    virtual void move(int dx, int dy) = 0;
    virtual void wheel(int vertical, int horizontal) = 0;  // in units of 1/120 notch
    virtual void flush() = 0;
};

// antiMicroX's default curve ("enhanced precision"): 0..1 of the deflection to 0..1 of the speed.
double enhancedPrecision(double deflection);
// The other curves, by the same rule: 0..1 of the deflection to the share of the speed (the extreme curve goes up to 1.5).
double applyCurve(MouseCurve curve, double deflection, double sensitivity);
// The position of an axis (0..1 from the device) as -1..1 after its mode.
double axisReading(AxisMode mode, double position);

class DesktopEngine {
public:
    using AxisProbe = std::function<float(const std::string& device, int code)>;  // 0..1, or below 0 when the axis is not there

    explicit DesktopEngine(OutputSink& sink) : sink_(sink) {}

    void setAxisProbe(AxisProbe probe);
    void setBindings(std::vector<Binding> bindings);  // everything held is let go first
    // On: the outputs open and the axes are read where they are. Off: everything held is let go and the outputs close.
    bool setActive(bool on);
    bool active() const;
    std::string error() const;

    void onInput(const input::InputEvent& ev);
    int tick();  // milliseconds until it wants to be called again, -1 when nothing is moving

private:
    using Clock = std::chrono::steady_clock;
    struct Live {
        bool held = false;
        double amount = 0;  // 0..1 between the dead zone and the end of an axis
        Clock::time_point nextWheel{};
    };
    void press(size_t index, Clock::time_point now);
    void release(size_t index);
    void releaseAll();
    void applyAxis(size_t index, float position, Clock::time_point now);
    void pressKey(int code);
    void releaseKey(int code);
    static void wheelNotch(OutputSink& sink, Direction d);

    mutable std::mutex m_;
    OutputSink& sink_;
    AxisProbe probe_;
    std::vector<Binding> bindings_;
    std::vector<Live> live_;
    std::map<int, int> keysDown_;  // key code -> how many bindings hold it
    bool active_ = false;
    std::string error_;
    double remainderX_ = 0, remainderY_ = 0;
    double wheelRemainderV_ = 0, wheelRemainderH_ = 0;
    bool moving_ = false;
    Clock::time_point lastTick_{};
};

}  // namespace lr::desktop
