// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kyr4l
#pragma once
// Listens on the session bus for the three ways a program reports progress on KDE: its job view, a notification with a progress value, and the progress on its taskbar icon.
// The bus delivers only those messages. Of a notification only the program's name and the value are read. Used from one thread, not while the simulator runs.
#include <chrono>
#include <string>

#include "desktop/ProgressTracker.hpp"
#include "desktop/SdBus.hpp"

namespace lr::desktop {

class ProgressWatcher {
public:
    ProgressWatcher() = default;
    ~ProgressWatcher() { disconnect(); }
    ProgressWatcher(const ProgressWatcher&) = delete;
    ProgressWatcher& operator=(const ProgressWatcher&) = delete;

    void setWanted(bool wanted);  // connects, and connects again when the bus went away
    bool available() const { return bus_ != nullptr; }
    int fd() const;
    short events() const;
    void process();               // after poll(), and regularly
    ProgressTracker& tracker() { return tracker_; }

private:
    using Clock = std::chrono::steady_clock;
    bool connect();
    void disconnect();
    void handle(sdbus::Message* m);

    const sdbus::Api* api_ = nullptr;
    sdbus::Bus* bus_ = nullptr;
    bool wanted_ = false;
    Clock::time_point nextAttempt_{};
    ProgressTracker tracker_;
};

}  // namespace lr::desktop
