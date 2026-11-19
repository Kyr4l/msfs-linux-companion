// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kyr4l
#pragma once
// Keeps the text of the aircraft's MCDU from its pop-out window: a thread of its own, at the lowest priority, about 8 pictures a
// second, and only the cells that changed are read again. It runs only while it is wanted (an iniBuilds Airbus in the sim and
// a unit or the on-screen panel to show it on); otherwise there is no thread, no X connection and no memory held.
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

#include "mcdu/McduProtocol.hpp"

namespace lr::mcdu {

class ScreenMirror {
public:
    explicit ScreenMirror(std::function<void()> changed);
    ~ScreenMirror();
    ScreenMirror(const ScreenMirror&) = delete;
    ScreenMirror& operator=(const ScreenMirror&) = delete;

    void setWanted(bool wanted);
    bool screen(Screen& out) const;  // the latest text, while it is recent
    std::string hint() const;        // what to do when there is no text yet, for the screen of the unit ("" when there is text)
    bool takeChanged() { return changed_.exchange(false); }

private:
    using Clock = std::chrono::steady_clock;
    void run();
    void publish(const Screen* screen, const char* hint);
    bool idle(std::chrono::milliseconds wait);  // false: asked to stop

    std::function<void()> notify_;
    mutable std::mutex m_;
    std::condition_variable cv_;
    std::thread thread_;
    bool stop_ = false;
    Screen latest_;
    bool have_ = false;
    Clock::time_point latestAt_{};
    std::string hint_;
    std::atomic<bool> changed_{false};
};

}  // namespace lr::mcdu
