// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kyr4l
#pragma once
// Reads the pictures of the simulator's pop-out windows (Right Alt + click on a cockpit screen). MSFS runs under Proton as an X11
// program, so its windows are read through the X server, from outside the simulator: nothing here can disturb it.
// libxcb is loaded when first needed (no build dependency, no X11 needed to start). XCB rather than Xlib: errors come back with
// the request, there is no process-wide error handler to fight over with the window toolkit.
#include <cstdint>
#include <string>
#include <vector>

namespace lr::mcdu {

class X11Capture {
public:
    X11Capture() = default;
    ~X11Capture();
    X11Capture(const X11Capture&) = delete;
    X11Capture& operator=(const X11Capture&) = delete;

    bool open(std::string& err);
    void close();
    bool isOpen() const { return conn_ != nullptr; }
    // MSFS 2020 and 2024 pop-outs of glass instruments whose window name starts so ("WASMINSTRUMENT", "WT21_FMC_"), at most a few dozen.
    std::vector<uint32_t> popOuts(const std::string& name);
    // The window's picture, 4 bytes a pixel (blue green red unused). False when the window is gone or cannot be read.
    bool grab(uint32_t window, std::vector<uint8_t>& out, int& width, int& height);

private:
    struct Api;
    Api* api_ = nullptr;
    void* conn_ = nullptr;
    uint32_t root_ = 0, clientList_ = 0;
};

}  // namespace lr::mcdu
