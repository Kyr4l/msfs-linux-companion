// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kyr4l
#pragma once
// The virtual mouse and keyboard (/dev/uinput). They exist only while the desktop controls are active, so the simulator never sees them.
// If this process dies the kernel removes them and lets go of every key that was held.
#include <array>
#include <string>

#include <linux/input.h>

#include "desktop/DesktopEngine.hpp"

namespace lr::desktop {

class UinputSink final : public OutputSink {
public:
    ~UinputSink() override { close(); }
    bool open(std::string& err) override;
    void close() override;
    void key(int code, bool down) override;
    void move(int dx, int dy) override;
    void wheel(int vertical, int horizontal) override;
    void flush() override;

private:
    struct Queue {
        int fd = -1;
        std::array<input_event, 32> events{};
        size_t count = 0;
    };
    static void push(Queue& q, int type, int code, int value);
    static void send(Queue& q);
    Queue keyboard_, mouse_;
    WheelNotches notchesV_, notchesH_;
};

}  // namespace lr::desktop
