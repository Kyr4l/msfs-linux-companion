// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kyr4l
#pragma once
#include <cstdlib>

namespace lr {
// MSFS_LINUX_COMPANION_ISOLATED is set by the tests: the program then neither probes a running simulator for its port nor
// opens joysticks or Winwing devices, so a test run next to a real simulator and real hardware cannot reach either.
inline bool isolatedRun() {
    static const bool isolated = std::getenv("MSFS_LINUX_COMPANION_ISOLATED") != nullptr;
    return isolated;
}
}  // namespace lr
