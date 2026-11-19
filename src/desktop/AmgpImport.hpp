// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kyr4l
#pragma once
// Reads an antiMicroX profile (.amgp) into desktop bindings. What has no equivalent here is listed in `notes`, never dropped silently.
#include <string>
#include <vector>

#include "desktop/DesktopTypes.hpp"

namespace lr::desktop {

struct ImportResult {
    bool ok = false;
    std::string error;
    std::string deviceName;
    std::vector<Binding> bindings;
    std::vector<std::string> notes;
};

// `devices`: the joysticks connected now. antiMicroX numbers buttons and axes the way SDL does, which only the device itself can translate to its codes.
ImportResult importAmgp(const std::string& xml, const std::vector<input::DeviceSnapshot>& devices);
ImportResult importAmgpFile(const std::string& path, const std::vector<input::DeviceSnapshot>& devices);

// A key of a profile (a Qt key code) as a Linux key code; 0 when it is not known.
int qtKeyToLinux(unsigned qtKey);

}  // namespace lr::desktop
