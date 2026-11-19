// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kyr4l
#pragma once
// One running copy per user: a second one would send every joystick movement to the desktop twice. Starting the program again shows the
// window of the copy that runs. An abstract socket goes away with its process, so a crash leaves nothing stale behind, and the Flatpak
// and the AppImage see the same one.
namespace lr::desktop {

class SingleInstance {
public:
    ~SingleInstance();
    // True: this is the only copy. False: another one runs and was told to show its window.
    bool acquire();
    int fd() const { return fd_; }
    bool showRequested();  // call when fd() is readable (or regularly): true when another start asked for the window

private:
    int fd_ = -1;
};

}  // namespace lr::desktop
