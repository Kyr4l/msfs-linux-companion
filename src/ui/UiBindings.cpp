// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kyr4l
#include "ui/Ui.hpp"
#include "ui/UiCommon.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <ctime>
#include <filesystem>

#include <imgui.h>
#include <imgui_stdlib.h>

#include "app/BindingView.hpp"
#include "app/Paths.hpp"
#include "util/Log.hpp"

namespace lr::app {

using namespace uidetail;

// ---------------------------------------------------------------- Bindings

// The throttle with gates: whether it is calibrated, what it sends at each gate, and the preset for the F/A-18E.
void Ui::drawDetentOptions() {
    AxisConfig& c = draft_.axis;
    AxisDetents& d = c.detents;
    const AxisCalibration* cal = ctx_.settings.calibrationFor(draft_.input);
    const bool ok = cal && cal->valid();
    const float fs = ImGui::GetFontSize();
    ImGui::Indent(fs);
    if (ok) {
        std::string gates;
        const int codes[4] = {cal->cutoffBtn, cal->idleBtn, cal->milBtn, cal->abBtn};
        const char* names[4] = {"cut-off", "idle", "end of the dry range", "afterburner"};
        for (int g = 0; g < 4; ++g)
            if (codes[g] >= 0) gates += std::string(gates.empty() ? "" : ", ") + names[g] + " = " + ctx_.input.describe({draft_.input.device, input::InputKind::Button, codes[g], 0});
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + fs * 40);
        ImGui::TextColored(kGreen, "Calibrated.%s%s", gates.empty() ? "" : " The gates are read from buttons: ", gates.c_str());
        ImGui::PopTextWrapPos();
        const float now = axisNow(draft_.input);
        drawGateBar(*cal, now, fs * 36);
        if (now >= 0) {
            const auto r = Engine::detentOutput(d, *cal, now, AxisZone::Dry, gateStateNow(*cal));
            ImGui::TextDisabled("The lever is in the %s.", r.zone == AxisZone::Cutoff ? "cut-off gate" : r.zone == AxisZone::Afterburner ? "afterburner gate" : "dry range (idle up to military power)");
        }
    } else {
        ImGui::TextColored(kYellow, "This axis is not calibrated%s: until it is, the lever acts as a plain axis.", cal ? " properly" : " yet");
    }
    if (ImGui::Button(ok ? "Calibrate again..." : "Calibrate this axis...")) openCalibration(draft_.input);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Teach the program where the gates of this lever are. Done once per axis; every profile uses it.");

    ImGui::Spacing();
    ImGui::TextUnformatted("Value sent");
    ImGui::SameLine();
    ImGui::TextDisabled("at the cut-off");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(fs * 5);
    ImGui::InputDouble("##outCutoff", &d.outCutoff, 0, 0, "%g");
    ImGui::SameLine();
    ImGui::TextDisabled("at idle");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(fs * 5);
    ImGui::InputDouble("##outIdle", &d.outIdle, 0, 0, "%g");
    ImGui::SameLine();
    ImGui::TextDisabled("at the end of the dry range");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(fs * 5);
    ImGui::InputDouble("##outMil", &d.outMil, 0, 0, "%g");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Also the value while the lever is in the afterburner gate. Between idle and here the value follows the lever.");
    const bool fractional = d.outIdle != std::round(d.outIdle) || d.outMil != std::round(d.outMil) || d.outCutoff != std::round(d.outCutoff);
    if (c.integer && fractional) {
        ImGui::TextColored(kYellow, "These values would be rounded to whole numbers.");
        ImGui::SameLine();
        if (ImGui::SmallButton("Allow decimals")) c.integer = false;
    }

