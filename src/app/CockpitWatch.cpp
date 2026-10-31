// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kyr4l
#include "app/CockpitWatch.hpp"

#include <algorithm>

namespace lr::app {

void CockpitWatch::start(clock::time_point at) {
    rows_.clear();
    startedAt_ = at;
    running_ = true;
}

double CockpitWatch::learnSecondsLeft(clock::time_point at) const {
    if (!learning(at)) return 0.0;
    return std::chrono::duration<double>(startedAt_ + kLearnTime - at).count();
}

void CockpitWatch::add(const sim::InputChange& c) {
    if (!running_ || c.time < startedAt_) return;
    auto it = rows_.find(c.name);
    if (it == rows_.end()) {
        Row r;
        r.name = c.name;
        r.lowest = std::min(c.oldValue, c.newValue);
        r.highest = std::max(c.oldValue, c.newValue);
        it = rows_.emplace(c.name, std::move(r)).first;
    }
    Row& r = it->second;
    r.before = c.oldValue;
    r.now = c.newValue;
    r.lowest = std::min({r.lowest, c.oldValue, c.newValue});
    r.highest = std::max({r.highest, c.oldValue, c.newValue});
    ++r.changes;
    r.last = c.time;
    if (c.time < startedAt_ + kLearnTime) r.noisy = true;  // changed while nobody touched the cockpit
}

std::vector<CockpitWatch::Row> CockpitWatch::rows(bool includeNoisy) const {
    std::vector<Row> out;
    for (const auto& [name, r] : rows_)
        if (includeNoisy || !r.noisy) out.push_back(r);
    std::sort(out.begin(), out.end(), [](const Row& a, const Row& b) { return a.last != b.last ? a.last > b.last : a.name < b.name; });
    return out;
}

size_t CockpitWatch::noisyCount() const {
    size_t n = 0;
    for (const auto& [name, r] : rows_) n += r.noisy ? 1 : 0;
    return n;
}

}  // namespace lr::app
