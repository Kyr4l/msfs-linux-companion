// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kyr4l
#pragma once
// Opens things on the desktop for the Direct To page of the MCDU: KRunner with a text filled in, and web addresses in the default browser.
#include <string>

namespace lr::desktop {

bool showInKRunner(const std::string& text);
bool openWebAddress(const std::string& url);

// The address of a search. `kind`: 0 Google, 1 YouTube, 2 Wikipedia, 3 airport charts (`text` is an airport code), 4 Steam store.
std::string searchAddress(int kind, const std::string& text);

}  // namespace lr::desktop
