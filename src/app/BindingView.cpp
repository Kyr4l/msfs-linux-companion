// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kyr4l
#include "app/BindingView.hpp"

#include <algorithm>
#include <cctype>
#include <tuple>

namespace lr::app {

namespace {
std::string lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

int kindRank(input::InputKind k) { return k == input::InputKind::Axis ? 0 : k == input::InputKind::Button ? 1 : 2; }
const char* kindKey(input::InputKind k) { return k == input::InputKind::Axis ? "axis" : k == input::InputKind::Button ? "button" : "hat"; }
const char* kindTitle(input::InputKind k) { return k == input::InputKind::Axis ? "Axes" : k == input::InputKind::Button ? "Buttons" : "Hats"; }
}  // namespace

std::string bindingGroupKey(const Binding& b, BindingsGroupBy by) {
    switch (by) {
        case BindingsGroupBy::Device: return b.input.device;
        case BindingsGroupBy::InputType: return kindKey(b.input.kind);
        case BindingsGroupBy::Custom: return b.group;
        case BindingsGroupBy::None: break;
    }
    return "";
}

std::vector<BindingGroup> arrangeBindings(const Profile& p, BindingsGroupBy by, bool autoSort,
                                          const std::function<std::string(const std::string&)>& deviceName) {
    std::vector<BindingGroup> groups;
    for (size_t i = 0; i < p.bindings.size(); ++i) {
        const Binding& b = p.bindings[i];
        const std::string key = bindingGroupKey(b, by);
        auto it = std::find_if(groups.begin(), groups.end(), [&](const BindingGroup& g) { return g.key == key; });
        if (it == groups.end()) {
            BindingGroup g;
            g.key = key;
            switch (by) {
                case BindingsGroupBy::None: break;
                case BindingsGroupBy::Device: g.title = deviceName ? deviceName(key) : key; break;
                case BindingsGroupBy::InputType: g.title = kindTitle(b.input.kind); break;
                case BindingsGroupBy::Custom: g.title = key.empty() ? "Ungrouped" : key; break;
            }
            groups.push_back(std::move(g));
            it = groups.end() - 1;
        }
        it->indices.push_back(static_cast<int>(i));
    }
    if (autoSort) {
        auto inputOrder = [&](int a, int b) {
            const Binding &x = p.bindings[static_cast<size_t>(a)], &y = p.bindings[static_cast<size_t>(b)];
            return std::make_tuple(x.input.device, kindRank(x.input.kind), x.input.code, x.input.dir, lower(x.label)) <
                   std::make_tuple(y.input.device, kindRank(y.input.kind), y.input.code, y.input.dir, lower(y.label));
        };
        for (auto& g : groups) std::stable_sort(g.indices.begin(), g.indices.end(), inputOrder);
        std::stable_sort(groups.begin(), groups.end(), [&](const BindingGroup& a, const BindingGroup& b) { return lower(a.title) < lower(b.title); });
    }
    if (by == BindingsGroupBy::Custom)  // the bindings without a group come last, however the groups are ordered
        std::stable_partition(groups.begin(), groups.end(), [](const BindingGroup& g) { return !g.key.empty(); });
    return groups;
}

void moveBinding(std::vector<Binding>& v, int from, int to) {
    const int n = static_cast<int>(v.size());
    if (from < 0 || to < 0 || from >= n || to >= n || from == to) return;
    Binding moved = std::move(v[static_cast<size_t>(from)]);
    v.erase(v.begin() + from);
    v.insert(v.begin() + to, std::move(moved));
}

std::string autoGroupFor(const Binding& b, const Catalog* catalog) {
    if (b.input.kind == input::InputKind::Axis && b.axis.detents.enabled) return "Throttle";
    const std::vector<Action>& actions = !b.onPress.empty() ? b.onPress : b.onRelease;
    if (!actions.empty()) {
        const Action& a = actions.front();
        if (lower(a.name).find("throttle") != std::string::npos) return "Throttle";
        if (catalog) {
            for (const auto& e : catalog->entries)
                if (e.api == a.name) {
                    if (!e.section.empty()) return e.section;
                    if (!e.panel.empty()) return e.panel;
                    break;
                }
        }
    }
    return b.input.kind == input::InputKind::Axis ? "Axes" : "Other";
}

int autoGroupBindings(Profile& p, const Catalog* catalog) {
    int n = 0;
    for (auto& b : p.bindings)
        if (b.group.empty()) {
            b.group = autoGroupFor(b, catalog);
            ++n;
        }
    return n;
}

}  // namespace lr::app
