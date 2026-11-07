// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kyr4l
#pragma once
// The sim's own control profiles (MSFS Settings > Controls), read from Steam's userdata, to find buttons that both the sim and this
// program act on: one press, two actions (an "APU START" here and "APU STARTER" in the sim's profile). Read only.
//
// Files: <Steam>/userdata/<user>/<app>/remote/inputprofile_* (2537590 = MSFS 2024, 1250410 = MSFS 2020), one device per file:
// <FriendlyName>, <Device DeviceName=...>, <AircraftInfo CategoryName=...> (AIRPLANE, HELICOPTER; none = every kind), and per action
// <Action ActionName="KEY_X"> with <KEY Information="Joystick Button N"/>. N counts from 1 in the order of the device's button codes
// (checked: the URSA MINOR throttle's evdev 720 is the sim's "Joystick Button 33", GEAR_UP in both).
#include <string>
#include <vector>

#include "app/Profile.hpp"

namespace lr::app {

struct MsfsControlProfile {
    std::string file, friendlyName, deviceName, category;  // category: "AIRPLANE", "HELICOPTER" or "" (every kind)
    int edition = 0;                                         // 2024 or 2020 (from the Steam app folder)
    long long modified = 0;                                  // file time: the newest of several is taken
    std::vector<std::pair<int, std::string>> buttons;        // (button number from 1, action without "KEY_")
};

// Every profile found (empty when there is no Steam userdata). `roots` overrides the Steam folders (tests).
std::vector<MsfsControlProfile> readMsfsControlProfiles(const std::vector<std::string>& roots = {});

// The profiles parsed from one file's text (tests).
bool parseMsfsControlProfile(const std::string& text, MsfsControlProfile& out);

struct DoubleBinding {
    size_t binding = 0;         // index in the companion profile
    int button = 0;             // the sim's button number
    std::string msfsAction;     // "APU_STARTER"
    std::string msfsProfile;    // its FriendlyName ("URSA-M-T")
};

// Buttons of `p` that the newest airplane profile of the same device in the sim also binds. `deviceButtons` gives, per device key
// ("vvvv:pppp:Name"), the device's name and its sorted button codes (input::DeviceSnapshot); devices not connected are skipped.
struct DeviceButtons {
    std::string key, name;
    std::vector<int> codes;
};
// `edition` (2024 / 2020, 0 = any) prefers the profiles of the sim that is connected.
std::vector<DoubleBinding> findDoubleBindings(const Profile& p, const std::vector<DeviceButtons>& devices, const std::vector<MsfsControlProfile>& sim, int edition = 0);

}  // namespace lr::app
