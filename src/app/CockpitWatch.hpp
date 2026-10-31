// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kyr4l
#pragma once
// What the Cockpit tab shows: the changes of the cockpit's input events (SimBridge::inputChanges) gathered per event, newest first.
//
// Some events change by themselves (a clock, a needle that follows the engine). To tell them from what the user does, watching starts with a
// short learning phase in which the cockpit is left alone: every event that changes then is "noisy" and is hidden unless asked for.
#include <chrono>
#include <string>
#include <unordered_map>
#include <vector>

#include "sim/SimBridge.hpp"

namespace lr::app {

class CockpitWatch {
public:
    using clock = std::chrono::steady_clock;

    struct Row {
        std::string name;
        double before = 0, now = 0;   // the value before the latest change, and after it
        double lowest = 0, highest = 0;  // the range of values seen
        size_t changes = 0;
        clock::time_point last;
        bool noisy = false;           // it changed during the learning phase
    };

    static constexpr std::chrono::milliseconds kLearnTime{3000};

    void start(clock::time_point at);  // forgets everything and begins the learning phase
    void stop() { running_ = false; }
    bool running() const { return running_; }
    bool learning(clock::time_point at) const { return running_ && at < startedAt_ + kLearnTime; }
    double learnSecondsLeft(clock::time_point at) const;

    // A change seen by the bridge. Changes older than start() (seen before watching began) are ignored.
    void add(const sim::InputChange& c);
    void clear() { rows_.clear(); }

    // newest first; the noisy ones only when asked for
    std::vector<Row> rows(bool includeNoisy) const;
    size_t noisyCount() const;

private:
    bool running_ = false;
    clock::time_point startedAt_{};
    std::unordered_map<std::string, Row> rows_;
};

}  // namespace lr::app
