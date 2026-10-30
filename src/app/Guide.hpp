// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kyr4l
#pragma once
// Step-by-step guides that work like a flowchart: a node is a question (the answer picks the next node), a list of
// steps (each pointing at a control of the aircraft's catalog), plain information, or the end. Questions can be
// answered from the simulator when it reports the state ("is the aircraft on the ground?"), and steps can show a
// tick when the simulator confirms them. Files: data/<aircraft>_guide.json, checked by tools/validate_guides.py;
// the format is described in docs/GUIDES.md.
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace lr::app {

// Something to read from the simulator: calculator code such as "(A:SIM ON GROUND, Bool)" or "(L:A32NX_X, number)", or a
// cockpit input event (B:) name. Calculator code needs the MobiFlight module; input events do not.
struct GuideProbe {
    std::string code;        // "(A:...)" / "(L:...)" calculator code (RPN is fine)
    std::string inputEvent;  // B: name without the prefix
    bool valid() const { return !code.empty() || !inputEvent.empty(); }
    bool needsMobiFlight() const { return inputEvent.empty() && !code.empty(); }
};

bool guideCompare(const std::string& op, double actual, double wanted);  // == != > < >= <=

struct GuideCondition {
    GuideProbe probe;
    std::string op = "==";
    double value = 0;
    bool valid() const { return probe.valid(); }
    // true / false when the simulator reports the value, nothing when it does not (unknown, not "false")
    std::optional<bool> evaluate(const std::function<std::optional<double>(const GuideProbe&)>& read) const;
};

struct GuideRef {
    std::string api;       // the catalog entry's api (the sim event / L: var / input event name)
    std::string function;  // optional: the entry's function text when the api alone is not unique
};

// A live value shown beside a step ("N2 right: 53.4 %"), for steps that say "wait until ...".
struct GuideReadout {
    std::string label, unit;
    GuideProbe probe;
};

struct GuideStep {
    std::string text;      // what to do
    std::string where;     // where the control is, in words
    std::string why;       // what it does / what to expect
    std::string warn;      // a caution
    std::string source;    // where this step comes from
    std::vector<GuideRef> controls;  // the controls to show (the first one is highlighted in its panel)
    GuideCondition check;  // the simulator state that shows the step is done
    std::vector<GuideReadout> readouts;
};

struct GuideChoice {
    std::string id, label, next, hint;
};

struct GuideAutoRule {
    std::string op;
    double value = 0;
    std::string choice;  // id of the choice this reading selects
};

struct GuideNode {
    std::string id, type;  // question | steps | info | end
    std::string title, text, help, source;
    std::string next;      // steps / info: the following node (empty: the guide ends)
    std::vector<GuideChoice> choices;
    GuideProbe autoProbe;  // question: what the simulator can tell
    std::vector<GuideAutoRule> autoRules;
    std::vector<GuideStep> steps;
    // the choice the simulator's reading selects, if it reports one
    std::optional<std::string> autoChoice(const std::function<std::optional<double>(const GuideProbe&)>& read) const;
};

struct GuideTopic {
    std::string id, title, summary, start;
};

struct Guide {
    std::string aircraft, title;
    std::string notice;  // what was checked against the real aircraft and what was not, shown on the topics page
    std::vector<std::string> sources;
    std::vector<GuideTopic> topics;
    std::unordered_map<std::string, GuideNode> nodes;

    bool load(const std::string& path, std::string& err);
    const GuideNode* node(const std::string& id) const;
    const GuideTopic* topic(const std::string& id) const;
    // every probe a node needs read (its auto probe and its step checks), for the watch lists
    std::vector<GuideProbe> probesOf(const GuideNode& n) const;
};

// Where the user is in a guide: the topic, the node, and the way back.
class GuideRun {
public:
    void start(const Guide& g, const std::string& topicId);
    void restart();
    bool active() const { return guide_ != nullptr && !cur_.empty(); }
    const Guide* guide() const { return guide_; }
    const GuideNode* current() const { return guide_ && !cur_.empty() ? guide_->node(cur_) : nullptr; }
    const std::string& topic() const { return topic_; }
    bool finished() const { return finished_; }
    bool choose(const std::string& choiceId);  // question: follow a choice
    bool advance();                            // steps / info: continue to the next node (or finish)
    bool back();
    bool canGoBack() const { return !history_.empty() || finished_; }
    size_t depth() const { return history_.size() + 1; }
    const std::vector<std::string>& history() const { return history_; }
    // false right after going Back to a question: the user is changing that answer, so it must not be taken from the
    // simulator again until the user moves forward
    bool autoAllowed() const { return !autoSuppressed_; }

private:
    void go(const std::string& next);
    const Guide* guide_ = nullptr;
    std::string topic_, cur_;
    std::vector<std::string> history_;
    bool finished_ = false;
    bool autoSuppressed_ = false;
};

}  // namespace lr::app
