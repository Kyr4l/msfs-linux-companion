// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kyr4l
#include "desktop/Autostart.hpp"

#include <cstdlib>
#include <filesystem>

#include <unistd.h>

#include "util/AtomicFile.hpp"

namespace lr::desktop {

namespace fs = std::filesystem;

namespace {
// $HOME, not $XDG_CONFIG_HOME: inside a Flatpak that one points into the sandbox, where the session never looks.
fs::path autostartDir() {
    const char* home = std::getenv("HOME");
    return fs::path(home ? home : "") / ".config" / "autostart";
}

// KDE's own autostart settings name the entry after the Flatpak: one entry, whoever made it. Two would start the program twice, and the second start opens the window.
fs::path entryPath() {
    const char* id = std::getenv("FLATPAK_ID");
    return autostartDir() / (id && *id ? std::string(id) + ".desktop" : std::string("msfs-linux-companion.desktop"));
}
fs::path olderEntryPath() { return autostartDir() / "msfs-linux-companion.desktop"; }

const char* const kAntimicroxEntries[] = {"io.github.antimicrox.antimicrox.desktop", "antimicrox.desktop"};

// Quoted the way a .desktop file wants it.
std::string quoted(const std::string& path) {
    std::string out = "\"";
    for (char c : path) {
        if (c == '"' || c == '`' || c == '$' || c == '\\') out += '\\';
        out += c;
    }
    return out + "\"";
}
}  // namespace

std::string autostartCommand() {
    if (const char* id = std::getenv("FLATPAK_ID"); id && *id) return "flatpak run --command=msfs-linux-companion " + std::string(id) + " --tray";
    if (const char* image = std::getenv("APPIMAGE"); image && *image) return quoted(image) + " --tray";
    std::error_code ec;
    const fs::path self = fs::read_symlink("/proc/self/exe", ec);
    return (ec ? std::string("msfs-linux-companion") : quoted(self.string())) + " --tray";
}

bool autostartEnabled() {
    std::error_code ec;
    return fs::exists(entryPath(), ec) || fs::exists(olderEntryPath(), ec);
}

bool setAutostart(bool on, std::string& err) {
    std::error_code ec;
    if (!on) {
        fs::remove(entryPath(), ec);
        if (ec) { err = ec.message(); return false; }
        fs::remove(olderEntryPath(), ec);
        if (ec) { err = ec.message(); return false; }
        return true;
    }
    if (olderEntryPath() != entryPath()) fs::remove(olderEntryPath(), ec);
    const char* id = std::getenv("FLATPAK_ID");
    const std::string icon = id && *id ? id : "msfs-linux-companion";
    const std::string text =
        "[Desktop Entry]\n"
        "Type=Application\n"
        "Name=MSFS Linux Companion\n"
        "Exec=" + autostartCommand() + "\n"
        "Icon=" + icon + "\n"
        "Terminal=false\n"
        "X-GNOME-Autostart-enabled=true\n";
    return writeFileAtomically(entryPath().string(), text, err);
}

bool antimicroxAutostarts() {
    std::error_code ec;
    for (const char* name : kAntimicroxEntries)
        if (fs::exists(autostartDir() / name, ec)) return true;
    return false;
}

bool disableAntimicroxAutostart(std::string& err) {
    std::error_code ec;
    for (const char* name : kAntimicroxEntries) {
        const fs::path p = autostartDir() / name;
        if (!fs::exists(p, ec)) continue;
        fs::rename(p, p.string() + ".disabled", ec);
        if (ec) { err = ec.message(); return false; }
    }
    return true;
}

}  // namespace lr::desktop
