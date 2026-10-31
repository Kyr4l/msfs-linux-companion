// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kyr4l
#pragma once
// How the Bindings tab arranges the bindings of a profile: in groups (by device, by input type, or the user's own group
// names), the groups and the rows inside them either in the order of the file (the manual order, changed by dragging rows)
// or sorted automatically.
#include <functional>
#include <string>
#include <vector>

#include "app/Catalog.hpp"
#include "app/Profile.hpp"

namespace lr::app {

struct BindingGroup {
    std::string key;           // what the bindings of the group share (device key, input kind, group name); empty = ungrouped
    std::string title;         // header text; empty when the list is not grouped (no header)
    std::vector<int> indices;  // into Profile::bindings, in display order
};

// `deviceName` turns a device key into the name shown for it.
std::vector<BindingGroup> arrangeBindings(const Profile& p, BindingsGroupBy by, bool autoSort,
                                          const std::function<std::string(const std::string&)>& deviceName);

// The key of the group `b` is in; two bindings may be dragged past each other only when their keys are equal.
std::string bindingGroupKey(const Binding& b, BindingsGroupBy by);

// Moves bindings[from] so that it takes the place of bindings[to]; the ones in between shift by one.
void moveBinding(std::vector<Binding>& v, int from, int to);

// The group a binding belongs in, judged from what it does: a lever with gates or a throttle event is "Throttle", otherwise
// the catalog section (or panel) of its first action, otherwise its kind of input.
std::string autoGroupFor(const Binding& b, const Catalog* catalog);

// Gives every binding without a group the one autoGroupFor() finds; returns how many it changed.
int autoGroupBindings(Profile& p, const Catalog* catalog);

}  // namespace lr::app
