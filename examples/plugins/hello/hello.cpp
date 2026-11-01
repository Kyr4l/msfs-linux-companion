// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kyr4l
// The example plugin: one of everything a plugin can add. Build it with cmake/CompanionPlugin.cmake (see CMakeLists.txt of this folder and
// docs/PLUGINS.md), copy hello.so to ~/.config/msfs-linux-companion/plugins/hello/ and start the program.
#include <atomic>
#include <string>

#include <imgui.h>

#include "plugin/Plugin.hpp"

namespace {

std::atomic<double> g_count{0};          // what the "count" command added up (input thread, read by the window)
std::atomic<float> g_level{0};           // the vibration level last seen by the output (haptics thread)
std::atomic<unsigned> g_inputEvents{0};  // joystick events seen (input thread)

}  // namespace

COMPANION_PLUGIN("hello", "Hello", "1.0", "Example plugin: a tab, a menu, a command, a vibration rule") {
    // a tab: anything Dear ImGui can draw, with the whole program at hand
    host.addTab("Hello", [&host] {
        lr::app::AppContext& app = host.app();
        const lr::sim::SimStatus st = app.bridge.status();
        ImGui::Text("Simulator: %s", st.state == lr::sim::ConnState::Connected ? "connected" : "not connected");
        ImGui::Text("Aircraft: %s", st.aircraft.empty() ? "-" : st.aircraft.c_str());
        ImGui::Text("Profile: %s, %zu bindings", app.settings.activeProfile.c_str(), app.settings.ensureActive().bindings.size());
        ImGui::Text("Count: %g", g_count.load());
        ImGui::Text("Vibration: %.0f %%", g_level.load() * 100.0f);
        ImGui::Text("Joystick events: %u", g_inputEvents.load());
        if (ImGui::Button("Count")) {
            // the same way a binding runs the command
            lr::app::Action a;
            a.kind = lr::app::ActionKind::Plugin;
            a.name = "hello.count";
            a.value = 1;
            app.engine.fire({a}, "hello tab");
        }
    });

    // a menu in the menu bar
    host.addMenu("Hello", [] {
        if (ImGui::MenuItem("Say hello")) LOG_INFO("hello: hello");
        if (ImGui::MenuItem("Reset count")) g_count = 0;
    });

    // a few words at the end of the status line
    host.addStatusItem([] { ImGui::Text("Hello: %g", g_count.load()); });

    // settings of its own, kept in the config file
    host.addSettingsSection("Hello", [&host] {
        nlohmann::json s = host.settings();
        bool greet = s.value("greet", true);
        if (ImGui::Checkbox("Greet at start", &greet)) {
            s["greet"] = greet;
            host.setSettings(s);
        }
    });

    // a command any binding can run: action kind "Plugin command", name "hello.count". An axis binding passes its position.
    host.addCommand("count", "Hello: count", [](double value) {
        const double add = value != 0 ? value : 1;
        g_count = g_count.load() + add;
        LOG_INFO("hello: count %g (now %g)", add, g_count.load());
    });

    // a vibration rule, listed in the Vibration tab with the built-in ones
    host.addVibrationRule("hello_high_g", "High G", "More than 1.8 G", [](const lr::sim::Telemetry& t) {
        return t.gForce > 1.8 ? static_cast<float>(std::min(1.0, (t.gForce - 1.8) / 1.5)) : 0.0f;
    });

    // something else to vibrate: here only remembered for the tab
    host.addVibrationOutput([](const lr::plugin::HapticsFrame& f) { g_level = std::max(f.left, f.right); });

    // the raw joystick events
    host.addInputListener([](const lr::input::InputEvent&) { ++g_inputEvents; });

    host.onStart([&host] {
        if (host.settings().value("greet", true)) LOG_INFO("hello: started, %zu catalogs loaded", host.app().catalogs.size());
    });
    host.onShutdown([] { LOG_INFO("hello: stopped, count %g", g_count.load()); });
    return true;
}