    ImGui::Spacing();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Fill in for the F/A-18E:");
    for (int engine = 1; engine <= 2; ++engine) {
        ImGui::SameLine();
        char label[64];
        snprintf(label, sizeof label, engine == 1 ? "Left throttle (1)" : "Right throttle (2)");
        if (ImGui::Button(label)) {
            // Asobo F/A-18E (the cockpit's own template, read from the sim files and checked on the aircraft): the throttle input event is a
            // fraction of the lever travel; the sim's own Idle preset sets 0 and military power is 0.8.
            //  * The cut-off needs the throttle at exactly 0 when STOP is set, and a fast pull from above lands on the idle gate (10 %) first,
            //    so the sequence is: 0, wait, 0 again, wait, STOP. A throttle value that changes the throttle clears STOP by itself, but
            //    leaving the cut-off at 0 changes nothing, so STOP is cleared explicitly.
            //  * The afterburner sets the throttle to L:XMLVAR_LastAB<n>ThrottleSet, which is 0 until the game's own throttle is used, so
            //    it is set to 1 first; leaving sends the throttle value again.
            d.outCutoff = 0.0; d.outIdle = 0.0; d.outMil = 0.8;
            c.integer = false;
            const std::string n = std::to_string(engine);
            auto event = [](const std::string& name, double value) { Action a; a.kind = ActionKind::InputEvent; a.name = name; a.value = value; return a; };
            auto wait = [](double ms) { Action a; a.kind = ActionKind::Delay; a.value = ms; return a; };
            Action lastAb;
            lastAb.kind = ActionKind::LVarSet;
            lastAb.name = "XMLVAR_LastAB" + n + "ThrottleSet";
            lastAb.value = 1;
            const std::string thr = "ENGINE_Throttle_" + n;
            draft_.onPress = {event(thr, 0)};
            draft_.onCutoff = {wait(100), event(thr, 0), wait(100), event(thr + "_STOP", 1)};
            draft_.onCutoffLeave = {event(thr + "_STOP", 0)};
            draft_.onAfterburner = {lastAb, wait(100), event(thr + "_Afterburner", 1)};
            draft_.onAfterburnerLeave = {event(thr + "_Afterburner", 0)};
            if (draft_.label.empty()) draft_.label = engine == 1 ? "Left throttle" : "Right throttle";
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Sets the values 0 (idle, as the sim's own Idle) / 0.8 (military power) on ENGINE_Throttle_%d and the actions the aircraft needs at the gates:\n"
                              "cut-off: throttle 0, wait, 0 again, wait, STOP (the aircraft refuses STOP unless the throttle is at exactly 0); leaving it clears STOP;\n"
                              "afterburner: XMLVAR_LastAB%dThrottleSet = 1 first (otherwise the afterburner drops the throttle to idle), then the afterburner.\n"
                              "It replaces the actions of this binding.", engine, engine);
    }
    ImGui::Unindent(fs);
}

// What the axis does: how it sends, the throttle gates, and the range of the values.
void Ui::drawAxisOptions() {
    AxisConfig& c = draft_.axis;
    const float fs = ImGui::GetFontSize();
    ImGui::SeparatorText("The axis");
    const float now = axisNow(draft_.input);
    const AxisCalibration* cal = c.detents.enabled && c.mode == AxisMode::Value ? ctx_.settings.calibrationFor(draft_.input) : nullptr;
    const bool gates = cal && cal->valid();  // the lever's gates decide the output instead of the range
    double shown = Engine::axisOutput(c, Engine::axisPosition(c, now >= 0 ? now : 0.0f));
    if (gates && now >= 0) shown = Engine::detentOutput(c.detents, *cal, now, AxisZone::Dry, gateStateNow(*cal)).value;
    char overlay[96];
    if (now < 0) snprintf(overlay, sizeof overlay, "device not connected");
    else if (c.mode == AxisMode::Value) snprintf(overlay, sizeof overlay, "%d%% of travel  ->  sends %s", static_cast<int>(now * 100 + 0.5f), Engine::formatValue(shown, c.integer && !gates).c_str());
    else snprintf(overlay, sizeof overlay, "%d%% of travel", static_cast<int>(now * 100 + 0.5f));
    fieldLabel("Position now");
    ImGui::ProgressBar(now < 0 ? 0.0f : now, ImVec2(fs * 26, 0), overlay);

    fieldLabel("It sends");
    if (ImGui::RadioButton("its position, as a value", c.mode == AxisMode::Value)) c.mode = AxisMode::Value;
    ImGui::SameLine();
    if (ImGui::RadioButton("a step up or down (a knob)", c.mode == AxisMode::Steps)) { c.mode = AxisMode::Steps; c.detents.enabled = false; }

    if (c.mode == AxisMode::Value) {
        fieldLabel("Throttle lever");
        ImGui::Checkbox("with gates: cut-off, idle, afterburner", &c.detents.enabled);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("For a throttle whose lever has gates. After a calibration the value sent follows them: flat at idle, following the lever\nin the dry range, fixed in the afterburner, with actions you choose when the lever reaches or leaves a gate.");
        if (c.detents.enabled) drawDetentOptions();
    }

    if (c.mode == AxisMode::Steps) {
        float step = static_cast<float>(c.stepPercent);
        fieldLabel("One step every");
        ImGui::SetNextItemWidth(fs * 12);
        if (ImGui::SliderFloat("##step", &step, 1.0f, 50.0f, "%.0f %% of the travel")) c.stepPercent = step;
        ImGui::TextDisabled("Example: 5%% = 20 steps over the full travel. Sweeping fast sends at most 10 steps per movement.");
    } else if (!gates) {
        struct Preset { const char* label; double lo, hi; bool integer; };
        static const Preset presets[] = {
            {"0 to 100 (percent)", 0, 100, true},
            {"-16384 to 16383 (sim axis event, with reverse)", -16384, 16383, true},
            {"0 to 16383 (sim axis event, no reverse)", 0, 16383, true},
            {"0 to 1", 0, 1, false},
        };
        const char* current = "Custom";
        for (const auto& pr : presets)
            if (c.outMin == pr.lo && c.outMax == pr.hi) current = pr.label;
        fieldLabel("Values sent");
        ImGui::SetNextItemWidth(fs * 24);
        if (ImGui::BeginCombo("##range", current)) {
            for (const auto& pr : presets)
                if (ImGui::Selectable(pr.label, c.outMin == pr.lo && c.outMax == pr.hi)) { c.outMin = pr.lo; c.outMax = pr.hi; c.integer = pr.integer; }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        ImGui::TextDisabled("from");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(fs * 6);
        ImGui::InputDouble("##outMin", &c.outMin, 0, 0, "%g");
        ImGui::SameLine();
        ImGui::TextDisabled("to");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(fs * 6);
        ImGui::InputDouble("##outMax", &c.outMax, 0, 0, "%g");
        double lo, hi;
        if (ctx_.suggestedAxisRange(draft_, lo, hi)) {
            if (axisRangeLooksWrong(c, lo, hi)) {
                ImGui::TextColored(kYellow, "This event expects %g to %g, but the values sent are %g to %g: the control would barely move.", lo, hi, c.outMin, c.outMax);
                ImGui::SameLine();
                if (ImGui::SmallButton("Use the right range")) { c.outMin = lo; c.outMax = hi; c.integer = true; }
            } else {
                ImGui::TextDisabled("This event expects %g to %g.", lo, hi);
            }
        }
    }

    // the rarely changed settings
    if (ImGui::TreeNode("More settings of the axis")) {
        if (!gates) {
            ImGui::Checkbox("Reverse the direction", &c.invert);
            float from = static_cast<float>(c.inMin * 100.0), to = static_cast<float>(c.inMax * 100.0);
            ImGui::SetNextItemWidth(fs * 8);
            if (ImGui::SliderFloat("Ignore the first part of the travel (%)", &from, 0.0f, 98.0f, "%.0f")) c.inMin = std::min<double>(from / 100.0, c.inMax - 0.02);
            ImGui::SetNextItemWidth(fs * 8);
            if (ImGui::SliderFloat("Use the travel up to (%)", &to, 2.0f, 100.0f, "%.0f")) c.inMax = std::max<double>(to / 100.0, c.inMin + 0.02);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("What lies outside the part of the travel you use is dead zone: it reads as the minimum or the maximum.");
        }
        if (c.mode == AxisMode::Value) {
            ImGui::Checkbox("Round the values to whole numbers", &c.integer);
            ImGui::SetNextItemWidth(fs * 6);
            ImGui::InputInt("Shortest time between two values (ms)", &c.minIntervalMs, 0, 0);
            c.minIntervalMs = std::clamp(c.minIntervalMs, 5, 1000);
        }
        ImGui::TreePop();
    }
}

void Ui::openEditor(int bindingIndex) {
    Profile& p = ctx_.settings.ensureActive();
    editIndex_ = bindingIndex;
    if (bindingIndex >= 0 && bindingIndex < static_cast<int>(p.bindings.size())) draft_ = p.bindings[static_cast<size_t>(bindingIndex)];
    else draft_ = Binding{};
    editorOpen_ = true;
    ctx_.engine.cancelCapture();
}

void Ui::drawBindingsTab() {
    Settings& s = ctx_.settings;
    Profile& p = s.ensureActive();

    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Profile");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(220);
    if (ImGui::BeginCombo("##profile", p.name.c_str())) {
        for (const auto& prof : s.profiles) {
            const std::string label = prof.name + "   [" + prof.aircraft + "]";
            if (ImGui::Selectable(label.c_str(), prof.name == p.name)) {
                s.activeProfile = prof.name;
                viewAircraft_.clear();
                ctx_.markDirty();
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Aircraft");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(260);
    if (ImGui::BeginCombo("##profile_aircraft", [&] {
            const Catalog* c = ctx_.catalogFor(p.aircraft);
            return (c && sim::sameAircraft(c->aircraft, p.aircraft)) ? c->label() : p.aircraft;
        }().c_str())) {
        for (const auto& c : ctx_.catalogs) {
            if (c.aircraft == kStandardCatalogId) continue;  // not an aircraft
            if (ImGui::Selectable(c.label().c_str(), sim::sameAircraft(c.aircraft, p.aircraft))) {
                p.aircraft = c.aircraft;
                viewAircraft_.clear();
                ctx_.markDirty();
            }
        }
        ImGui::EndCombo();
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Which aircraft this profile is for. It decides which catalog the editor offers; existing bindings are never changed.");
    ImGui::SameLine();
    if (ImGui::Button("New profile")) {
        newProfileName_ = "New profile";
        newProfileAircraft_ = p.aircraft;
        const std::string det = detectAircraft(status_.aircraft);
        if (!det.empty()) newProfileAircraft_ = det;
        renamingProfile_ = false;
        ImGui::OpenPopup("profile_name");
    }
    ImGui::SameLine();
    if (ImGui::Button("Rename")) { newProfileName_ = p.name; renamingProfile_ = true; ImGui::OpenPopup("profile_name"); }
    ImGui::SameLine();
    if (ImGui::Button("Duplicate")) {
        Profile copy = p;
        copy.name = uniqueProfileName(p.name + " copy");
        s.profiles.push_back(copy);
        s.activeProfile = copy.name;
        ctx_.markDirty();
    }
    ImGui::SameLine();
    if (ImGui::Button("Delete") && s.profiles.size() > 1) ImGui::OpenPopup("confirm_delete_profile");
    if (ImGui::BeginPopup("profile_name")) {
        ImGui::InputText("Name", &newProfileName_);
        if (!renamingProfile_) {
            const Catalog* chosen = ctx_.catalogs.find(newProfileAircraft_);
            if (ImGui::BeginCombo("Aircraft##new", chosen ? chosen->label().c_str() : newProfileAircraft_.c_str())) {
                for (const auto& c : ctx_.catalogs)
                    if (c.aircraft != kStandardCatalogId && ImGui::Selectable(c.label().c_str(), sim::sameAircraft(c.aircraft, newProfileAircraft_))) newProfileAircraft_ = c.aircraft;
                ImGui::EndCombo();
            }
        }
        if (ImGui::Button("OK") && !newProfileName_.empty()) {
            if (renamingProfile_) {
                if (newProfileName_ != p.name) { p.name = uniqueProfileName(newProfileName_); s.activeProfile = p.name; }
            } else {
                Profile created;
                created.name = uniqueProfileName(newProfileName_);
                created.aircraft = newProfileAircraft_;
                s.profiles.push_back(created);
                s.activeProfile = created.name;
                viewAircraft_.clear();
            }
            ctx_.markDirty();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    if (ImGui::BeginPopup("confirm_delete_profile")) {
        ImGui::Text("Delete profile \"%s\" and its %zu bindings?", p.name.c_str(), p.bindings.size());
        if (ImGui::Button("Delete")) {
            std::string name = p.name;
            s.profiles.erase(std::remove_if(s.profiles.begin(), s.profiles.end(), [&](const Profile& x) { return x.name == name; }), s.profiles.end());
            s.activeProfile = s.profiles.front().name;
            ctx_.markDirty();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    {
        const std::string det = detectAircraft(status_.aircraft);
        if (!det.empty() && !sim::sameAircraft(det, p.aircraft)) {
            const sim::AircraftId loaded = sim::identifyAircraft(status_.aircraft);
            const Catalog* profCat = ctx_.catalogFor(p.aircraft);
            const std::string profLabel = (profCat && sim::sameAircraft(profCat->aircraft, p.aircraft)) ? profCat->label() : p.aircraft;
            ImGui::TextColored(kYellow, "The loaded aircraft is the %s, this profile is for the %s.", loaded.label.c_str(), profLabel.c_str());
            ImGui::SameLine();
            const Profile* match = nullptr;
            for (const auto& prof : s.profiles)
                if (sim::sameAircraft(prof.aircraft, det)) { match = &prof; break; }
            if (match) {
                if (ImGui::SmallButton((std::string("Switch to \"") + match->name + "\"").c_str())) {
                    s.activeProfile = match->name;
                    viewAircraft_.clear();
                    ctx_.markDirty();
                }
            } else if (ImGui::SmallButton("Create a profile for it")) {
                newProfileName_ = sim::identifyAircraft(status_.aircraft).label;
                newProfileAircraft_ = ctx_.catalogs.canonicalId(det);
                renamingProfile_ = false;
                ImGui::OpenPopup("profile_name");
            }
        }
    }
    {
        int wrong = 0;
        for (const auto& b : p.bindings) {
            double lo, hi;
            if (b.input.kind == input::InputKind::Axis && ctx_.suggestedAxisRange(b, lo, hi) && axisRangeLooksWrong(b.axis, lo, hi)) ++wrong;
        }
        if (wrong) {
            ImGui::TextColored(kYellow, "%d axis binding%s in this profile send%s an axis event with a percent-style output range, so the control barely moves.",
                               wrong, wrong == 1 ? "" : "s", wrong == 1 ? "s" : "");
            ImGui::SameLine();
            if (ImGui::SmallButton("Fix the ranges")) ctx_.fixAxisRanges(p);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Sets the output range of those bindings to what the event expects (for example -16383 to 16384 for a throttle axis).");
        }
    }
    ImGui::Separator();
    Profile& prof = s.ensureActive();
    if (ImGui::Button("+ Add binding")) openEditor(-1);
    ImGui::SameLine();
    {   // how the list is arranged: one list, grouped, sorted automatically, or in the order made by dragging rows
        const BindingsGroupBy by = s.bindingsGroupBy;
        std::string label = std::string("View: ") + (by == BindingsGroupBy::Device ? "by device" : by == BindingsGroupBy::InputType ? "by input type" : by == BindingsGroupBy::Custom ? "by my groups" : "one list") +
                            (s.bindingsAutoSort ? ", sorted" : "") + " ...";
        if (ImGui::Button(label.c_str())) ImGui::OpenPopup("bindings_view");
        if (ImGui::BeginPopup("bindings_view")) {
            ImGui::TextDisabled("Show the bindings");
            int pick = static_cast<int>(s.bindingsGroupBy);
            const char* options[] = {"in one list", "grouped by device", "grouped by input type (axes, buttons, hats)", "grouped by my own groups"};
            for (int i = 0; i < 4; ++i)
                if (ImGui::RadioButton(options[i], pick == i)) { s.bindingsGroupBy = static_cast<BindingsGroupBy>(i); ctx_.dirty = true; }
            ImGui::Separator();
            if (ImGui::Checkbox("Sort them automatically", &s.bindingsAutoSort)) ctx_.dirty = true;
            ImGui::TextDisabled(s.bindingsAutoSort ? "By name of group, then by device and input." : "In your order: drag a row onto another to move it.");
            if (s.bindingsGroupBy == BindingsGroupBy::Custom) {
                ImGui::Separator();
                ImGui::TextDisabled("A group is set in each binding's editor, or by dragging a row onto a group.");
                if (ImGui::Button("Give the ungrouped bindings a group")) {
                    if (autoGroupBindings(prof, ctx_.catalogFor(prof.aircraft)) > 0) ctx_.markDirty();
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Throttle for throttle levers and events, otherwise the panel section of the control in the catalog, otherwise Axes or Other.\nGroups you set yourself are left alone.");
            }
            ImGui::EndPopup();
        }
    }
    ImGui::SameLine();
    ImGui::TextDisabled("Leave the buttons you bind here unassigned in the MSFS controls menu.");

    const BindingsGroupBy by = s.bindingsGroupBy;
    const bool canReorder = !s.bindingsAutoSort;
    const bool canRegroup = by == BindingsGroupBy::Custom;
    const auto groups = arrangeBindings(prof, by, s.bindingsAutoSort, [this](const std::string& key) { return deviceDisplayName(key); });
    ImGuiTableFlags tflags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp;
    if (ImGui::BeginTable("bindings", 7, tflags, ImVec2(0, 0))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("On", ImGuiTableColumnFlags_WidthFixed, 30);
        ImGui::TableSetupColumn("Input", ImGuiTableColumnFlags_WidthStretch, 1.2f);
        ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn("On press", ImGuiTableColumnFlags_WidthStretch, 2.0f);
        ImGui::TableSetupColumn("On release", ImGuiTableColumnFlags_WidthStretch, 1.5f);
        ImGui::TableSetupColumn("Repeat", ImGuiTableColumnFlags_WidthFixed, 60);
        ImGui::TableSetupColumn("##actions", ImGuiTableColumnFlags_WidthFixed, 230);
        ImGui::TableHeadersRow();

        int toDelete = -1, moveFrom = -1, moveTo = -1;  // Up / Down: two bindings swap places
        int dropFrom = -1, dropOnRow = -1, dropOnGroup = -1;  // a dragged row, dropped on a row or on the header of a group
        std::string dropGroup;
        const std::string dragType = "BINDING_ROW";
        for (const auto& g : groups) {
            bool open = true;
            if (!g.title.empty()) {
                ImGui::TableNextRow();
                ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, ImGui::GetColorU32(ImVec4(0.20f, 0.26f, 0.38f, 0.8f)));
                ImGui::TableSetColumnIndex(1);
                char label[320];
                snprintf(label, sizeof label, "%s  (%zu)###group:%s", g.title.c_str(), g.indices.size(), g.key.c_str());
                open = ImGui::TreeNodeEx(label, ImGuiTreeNodeFlags_SpanAllColumns | ImGuiTreeNodeFlags_DefaultOpen);
                if (canRegroup && ImGui::BeginDragDropTarget()) {
                    if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload(dragType.c_str())) {
                        dropFrom = *static_cast<const int*>(pl->Data);
                        dropOnGroup = 1;
                        dropGroup = g.key;
                    }
                    ImGui::EndDragDropTarget();
                }
            }
            if (open) {
                for (size_t pos = 0; pos < g.indices.size(); ++pos) {
                    const int i = g.indices[pos];
                    Binding& b = prof.bindings[static_cast<size_t>(i)];
                    ImGui::PushID(i);
                    ImGui::TableNextRow();
                    if (inputIsPressed(b.input)) ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, ImGui::GetColorU32(ImVec4(0.2f, 0.45f, 0.2f, 0.6f)));
                    ImGui::TableSetColumnIndex(0);
                    {   // the whole row is the handle to drag it by, and the place to drop another one; the widgets drawn after it stay clickable
                        const ImVec2 rowPos = ImGui::GetCursorScreenPos();
                        ImGui::Selectable("##row", false, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap,
                                          ImVec2(0, ImGui::GetTextLineHeight() * 2 + ImGui::GetStyle().ItemSpacing.y));
                        if ((canReorder || canRegroup) && ImGui::BeginDragDropSource()) {
                            ImGui::SetDragDropPayload(dragType.c_str(), &i, sizeof i);
                            ImGui::Text("%s", b.label.empty() ? ctx_.input.describe(b.input).c_str() : b.label.c_str());
                            ImGui::EndDragDropSource();
                        }
                        if (ImGui::BeginDragDropTarget()) {
                            const ImGuiPayload* peek = ImGui::GetDragDropPayload();
                            if (peek && peek->IsDataType(dragType.c_str()) && peek->DataSize == sizeof(int)) {
                                const int from = *static_cast<const int*>(peek->Data);
                                if (from >= 0 && from < static_cast<int>(prof.bindings.size()) && from != i) {
                                    const bool sameGroup = bindingGroupKey(prof.bindings[static_cast<size_t>(from)], by) == bindingGroupKey(b, by);
                                    // moving a row past others is only meaningful inside its group; "my groups" lets it change group too
                                    const bool allowed = (canRegroup && (canReorder || !sameGroup)) || (canReorder && sameGroup);
                                    if (allowed)
                                        if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload(dragType.c_str())) { dropFrom = *static_cast<const int*>(pl->Data); dropOnRow = i; }
                                }
                            }
                            ImGui::EndDragDropTarget();
                        }
                        ImGui::SetCursorScreenPos(rowPos);
                    }
                    if (ImGui::Checkbox("##on", &b.enabled)) ctx_.markDirty();
                    ImGui::TableSetColumnIndex(1);
                    ImGui::TextUnformatted(ctx_.input.describe(b.input).c_str());
                    if (b.input.kind == input::InputKind::Axis) {
                        const float now = axisNow(b.input);
                        ImGui::TextDisabled("%s  %s%s%s", deviceDisplayName(b.input.device).c_str(), b.axis.mode == AxisMode::Steps ? "steps" : b.axis.detents.enabled ? "value, gates" : "value",
                                            now >= 0 ? "  now " : "", now >= 0 ? (std::to_string(static_cast<int>(now * 100 + 0.5f)) + "%").c_str() : "");
                    } else {
                        ImGui::TextDisabled("%s", deviceDisplayName(b.input.device).c_str());
                    }
                    ImGui::TableSetColumnIndex(2);
                    ImGui::TextUnformatted(b.label.c_str());
                    {
                        double lo, hi;
                        if (b.input.kind == input::InputKind::Axis && ctx_.suggestedAxisRange(b, lo, hi) && axisRangeLooksWrong(b.axis, lo, hi)) {
                            ImGui::SameLine();
                            ImGui::TextColored(kYellow, "(!)");
                            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Output range %g..%g, but the event expects %g..%g.", b.axis.outMin, b.axis.outMax, lo, hi);
                        }
                    }
                    auto actionLine = [&](const Action& a) {
                        ImGui::TextUnformatted(a.describe().c_str());
                        if (a.kind == ActionKind::InputEvent || a.kind == ActionKind::InputEventToggle) {
                            if (auto v = ctx_.bridge.inputEventValue(a.name)) {
                                ImGui::SameLine();
                                ImGui::TextColored(*v != 0 ? kGreen : kGrey, "[now %g]", *v);
                            }
                        }
                        if (a.kind == ActionKind::LVarSet || a.kind == ActionKind::LVarToggle) {
                            if (auto v = ctx_.bridge.lvarValue(a.name)) {
                                ImGui::SameLine();
                                ImGui::TextColored(*v != 0 ? kGreen : kGrey, "[now %g]", *v);
                            }
                        }
                    };
                    ImGui::TableSetColumnIndex(3);
                    for (const auto& a : b.onPress) actionLine(a);
                    ImGui::TableSetColumnIndex(4);
                    for (const auto& a : b.onRelease) actionLine(a);
                    ImGui::TableSetColumnIndex(5);
                    if (b.input.kind == input::InputKind::Axis) ImGui::TextDisabled("%s", b.axis.mode == AxisMode::Steps ? (std::to_string(static_cast<int>(b.axis.stepPercent + 0.5)) + "% step").c_str() : "axis");
                    else if (b.repeat) ImGui::Text("%d ms", b.repeatIntervalMs);
                    ImGui::TableSetColumnIndex(6);
                    if (ImGui::SmallButton("Edit")) openEditor(i);
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Test")) {
                        if (b.input.kind == input::InputKind::Axis && b.axis.mode == AxisMode::Value) {
                            const float now = axisNow(b.input);
                            const double out = Engine::axisOutput(b.axis, Engine::axisPosition(b.axis, now >= 0 ? now : 0.5f));
                            ctx_.engine.fire(Engine::withValue(b.onPress, out, b.axis.integer), "test " + b.label);
                        } else {
                            ctx_.engine.fire(b.onPress, "test " + b.label);
                        }
                    }
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Fire the press actions now");
                    ImGui::SameLine();
                    ImGui::BeginDisabled(!canReorder);
                    if (ImGui::SmallButton("Up") && pos > 0) { moveFrom = i; moveTo = g.indices[pos - 1]; }
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Down") && pos + 1 < g.indices.size()) { moveFrom = i; moveTo = g.indices[pos + 1]; }
                    ImGui::EndDisabled();
                    ImGui::SameLine();
                    if (ImGui::SmallButton("X")) toDelete = i;
                    ImGui::PopID();
                }
            }
            if (!g.title.empty() && open) ImGui::TreePop();
        }
        ImGui::EndTable();
        if (toDelete >= 0) { prof.bindings.erase(prof.bindings.begin() + toDelete); ctx_.markDirty(); }
        if (moveFrom >= 0) { std::swap(prof.bindings[static_cast<size_t>(moveFrom)], prof.bindings[static_cast<size_t>(moveTo)]); ctx_.markDirty(); }
        if (dropFrom >= 0 && dropFrom < static_cast<int>(prof.bindings.size())) {
            if (dropOnGroup >= 0) {
                prof.bindings[static_cast<size_t>(dropFrom)].group = dropGroup;  // dropped on the header of a group
                ctx_.markDirty();
            } else if (dropOnRow >= 0 && dropOnRow < static_cast<int>(prof.bindings.size())) {
                if (canRegroup) prof.bindings[static_cast<size_t>(dropFrom)].group = prof.bindings[static_cast<size_t>(dropOnRow)].group;
                if (canReorder) moveBinding(prof.bindings, dropFrom, dropOnRow);
                ctx_.markDirty();
            }
        }
    }
}

void Ui::drawActionList(const char* id, std::vector<Action>& actions, int listIndex) {
    ImGui::PushID(id);
    const float fs = ImGui::GetFontSize();
    int toDelete = -1;
    for (int i = 0; i < static_cast<int>(actions.size()); ++i) {
        Action& a = actions[static_cast<size_t>(i)];
        ImGui::PushID(i);
        ImGui::SetNextItemWidth(fs * 17);
        actionKindCombo("##kind", a.kind);
        ImGui::SameLine();
        if (a.kind == ActionKind::Delay) {
            ImGui::SetNextItemWidth(fs * 6);
            ImGui::InputDouble("##value", &a.value, 0, 0, "%g");
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("The actions after this one run this many milliseconds later (the program does not stand still while it waits)");
            ImGui::SameLine();
            ImGui::TextDisabled("ms, then the next one");
        } else {
            ImGui::SetNextItemWidth(fs * 22);
            const char* hint = a.kind == ActionKind::SimEvent ? "A32NX.FCU_AP_1_PUSH or GEAR_UP" :
                               a.kind == ActionKind::HEvent ? "A32NX_EFIS_L_CHRONO_PUSHED" :
                               a.kind == ActionKind::LVarSet ? "A32NX_AUTOBRAKES_ARMED_MODE_SET" :
                               a.kind == ActionKind::LVarToggle ? "A32NX_OVHD_APU_MASTER_SW_PB_IS_ON" :
                               (a.kind == ActionKind::InputEvent || a.kind == ActionKind::InputEventToggle) ? "LIGHTING_LANDING_1" : "1 (>L:A32NX_SOME_VAR)";
            ImGui::InputTextWithHint("##name", hint, &a.name);
            if (a.kind == ActionKind::SimEvent || a.kind == ActionKind::LVarSet || a.kind == ActionKind::InputEvent || a.kind == ActionKind::InputEventToggle) {
                ImGui::SameLine();
                ImGui::SetNextItemWidth(fs * 6);
                ImGui::InputDouble("##value", &a.value, 0, 0, "%g");
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip(a.kind == ActionKind::SimEvent ? "Event parameter (0 for plain push buttons)"
                                      : a.kind == ActionKind::LVarSet ? "Value written to the L: variable"
                                      : a.kind == ActionKind::InputEvent ? "Value the cockpit control is set to (switch position)"
                                                                         : "Number of positions to cycle through (2 = on/off, 3 = a three-position switch)");
            }
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("remove")) toDelete = i;
        ImGui::PopID();
    }
    if (toDelete >= 0) actions.erase(actions.begin() + toDelete);
    if (actions.empty()) ImGui::TextDisabled("Nothing yet.");
    if (ImGui::SmallButton("+ Add an action")) actions.push_back(Action{});
    ImGui::SameLine();
    // One open-flag per list: BeginPopupModal() writes false into its p_open whenever its popup is not open, so a
    // flag shared by several lists was cleared every frame by the lists that were not open.
    bool& popupOpen = catalogPopupOpen_[std::clamp(listIndex, 0, 5)];
    if (ImGui::SmallButton("+ Pick from the catalog")) { catalogTarget_ = listIndex; popupOpen = true; ImGui::OpenPopup("catalog_pick"); }
    ImGui::SameLine();
    ImGui::BeginDisabled(actions.empty());
    if (ImGui::SmallButton("Run now")) {
        if (draft_.input.kind == input::InputKind::Axis && draft_.axis.mode == AxisMode::Value && listIndex == 0) {
            const float now = axisNow(draft_.input);
            const double out = Engine::axisOutput(draft_.axis, Engine::axisPosition(draft_.axis, now >= 0 ? now : 0.5f));
            ctx_.engine.fire(Engine::withValue(actions, out, draft_.axis.integer), "test (axis at its current position)");
        } else {
            ctx_.engine.fire(actions, "test");
        }
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Carry out this list now, as a test (needs the simulator)");
    ImGui::SetNextWindowSize(ImVec2(ImGui::GetMainViewport()->WorkSize.x * 0.85f, ImGui::GetMainViewport()->WorkSize.y * 0.8f), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("catalog_pick", &popupOpen, ImGuiWindowFlags_NoSavedSettings)) {
        drawCatalogBrowser("picker", true);
        ImGui::EndPopup();
    }
    ImGui::PopID();
}

void Ui::drawBindingEditor() {
    ImGui::SetNextWindowSize(ImVec2(920, 640), ImGuiCond_FirstUseEver);
    const char* title = editIndex_ >= 0 ? "Edit binding###binding_editor" : "New binding###binding_editor";
    if (!ImGui::Begin(title, &editorOpen_)) { ImGui::End(); return; }
    const float fs = ImGui::GetFontSize();

    // Input capture
    const bool capturing = ctx_.engine.capturing();
    if (auto captured = ctx_.engine.takeCaptured()) {
        draft_.input = *captured;
        // New axis binding for an axis event still on the 0..100 default: use the range the event expects.
        double lo, hi;
        if (captured->kind == input::InputKind::Axis && draft_.axis.outMin == 0 && draft_.axis.outMax == 100 &&
            ctx_.suggestedAxisRange(draft_, lo, hi) && axisRangeLooksWrong(draft_.axis, lo, hi)) {
            draft_.axis.outMin = lo;
            draft_.axis.outMax = hi;
            draft_.axis.integer = true;
        }
    }
    fieldLabel("Input");
    if (draft_.input.device.empty()) ImGui::TextColored(kYellow, "none yet");
    else ImGui::Text("%s  on  %s", ctx_.input.describe(draft_.input).c_str(), deviceDisplayName(draft_.input.device).c_str());
    ImGui::SameLine();
    if (!capturing) {
        if (ImGui::Button("Choose by pressing it")) ctx_.engine.beginCapture();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Then press the button, move the hat, or move the axis about a quarter of its travel.");
    } else {
        ImGui::TextColored(kYellow, "Press the button, move the hat or move the axis a quarter of its travel...");
        ImGui::SameLine();
        if (ImGui::Button("Cancel##listen")) ctx_.engine.cancelCapture();
    }
    fieldLabel("Name");
    ImGui::SetNextItemWidth(fs * 16);
    ImGui::InputTextWithHint("##label", "e.g. AP1 push", &draft_.label);
    ImGui::SameLine();
    ImGui::TextDisabled("Group");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(fs * 11);
    ImGui::InputTextWithHint("##group", "optional", &draft_.group);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("For the \"My groups\" view of the list of bindings (for example Throttle).");
    ImGui::SameLine();
    if (ImGui::SmallButton("pick...")) ImGui::OpenPopup("group_pick");
    if (ImGui::BeginPopup("group_pick")) {
        std::set<std::string> names;
        for (const auto& b : ctx_.settings.ensureActive().bindings)
            if (!b.group.empty()) names.insert(b.group);
        names.insert("Throttle");
        if (ImGui::Selectable("(no group)")) draft_.group.clear();
        for (const auto& n : names)
            if (ImGui::Selectable(n.c_str(), n == draft_.group)) draft_.group = n;
        ImGui::EndPopup();
    }

    const bool isAxis = draft_.input.kind == input::InputKind::Axis;
    const bool axisValue = isAxis && draft_.axis.mode == AxisMode::Value;
    if (isAxis) drawAxisOptions();

    if (!isAxis) {
        ImGui::SeparatorText("When pressed");
        drawActionList("press", draft_.onPress, 0);
        ImGui::SeparatorText("When released");
        ImGui::TextDisabled("Useful for 2-position switches: set a value when pressed, another when released.");
        drawActionList("release", draft_.onRelease, 1);
    } else if (axisValue) {
        ImGui::SeparatorText("What it sends");
        ImGui::TextDisabled("The value of the axis goes in as the parameter of a sim event, the value of an L: variable, or {v} in RPN code.");
        drawActionList("press", draft_.onPress, 0);
        if (draft_.axis.detents.enabled) {
            ImGui::SeparatorText("At the gates");
            ImGui::TextDisabled("Run once when the lever reaches a gate or leaves it. What follows a Wait runs later.");
            if (ImGui::BeginTabBar("gates")) {
                if (ImGui::BeginTabItem("Cut-off")) {
                    ImGui::TextUnformatted("When the lever reaches the cut-off");
                    drawActionList("cutoff", draft_.onCutoff, 2);
                    ImGui::Spacing();
                    ImGui::TextUnformatted("When it leaves the cut-off");
                    drawActionList("cutoff_leave", draft_.onCutoffLeave, 3);
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("Afterburner")) {
                    ImGui::TextUnformatted("When the lever reaches the afterburner");
                    drawActionList("afterburner", draft_.onAfterburner, 4);
                    ImGui::Spacing();
                    ImGui::TextUnformatted("When it leaves the afterburner");
                    drawActionList("afterburner_leave", draft_.onAfterburnerLeave, 5);
                    ImGui::EndTabItem();
                }
                ImGui::EndTabBar();
            }
        }
    } else {
        ImGui::SeparatorText("When the axis moves up (+)");
        drawActionList("press", draft_.onPress, 0);
        ImGui::SeparatorText("When it moves down (-)");
        drawActionList("release", draft_.onRelease, 1);
    }

    ImGui::SeparatorText("Options");
    if (!isAxis) ImGui::Checkbox("Repeat the press actions while the button is held", &draft_.repeat);
    if (!isAxis && draft_.repeat) {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(fs * 6);
        ImGui::InputInt("first after (ms)", &draft_.repeatDelayMs);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(fs * 6);
        ImGui::InputInt("then every (ms)", &draft_.repeatIntervalMs);
        draft_.repeatDelayMs = std::max(0, draft_.repeatDelayMs);
        draft_.repeatIntervalMs = std::max(10, draft_.repeatIntervalMs);
    }
    ImGui::Checkbox("This binding is on", &draft_.enabled);

    ImGui::Separator();
    const bool canSave = !draft_.input.device.empty();
    if (!canSave) ImGui::BeginDisabled();
    if (ImGui::Button("Save", ImVec2(fs * 9, 0))) {
        Profile& p = ctx_.settings.ensureActive();
        if (editIndex_ >= 0 && editIndex_ < static_cast<int>(p.bindings.size())) p.bindings[static_cast<size_t>(editIndex_)] = draft_;
        else p.bindings.push_back(draft_);
        ctx_.markDirty();
        editorOpen_ = false;
        ctx_.engine.cancelCapture();
    }
    if (!canSave) ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(fs * 9, 0))) { editorOpen_ = false; ctx_.engine.cancelCapture(); }
    if (!canSave) { ImGui::SameLine(); ImGui::TextDisabled("Choose the input first."); }
    ImGui::End();
    if (!editorOpen_) ctx_.engine.cancelCapture();
}

}  // namespace lr::app
