// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kyr4l
#include "plugin/PluginManager.hpp"

#include <dlfcn.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>

#include <imgui_internal.h>

#include "app/Paths.hpp"

namespace lr::plugin {

namespace fs = std::filesystem;

namespace {

bool isLibrary(const fs::path& p) { return p.extension() == ".so"; }

// The libraries directly in a folder, in name order.
std::vector<fs::path> librariesIn(const fs::path& dir) {
    std::vector<fs::path> out;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(dir, ec))
        if (e.is_regular_file(ec) && isLibrary(e.path())) out.push_back(e.path());
    std::sort(out.begin(), out.end());
    return out;
}

bool isPluginFolder(const fs::path& dir) {
    std::error_code ec;
    return !librariesIn(dir).empty() || fs::is_directory(dir / "data", ec) || fs::is_regular_file(dir / "plugin.json", ec);
}

}  // namespace

// ---------------------------------------------------------------- Host

class PluginManager::HostImpl final : public Host {
public:
    HostImpl(PluginManager& mgr, app::AppContext& ctx, std::string id, std::string folder) : mgr_(mgr), ctx_(ctx), id_(std::move(id)), folder_(std::move(folder)) {}

    const std::string& id() const override { return id_; }
    const std::string& folder() const override { return folder_; }
    app::AppContext& app() override { return ctx_; }

    nlohmann::json settings() const override {
        auto it = ctx_.settings.pluginSettings.find(id_);
        if (it == ctx_.settings.pluginSettings.end()) return nlohmann::json::object();
        return nlohmann::json::parse(it->second, nullptr, false);  // a broken text gives a discarded value, not an exception
    }
    void setSettings(const nlohmann::json& value) override {
        std::string text = value.dump();
        std::string& kept = ctx_.settings.pluginSettings[id_];
        if (kept == text) return;
        kept = std::move(text);
        ctx_.dirty = true;
    }

    void addTab(const std::string& name, std::function<void()> draw) override { if (open("addTab")) tabs.push_back({id_, name, {}, std::move(draw)}); }
    void addMenu(const std::string& name, std::function<void()> draw) override { if (open("addMenu")) menus.push_back({id_, name, {}, std::move(draw)}); }
    void addStatusItem(std::function<void()> draw) override { if (open("addStatusItem")) status.push_back({id_, {}, {}, std::move(draw)}); }
    void addSettingsSection(const std::string& title, std::function<void()> draw) override {
        if (open("addSettingsSection")) sections.push_back({id_, title, {}, std::move(draw)});
    }
    void addWindow(std::function<void()> draw) override { if (open("addWindow")) windows.push_back({id_, {}, {}, std::move(draw)}); }
    void addCommand(const std::string& name, const std::string& label, std::function<void(double)> run) override {
        if (open("addCommand")) commands.push_back({id_, id_ + "." + name, label.empty() ? name : label, std::move(run)});
    }
    void addInputListener(std::function<void(const input::InputEvent&)> listener) override {
        if (open("addInputListener")) inputListeners.push_back(std::move(listener));
    }
    void addVibrationRule(const std::string& id, const std::string& label, const std::string& description, std::function<float(const sim::Telemetry&)> level) override {
        if (open("addVibrationRule")) rules.push_back({id, label, description, std::move(level)});
    }
    void addVibrationOutput(std::function<void(const HapticsFrame&)> output) override {
        if (open("addVibrationOutput")) outputs.push_back(std::move(output));
    }
    void onStart(std::function<void()> fn) override { if (open("onStart")) startHooks.push_back(std::move(fn)); }
    void onShutdown(std::function<void()> fn) override { if (open("onShutdown")) shutdownHooks.push_back(std::move(fn)); }

