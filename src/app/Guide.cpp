// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kyr4l
#include "app/Guide.hpp"

#include <cmath>
#include <fstream>

#include <nlohmann/json.hpp>

namespace lr::app {

std::vector<std::string> GuideProbe::inputEvents() const {
    std::vector<std::string> out;
    size_t start = 0;
    while (start <= inputEvent.size()) {
        const size_t bar = inputEvent.find('|', start);
        const std::string one = inputEvent.substr(start, bar == std::string::npos ? std::string::npos : bar - start);
        if (!one.empty()) out.push_back(one);
        if (bar == std::string::npos) break;
        start = bar + 1;
    }
    return out;
}


bool guideCompare(const std::string& op, double a, double b) {
    if (op == "==") return std::fabs(a - b) < 0.5e-3 + 1e-9 * std::fabs(b);
    if (op == "!=") return !guideCompare("==", a, b);
    if (op == ">") return a > b;
    if (op == "<") return a < b;
    if (op == ">=") return a >= b;
    if (op == "<=") return a <= b;
    return false;
}

std::optional<bool> GuideCondition::evaluate(const std::function<std::optional<double>(const GuideProbe&)>& read) const {
    if (!valid()) return std::nullopt;
    const auto v = read(probe);
    if (!v) return std::nullopt;
    return guideCompare(op, *v, value);
}

std::optional<std::string> GuideNode::autoChoice(const std::function<std::optional<double>(const GuideProbe&)>& read) const {
    if (!autoProbe.valid()) return std::nullopt;
    const auto v = read(autoProbe);
    if (!v) return std::nullopt;
    for (const auto& r : autoRules)
        if (guideCompare(r.op, *v, r.value)) return r.choice;
    return std::nullopt;
}

namespace {
using json = nlohmann::json;

GuideProbe probeFrom(const json& j) {
    GuideProbe p;
    p.code = j.value("code", "");
    p.inputEvent = j.value("input_event", "");
    return p;
}

GuideStep stepFrom(const json& j) {
    GuideStep s;
    s.text = j.value("text", "");
    s.where = j.value("where", "");
    s.why = j.value("why", "");
    s.warn = j.value("warn", "");
    s.source = j.value("source", "");
    for (const auto& c : j.value("controls", json::array())) s.controls.push_back({c.value("api", ""), c.value("function", "")});
    for (const auto& k : j.value("keys", json::array())) s.keys.push_back(k.get<std::string>());
    if (j.contains("check") && j["check"].is_object()) {
        const auto& c = j["check"];
        s.check.probe = probeFrom(c);
        s.check.op = c.value("op", "==");
        s.check.value = c.value("value", 0.0);
    }
    for (const auto& r : j.value("readouts", json::array())) {
        GuideReadout o;
        o.label = r.value("label", "");
        o.unit = r.value("unit", "");
        o.probe = probeFrom(r);
        s.readouts.push_back(std::move(o));
    }
    return s;
}

GuideNode nodeFrom(const std::string& id, const json& j) {
    GuideNode n;
    n.id = id;
    n.type = j.value("type", "info");
    n.title = j.value("title", "");
    n.text = j.value("text", "");
    n.help = j.value("help", "");
    n.source = j.value("source", "");
    n.next = j.value("next", "");
    for (const auto& c : j.value("choices", json::array()))
        n.choices.push_back({c.value("id", ""), c.value("label", ""), c.value("next", ""), c.value("hint", "")});
    if (j.contains("auto") && j["auto"].is_object()) {
        n.autoProbe = probeFrom(j["auto"]);
        for (const auto& r : j["auto"].value("rules", json::array()))
            n.autoRules.push_back({r.value("op", "=="), r.value("value", 0.0), r.value("choice", "")});
    }
    for (const auto& s : j.value("steps", json::array())) n.steps.push_back(stepFrom(s));
    return n;
}
}  // namespace

bool Guide::load(const std::string& path, std::string& err) {
    std::ifstream in(path);
    if (!in) { err = "cannot open " + path; return false; }
    try {
        json j;
        in >> j;
        aircraft = j.value("aircraft", "");
        title = j.value("title", "");
        keypad = j.value("keypad", "");
        notice = j.value("notice", "");
        sources.clear();
        for (const auto& s : j.value("sources", json::array())) sources.push_back(s.get<std::string>());
        topics.clear();
        for (const auto& t : j.value("topics", json::array()))
            topics.push_back({t.value("id", ""), t.value("title", ""), t.value("summary", ""), t.value("start", "")});
        nodes.clear();
        if (j.contains("nodes") && j["nodes"].is_object())
            for (auto it = j["nodes"].begin(); it != j["nodes"].end(); ++it) nodes[it.key()] = nodeFrom(it.key(), it.value());
    } catch (const std::exception& e) {
        err = std::string("parse error: ") + e.what();
        return false;
    }
    if (aircraft.empty() || topics.empty()) { err = "no aircraft or no topics"; return false; }
    for (const auto& t : topics)
        if (!node(t.start)) { err = "topic " + t.id + " starts at the unknown node " + t.start; return false; }
    return true;
}

const GuideNode* Guide::node(const std::string& id) const {
    auto it = nodes.find(id);
    return it == nodes.end() ? nullptr : &it->second;
}

const GuideTopic* Guide::topic(const std::string& id) const {
    for (const auto& t : topics)
        if (t.id == id) return &t;
    return nullptr;
}

std::vector<GuideProbe> Guide::probesOf(const GuideNode& n) const {
    std::vector<GuideProbe> out;
    if (n.autoProbe.valid()) out.push_back(n.autoProbe);
    for (const auto& s : n.steps) {
        if (s.check.valid()) out.push_back(s.check.probe);
        for (const auto& r : s.readouts) out.push_back(r.probe);
    }
    return out;
}

void GuideRun::start(const Guide& g, const std::string& topicId) {
    guide_ = &g;
    topic_ = topicId;
    restart();
}

void GuideRun::restart() {
    autoSuppressed_ = false;
    history_.clear();
    finished_ = false;
    cur_.clear();
    const GuideTopic* t = guide_ ? guide_->topic(topic_) : nullptr;
    if (t && guide_->node(t->start)) cur_ = t->start;
}

void GuideRun::go(const std::string& next) {
    autoSuppressed_ = false;
    history_.push_back(cur_);
    if (next.empty() || !guide_->node(next)) { finished_ = true; return; }
    cur_ = next;
}

bool GuideRun::choose(const std::string& choiceId) {
    const GuideNode* n = current();
    if (!n || finished_ || n->type != "question") return false;
    for (const auto& c : n->choices)
        if (c.id == choiceId) { go(c.next); return true; }
    return false;
}

bool GuideRun::advance() {
    const GuideNode* n = current();
    if (!n || finished_ || n->type == "question") return false;
    go(n->type == "end" ? std::string() : n->next);
    return true;
}

bool GuideRun::back() {
    if (finished_) {  // go() recorded the node the guide ended on; that node is still the current one
        finished_ = false;
        if (!history_.empty()) history_.pop_back();
        autoSuppressed_ = current() && current()->type == "question";
        return true;
    }
    if (history_.empty()) return false;
    cur_ = history_.back();
    history_.pop_back();
    autoSuppressed_ = current() && current()->type == "question";
    return true;
}

}  // namespace lr::app
