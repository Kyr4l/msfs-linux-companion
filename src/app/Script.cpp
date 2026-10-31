// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kyr4l
#include "app/Script.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <thread>

namespace lr::app {

namespace {
using clock = std::chrono::steady_clock;

struct Line {
    std::string cmd;
    std::vector<std::string> args;
    std::string expr;  // calculator code (with its parentheses) for code / expect
    int number = 0;
};

std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r");
    size_t b = s.find_last_not_of(" \t\r");
    return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}

// "expect (A:LIGHT NAV, Bool) == 1 800": pulls the parenthesised expression out, splits the rest on spaces
bool parseLine(const std::string& raw, Line& out, std::string& err) {
    std::string s = trim(raw);
    if (s.empty() || s[0] == '#') return false;
    size_t sp = s.find_first_of(" \t");
    out.cmd = sp == std::string::npos ? s : s.substr(0, sp);
    std::string rest = sp == std::string::npos ? std::string() : trim(s.substr(sp));
    if (out.cmd == "code" || out.cmd == "expect") {
        if (rest.empty() || rest[0] != '(') { err = "expected calculator code in parentheses"; return false; }
        // the code may be RPN with several groups ("(A:X, Bool) (A:Y, Bool) +"): for expect it ends before the trailing
        // "OP VALUE [MS]" tokens, for code it is the whole rest of the line
        std::vector<std::string> tok;
        {
            std::istringstream ss(rest);
            std::string t;
            while (ss >> t) tok.push_back(t);
        }
        auto isNumber = [](const std::string& t) { char* e = nullptr; std::strtod(t.c_str(), &e); return !t.empty() && e && *e == 0; };
        auto isOp = [](const std::string& t) { return t == "==" || t == "!=" || t == ">" || t == "<" || t == ">=" || t == "<="; };
        size_t keep = tok.size();
        if (out.cmd == "expect") {
            if (keep >= 4 && isNumber(tok[keep - 1]) && isNumber(tok[keep - 2]) && isOp(tok[keep - 3])) keep -= 3;
            else if (keep >= 3 && isNumber(tok[keep - 1]) && isOp(tok[keep - 2])) keep -= 2;
            else { err = "expect needs: CODE OP VALUE [MS]"; return false; }
            out.args.assign(tok.begin() + static_cast<long>(keep), tok.end());
        }
        int depth = 0;
        for (const char c : rest) {
            if (c == '(') ++depth;
            else if (c == ')') --depth;
        }
        for (size_t i = 0; i < keep; ++i) out.expr += (i ? " " : "") + tok[i];
        if (out.expr.empty() || (out.expr.back() != ')' && out.expr.back() != '+' && out.expr.back() != '-' && out.expr.back() != '*' && out.expr.back() != '/' && !std::isalnum(static_cast<unsigned char>(out.expr.back())))) {
            err = "bad calculator code";
            return false;
        }
        if (depth != 0) { err = "unbalanced parentheses"; return false; }
        return true;
    }
    if (out.cmd == "rewatch") { out.expr = rest; return true; }  // codes separated by " | "
    std::istringstream ss(rest);
    std::string tok;
    while (ss >> tok) out.args.push_back(tok);
    return true;
}

bool compare(double v, const std::string& op, double ref) {
    const double eps = 1e-6 + 1e-9 * std::fabs(ref);
    if (op == "==") return std::fabs(v - ref) <= std::max(eps, 0.0005);
    if (op == "!=") return std::fabs(v - ref) > std::max(eps, 0.0005);
    if (op == ">") return v > ref;
    if (op == "<") return v < ref;
    if (op == ">=") return v >= ref - eps;
    if (op == "<=") return v <= ref + eps;
    return false;
}

std::string stripB(std::string n) { return n.rfind("B:", 0) == 0 ? n.substr(2) : n; }
}  // namespace

ScriptNeeds scanScript(const std::string& path) {
    ScriptNeeds needs;
    std::ifstream in(path);
    if (!in) { needs.error = "cannot open " + path; return needs; }
    std::string raw;
    int number = 0;
    while (std::getline(in, raw)) {
        ++number;
        Line l;
        std::string err;
        if (!parseLine(raw, l, err)) {
            if (!err.empty()) { needs.error = "line " + std::to_string(number) + ": " + err; return needs; }
            continue;
        }
        static const char* known[] = {"set", "toggle", "press", "event", "get", "code", "expect", "expectb", "sleep", "rewatch"};
        if (std::find_if(std::begin(known), std::end(known), [&](const char* k) { return l.cmd == k; }) == std::end(known)) {
            needs.error = "line " + std::to_string(number) + ": unknown command '" + l.cmd + "'";
            return needs;
        }
        if (l.cmd == "code" || l.cmd == "expect") {
            needs.calculatorCode = true;
            if (std::find(needs.codes.begin(), needs.codes.end(), l.expr) == needs.codes.end()) needs.codes.push_back(l.expr);
        }
        if (l.cmd == "set" || l.cmd == "toggle" || l.cmd == "press" || l.cmd == "get" || l.cmd == "expectb") {
            if (l.args.empty()) { needs.error = "line " + std::to_string(number) + ": missing event name"; return needs; }
            const std::string n = stripB(l.args[0]);
            if (std::find(needs.inputEvents.begin(), needs.inputEvents.end(), n) == needs.inputEvents.end()) needs.inputEvents.push_back(n);
        }
    }
    needs.ok = true;
    return needs;
}