    // What companion_plugin_load() registered: taken over when it succeeded, dropped when it did not.
    void commit() {
        frozen_ = true;
        auto move = [](std::vector<Hook>& from, std::vector<Hook>& to) { for (auto& h : from) to.push_back(std::move(h)); from.clear(); };
        move(tabs, mgr_.tabs_);
        move(menus, mgr_.menus_);
        move(status, mgr_.status_);
        move(sections, mgr_.sections_);
        move(windows, mgr_.windows_);
        for (auto& c : commands) mgr_.commands_.push_back(std::move(c));
        for (auto& l : inputListeners) mgr_.inputListeners_.emplace_back(id_, std::move(l));
        for (auto& f : startHooks) mgr_.startHooks_.emplace_back(id_, std::move(f));
        for (auto& f : shutdownHooks) mgr_.shutdownHooks_.emplace_back(id_, std::move(f));
        for (auto& r : rules) ctx_.haptics.addRule(std::move(r));
        for (auto& o : outputs) {
            ctx_.haptics.addOutput([id = id_, out = std::move(o)](const haptics::HapticsOutputFrame& f) {
                HapticsFrame frame;
                frame.enabled = f.enabled;
                frame.flying = f.flying;
                frame.fresh = f.fresh;
                frame.left = f.left;
                frame.right = f.right;
                frame.contrib = &f.contrib;
                frame.telemetry = &f.telemetry;
                out(frame);
            });
        }
        commands.clear();
        inputListeners.clear();
        startHooks.clear();
        shutdownHooks.clear();
        rules.clear();
        outputs.clear();
    }
    void discard() {
        frozen_ = true;
        tabs.clear(); menus.clear(); status.clear(); sections.clear(); windows.clear();
        commands.clear();
        inputListeners.clear();
        startHooks.clear();
        shutdownHooks.clear();
        rules.clear();
        outputs.clear();
    }

private:
    bool open(const char* what) const {
        if (!frozen_) return true;
        LOG_ERROR("Plugin %s: %s() after companion_plugin_load() returned is ignored", id_.c_str(), what);
        return false;
    }

    PluginManager& mgr_;
    app::AppContext& ctx_;
    std::string id_, folder_;
    bool frozen_ = false;
    std::vector<Hook> tabs, menus, status, sections, windows;
    std::vector<Command> commands;
    std::vector<std::function<void(const input::InputEvent&)>> inputListeners;
    std::vector<haptics::ExtraRule> rules;
    std::vector<std::function<void(const HapticsFrame&)>> outputs;
    std::vector<std::function<void()>> startHooks, shutdownHooks;
};

// ---------------------------------------------------------------- PluginManager

PluginManager::PluginManager() = default;
PluginManager::~PluginManager() = default;  // the libraries stay loaded: the program ends right after this

void PluginManager::load(app::AppContext& ctx, const std::vector<std::string>& extra, bool scanDefault) {
    // in the order they are loaded; ownFolder: the folder is the plugin's own (its data/ is the plugin's), not a root of several plugins
    struct Found { fs::path folder, library; bool ownFolder; };
    std::vector<Found> found;
    auto addFolder = [&](const fs::path& dir) {
        const auto libs = librariesIn(dir);
        if (libs.size() > 1) LOG_WARN("Plugin folder %s has %zu libraries: only %s is loaded", dir.c_str(), libs.size(), libs.front().filename().c_str());
        found.push_back({dir, libs.empty() ? fs::path() : libs.front(), true});
    };
    // a root holds plugin folders and single libraries
    auto addRoot = [&](const fs::path& root) {
        std::error_code ec;
        if (!fs::is_directory(root, ec)) return;
        std::vector<fs::path> entries;
        for (const auto& e : fs::directory_iterator(root, ec)) entries.push_back(e.path());
        std::sort(entries.begin(), entries.end());
        for (const auto& p : entries) {
            if (fs::is_directory(p, ec)) {
                if (isPluginFolder(p)) addFolder(p);
            } else if (isLibrary(p)) {
                found.push_back({root, p, false});
            }
        }
    };
    // a path given by the user: a library, a plugin folder, or a root
    auto addGiven = [&](const fs::path& p) {
        std::error_code ec;
        if (fs::is_regular_file(p, ec) && isLibrary(p)) found.push_back({p.parent_path(), p, true});
        else if (fs::is_directory(p, ec) && isPluginFolder(p)) addFolder(p);
        else if (fs::is_directory(p, ec)) addRoot(p);
        else LOG_WARN("Plugin %s: no such file or folder", p.c_str());
    };

    if (scanDefault) {
        addRoot(fs::path(app::configDir()) / "plugins");
        addRoot(app::installedPluginDir());
    }
    if (const char* env = std::getenv("MSFS_LINUX_COMPANION_PLUGIN_PATH"); env && *env) {
        std::string s = env;
        for (size_t start = 0; start <= s.size();) {
            const size_t end = std::min(s.find(':', start), s.size());
            if (end > start) addGiven(fs::absolute(s.substr(start, end - start)));
            start = end + 1;
        }
    }
    for (const auto& e : extra) addGiven(fs::absolute(e));

    std::vector<fs::path> seen;
    for (const auto& f : found) {
        std::error_code ec;
        const fs::path key = fs::weakly_canonical(f.library.empty() ? f.folder : f.library, ec);
        if (std::find(seen.begin(), seen.end(), key) != seen.end()) continue;  // given twice (--plugin and the user's folder)
        seen.push_back(key);
        loadOne(ctx, fs::weakly_canonical(f.folder, ec).string(), f.library.empty() ? std::string() : key.string(), f.ownFolder);
    }
    if (!records_.empty()) {
        const auto loaded = std::count_if(records_.begin(), records_.end(), [](const PluginRecord& r) { return r.state == PluginState::Loaded; });
        LOG_INFO("Plugins: %ld loaded, %zu found", static_cast<long>(loaded), records_.size());
    }
}

