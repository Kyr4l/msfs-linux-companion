// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kyr4l
#pragma once
// Joystick input straight from the Linux evdev interface (/dev/input/event*), no SDL needed.
// Runs on its own thread so bindings keep working whatever the UI thread is doing.
#include <array>
#include <atomic>
#include <chrono>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace lr::input {

enum class InputKind { Button, Hat, Axis };

struct InputId {
    std::string device;  // stable device key "vvvv:pppp:Name"
    InputKind kind = InputKind::Button;
    int code = 0;        // evdev key code for buttons, hat index (0..3) for hats, ABS_* code (0..15) for axes
    int dir = 0;         // hats only: 0 up, 1 right, 2 down, 3 left
    bool operator==(const InputId& o) const { return device == o.device && kind == o.kind && code == o.code && dir == o.dir; }
};

struct InputEvent {
    InputId id;
    bool pressed = false;
    float axis = 0;      // axes only: position normalised to 0..1 (0 = device minimum)
    float axisPrev = 0;  // axes only: the previously reported position (the resting value before a move)
    std::chrono::steady_clock::time_point time;
};

struct DeviceSnapshot {
    std::string key;
    std::string name;
    std::string path;
    uint16_t vendor = 0, product = 0;
    std::vector<int> buttonCodes;   // sorted; index = joystick button number
    std::vector<bool> buttonState;  // parallel to buttonCodes
    int hatCount = 0;
    std::array<std::pair<int, int>, 4> hats{};  // (x, y) in -1..1
    int axisCount = 0;
    std::vector<int> axisCodes;     // ABS_* codes of the axes, ascending; index = axis number - 1
    std::vector<float> axisValue;   // parallel, normalised 0..1, updated live
};

std::string hatDirName(int dir);
const char* kindName(InputKind k);
InputKind kindFromName(const std::string& name);  // "button" / "hat" / "axis", anything else = button
const char* axisName(int evdevCode);                // "X", "Y", "Z", "Rx", "Ry", "Rz", "Slider", "Dial"
constexpr float kAxisReportStep = 1.0f / 512.0f;     // smallest change reported (filters sensor noise)

class InputManager {
public:
    using EventCallback = std::function<void(const InputEvent&)>;
    // Called after every wake-up; returns the milliseconds until it wants to be called again (-1: only when input arrives).
    using TickCallback = std::function<int()>;

    InputManager();
    ~InputManager();

    void start(EventCallback onEvent, TickCallback onTick);
    void stop();
    void rescan();
    int scanCount() const { return scans_.load(); }  // 0 until the first scan finished

    std::vector<DeviceSnapshot> devices() const;
    // "Btn 12" / "Hat 1 Up"; falls back to the raw code when the device is unknown.
    std::string describe(const InputId& id) const;
    int buttonIndex(const std::string& deviceKey, int code) const;
    bool buttonDown(const std::string& deviceKey, int evdevCode) const;  // false when the device or the button is unknown
    float axisValue(const std::string& deviceKey, int evdevCode) const;  // 0..1, or -1 when the device or the axis is unknown

private:
    struct Device;
    static void refreshSnapshot(Device& d);  // copies what the device reports now (buttons, hats, axes) into its snapshot
    void threadMain();
    void scanDevices();
    void closeDevice(size_t idx);
    bool openDevice(const std::string& path);
    void readDevice(size_t idx);

    std::vector<Device> devices_;
    mutable std::mutex m_;
    std::vector<DeviceSnapshot> snapshots_;
    std::thread thread_;
    std::atomic<bool> running_{false};
    std::atomic<bool> rescanRequested_{true};
    std::atomic<int> scans_{0};
    EventCallback onEvent_;
    TickCallback onTick_;
    int inotifyFd_ = -1;
    std::vector<std::string> failedPaths_;
};

}  // namespace lr::input
