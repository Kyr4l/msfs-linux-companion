// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kyr4l
// Guides: loading, the flow (questions, steps, back, finishing), answers taken from the simulator, step checks, and
// that every shipped guide file loads.
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>

#include "app/Guide.hpp"

using namespace lr::app;

static int failures = 0;
#define CHECK(cond)                                                                         \
    do {                                                                                    \
        if (!(cond)) { std::printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); ++failures; } \
    } while (0)

int main(int argc, char** argv) {
    const std::string root = argc > 1 ? argv[1] : ".";
    Guide g;
    std::string err;
    CHECK(g.load(root + "/tests/fixtures/guide_test.json", err));
    CHECK(g.aircraft == "Test" && g.topics.size() == 1 && g.nodes.size() == 4);

    std::map<std::string, double> sim;  // what the "simulator" reports, by probe key
    auto read = [&](const GuideProbe& p) -> std::optional<double> {
        auto it = sim.find(p.code + p.inputEvent);
        if (it == sim.end()) return std::nullopt;
        return it->second;
    };

    // answers from the simulator: nothing known, then on the ground, then flying
    const GuideNode* q = g.node("q");
    CHECK(q && !q->autoChoice(read));
    sim["(A:SIM ON GROUND, Bool)"] = 1;
    CHECK(q->autoChoice(read) && *q->autoChoice(read) == "yes");
    sim["(A:SIM ON GROUND, Bool)"] = 0;
    CHECK(q->autoChoice(read) && *q->autoChoice(read) == "no");

    // step checks: unknown is not false
    const GuideStep& brake = g.node("s")->steps[0];
    const GuideStep& av = g.node("s")->steps[1];
    CHECK(!brake.check.evaluate(read));
    sim["(A:BRAKE PARKING POSITION, Bool)"] = 1;
    CHECK(brake.check.evaluate(read) && *brake.check.evaluate(read));
    sim["(A:BRAKE PARKING POSITION, Bool)"] = 0;
    CHECK(brake.check.evaluate(read) && !*brake.check.evaluate(read));
    sim["AV"] = 2;
    CHECK(av.check.evaluate(read) && *av.check.evaluate(read));  // an input event probe, ">="
    CHECK(av.check.probe.inputEvent == "AV" && !av.check.probe.needsMobiFlight() && brake.check.probe.needsMobiFlight());
    CHECK(brake.readouts.size() == 1 && brake.readouts[0].label == "N2");
    CHECK(g.probesOf(*g.node("s")).size() == 3);  // the brake check and readout, the avionics check
    CHECK(guideCompare("==", 0.7500001, 0.75) && !guideCompare("==", 0.76, 0.75) && guideCompare("<=", 3, 10) && !guideCompare("?", 1, 1));

    // the flow
    GuideRun run;
    CHECK(!run.active());
    run.start(g, "t");
    CHECK(run.active() && run.current()->id == "q" && run.depth() == 1 && !run.canGoBack());
    CHECK(!run.advance());          // a question is answered, not skipped
    CHECK(!run.choose("maybe"));    // unknown choice
    CHECK(run.choose("yes") && run.current()->id == "s" && run.depth() == 2 && run.canGoBack());
    CHECK(!run.choose("yes"));      // not a question
    CHECK(run.back() && run.current()->id == "q" && run.depth() == 1);
    CHECK(run.choose("no") && run.current()->id == "air");
    CHECK(run.advance() && run.finished());       // info with no next: the guide ends
    CHECK(run.back() && !run.finished() && run.current()->id == "air");
    CHECK(run.back() && run.current()->id == "q");
    CHECK(run.choose("yes") && run.advance() && run.current()->id == "e");
    CHECK(run.advance() && run.finished());       // an end node
    CHECK(run.back() && run.current()->id == "e" && !run.finished());
    run.restart();
    CHECK(run.current()->id == "q" && run.depth() == 1);

    // answers taken from the simulator: not again right after Back (the user is changing that answer)
    {
        GuideRun r;
        r.start(g, "t");
        CHECK(r.autoAllowed());
        CHECK(r.choose("yes") && r.autoAllowed());
        CHECK(r.back() && r.current()->id == "q" && !r.autoAllowed());   // back on the question: do not auto-answer it again
        CHECK(r.choose("no") && r.autoAllowed());                        // moving forward lifts it
        CHECK(r.advance() && r.finished() && r.back() && r.current()->id == "air" && r.autoAllowed());  // not a question
        CHECK(r.back() && !r.autoAllowed());
        r.restart();
        CHECK(r.autoAllowed());
        CHECK(r.history().empty() && r.choose("yes") && r.history().size() == 1 && r.history()[0] == "q");
    }

    // broken files are refused
    {
        const auto path = (std::filesystem::temp_directory_path() / "msfs-linuxrebind-bad-guide.json").string();
        std::ofstream(path) << R"({"aircraft":"X","topics":[{"id":"t","title":"T","start":"nowhere"}],"nodes":{}})";
        Guide bad;
        CHECK(!bad.load(path, err) && err.find("nowhere") != std::string::npos);
        std::ofstream(path) << "{ not json";
        CHECK(!bad.load(path, err));
        CHECK(!bad.load(path + ".missing", err));
        std::filesystem::remove(path);
    }

    // every guide that ships loads, and every topic leads somewhere
    for (const auto& e : std::filesystem::directory_iterator(root + "/data")) {
        const std::string name = e.path().filename().string();
        if (name.size() < 11 || name.compare(name.size() - 11, 11, "_guide.json") != 0) continue;
        Guide shipped;
        if (!shipped.load(e.path().string(), err)) { std::printf("  %s: %s\n", name.c_str(), err.c_str()); ++failures; continue; }
        for (const auto& t : shipped.topics) {
            GuideRun r;
            r.start(shipped, t.id);
            CHECK(r.active());
        }
    }

    std::printf(failures ? "\n%d check(s) FAILED\n" : "all guide checks passed\n", failures);
    return failures ? 1 : 0;
}
