// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kyr4l
#pragma once
// The screen of the FlyByWire A32NX, from its SimBridge: the same WebSocket its remote MCDU web page uses (ws://127.0.0.1:8380/interfaces/v1/mcdu).
// The aircraft sends "update:{json}" whenever the page changes; keys go back as "event:left:<key>". One thread, never blocks.
#include <array>
#include <chrono>
#include <string>
#include <string_view>
#include <vector>

#include "mcdu/McduProtocol.hpp"

namespace lr::mcdu {

using LampState = std::array<bool, kLamps.size()>;  // in the order of kLamps

// One side of the aircraft's update message onto a screen. False: not an update that can be shown.
bool parseSimBridgeUpdate(std::string_view json, Screen& screen, LampState& lamps);
// A cell of the aircraft's page ("{green}FL350{end}") as characters.
std::vector<Cell> parseMarkup(std::string_view text, bool smallByDefault);

class SimLink {
public:
    SimLink() = default;
    ~SimLink() { disconnect(); }
    SimLink(const SimLink&) = delete;
    SimLink& operator=(const SimLink&) = delete;

    void setWanted(bool wanted);  // only while an aircraft that has it is loaded
    bool connected() const { return state_ == State::Open; }
    bool hasScreen() const { return hasScreen_; }
    const Screen& screen() const { return screen_; }
    const LampState& lamps() const { return lamps_; }

    int fd() const { return fd_; }
    short pollEvents() const;
    void process(short revents);  // after poll(), and regularly: connects, reads, answers
    bool takeChanged();           // true once after a new screen arrived
    bool sendKey(const char* keyId);

private:
    enum class State { Off, Connecting, Handshake, Open };
    using Clock = std::chrono::steady_clock;
    void startConnect();
    void disconnect();
    void readAvailable();
    bool parseHandshake();
    void parseFrames();
    void handleMessage(std::string_view text);
    bool sendFrame(uint8_t opcode, std::string_view payload);

    State state_ = State::Off;
    bool wanted_ = false;
    int fd_ = -1;
    Clock::time_point nextAttempt_{};
    std::string rx_, message_;
    Screen screen_;
    LampState lamps_{};
    bool hasScreen_ = false, changed_ = false;
    uint32_t maskCounter_ = 0x5a17c3e1;
};

}  // namespace lr::mcdu
