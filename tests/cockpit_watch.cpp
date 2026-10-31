// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kyr4l
// The model of the Cockpit tab: changes gathered per event, the learning phase that marks what changes by itself, the order.
#include <cstdio>

#include "app/CockpitWatch.hpp"

using namespace lr;
using namespace lr::app;
using std::chrono::milliseconds;

static int failures = 0;
#define CHECK(cond)                                                                         \
    do {                                                                                    \
        if (!(cond)) { std::printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); ++failures; } \
    } while (0)

static sim::InputChange change(const char* name, double from, double to, CockpitWatch::clock::time_point at, uint64_t serial) {
    sim::InputChange c;
    c.serial = serial;
    c.time = at;
    c.name = name;
    c.oldValue = from;
    c.newValue = to;
    return c;
}

int main() {
    const auto t0 = CockpitWatch::clock::now();
    CockpitWatch w;
    CHECK(!w.running() && !w.learning(t0) && w.rows(true).empty());
    w.add(change("EARLY", 0, 1, t0, 1));  // not watching yet: ignored
    CHECK(w.rows(true).empty());

    w.start(t0);
    CHECK(w.running() && w.learning(t0 + milliseconds(100)) && w.learnSecondsLeft(t0 + milliseconds(1000)) > 1.9);
    CHECK(!w.learning(t0 + milliseconds(3100)) && w.learnSecondsLeft(t0 + milliseconds(3100)) == 0.0);

    // a clock that moves during the learning phase is "noisy" for good, a switch clicked afterwards is not
    w.add(change("CLOCK_SECONDS", 10, 11, t0 + milliseconds(500), 1));
    w.add(change("CLOCK_SECONDS", 11, 12, t0 + milliseconds(1500), 2));
    w.add(change("CLOCK_SECONDS", 12, 13, t0 + milliseconds(4000), 3));
    w.add(change("LIGHTING_LANDING_1", 0, 1, t0 + milliseconds(4200), 4));
    w.add(change("LIGHTING_LANDING_1", 1, 0, t0 + milliseconds(5000), 5));
    w.add(change("AS1000_BARO_PFD", 29.92, 29.93, t0 + milliseconds(6000), 6));

    CHECK(w.noisyCount() == 1);
    auto quiet = w.rows(false);
    CHECK(quiet.size() == 2 && quiet[0].name == "AS1000_BARO_PFD" && quiet[1].name == "LIGHTING_LANDING_1");  // newest first
    CHECK(quiet[1].changes == 2 && quiet[1].before == 1 && quiet[1].now == 0 && quiet[1].lowest == 0 && quiet[1].highest == 1 && !quiet[1].noisy);
    auto all = w.rows(true);
    CHECK(all.size() == 3 && all[2].name == "CLOCK_SECONDS" && all[2].noisy && all[2].changes == 3);

    // a change seen before watching began is not shown; a new start forgets the list
    w.add(change("OLD", 0, 1, t0 - milliseconds(10), 7));
    CHECK(w.rows(true).size() == 3);
    w.clear();
    CHECK(w.rows(true).empty() && w.running());
    w.stop();
    CHECK(!w.running());
    w.add(change("LATE", 0, 1, t0 + milliseconds(9000), 8));
    CHECK(w.rows(true).empty());
    w.start(t0 + milliseconds(10000));
    CHECK(w.rows(true).empty() && w.learning(t0 + milliseconds(10500)));

    std::printf(failures ? "\n%d check(s) FAILED\n" : "all cockpit watch checks passed\n", failures);
    return failures ? 1 : 0;
}
