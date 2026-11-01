// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kyr4l
#pragma once
// Finds the plugins, loads them and keeps what they registered (see Plugin.hpp for the plugin's side, docs/PLUGINS.md for the folders).
//
// A plugin is a folder (or a single .so file). In the folder: at most one library (*.so) exporting companion_plugin_info() and
// companion_plugin_load(), and / or a data/ folder with catalogs, guides and keypads, read like the program's own data. A folder with only
// data/ is a plugin too (an aircraft without code); its id is the folder name, or "id" in an optional plugin.json.
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "plugin/Plugin.hpp"

namespace lr::plugin {

enum class PluginState { Loaded, Disabled, Failed };

struct PluginRecord {
    std::string id, name, version, description, builtFor;
    std::string folder;   // the plugin's folder
    std::string library;  // the .so, or empty for a data-only plugin
    bool hasData = false;
    PluginState state = PluginState::Failed;
    std::string error;    // why it failed
};

struct Hook {
    std::string plugin;  // id of the plugin that registered it
    std::string name;    // tab / menu / section title, or a command's full name "<id>.<name>"
    std::string label;   // commands
    std::function<void()> draw;
    mutable bool broken = false;  // it threw: not called again
};

struct Command {
    std::string plugin, name, label;
    std::function<void(double)> run;
};

class PluginManager {
public:
    PluginManager();
    ~PluginManager();

    // Looks for plugins in the user's folder ($XDG_CONFIG_HOME/msfs-linux-companion/plugins), the installed one
    // (<prefix>/lib/msfs-linux-companion/plugins) and the folders of MSFS_LINUX_COMPANION_PLUGIN_PATH (colon separated), plus `extra`
    // (--plugin). With `scanDefault` false (--no-plugins, or a test run) only the last two. Each one is loaded unless its id is in
    // ctx.settings.disabledPlugins. Call once, before the threads start.
    void load(app::AppContext& ctx, const std::vector<std::string>& extra, bool scanDefault);
    std::vector<std::string> dataDirs() const;  // the data/ folders of the loaded plugins
    void start();     // the onStart hooks
    void shutdown();  // the onShutdown hooks, last loaded first (once)

    const std::vector<PluginRecord>& plugins() const { return records_; }
    const std::vector<Hook>& tabs() const { return tabs_; }
    const std::vector<Hook>& menus() const { return menus_; }
    const std::vector<Hook>& statusItems() const { return status_; }
    const std::vector<Hook>& settingsSections() const { return sections_; }
    const std::vector<Hook>& windows() const { return windows_; }
    const std::vector<Command>& commands() const { return commands_; }
    const Command* findCommand(const std::string& name) const;

    // Called by the binding engine for an action of the kind Plugin. False when there is no such command.
    bool runCommand(const std::string& name, double value, const std::string& origin) const;
    void onInput(const input::InputEvent& ev) const;  // input thread
    // Calls a draw hook; one that throws is logged, the ImGui state is put back, and it is not called again.
    static void draw(const Hook& h);

private:
    class HostImpl;
    friend class HostImpl;
    void loadOne(app::AppContext& ctx, const std::string& folder, const std::string& library, bool ownFolder);
    std::vector<PluginRecord> records_;
    std::vector<std::unique_ptr<HostImpl>> hosts_;
    std::vector<Hook> tabs_, menus_, status_, sections_, windows_;
    std::vector<Command> commands_;
    std::vector<std::pair<std::string, std::function<void(const input::InputEvent&)>>> inputListeners_;
    std::vector<std::pair<std::string, std::function<void()>>> startHooks_, shutdownHooks_;
    bool shutDown_ = false;
};

}  // namespace lr::plugin
