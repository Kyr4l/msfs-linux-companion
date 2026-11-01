// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kyr4l
// Two plugins that must not take the program down (tests/plugins.sh): with BAD_API one built for another plugin API, without it one
// whose load function throws after it registered a tab.
#include "plugin/Plugin.hpp"

#ifdef BAD_API
extern "C" __attribute__((visibility("default"))) const lr::plugin::Info* companion_plugin_info() {
    static const lr::plugin::Info info{lr::plugin::kApiVersion + 100, IMGUI_VERSION_NUM, COMPANION_VERSION, "bad_api", "Bad API", "1", ""};
    return &info;
}
extern "C" __attribute__((visibility("default"))) bool companion_plugin_load(lr::plugin::Host&) { return true; }
#else
#include <stdexcept>
COMPANION_PLUGIN("bad_load", "Bad load", "1", "") {
    host.addTab("Bad", [] {});
    host.addCommand("x", "", [](double) {});
    throw std::runtime_error("no device here");
}
#endif