void PluginManager::loadOne(app::AppContext& ctx, const std::string& folder, const std::string& library, bool ownFolder) {
    PluginRecord rec;
    rec.folder = folder;
    rec.library = library;
    std::error_code ec;
    rec.hasData = ownFolder && fs::is_directory(fs::path(folder) / "data", ec);

    const std::string stem = library.empty() ? fs::path(folder).filename().string() : fs::path(library).stem().string();
    rec.id = rec.name = stem;
    if (ownFolder) {
        std::ifstream in(fs::path(folder) / "plugin.json");
        if (in) {
            const nlohmann::json j = nlohmann::json::parse(in, nullptr, false);
            if (j.is_object()) {
                rec.id = j.value("id", rec.id);
                rec.name = j.value("name", rec.id);
                rec.version = j.value("version", "");
                rec.description = j.value("description", "");
            }
        }
    }
    auto fail = [&](const std::string& why) {
        rec.state = PluginState::Failed;
        rec.error = why;
        rec.hasData = false;  // the data of a plugin that did not load is not read either
        LOG_ERROR("Plugin %s (%s): %s", rec.id.c_str(), library.empty() ? folder.c_str() : library.c_str(), why.c_str());
        records_.push_back(rec);
    };
    auto disabled = [&] { return std::find(ctx.settings.disabledPlugins.begin(), ctx.settings.disabledPlugins.end(), rec.id) != ctx.settings.disabledPlugins.end(); };
    auto duplicate = [&]() -> const PluginRecord* {
        for (const auto& r : records_)
            if (r.id == rec.id && r.state == PluginState::Loaded) return &r;
        return nullptr;
    };

    if (library.empty()) {
        if (const PluginRecord* d = duplicate()) return fail("a plugin with the id \"" + rec.id + "\" is already loaded from " + d->folder);
        if (disabled()) { rec.state = PluginState::Disabled; rec.hasData = false; records_.push_back(rec); LOG_INFO("Plugin %s: disabled", rec.id.c_str()); return; }
        rec.state = PluginState::Loaded;
        records_.push_back(rec);
        LOG_INFO("Plugin %s %s: data from %s", rec.id.c_str(), rec.version.c_str(), folder.c_str());
        return;
    }

    // RTLD_NOW: a function the plugin needs and this build does not have is reported here, not as a crash when it is first called
    void* handle = dlopen(library.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!handle) return fail(std::string("cannot load: ") + dlerror());
    using InfoFn = const Info* (*)();
    using LoadFn = bool (*)(Host&);
    auto infoFn = reinterpret_cast<InfoFn>(dlsym(handle, "companion_plugin_info"));
    auto loadFn = reinterpret_cast<LoadFn>(dlsym(handle, "companion_plugin_load"));
    if (!infoFn || !loadFn) { dlclose(handle); return fail("not a plugin (companion_plugin_info / companion_plugin_load missing)"); }
    const Info* info = infoFn();
    if (!info || info->api != kApiVersion) {
        const int api = info ? info->api : 0;
        dlclose(handle);
        return fail("built for plugin API " + std::to_string(api) + ", this program has " + std::to_string(kApiVersion) + ": rebuild it");
    }
    rec.id = info->id && *info->id ? info->id : stem;
    rec.name = info->name && *info->name ? info->name : rec.id;
    rec.version = info->version ? info->version : "";
    rec.description = info->description ? info->description : "";
    rec.builtFor = info->builtFor ? info->builtFor : "";
    if (info->imgui != IMGUI_VERSION_NUM) {
        dlclose(handle);
        return fail("built with Dear ImGui " + std::to_string(info->imgui) + ", this program has " + std::to_string(IMGUI_VERSION_NUM) + ": rebuild it");
    }
    if (const PluginRecord* d = duplicate()) { dlclose(handle); return fail("a plugin with the id \"" + rec.id + "\" is already loaded from " + d->folder); }
    if (disabled()) {
        dlclose(handle);  // nothing of it ran but its static constructors
        rec.state = PluginState::Disabled;
        rec.hasData = false;
        records_.push_back(rec);
        LOG_INFO("Plugin %s: disabled", rec.id.c_str());
        return;
    }
    if (rec.builtFor != COMPANION_VERSION)
        LOG_WARN("Plugin %s was built for version %s of this program, this is %s: rebuild it if anything goes wrong", rec.id.c_str(),
                 rec.builtFor.empty() ? "(unknown)" : rec.builtFor.c_str(), COMPANION_VERSION);

    auto host = std::make_unique<HostImpl>(*this, ctx, rec.id, folder);
    bool ok = false;
    std::string why = "companion_plugin_load() returned false";
    try {
        ok = loadFn(*host);
    } catch (const std::exception& e) {
        why = std::string("companion_plugin_load() threw: ") + e.what();
    } catch (...) {
        why = "companion_plugin_load() threw";
    }
    // never dlclose()d from here on: the plugin may have started threads or handed out callbacks
    if (!ok) {
        host->discard();
        hosts_.push_back(std::move(host));
        return fail(why);
    }
    host->commit();
    hosts_.push_back(std::move(host));
    rec.state = PluginState::Loaded;
    records_.push_back(rec);
    LOG_INFO("Plugin %s %s loaded from %s", rec.id.c_str(), rec.version.c_str(), library.c_str());
}

