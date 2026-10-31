// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kyr4l
// The Bindings tab's arrangement: grouping (device, input type, the user's groups), manual and automatic order, dragging a row
// to another place, the groups filled in from what a binding does, and that the choices survive a save.
#include <cstdio>
#include <filesystem>
#include <map>

#include "app/BindingView.hpp"

using namespace lr;
using namespace lr::app;
using lr::input::InputKind;

static int failures = 0;
#define CHECK(cond)                                                                         \
    do {                                                                                    \
        if (!(cond)) { std::printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); ++failures; } \
    } while (0)

static Binding make(const char* label, const char* dev, InputKind kind, int code, const char* action = nullptr, ActionKind ak = ActionKind::SimEvent) {
    Binding b;
    b.label = label;
    b.input = {dev, kind, code, 0};
    if (action) {
        Action a;
        a.kind = ak;
        a.name = action;
        b.onPress = {a};
    }
    return b;
}

static std::vector<int> idx(const BindingGroup& g) { return g.indices; }
using V = std::vector<int>;

int main() {
    const std::string T = "4098:b970:Throttle", S = "4098:bc2a:Stick";
    Profile p;
    p.bindings = {
        make("Left throttle", T.c_str(), InputKind::Axis, 0),                         // 0
        make("Right throttle", T.c_str(), InputKind::Axis, 1),                        // 1
        make("Trigger", S.c_str(), InputKind::Button, 288, "GEAR_UP"),                // 2
        make("Aileron trim", S.c_str(), InputKind::Axis, 0),                          // 3
        make("Speed brake", T.c_str(), InputKind::Button, 290, "SPEEDBRAKE"),         // 4
        make("Coolie hat", S.c_str(), InputKind::Hat, 0),                             // 5
    };
    const auto name = [](const std::string& key) { return key == "4098:b970:Throttle" ? std::string("Throttle device") : std::string("Stick"); };

    // no grouping: one flat list in the order of the file, no header
    {
        const auto g = arrangeBindings(p, BindingsGroupBy::None, false, name);
        CHECK(g.size() == 1 && g[0].title.empty() && idx(g[0]) == V({0, 1, 2, 3, 4, 5}));
    }
    // by device: groups in order of first appearance, rows in the order of the file
    {
        const auto g = arrangeBindings(p, BindingsGroupBy::Device, false, name);
        CHECK(g.size() == 2 && g[0].title == "Throttle device" && g[1].title == "Stick");
        CHECK(idx(g[0]) == V({0, 1, 4}) && idx(g[1]) == V({2, 3, 5}));
    }
    // by device, sorted: groups by name, rows by input kind (axes, buttons, hats) then number
    {
        const auto g = arrangeBindings(p, BindingsGroupBy::Device, true, name);
        CHECK(g.size() == 2 && g[0].title == "Stick" && g[1].title == "Throttle device");
        CHECK(idx(g[0]) == V({3, 2, 5}) && idx(g[1]) == V({0, 1, 4}));
    }
    // by input type
    {
        const auto g = arrangeBindings(p, BindingsGroupBy::InputType, false, name);
        CHECK(g.size() == 3 && g[0].title == "Axes" && g[1].title == "Buttons" && g[2].title == "Hats");
        CHECK(idx(g[0]) == V({0, 1, 3}) && idx(g[1]) == V({2, 4}) && idx(g[2]) == V({5}));
    }
    // the user's groups: the ungrouped bindings are last however the rest is ordered
    {
        Profile q = p;
        q.bindings[0].group = q.bindings[1].group = "Throttle";
        q.bindings[2].group = "Stick";
        auto g = arrangeBindings(q, BindingsGroupBy::Custom, false, name);
        CHECK(g.size() == 3 && g[0].title == "Throttle" && g[1].title == "Stick" && g[2].title == "Ungrouped" && g[2].key.empty());
        CHECK(idx(g[0]) == V({0, 1}) && idx(g[1]) == V({2}) && idx(g[2]) == V({3, 4, 5}));
        g = arrangeBindings(q, BindingsGroupBy::Custom, true, name);
        CHECK(g[0].title == "Stick" && g[1].title == "Throttle" && g[2].title == "Ungrouped");
        q.bindings[0].group.clear();
        q.bindings[1].group.clear();
        q.bindings[2].group.clear();
        g = arrangeBindings(q, BindingsGroupBy::Custom, false, name);
        CHECK(g.size() == 1 && g[0].title == "Ungrouped" && g[0].indices.size() == 6);
        // a lower-case group name sorts with the others, whatever the case
        q.bindings[0].group = "alpha";
        q.bindings[2].group = "Beta";
        g = arrangeBindings(q, BindingsGroupBy::Custom, true, name);
        CHECK(g[0].title == "alpha" && g[1].title == "Beta");
    }
    {   // every binding is in exactly one group, in every mode
        for (auto by : {BindingsGroupBy::None, BindingsGroupBy::Device, BindingsGroupBy::InputType, BindingsGroupBy::Custom})
            for (bool sorted : {false, true}) {
                size_t n = 0;
                std::vector<bool> seen(p.bindings.size(), false);
                for (const auto& g : arrangeBindings(p, by, sorted, name))
                    for (int i : g.indices) { ++n; seen[static_cast<size_t>(i)] = true; }
                CHECK(n == p.bindings.size() && std::count(seen.begin(), seen.end(), true) == static_cast<long>(p.bindings.size()));
            }
        CHECK(arrangeBindings(Profile{}, BindingsGroupBy::Device, false, name).empty());
    }
    CHECK(bindingGroupKey(p.bindings[0], BindingsGroupBy::Device) == T && bindingGroupKey(p.bindings[0], BindingsGroupBy::InputType) == "axis" &&
          bindingGroupKey(p.bindings[0], BindingsGroupBy::None).empty());

    // dragging a row: it takes the place of the row it is dropped on
    {
        std::vector<Binding> v;
        for (const char* l : {"A", "B", "C", "D"}) v.push_back(make(l, "d", InputKind::Button, 1));
        auto order = [&] { std::string s; for (const auto& b : v) s += b.label; return s; };
        moveBinding(v, 0, 2);  CHECK(order() == "BCAD");
        moveBinding(v, 3, 1);  CHECK(order() == "BDCA");
        moveBinding(v, 1, 1);  CHECK(order() == "BDCA");
        moveBinding(v, -1, 2); moveBinding(v, 0, 9); CHECK(order() == "BDCA");
        moveBinding(v, 2, 0);  CHECK(order() == "CBDA");
    }

    // groups from what a binding does
    {
        Catalog cat;
        CatalogEntry fcu;
        fcu.api = "A32NX.FCU_AP_1_PUSH"; fcu.section = "FCU"; fcu.panel = "Glareshield";
        CatalogEntry bare;
        bare.api = "A32NX.SOMETHING"; bare.panel = "Pedestal";
        cat.entries = {fcu, bare};
        Binding gates = make("lever", T.c_str(), InputKind::Axis, 0);
        gates.axis.detents.enabled = true;
        CHECK(autoGroupFor(gates, &cat) == "Throttle");
        CHECK(autoGroupFor(make("t", T.c_str(), InputKind::Button, 1, "ENGINE_Throttle_1", ActionKind::InputEvent), nullptr) == "Throttle");
        CHECK(autoGroupFor(make("t", T.c_str(), InputKind::Button, 1, "THROTTLE1_AXIS_SET_EX1"), &cat) == "Throttle");
        CHECK(autoGroupFor(make("ap", S.c_str(), InputKind::Button, 1, "A32NX.FCU_AP_1_PUSH"), &cat) == "FCU");
        CHECK(autoGroupFor(make("x", S.c_str(), InputKind::Button, 1, "A32NX.SOMETHING"), &cat) == "Pedestal");
        CHECK(autoGroupFor(make("u", S.c_str(), InputKind::Button, 1, "UNKNOWN_EVENT"), &cat) == "Other");
        CHECK(autoGroupFor(make("u", S.c_str(), InputKind::Axis, 1, "UNKNOWN_EVENT"), &cat) == "Axes");
        CHECK(autoGroupFor(make("none", S.c_str(), InputKind::Button, 1), &cat) == "Other");
        Binding releaseOnly = make("r", S.c_str(), InputKind::Button, 1);
        Action rel;
        rel.name = "A32NX.FCU_AP_1_PUSH";
        releaseOnly.onRelease = {rel};
        CHECK(autoGroupFor(releaseOnly, &cat) == "FCU");

        Profile q;
        q.bindings = {make("a", S.c_str(), InputKind::Button, 1, "A32NX.FCU_AP_1_PUSH"), make("b", S.c_str(), InputKind::Button, 2, "A32NX.FCU_AP_1_PUSH")};
        q.bindings[1].group = "Mine";
        CHECK(autoGroupBindings(q, &cat) == 1 && q.bindings[0].group == "FCU" && q.bindings[1].group == "Mine");
        CHECK(autoGroupBindings(q, &cat) == 0);  // nothing left to fill in
    }

    // the choices are saved: the groups of the bindings, how the tab groups and whether it sorts; old files read as before
    {
        Settings st;
        st.profiles.push_back(p);
        st.profiles[0].bindings[0].group = "Throttle";
        st.activeProfile = st.profiles[0].name;
        st.bindingsGroupBy = BindingsGroupBy::Custom;
        st.bindingsAutoSort = true;
        const auto path = (std::filesystem::temp_directory_path() / "msfs-linux-companion-view-test.json").string();
        std::string err;
        CHECK(saveSettings(path, st, err));
        Settings back;
        CHECK(loadSettings(path, back, err));
        CHECK(back.bindingsGroupBy == BindingsGroupBy::Custom && back.bindingsAutoSort);
        CHECK(back.profiles[0].bindings[0].group == "Throttle" && back.profiles[0].bindings[1].group.empty());
        for (auto by : {BindingsGroupBy::None, BindingsGroupBy::Device, BindingsGroupBy::InputType}) {
            st.bindingsGroupBy = by;
            CHECK(saveSettings(path, st, err) && loadSettings(path, back, err) && back.bindingsGroupBy == by);
        }
        std::filesystem::remove(path);
        Settings fresh;
        CHECK(fresh.bindingsGroupBy == BindingsGroupBy::None && !fresh.bindingsAutoSort);
    }

    std::printf(failures ? "\n%d check(s) FAILED\n" : "all binding view checks passed\n", failures);
    return failures ? 1 : 0;
}
