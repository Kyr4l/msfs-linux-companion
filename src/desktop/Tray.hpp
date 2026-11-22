// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kyr4l
#pragma once
// The icon in the system tray (StatusNotifierItem, with its menu over com.canonical.dbusmenu). Runs on the main thread; costs nothing until the bus has something for it.
#include <functional>
#include <string>
#include <vector>

#include "desktop/DesktopTypes.hpp"
#include "desktop/SdBus.hpp"

namespace lr::desktop {

class Tray {
public:
    struct Callbacks {
        std::function<void()> open, quit;
        std::function<void(bool)> desktopControls;
    };

    Tray() = default;
    ~Tray();
    Tray(const Tray&) = delete;
    Tray& operator=(const Tray&) = delete;

    bool start(Callbacks callbacks, const std::string& iconName);
    bool available() const { return registered_; }  // a tray host took the icon: hiding the window leaves a way back
    int fd() const;
    short events() const;
    int timeoutMs() const;  // how long the bus can wait, -1: no limit
    void process();         // handles what arrived

    void setDesktopControls(bool on);
    void setStatus(const std::string& text);
    void setIcon(TrayIcon icon);  // shown at once

private:
    static int onItem(sdbus::Message* m, void* self, sdbus::Error* err);
    static int onMenu(sdbus::Message* m, void* self, sdbus::Error* err);
    static int onWatcherChanged(sdbus::Message* m, void* self, sdbus::Error* err);
    static int onRegistered(sdbus::Message* m, void* self, sdbus::Error* err);
    void registerWithWatcher();
    void appendItemProperty(sdbus::Message* reply, const std::string& name);
    void appendMenuItem(sdbus::Message* reply, int id);
    void appendMenuProps(sdbus::Message* reply, int id);
    void menuClicked(int id);
    void signalLayout();
    void buildPixmaps();

    const sdbus::Api* api_ = nullptr;
    sdbus::Bus* bus_ = nullptr;
    std::vector<sdbus::Slot*> slots_;
    sdbus::Slot* pendingRegister_ = nullptr;
    Callbacks cb_;
    std::string iconName_, status_;
    struct Pixmap { int size; std::vector<uint8_t> argb; };
    std::vector<Pixmap> pixmaps_;  // the icon as ARGB: for hosts that cannot find it by name, and the only icon in one colour
    TrayIcon icon_ = TrayIcon::Colour;
    bool registered_ = false;
    bool desktopControls_ = false;
    unsigned revision_ = 1;
};

}  // namespace lr::desktop