std::vector<std::string> PluginManager::dataDirs() const {
    std::vector<std::string> out;
    for (const auto& r : records_)
        if (r.state == PluginState::Loaded && r.hasData) out.push_back((fs::path(r.folder) / "data").string());
    return out;
}

void PluginManager::start() {
    for (const auto& [id, fn] : startHooks_) {
        try {
            fn();
        } catch (const std::exception& e) {
            LOG_ERROR("Plugin %s: start hook threw: %s", id.c_str(), e.what());
        }
    }
}

void PluginManager::shutdown() {
    if (shutDown_) return;
    shutDown_ = true;
    for (auto it = shutdownHooks_.rbegin(); it != shutdownHooks_.rend(); ++it) {
        try {
            it->second();
        } catch (const std::exception& e) {
            LOG_ERROR("Plugin %s: shutdown hook threw: %s", it->first.c_str(), e.what());
        }
    }
}

const Command* PluginManager::findCommand(const std::string& name) const {
    for (const auto& c : commands_)
        if (c.name == name) return &c;
    return nullptr;
}

bool PluginManager::runCommand(const std::string& name, double value, const std::string& origin) const {
    const Command* c = findCommand(name);
    if (!c) return false;
    try {
        c->run(value);
    } catch (const std::exception& e) {
        LOG_ERROR("Plugin command %s (%s) threw: %s", name.c_str(), origin.c_str(), e.what());
    }
    return true;
}

void PluginManager::onInput(const input::InputEvent& ev) const {
    for (const auto& [id, fn] : inputListeners_) {
        try {
            fn(ev);
        } catch (const std::exception& e) {
            LOG_ERROR("Plugin %s: input listener threw: %s", id.c_str(), e.what());
        }
    }
}

void PluginManager::draw(const Hook& h) {
    if (h.broken) return;
    ImGuiErrorRecoveryState state;
    ImGui::ErrorRecoveryStoreState(&state);
    auto broke = [&](const char* what) {
        h.broken = true;
        LOG_ERROR("Plugin %s: drawing %s threw (%s); it is not drawn again until the program is restarted", h.plugin.c_str(), h.name.empty() ? "its window" : h.name.c_str(), what);
        // close what the hook left open (windows, ids, styles ...) so the rest of the frame is drawn as usual
        ImGuiIO& io = ImGui::GetIO();
        const bool asserts = io.ConfigErrorRecoveryEnableAssert;
        io.ConfigErrorRecoveryEnableAssert = false;
        ImGui::ErrorRecoveryTryToRecoverState(&state);
        io.ConfigErrorRecoveryEnableAssert = asserts;
    };
    try {
        h.draw();
    } catch (const std::exception& e) {
        broke(e.what());
    } catch (...) {
        broke("unknown exception");
    }
}

}  // namespace lr::plugin
