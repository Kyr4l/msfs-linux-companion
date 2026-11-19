// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kyr4l
#include "desktop/DesktopEngine.hpp"

#include <algorithm>
#include <cmath>

namespace lr::desktop {

namespace {
constexpr int kMoveIntervalMs = 8;         // 125 updates a second while the pointer moves
constexpr double kMaxStepSeconds = 0.05;   // after a stall the pointer does not jump
constexpr double kHysteresis = 0.005;      // an axis resting on the edge of its dead zone does not chatter
constexpr int kSpeedUnit = 20;             // antiMicroX: pixels a second per unit of mouse speed
}  // namespace

double enhancedPrecision(double d) {
    if (d <= 0.4) return d * 0.37;
    if (d <= 0.75) return d - 0.252;
    return d * 2.008 - 1.008;
}

double applyCurve(MouseCurve curve, double d, double sensitivity) {
    switch (curve) {
        case MouseCurve::Linear: return d;
        case MouseCurve::Quadratic: return d * d;
        case MouseCurve::Cubic: return d * d * d;
        case MouseCurve::QuadraticExtreme: return d >= 0.95 ? d * d * 1.5 : d * d;
        case MouseCurve::Power: return std::clamp(std::pow(d, 1.0 / std::clamp(sensitivity, 0.001, 1000.0)), 0.0, 1.0);
        case MouseCurve::EnhancedPrecision: break;
    }
    return enhancedPrecision(d);
}

double axisReading(AxisMode mode, double position) {
    const double v = position * 2.0 - 1.0;
    switch (mode) {
        case AxisMode::Negative: return (v - 1.0) / 2.0;
        case AxisMode::Positive: return (v + 1.0) / 2.0;
        case AxisMode::NegativeHalf: return v <= 0 ? v : -v;
        case AxisMode::PositiveHalf: return v >= 0 ? v : -v;
        case AxisMode::Normal: break;
    }
    return v;
}

void DesktopEngine::setAxisProbe(AxisProbe probe) {
    std::lock_guard<std::mutex> lock(m_);
    probe_ = std::move(probe);
}

void DesktopEngine::setBindings(std::vector<Binding> bindings) {
    std::lock_guard<std::mutex> lock(m_);
    releaseAll();
    if (bindings.size() > kMaxBindings) bindings.resize(kMaxBindings);
    bindings_ = std::move(bindings);
    live_.assign(bindings_.size(), Live{});
    if (active_ && probe_) {
        const auto now = Clock::now();
        for (size_t i = 0; i < bindings_.size(); ++i)
            if (bindings_[i].input.kind == input::InputKind::Axis) {
                const float v = probe_(bindings_[i].input.device, bindings_[i].input.code);
                if (v >= 0) applyAxis(i, v, now);
            }
        sink_.flush();
    }
}

bool DesktopEngine::setActive(bool on) {
    std::lock_guard<std::mutex> lock(m_);
    if (on == active_) return active_;
    if (!on) {
        releaseAll();
        sink_.flush();
        sink_.close();
        active_ = false;
        return false;
    }
    std::string err;
    if (!sink_.open(err)) {
        error_ = err;
        return false;
    }
    error_.clear();
    active_ = true;
    remainderX_ = remainderY_ = 0;
    lastTick_ = Clock::now();
    live_.assign(bindings_.size(), Live{});
    if (probe_) {
        for (size_t i = 0; i < bindings_.size(); ++i)
            if (bindings_[i].input.kind == input::InputKind::Axis) {
                const float v = probe_(bindings_[i].input.device, bindings_[i].input.code);
                if (v >= 0) applyAxis(i, v, lastTick_);
            }
        sink_.flush();
    }
    return true;
}

bool DesktopEngine::active() const {
    std::lock_guard<std::mutex> lock(m_);
    return active_;
}

std::string DesktopEngine::error() const {
    std::lock_guard<std::mutex> lock(m_);
    return error_;
}

void DesktopEngine::pressKey(int code) {
    if (keysDown_[code]++ == 0) sink_.key(code, true);
}

void DesktopEngine::releaseKey(int code) {
    auto it = keysDown_.find(code);
    if (it == keysDown_.end()) return;
    if (--it->second <= 0) {
        keysDown_.erase(it);
        sink_.key(code, false);
    }
}

void DesktopEngine::wheelNotch(OutputSink& sink, Direction d) {
    switch (d) {
        case Direction::Up: sink.wheel(kWheelNotch, 0); break;
        case Direction::Down: sink.wheel(-kWheelNotch, 0); break;
        case Direction::Left: sink.wheel(0, -kWheelNotch); break;
        case Direction::Right: sink.wheel(0, kWheelNotch); break;
    }
}

void DesktopEngine::press(size_t index, Clock::time_point now) {
    Live& l = live_[index];
    if (l.held) return;
    l.held = true;
    const Binding& b = bindings_[index];
    bool wheel = false;
    for (const Output& o : b.outputs) {
        if (o.kind == OutputKind::Key || o.kind == OutputKind::MouseButton) pressKey(o.code);
        else if (o.kind == OutputKind::Wheel && b.input.kind != input::InputKind::Axis) {  // an axis scrolls smoothly from rest, in tick()
            wheelNotch(sink_, static_cast<Direction>(o.code));  // the first notch comes at once, then the repeat
            wheel = true;
        }
    }
    if (wheel) l.nextWheel = now + std::chrono::milliseconds(1000 / std::clamp(b.wheelSpeed, 1, 100));
}

void DesktopEngine::release(size_t index) {
    Live& l = live_[index];
    if (!l.held) return;
    l.held = false;
    l.amount = 0;
    for (const Output& o : bindings_[index].outputs)
        if (o.kind == OutputKind::Key || o.kind == OutputKind::MouseButton) releaseKey(o.code);
}

void DesktopEngine::releaseAll() {
    for (size_t i = 0; i < live_.size(); ++i) release(i);
    for (const auto& [code, count] : keysDown_) sink_.key(code, false);  // nothing stays down, whatever the counts say
    keysDown_.clear();
}

void DesktopEngine::applyAxis(size_t index, float position, Clock::time_point now) {
    const Binding& b = bindings_[index];
    const double v = axisReading(b.mode, static_cast<double>(position));
    const double towards = b.side < 0 ? -v : v;
    const double dead = std::clamp(b.deadZone, 0.0, 0.99);
    const double top = std::max(dead + 0.001, std::min(b.maxZone, 1.0));
    Live& l = live_[index];
    if (towards >= dead) {
        l.amount = std::clamp((towards - dead) / (top - dead), 0.0, 1.0);
        press(index, now);
    } else if (l.held && towards < dead - kHysteresis) {
        release(index);
    }
}

void DesktopEngine::onInput(const input::InputEvent& ev) {
    std::lock_guard<std::mutex> lock(m_);
    if (!active_) return;
    const auto now = Clock::now();
    bool touched = false;
    for (size_t i = 0; i < bindings_.size(); ++i) {
        const Binding& b = bindings_[i];
        if (!(b.input == ev.id)) continue;
        touched = true;
        if (ev.id.kind == input::InputKind::Axis) applyAxis(i, ev.axis, now);
        else if (ev.pressed) press(i, now);
        else release(i);
    }
    if (touched) sink_.flush();
}

int DesktopEngine::tick() {
    std::lock_guard<std::mutex> lock(m_);
    if (!active_) return -1;
    const auto now = Clock::now();
    // the time since the pointer or the wheel last moved does not count: they start from rest instead of jumping
    const double dt = moving_ ? std::min(std::chrono::duration<double>(now - lastTick_).count(), kMaxStepSeconds) : 0.0;
    lastTick_ = now;
    double dx = 0, dy = 0, scrollV = 0, scrollH = 0;
    bool moving = false, scrolling = false;
    int wait = -1;
    for (size_t i = 0; i < bindings_.size(); ++i) {
        Live& l = live_[i];
        if (!l.held) continue;
        const Binding& b = bindings_[i];
        for (const Output& o : b.outputs) {
            if (o.kind == OutputKind::MouseMove) {
                moving = true;
                // a button held down moves at full speed, an axis by how far it is pushed
                const double amount = b.input.kind == input::InputKind::Axis ? l.amount : 1.0;
                const double pixels = applyCurve(b.curve, amount, b.sensitivity) * std::clamp(b.mouseSpeed, 1, 300) * kSpeedUnit * dt;
                switch (static_cast<Direction>(o.code)) {
                    case Direction::Up: dy -= pixels; break;
                    case Direction::Down: dy += pixels; break;
                    case Direction::Left: dx -= pixels; break;
                    case Direction::Right: dx += pixels; break;
                }
            } else if (o.kind == OutputKind::Wheel && b.input.kind == input::InputKind::Axis) {
                // high resolution: the further the axis is pushed, the faster it scrolls, up to the wheel speed at full travel
                scrolling = true;
                const double units = applyCurve(b.curve, l.amount, b.sensitivity) * std::clamp(b.wheelSpeed, 1, 100) * kWheelNotch * dt;
                switch (static_cast<Direction>(o.code)) {
                    case Direction::Up: scrollV += units; break;
                    case Direction::Down: scrollV -= units; break;
                    case Direction::Left: scrollH -= units; break;
                    case Direction::Right: scrollH += units; break;
                }
            } else if (o.kind == OutputKind::Wheel) {
                const auto interval = std::chrono::milliseconds(1000 / std::clamp(b.wheelSpeed, 1, 100));
                if (now >= l.nextWheel) {
                    wheelNotch(sink_, static_cast<Direction>(o.code));
                    l.nextWheel = std::max(l.nextWheel + interval, now);  // a late tick does not make up for lost notches
                }
                const int ms = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(l.nextWheel - now).count()) + 1;
                wait = wait < 0 ? ms : std::min(wait, ms);
            }
        }
    }
    remainderX_ += dx;
    remainderY_ += dy;
    const int stepX = static_cast<int>(remainderX_), stepY = static_cast<int>(remainderY_);
    remainderX_ -= stepX;
    remainderY_ -= stepY;
    if (stepX || stepY) sink_.move(stepX, stepY);
    if (!moving) remainderX_ = remainderY_ = 0;
    wheelRemainderV_ += scrollV;
    wheelRemainderH_ += scrollH;
    const int stepV = static_cast<int>(wheelRemainderV_), stepH = static_cast<int>(wheelRemainderH_);
    wheelRemainderV_ -= stepV;
    wheelRemainderH_ -= stepH;
    if (stepV || stepH) sink_.wheel(stepV, stepH);
    if (!scrolling) wheelRemainderV_ = wheelRemainderH_ = 0;
    moving_ = moving || scrolling;
    sink_.flush();
    if (moving_) return kMoveIntervalMs;
    return wait;
}

}  // namespace lr::desktop
