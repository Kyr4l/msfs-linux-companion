// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kyr4l
// Guides: loading, the flow (questions, steps, back, finishing), answers taken from the simulator, step checks, and
// that every shipped guide file loads, and the keypad drawings the guides' `keys` point at.
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>

#include "app/Guide.hpp"
#include "app/Keypad.hpp"

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
        const auto path = (std::filesystem::temp_directory_path() / "msfs-linux-companion-bad-guide.json").string();
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

    // keypads: the A32NX MCDU, and every key a shipped guide names exists on the keypad the guide points at
    {
        CHECK(splitKeyChoices("L1").size() == 1 && splitKeyChoices("LARROW/RARROW").size() == 2 && splitKeyChoices("A/B/C")[2] == "C" && splitKeyChoices("").empty());
        KeypadSet pads;
        CHECK(pads.load(root + "/data/a32nx_keypads.json", err));
        const Keypad* mcdu = pads.find("a32nx_mcdu");
        CHECK(mcdu && mcdu->keys.size() == 70 && mcdu->width > 100 && mcdu->height > 100 && !mcdu->hEventPrefix.empty());
        if (mcdu) {
            // the line select keys run down both sides of the screen, the page keys sit under it, the digits under the page keys
            const KeypadKey *l1 = mcdu->key("L1"), *l6 = mcdu->key("L6"), *r1 = mcdu->key("R1"), *dir = mcdu->key("DIR"), *init = mcdu->key("INIT"), *n5 = mcdu->key("5");
            CHECK(l1 && l6 && r1 && dir && init && n5);
            if (l1 && l6 && r1 && dir && init && n5) {
                CHECK(l1->y < l6->y && l1->x < r1->x && std::fabs(l1->y - r1->y) < 1.0f);
                CHECK(dir->x < init->x && l6->y < dir->y && dir->y < n5->y);
            }
            CHECK(!mcdu->key("NOT_A_KEY"));
            for (const auto& k : mcdu->keys) CHECK(k.w > 5 && k.h > 3 && k.x >= 0 && k.y >= 0 && k.x + k.w <= mcdu->width + 0.2f && k.y + k.h <= mcdu->height + 0.2f);
        }
        Guide a32;
        CHECK(a32.load(root + "/data/a32nx_guide.json", err) && a32.keypad == "a32nx_mcdu" && pads.find(a32.keypad));
        const GuideNode* init = a32.node("fp_init");
        CHECK(init && init->steps.size() > 1 && init->steps[0].keys.size() == 1 && init->steps[0].keys[0] == "INIT" && init->steps[1].keys[0] == "R1");
        size_t tagged = 0;
        for (const auto& [id, node] : a32.nodes)
            for (const auto& st : node.steps)
                for (const auto& entry : st.keys) {
                    ++tagged;
                    for (const auto& id2 : splitKeyChoices(entry))
                        if (!mcdu || !mcdu->key(id2)) { std::printf("  %s: key %s is not on the keypad\n", id.c_str(), id2.c_str()); ++failures; }
                }
        CHECK(tagged > 50);
        KeypadSet bad;
        CHECK(!bad.load(root + "/data/does_not_exist_keypads.json", err));
    }

    std::printf(failures ? "\n%d check(s) FAILED\n" : "all guide checks passed\n", failures);
    return failures ? 1 : 0;
}
