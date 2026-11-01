// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kyr4l
#pragma once
// The plugin interface: what a plugin (a shared library, see docs/PLUGINS.md) is given when it is loaded.
//
// A plugin is C++ built against the headers of this program, and it is loaded into the program itself: it can call anything the program
// has (host.app() is the whole application) and draw with Dear ImGui like the built-in tabs do. The Host calls below are the convenient part
// of that: they hook the plugin into the places where the program would otherwise have to be changed (a tab, a menu, a command a binding
// can run, a vibration rule ...).
//
// Rules:
//   * Everything is registered from companion_plugin_load(). Registering later is refused (and logged).
//   * Each callback says on which thread it runs. Draw callbacks run on the window thread between ImGui::NewFrame() and ImGui::Render().
//   * The plugin is built with the same compiler, the same Dear ImGui (third_party/imgui) and the same version of this program. The program
//     refuses a plugin built for another plugin API or another Dear ImGui, and warns about one built for another version of the program.
//   * A plugin is never unloaded while the program runs: callbacks stay valid until the shutdown hooks have run.
#include <functional>
#include <string>

#include <imgui.h>
#include <nlohmann/json.hpp>

#include "input/Evdev.hpp"
#include "sim/SimBridge.hpp"
#include "ui/Ui.hpp"
#include "util/Log.hpp"

namespace lr::plugin {

// Raised whenever Host, HapticsFrame or the way plugins are loaded changes in a way an already built plugin would not survive.
constexpr int kApiVersion = 1;

struct Info {
    int api = kApiVersion;
    int imgui = IMGUI_VERSION_NUM;
    const char* builtFor = "";  // the version of this program the plugin was built against (COMPANION_VERSION)
    const char* id = "";        // short and stable: names the plugin's settings, its commands ("<id>.<command>") and its log lines
    const char* name = "";      // shown in Settings > Plugins
    const char* version = "";
    const char* description = "";
};

// What the vibration engine computed, handed to every vibration output about 30 times a second while a flight is under way (4 times a
// second otherwise). The flight data comes whether vibration is on or not; the levels are 0..1, after the master strength, and both 0 (as
// is every rule's level) while vibration is off.
struct HapticsFrame {
    bool enabled = false;  // Vibration > Enabled
    bool flying = false;   // the sim says the user is flying (not a menu, not paused)
    bool fresh = false;    // flight data is arriving
    float left = 0, right = 0;
    const std::map<std::string, float>* contrib = nullptr;  // level of each rule, by rule id
    const sim::Telemetry* telemetry = nullptr;
};

class Host {
public:
    virtual ~Host() = default;

    virtual const std::string& id() const = 0;
    virtual const std::string& folder() const = 0;  // where the plugin's library is; its data/ folder is loaded like the program's own data
    virtual app::AppContext& app() = 0;             // everything: the bridge to the sim, the binding engine, catalogs, guides, settings ...

    // The plugin's own settings, kept in the config file ("plugins" > "settings" > id). Window thread only.
    virtual nlohmann::json settings() const = 0;
    virtual void setSettings(const nlohmann::json& value) = 0;  // saved with the rest of the config a moment later

    // ---- window (all called on the window thread)
    virtual void addTab(const std::string& name, std::function<void()> draw) = 0;                // a tab of its own (also selectable with --tab NAME)
    virtual void addMenu(const std::string& name, std::function<void()> draw) = 0;               // a menu in the menu bar; draw() adds the items
    virtual void addStatusItem(std::function<void()> draw) = 0;                                  // a few words at the end of the status line
    virtual void addSettingsSection(const std::string& title, std::function<void()> draw) = 0;   // a section of its own in the Settings tab
    virtual void addWindow(std::function<void()> draw) = 0;                                      // every frame after the main window: own windows, popups

    // ---- program
    // A command a binding can run: the action kind "Plugin" with the name "<id>.<name>". value is the action's value (an axis binding passes
    // its position, scaled to the binding's output range). Runs on the input thread (or the window thread for a Test button): keep it short.
    virtual void addCommand(const std::string& name, const std::string& label, std::function<void(double value)> run) = 0;
    // Raw joystick events, before the bindings see them. Input thread.
    virtual void addInputListener(std::function<void(const input::InputEvent&)> listener) = 0;
    // A vibration rule like the built-in ones (listed in the Vibration tab, with its own switch and gain): flight data -> level 0..1.
    // Haptics thread; only called while vibration is on and the user is flying.
    virtual void addVibrationRule(const std::string& id, const std::string& label, const std::string& description,
                                  std::function<float(const sim::Telemetry&)> level) = 0;
    // Something else to vibrate, or anything that wants the flight data: gets every frame of the vibration engine (see HapticsFrame). Haptics thread.
    virtual void addVibrationOutput(std::function<void(const HapticsFrame&)> output) = 0;
    // After every plugin is loaded, the data files are read and the threads run, before the window opens. Main thread.
    virtual void onStart(std::function<void()> fn) = 0;
    // When the program ends, after the input and haptics threads stopped, before the bridge to the sim stops. Main thread.
    virtual void onShutdown(std::function<void()> fn) = 0;
};

}  // namespace lr::plugin

// The two functions a plugin library exports: companion_plugin_info() and companion_plugin_load(). COMPANION_PLUGIN writes the first one
// and starts the second, whose body follows:
//
//   COMPANION_PLUGIN("hello", "Hello", "1.0", "Says hello") {
//       host.addTab("Hello", [] { ImGui::TextUnformatted("Hello"); });
//       return true;
//   }
//
// The load function returns false (or throws) to say it could not start; whatever it registered is then dropped.
#ifndef COMPANION_VERSION
#define COMPANION_VERSION ""
#endif
#define COMPANION_PLUGIN(ID, NAME, VERSION, DESCRIPTION)                                                             \
    extern "C" __attribute__((visibility("default"))) const lr::plugin::Info* companion_plugin_info() {             \
        static const lr::plugin::Info info{lr::plugin::kApiVersion, IMGUI_VERSION_NUM, COMPANION_VERSION, ID, NAME,   \
                                           VERSION, DESCRIPTION};                                                     \
        return &info;                                                                                                 \
    }                                                                                                                 \
    extern "C" __attribute__((visibility("default"))) bool companion_plugin_load(lr::plugin::Host& host)