int runScript(sim::SimBridge& bridge, const std::string& path) {
    const ScriptNeeds needs = scanScript(path);
    if (!needs.ok) { std::fprintf(stderr, "script: %s\n", needs.error.c_str()); return -1; }
    bridge.watchLVars(needs.codes);
    bridge.watchInputEvents(needs.inputEvents);
    // let the first values arrive so that "get" and the first expectation are not racing the registration
    for (int i = 0; i < 40; ++i) {
        bool all = true;
        for (const auto& c : needs.codes) all &= bridge.lvarValue(c).has_value();
        for (const auto& n : needs.inputEvents) all &= bridge.inputEventValue(n).has_value();
        if (all) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    std::ifstream in(path);
    std::string raw;
    int number = 0, failures = 0, checks = 0;
    while (std::getline(in, raw)) {
        ++number;
        Line l;
        std::string err;
        if (!parseLine(raw, l, err)) continue;
        auto arg = [&](size_t i) { return i < l.args.size() ? l.args[i] : std::string(); };
        auto num = [&](size_t i, double dflt) { return i < l.args.size() ? std::atof(l.args[i].c_str()) : dflt; };
        if (l.cmd == "set") {
            bridge.setInputEvent(stripB(arg(0)), num(1, 0), "script");
        } else if (l.cmd == "toggle") {
            bridge.toggleInputEvent(stripB(arg(0)), static_cast<int>(num(1, 2)), "script");
        } else if (l.cmd == "press") {
            bridge.setInputEvent(stripB(arg(0)), 1, "script");
        } else if (l.cmd == "event") {
            bridge.sendEvent(arg(0), num(1, 0), "script");
        } else if (l.cmd == "rewatch") {
            // swap the whole watch set while connected, the way the guides do when a page changes, and report what arrives
            std::vector<std::string> codes;
            for (size_t pos = 0; pos <= l.expr.size();) {
                size_t bar = l.expr.find(" | ", pos);
                if (bar == std::string::npos) bar = l.expr.size();
                const std::string c = trim(l.expr.substr(pos, bar - pos));
                if (!c.empty()) codes.push_back(c);
                pos = bar + 3;
            }
            const auto t0 = std::chrono::steady_clock::now();
            bridge.watchLVars(codes);
            size_t got = 0;
            for (int i = 0; i < 40; ++i) {
                got = 0;
                for (const auto& c : codes) got += bridge.lvarValue(c).has_value();
                if (got == codes.size()) break;
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
            std::printf("rewatch: %zu of %zu value(s) after %lld ms\n", got, codes.size(), static_cast<long long>(ms));
            for (const auto& c : codes) {
                const auto v = bridge.lvarValue(c);
                if (v) std::printf("  %s = %g\n", c.c_str(), *v); else std::printf("  %s = (none)\n", c.c_str());
            }
        } else if (l.cmd == "sleep") {
            std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<int>(num(0, 100))));
        } else if (l.cmd == "get" || l.cmd == "code") {
            std::this_thread::sleep_for(std::chrono::milliseconds(700));  // the value is refreshed at about 2 Hz
            const std::string key = l.cmd == "get" ? stripB(arg(0)) : l.expr;
            auto v = l.cmd == "get" ? bridge.inputEventValue(key) : bridge.lvarValue(key);
            if (v) std::printf("%s = %g\n", l.cmd == "get" ? ("B:" + key).c_str() : key.c_str(), *v);
            else std::printf("%s = (no value)\n", l.cmd == "get" ? ("B:" + key).c_str() : key.c_str());
        } else if (l.cmd == "expect" || l.cmd == "expectb") {
            const bool isCode = l.cmd == "expect";
            const std::string key = isCode ? l.expr : stripB(arg(0));
            const std::string op = arg(isCode ? 0 : 1);
            const double ref = num(isCode ? 1 : 2, 0);
            const int timeout = static_cast<int>(num(isCode ? 2 : 3, 1500));
            const auto deadline = clock::now() + std::chrono::milliseconds(timeout);
            std::optional<double> v;
            bool good = false;
            do {
                std::this_thread::sleep_for(std::chrono::milliseconds(120));
                v = isCode ? bridge.lvarValue(key) : bridge.inputEventValue(key);
                good = v && compare(*v, op, ref);
            } while (!good && clock::now() < deadline);
            ++checks;
            std::string label = isCode ? key : "B:" + key;
            if (good) std::printf("ok:   %s %s %g (is %g)\n", label.c_str(), op.c_str(), ref, *v);
            else { ++failures; std::printf("FAIL: %s %s %g (is %s)  [line %d]\n", label.c_str(), op.c_str(), ref, v ? std::to_string(*v).c_str() : "unknown", number); }
        }
    }
    std::printf("%d check(s), %d failed\n", checks, failures);
    return failures;
}

}  // namespace lr::app
