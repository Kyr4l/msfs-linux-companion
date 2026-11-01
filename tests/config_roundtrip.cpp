// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kyr4l
// Config compatibility test: a schema-1 file (written by 0.1.x) must load without loss, saving it must
// keep a pristine backup, and the upgraded file must round-trip. Optional argv[1]: a real config to check
// (it is copied first, never modified).
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <unistd.h>
#include <fstream>
#include <limits>
#include <sstream>
#include <string>

#include <nlohmann/json.hpp>

#include "app/Profile.hpp"

namespace fs = std::filesystem;
static int failures = 0;
#define CHECK(cond)                                                              \
    do {                                                                         \
        if (!(cond)) { std::printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); ++failures; } \
    } while (0)

static std::string slurp(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

using namespace lr::app;

static void sameBindings(const Profile& a, const Profile& b) {
    CHECK(a.bindings.size() == b.bindings.size());
    for (size_t i = 0; i < a.bindings.size() && i < b.bindings.size(); ++i) {
        const Binding &x = a.bindings[i], &y = b.bindings[i];
        CHECK(x.label == y.label);
        CHECK(x.input == y.input);
        CHECK(x.enabled == y.enabled && x.repeat == y.repeat && x.repeatDelayMs == y.repeatDelayMs && x.repeatIntervalMs == y.repeatIntervalMs);
        CHECK(x.onPress.size() == y.onPress.size() && x.onRelease.size() == y.onRelease.size());
        for (size_t k = 0; k < x.onPress.size() && k < y.onPress.size(); ++k)
            CHECK(x.onPress[k].kind == y.onPress[k].kind && x.onPress[k].name == y.onPress[k].name && x.onPress[k].value == y.onPress[k].value);
        for (size_t k = 0; k < x.onRelease.size() && k < y.onRelease.size(); ++k)
            CHECK(x.onRelease[k].kind == y.onRelease[k].kind && x.onRelease[k].name == y.onRelease[k].name);
    }
}

static int versionOf(const fs::path& p) {
    try {
        nlohmann::json j;
        std::ifstream in(p);
        in >> j;
        return j.value("version", 1);
    } catch (...) {
        return 1;
    }
}

static void checkUpgrade(const fs::path& original, const fs::path& work, bool verifyFixtureValues) {
    const int from = versionOf(original);
    const std::string bakName = work.string() + ".v" + std::to_string(from) + ".bak";
    fs::remove(work);
    fs::remove(bakName);
    fs::copy_file(original, work);
    const std::string before = slurp(work);

    Settings s;
    std::string err;
    CHECK(loadSettings(work.string(), s, err));
    CHECK(s.loadedVersion == from);
    CHECK(!s.profiles.empty());
    if (from == 1) for (const auto& p : s.profiles) CHECK(p.aircraft == "A32NX");  // schema 1 profiles are A32NX profiles
    if (from >= 2 && verifyFixtureValues) CHECK(s.profiles.size() == 2 && s.profiles[1].aircraft == "A380X");

    if (verifyFixtureValues) {
        CHECK(s.profiles.size() == (from >= 2 ? 2u : 1u) && s.profiles[0].bindings.size() == 2);
        const Binding& b0 = s.profiles[0].bindings[0];
        CHECK(b0.label == "APU master" && b0.onPress.size() == 1 && b0.onPress[0].kind == ActionKind::LVarToggle);
        const Binding& b1 = s.profiles[0].bindings[1];
        CHECK(b1.input.kind == lr::input::InputKind::Hat && b1.input.dir == 1 && b1.repeat && !b1.enabled);
        CHECK(b1.repeatDelayMs == 300 && b1.repeatIntervalMs == 80 && b1.onRelease.size() == 1);
        CHECK(s.haptics.enabled && s.haptics.master > 0.79f && s.haptics.master < 0.81f);
        CHECK(s.haptics.rules.count("stall") && !s.haptics.rules["stall"].enabled);
        auto it = s.haptics.devices.find("4098:b970");
        CHECK(it != s.haptics.devices.end());
        if (it != s.haptics.devices.end()) CHECK(it->second.identity.left.device == 4 && it->second.identity.left.index == 14 && it->second.identity.right.device == 8 && it->second.identity.right.index == 16);
    }

    // save (the upgrade): backup must appear, byte-identical to what was on disk
    CHECK(saveSettings(work.string(), s, err));
    const std::string bak = bakName;
    CHECK(fs::exists(bak));
    CHECK(slurp(bak) == before);

    Settings t;
    CHECK(loadSettings(work.string(), t, err));
    CHECK(t.loadedVersion == kConfigVersion);
    CHECK(t.profiles.size() == s.profiles.size());
    for (size_t i = 0; i < s.profiles.size() && i < t.profiles.size(); ++i) {
        CHECK(t.profiles[i].name == s.profiles[i].name && t.profiles[i].aircraft == s.profiles[i].aircraft);
        sameBindings(s.profiles[i], t.profiles[i]);
    }
    CHECK(t.activeProfile == s.activeProfile && t.host == s.host && t.port == s.port && t.autoConnect == s.autoConnect);
    CHECK(t.guideAutodetect == s.guideAutodetect);

    // second save must not overwrite the backup with the new format
    t.profiles[0].aircraft = "A380X";
    CHECK(saveSettings(work.string(), t, err));
    CHECK(slurp(bak) == before);
    Settings u;
    CHECK(loadSettings(work.string(), u, err));
    CHECK(u.profiles[0].aircraft == "A380X");
    sameBindings(s.profiles[0], u.profiles[0]);
}

int main(int argc, char** argv) {
    const fs::path tmp = fs::temp_directory_path() / "msfs-linux-companion-config-test";
    fs::create_directories(tmp);

    std::printf("fixture: schema 1 -> %d upgrade\n", kConfigVersion);
    checkUpgrade(fs::path(COMPANION_TEST_FIXTURES) / "config_v1.json", tmp / "fixture1.json", true);
    std::printf("fixture: schema 2 -> %d upgrade\n", kConfigVersion);
    checkUpgrade(fs::path(COMPANION_TEST_FIXTURES) / "config_v2.json", tmp / "fixture2.json", true);

    if (argc > 1) {
        std::printf("real config copy: %s\n", argv[1]);
        checkUpgrade(argv[1], tmp / "real.json", false);
    }

    std::printf("future schema + unknown fields load without error\n");
    {
        const fs::path f = tmp / "future.json";
        std::ofstream(f) << R"({"version": 99, "mystery": [1,2,3], "profiles": [{"name": "X", "aircraft": "B787", "extra": true, "bindings": []}]})";
        Settings s;
        std::string err;
        CHECK(loadSettings(f.string(), s, err));
        CHECK(s.profiles.size() == 1 && s.profiles[0].aircraft == "B787");
    }

    std::printf("an action of an unknown kind is left out, not turned into a sim event\n");
    {
        const fs::path f = tmp / "unknown_kind.json";
        std::ofstream(f) << R"({"version": 4, "profiles": [{"name": "X", "aircraft": "A32NX", "bindings": [{"label": "b", "input": {"device": "d", "kind": "button", "code": 1},
            "on_press": [{"kind": "key_press", "name": "SPACE"}, {"kind": "sim_event", "name": "GEAR_UP"}]}]}]})";
        Settings s;
        std::string err;
        CHECK(loadSettings(f.string(), s, err));
        CHECK(s.profiles.size() == 1 && s.profiles[0].bindings.size() == 1 && s.profiles[0].bindings[0].onPress.size() == 1);
        if (s.profiles[0].bindings[0].onPress.size() == 1) CHECK(s.profiles[0].bindings[0].onPress[0].name == "GEAR_UP");
    }

    std::printf("a number that is not finite does not make the saved file unreadable\n");
    {
        Settings s;
        Profile p;
        p.name = "P";
        Binding b;
        b.input = {"dev", lr::input::InputKind::Axis, 3, 0};
        Action a;
        a.name = "THROTTLE_SET";
        a.value = std::numeric_limits<double>::infinity();
        b.onPress.push_back(a);
        b.axis.outMax = std::numeric_limits<double>::quiet_NaN();
        p.bindings.push_back(b);
        s.profiles.push_back(p);
        s.activeProfile = "P";
        std::string err;
        const fs::path f = tmp / "nonfinite.json";
        CHECK(saveSettings(f.string(), s, err));
        Settings t;
        CHECK(loadSettings(f.string(), t, err));
        CHECK(t.profiles.size() == 1 && t.profiles[0].bindings.size() == 1 && t.profiles[0].bindings[0].onPress[0].value == 0.0 && t.profiles[0].bindings[0].axis.outMax == 0.0);
    }

    std::printf("the settings of the gates stay in the file while the gates are off\n");
    {
        Settings s;
        Profile p;
        p.name = "P";
        Binding b;
        b.input = {"dev", lr::input::InputKind::Axis, 3, 0};
        b.axis.detents.enabled = false;
        b.axis.detents.outIdle = 0.25;
        Action a;
        a.name = "STOP";
        b.onCutoff.push_back(a);
        p.bindings.push_back(b);
        Binding plain;
        plain.input = {"dev", lr::input::InputKind::Axis, 4, 0};
        p.bindings.push_back(plain);
        s.profiles.push_back(p);
        s.activeProfile = "P";
        std::string err;
        const fs::path f = tmp / "gates_off.json";
        CHECK(saveSettings(f.string(), s, err));
        Settings t;
        CHECK(loadSettings(f.string(), t, err));
        CHECK(t.profiles.size() == 1 && t.profiles[0].bindings.size() == 2);
        if (t.profiles[0].bindings.size() == 2) {
            CHECK(!t.profiles[0].bindings[0].axis.detents.enabled && t.profiles[0].bindings[0].axis.detents.outIdle == 0.25);
            CHECK(t.profiles[0].bindings[0].onCutoff.size() == 1 && t.profiles[0].bindings[0].onCutoff[0].name == "STOP");
        }
        // a plain axis is written as before: no gate settings of its own
        nlohmann::json j;
        std::ifstream in(f);
        in >> j;
        CHECK(!j["profiles"][0]["bindings"][1]["axis"].contains("detents") && !j["profiles"][0]["bindings"][1].contains("zone_actions"));
    }

    std::printf("a port outside 0..65535 is clamped, a config path without a directory can be saved\n");
    {
        const fs::path f = tmp / "port.json";
        std::ofstream(f) << R"({"version": 4, "port": 70000, "profiles": [{"name": "X", "aircraft": "A32NX", "bindings": []}]})";
        Settings s;
        std::string err;
        CHECK(loadSettings(f.string(), s, err));
        CHECK(s.port == 65535);
        const fs::path before = fs::current_path();
        fs::current_path(tmp);
        CHECK(saveSettings("no-directory-part.json", s, err));
        CHECK(fs::exists(tmp / "no-directory-part.json") && !fs::exists(tmp / "no-directory-part.json.tmp"));
        fs::current_path(before);
    }

    std::printf("the engine is given a profile again only when something it uses has changed\n");
    {
        Profile p;
        p.name = "P";
        Binding b;
        b.input = {"dev", lr::input::InputKind::Button, 1, 0};
        Action a;
        a.name = "GEAR_UP";
        b.onPress.push_back(a);
        p.bindings.push_back(b);
        const std::vector<AxisCalibration> none;
        const std::string first = engineFingerprint(p, none);
        p.bindings[0].group = "Gear";  // only the Bindings tab looks at it
        CHECK(engineFingerprint(p, none) == first);
        p.bindings[0].onPress[0].name = "GEAR_DOWN";
        CHECK(engineFingerprint(p, none) != first);
        std::vector<AxisCalibration> cals(1);
        CHECK(engineFingerprint(p, cals) != engineFingerprint(p, none));
    }

    {   // the plugins' part of the config: which are off, and each one's own settings, unchanged through a save
        const fs::path work = fs::temp_directory_path() / ("plugins_roundtrip_" + std::to_string(getpid()) + ".json");
        Settings s;
        s.profiles.push_back(Profile{});
        s.disabledPlugins = {"hello"};
        s.pluginSettings["hello"] = R"({"greet":false,"list":[1,2]})";
        std::string err;
        CHECK(saveSettings(work.string(), s, err));
        Settings t;
        CHECK(loadSettings(work.string(), t, err));
        CHECK(t.disabledPlugins == s.disabledPlugins);
        CHECK(t.pluginSettings.size() == 1 && t.pluginSettings["hello"] == R"({"greet":false,"list":[1,2]})");
        Settings none;
        none.profiles.push_back(Profile{});
        CHECK(saveSettings(work.string(), none, err));
        CHECK(slurp(work).find("\"plugins\"") == std::string::npos);  // a config without plugins is written as before
        fs::remove(work);
    }

    std::printf(failures ? "\n%d check(s) FAILED\n" : "\nall checks passed\n", failures);
    return failures ? 1 : 0;
}
