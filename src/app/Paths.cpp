// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kyr4l
#include "app/Paths.hpp"

#include "util/Log.hpp"

#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <thread>
#include <filesystem>
#include <vector>
#include <ctime>

extern char** environ;  // POSIX; must be declared at global scope

namespace lr::app {

namespace fs = std::filesystem;

// The program was called msfs-linuxrebind until 0.6.x. The first time the new folder is looked up and does not exist, the old one
// (config, imgui.ini, live catalogs) is copied into it; the old folder stays, so an older build keeps its settings.
static void migrateLegacyConfig(const fs::path& base) {
    const fs::path from = base / "msfs-linuxrebind", to = base / "msfs-linux-companion", tmp = base / "msfs-linux-companion.migrating";
    std::error_code ec;
    if (!fs::is_directory(from, ec) || fs::exists(to, ec)) return;
    fs::remove_all(tmp, ec);
    fs::copy(from, tmp, fs::copy_options::recursive, ec);  // into a temporary name first: an interrupted copy must not look like a migrated folder
    if (!ec) fs::rename(tmp, to, ec);
    if (ec) {
        fs::remove_all(tmp, ec);
        LOG_WARN("Could not copy the settings of the former folder %s to %s: %s", from.c_str(), to.c_str(), ec.message().c_str());
        return;
    }
    LOG_INFO("Settings copied from the former folder %s to %s (the old folder was left in place)", from.c_str(), to.c_str());
}

std::string configDir() {
    const char* xdg = std::getenv("XDG_CONFIG_HOME");
    fs::path base;
    if (xdg && *xdg) base = xdg;
    else if (const char* home = std::getenv("HOME"); home && *home) base = fs::path(home) / ".config";
    else base = ".";
    static const bool migrated = (migrateLegacyConfig(base), true);  // once per run
    (void)migrated;
    return (base / "msfs-linux-companion").string();
}

std::string stateDir() {
    const char* xdg = std::getenv("XDG_STATE_HOME");
    fs::path base;
    if (xdg && *xdg) base = xdg;
    else if (const char* home = std::getenv("HOME"); home && *home) base = fs::path(home) / ".local" / "state";
    else base = ".";
    const fs::path dir = base / "msfs-linux-companion";
    std::error_code ec;
    fs::create_directories(dir, ec);
    return dir.string();
}

std::string newSessionLogPath() {
    const fs::path dir = stateDir();
    std::vector<fs::path> old;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(dir, ec)) {
        const std::string n = e.path().filename().string();
        if (n.rfind("session-", 0) == 0 && n.size() > 4 && n.compare(n.size() - 4, 4, ".log") == 0) old.push_back(e.path());
    }
    std::sort(old.begin(), old.end());
    while (old.size() > 39) { fs::remove(old.front(), ec); old.erase(old.begin()); }
    char stamp[32];
    const std::time_t t = std::time(nullptr);
    std::tm tm{};
    localtime_r(&t, &tm);
    std::strftime(stamp, sizeof stamp, "%Y%m%d-%H%M%S", &tm);
    return (dir / (std::string("session-") + stamp + "-" + std::to_string(getpid()) + ".log")).string();
}

std::string configFile() { return (fs::path(configDir()) / "config.json").string(); }
std::string imguiIniFile() { return (fs::path(configDir()) / "imgui.ini").string(); }

static fs::path exeDir() {
    char buf[4096];
    ssize_t n = readlink("/proc/self/exe", buf, sizeof buf - 1);
    if (n <= 0) return fs::current_path();
    buf[n] = 0;
    return fs::path(buf).parent_path();
}

static std::vector<fs::path> dataDirs() {
    const fs::path exe = exeDir();
    return {
        fs::path(configDir()),                               // user override
        exe / "data",                                        // run from build dir with data copied
        exe / ".." / "share" / "msfs-linux-companion" / "data",  // installed
#ifdef COMPANION_SOURCE_DATA_DIR
        fs::path(COMPANION_SOURCE_DATA_DIR),               // dev build
#endif
        fs::current_path() / "data",
    };
}

std::vector<std::string> listDataFiles(const std::string& suffix) {
    std::vector<std::string> out;
    std::vector<std::string> names;
    for (const auto& dir : dataDirs()) {
        std::error_code ec;
        if (!fs::is_directory(dir, ec)) continue;
        for (const auto& e : fs::directory_iterator(dir, ec)) {
            const std::string n = e.path().filename().string();
            if (n.size() < suffix.size() || n.compare(n.size() - suffix.size(), suffix.size(), suffix) != 0) continue;
            if (std::find(names.begin(), names.end(), n) != names.end()) continue;  // earlier dir already provided it
            names.push_back(n);
            out.push_back(fs::weakly_canonical(e.path(), ec).string());
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

bool openDocsUrl(const std::string& url) {
    static const std::string prefix = "https://docs.flybywiresim.com/";
    if (url.compare(0, prefix.size(), prefix) != 0 || url.size() > 300) return false;
    for (char c : url.substr(prefix.size()))
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '/' || c == '-' || c == '_' || c == '.')) return false;
    char* argv[] = {const_cast<char*>("xdg-open"), const_cast<char*>(url.c_str()), nullptr};
    pid_t pid;
    if (posix_spawnp(&pid, "xdg-open", nullptr, nullptr, argv, environ) != 0) return false;
    // do not wait: xdg-open may stay around while the browser starts; reap it in the background
    std::thread([pid] { int st; waitpid(pid, &st, 0); }).detach();
    return true;
}

}  // namespace lr::app
