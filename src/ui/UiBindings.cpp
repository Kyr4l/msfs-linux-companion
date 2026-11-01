// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kyr4l
#include "ui/Ui.hpp"
#include "app/AxisTargets.hpp"
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
        const char* names[4] = {"cut-off", "idle", "max", "afterburner"};
        for (int g = 0; g < 4; ++g)
            if (codes[g] >= 0) gates += std::string(gates.empty() ? "" : ", ") + names[g] + " = " + ctx_.input.describe({draft_.input.device, input::InputKind::Button, codes[g], 0});
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + fs * 40);
        ImGui::TextColored(kGreen, "Calibrated%s%s", gates.empty() ? "" : ": ", gates.c_str());
        ImGui::PopTextWrapPos();
        const float now = axisNow(draft_.input);
        drawGateBar(*cal, now, fs * 36);
        if (now >= 0) {
            const auto r = Engine::detentOutput(d, *cal, now, AxisZone::Dry, gateStateNow(*cal));
            ImGui::TextDisabled("Lever: %s", r.zone == AxisZone::Cutoff ? "cut-off" : r.zone == AxisZone::Afterburner ? "afterburner" : "normal");
        }
    } else {
        ImGui::TextColored(kYellow, "Not calibrated");
    }
    if (ImGui::Button("Calibrate")) openCalibration(draft_.input);

    ImGui::Spacing();
    ImGui::TextUnformatted("Values");
    ImGui::SameLine();
    ImGui::TextDisabled("cut-off");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(fs * 5);
    ImGui::InputDouble("##outCutoff", &d.outCutoff, 0, 0, "%g");
    ImGui::SameLine();
    ImGui::TextDisabled("idle");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(fs * 5);
    ImGui::InputDouble("##outIdle", &d.outIdle, 0, 0, "%g");
    ImGui::SameLine();
    ImGui::TextDisabled("max");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(fs * 5);
    ImGui::InputDouble("##outMil", &d.outMil, 0, 0, "%g");
    const bool fractional = d.outIdle != std::round(d.outIdle) || d.outMil != std::round(d.outMil) || d.outCutoff != std::round(d.outCutoff);
    if (c.integer && fractional) {
        ImGui::TextColored(kYellow, "Values are rounded.");
        ImGui::SameLine();
        if (ImGui::SmallButton("Allow decimals")) c.integer = false;
    }

    // the presets only mean something for that one aircraft: out of the way (folded) for every other
    ImGui::Spacing();
    const bool forFa18 = sim::sameAircraft(ctx_.settings.ensureActive().aircraft, "Asobo_FA18E");
    const bool showPresets = ImGui::TreeNodeEx("F/A-18E presets", forFa18 ? ImGuiTreeNodeFlags_DefaultOpen : ImGuiTreeNodeFlags_None);
    for (int engine = 1; showPresets && engine <= 2; ++engine) {
        if (engine == 2) ImGui::SameLine();
        if (ImGui::Button(engine == 1 ? "Throttle 1" : "Throttle 2")) {
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
    }
    if (showPresets) ImGui::TreePop();
    ImGui::Unindent(fs);
}

// What the axis does: how it sends, the throttle gates, and the range of the values.
void Ui::drawAxisOptions() {
    AxisConfig& c = draft_.axis;
    const float fs = ImGui::GetFontSize();
    ImGui::SeparatorText("Axis");
    const float now = axisNow(draft_.input);
    const AxisCalibration* cal = c.detents.enabled && c.mode == AxisMode::Value ? ctx_.settings.calibrationFor(draft_.input) : nullptr;
    const bool gates = cal && cal->valid();  // the lever's gates decide the output instead of the range
    double shown = Engine::axisOutput(c, Engine::axisPosition(c, now >= 0 ? now : 0.0f));
    if (gates && now >= 0) shown = Engine::detentOutput(c.detents, *cal, now, AxisZone::Dry, gateStateNow(*cal)).value;
    char overlay[96];
    if (now < 0) snprintf(overlay, sizeof overlay, "No device");
    else if (c.mode == AxisMode::Value) snprintf(overlay, sizeof overlay, "%d%%  ->  %s", static_cast<int>(now * 100 + 0.5f), Engine::formatValue(shown, c.integer && !gates).c_str());
    else snprintf(overlay, sizeof overlay, "%d%%", static_cast<int>(now * 100 + 0.5f));
    fieldLabel("Position");
    ImGui::ProgressBar(now < 0 ? 0.0f : now, ImVec2(fs * 26, 0), overlay);

    fieldLabel("Mode");
    if (ImGui::RadioButton("Value", c.mode == AxisMode::Value)) c.mode = AxisMode::Value;
    ImGui::SameLine();
    if (ImGui::RadioButton("Steps", c.mode == AxisMode::Steps)) { c.mode = AxisMode::Steps; c.detents.enabled = false; }

    if (c.mode == AxisMode::Value) {
        fieldLabel("Throttle");
        ImGui::Checkbox("Gates", &c.detents.enabled);
        if (c.detents.enabled) drawDetentOptions();
    }

    if (c.mode == AxisMode::Steps) {
        float step = static_cast<float>(c.stepPercent);
        fieldLabel("Step size");
        ImGui::SetNextItemWidth(fs * 12);
        if (ImGui::SliderFloat("##step", &step, 1.0f, 50.0f, "%.0f%%")) c.stepPercent = step;
    } else if (!gates) {
        struct Preset { const char* label; double lo, hi; bool integer; };
        static const Preset presets[] = {
            {"0 to 100", 0, 100, true},
            {"-16384 to 16383", -16384, 16383, true},
            {"0 to 16383", 0, 16383, true},
            {"0 to 1", 0, 1, false},
        };
        const char* current = "Custom";
        for (const auto& pr : presets)
            if (c.outMin == pr.lo && c.outMax == pr.hi) current = pr.label;
        fieldLabel("Range");
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
                ImGui::TextColored(kYellow, "Range should be %g to %g", lo, hi);
                ImGui::SameLine();
                if (ImGui::SmallButton("Fix")) { c.outMin = lo; c.outMax = hi; c.integer = true; }
            } else {
                ImGui::TextDisabled("Event range: %g to %g", lo, hi);
            }
        }
    }

    // the rarely changed settings
    if (ImGui::TreeNode("More")) {
        if (!gates) {
            ImGui::Checkbox("Invert", &c.invert);
            float from = static_cast<float>(c.inMin * 100.0), to = static_cast<float>(c.inMax * 100.0);
            ImGui::SetNextItemWidth(fs * 8);
            if (ImGui::SliderFloat("Min travel (%)", &from, 0.0f, 98.0f, "%.0f")) c.inMin = std::min<double>(from / 100.0, c.inMax - 0.02);
            ImGui::SetNextItemWidth(fs * 8);
            if (ImGui::SliderFloat("Max travel (%)", &to, 2.0f, 100.0f, "%.0f")) c.inMax = std::max<double>(to / 100.0, c.inMin + 0.02);
        }
        if (c.mode == AxisMode::Value) {
            ImGui::Checkbox("Round to integers", &c.integer);
            ImGui::SetNextItemWidth(fs * 6);
            ImGui::InputInt("Min interval (ms)", &c.minIntervalMs, 0, 0);
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
    insertAt_ = -1;
    draftIsCopy_ = false;
    editorOpen_ = true;
    ctx_.engine.cancelCapture();
}

void Ui::openEditorWith(std::vector<Action> press, const std::string& label) {
    ctx_.settings.ensureActive();
    draft_ = Binding{};
    draft_.label = label;
    draft_.onPress = std::move(press);
    editIndex_ = -1;
    insertAt_ = -1;
    draftIsCopy_ = false;
    editorOpen_ = true;
    ctx_.engine.cancelCapture();
    ctx_.engine.beginCapture();  // the next thing wanted is the button
}

// A copy keeps everything the binding does (its actions, its axis settings, its group, its repeat) and has no input yet: it is for another
// button or axis, the same one would fire twice. The editor opens waiting for that input, and the copy is only added to the profile
// when it is saved, right after the original.
void Ui::openEditorCopy(int bindingIndex) {
    const Profile& p = ctx_.settings.ensureActive();
    if (bindingIndex < 0 || bindingIndex >= static_cast<int>(p.bindings.size())) return;
    draft_ = p.bindings[static_cast<size_t>(bindingIndex)];
    draft_.input.device.clear();
    draft_.input.code = 0;
    draft_.input.dir = 0;
    if (!draft_.label.empty()) draft_.label += " copy";
    editIndex_ = -1;
    insertAt_ = bindingIndex + 1;
    draftIsCopy_ = true;
    editorOpen_ = true;
    ctx_.engine.cancelCapture();
    ctx_.engine.beginCapture();
}

bool Ui::bindingMatches(const Binding& b, const std::vector<std::string>& words) const {
    if (words.empty()) return true;
    std::string text = b.label + ' ' + b.group + ' ' + ctx_.input.describe(b.input) + ' ' + deviceDisplayName(b.input.device);
    for (const auto* list : {&b.onPress, &b.onRelease, &b.onCutoff, &b.onCutoffLeave, &b.onAfterburner, &b.onAfterburnerLeave})
        for (const Action& a : *list) text += ' ' + a.describe();
    return matchesAll(std::move(text), words);
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
    ImGui::SameLine();
    if (ImGui::Button("New")) {
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
    ImGui::BeginDisabled(s.profiles.size() <= 1);  // the last profile cannot be deleted
    if (ImGui::Button("Delete")) ImGui::OpenPopup("confirm_delete_profile");
    ImGui::EndDisabled();
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
        ImGui::Text("Delete \"%s\"?", p.name.c_str());
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
            ImGui::TextColored(kYellow, "Loaded: %s. Profile: %s.", loaded.label.c_str(), profLabel.c_str());
            ImGui::SameLine();
            const Profile* match = nullptr;
            for (const auto& prof : s.profiles)
                if (sim::sameAircraft(prof.aircraft, det)) { match = &prof; break; }
            if (match) {
                if (ImGui::SmallButton((std::string("Use ") + match->name).c_str())) {
                    s.activeProfile = match->name;
                    viewAircraft_.clear();
                    ctx_.markDirty();
                }
            } else if (ImGui::SmallButton("New profile")) {
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
            ImGui::TextColored(kYellow, "%d axis binding%s with the wrong range", wrong, wrong == 1 ? "" : "s");
            ImGui::SameLine();
            if (ImGui::SmallButton("Fix")) ctx_.fixAxisRanges(p);
        }
    }
    ImGui::Separator();
    Profile& prof = s.ensureActive();
    if (ImGui::Button("Add binding")) openEditor(-1);
    ImGui::SameLine();
    {   // how the list is arranged: one list, grouped, sorted automatically, or in the order made by dragging rows
        if (ImGui::Button("View")) ImGui::OpenPopup("bindings_view");
        if (ImGui::BeginPopup("bindings_view")) {
            ImGui::TextDisabled("Group by");
            int pick = static_cast<int>(s.bindingsGroupBy);
            const char* options[] = {"None", "Device", "Input type", "My groups"};
            for (int i = 0; i < 4; ++i)
                if (ImGui::RadioButton(options[i], pick == i)) { s.bindingsGroupBy = static_cast<BindingsGroupBy>(i); ctx_.dirty = true; }
            ImGui::Separator();
            if (ImGui::Checkbox("Sort", &s.bindingsAutoSort)) ctx_.dirty = true;
            if (s.bindingsGroupBy == BindingsGroupBy::Custom) {
                ImGui::Separator();
                if (ImGui::Button("Auto-group")) {
                    if (autoGroupBindings(prof, ctx_.catalogFor(prof.aircraft)) > 0) ctx_.markDirty();
                }
            }
            ImGui::EndPopup();
        }
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 14);
    ImGui::InputTextWithHint("##binding_filter", "Filter", &bindingFilter_);
    ImGui::SameLine();
    ImGui::TextDisabled("Unbind these buttons in MSFS.");

    const std::vector<std::string> filterWords = searchWords(bindingFilter_);
    const BindingsGroupBy by = s.bindingsGroupBy;
    const bool canReorder = !s.bindingsAutoSort && filterWords.empty();  // Up and Down move past rows that a filter hides
    const bool canRegroup = by == BindingsGroupBy::Custom;
    const auto groups = arrangeBindings(prof, by, s.bindingsAutoSort, [this](const std::string& key) { return deviceDisplayName(key); });
    ImGuiTableFlags tflags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp;
    if (ImGui::BeginTable("bindings", 7, tflags, ImVec2(0, 0))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("On", ImGuiTableColumnFlags_WidthFixed, 30);
        ImGui::TableSetupColumn("Input", ImGuiTableColumnFlags_WidthStretch, 1.2f);
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn("Press", ImGuiTableColumnFlags_WidthStretch, 2.0f);
        ImGui::TableSetupColumn("Release", ImGuiTableColumnFlags_WidthStretch, 1.5f);
        ImGui::TableSetupColumn("Repeat", ImGuiTableColumnFlags_WidthFixed, 60);
        ImGui::TableSetupColumn("##actions", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 21);
        ImGui::TableHeadersRow();

        int toDelete = -1, moveFrom = -1, moveTo = -1;  // Up / Down: two bindings swap places
        int dropFrom = -1, dropOnRow = -1, dropOnGroup = -1;  // a dragged row, dropped on a row or on the header of a group
        std::string dropGroup;
        const std::string dragType = "BINDING_ROW";
        int rowsShown = 0;
        for (const auto& g : groups) {
            std::vector<int> shown;  // the rows of the group that pass the filter box
            for (int i : g.indices)
                if (bindingMatches(prof.bindings[static_cast<size_t>(i)], filterWords)) shown.push_back(i);
            if (shown.empty()) continue;
            rowsShown += static_cast<int>(shown.size());
            bool open = true;
            if (!g.title.empty()) {
                ImGui::TableNextRow();
                ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, ImGui::GetColorU32(ImVec4(0.20f, 0.26f, 0.38f, 0.8f)));
                ImGui::TableSetColumnIndex(1);
                char label[320];
                snprintf(label, sizeof label, "%s  (%zu)###group:%s", g.title.c_str(), shown.size(), g.key.c_str());
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
                for (size_t pos = 0; pos < shown.size(); ++pos) {
                    const int i = shown[pos];
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
                                            now >= 0 ? "  " : "", now >= 0 ? (std::to_string(static_cast<int>(now * 100 + 0.5f)) + "%").c_str() : "");
                    } else {
                        ImGui::TextDisabled("%s", deviceDisplayName(b.input.device).c_str());
                    }
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", deviceDisplayName(b.input.device).c_str());  // the column clips a long device name
                    ImGui::TableSetColumnIndex(2);
                    ImGui::TextUnformatted(b.label.c_str());
                    {
                        double lo, hi;
                        if (b.input.kind == input::InputKind::Axis && ctx_.suggestedAxisRange(b, lo, hi) && axisRangeLooksWrong(b.axis, lo, hi)) {
                            ImGui::SameLine();
                            ImGui::TextColored(kYellow, "(!)");
                            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Range should be %g to %g", lo, hi);
                        }
                    }
                    auto actionLine = [&](const Action& a) {
                        ImGui::TextUnformatted(a.describe().c_str());
                        if (a.kind == ActionKind::InputEvent || a.kind == ActionKind::InputEventToggle) {
                            if (auto v = ctx_.bridge.inputEventValue(a.name)) {
                                ImGui::SameLine();
                                ImGui::TextColored(*v != 0 ? kGreen : kGrey, "[%g]", *v);
                            }
                        }
                        if (a.kind == ActionKind::LVarSet || a.kind == ActionKind::LVarToggle) {
                            if (auto v = ctx_.bridge.lvarValue(a.name)) {
                                ImGui::SameLine();
                                ImGui::TextColored(*v != 0 ? kGreen : kGrey, "[%g]", *v);
                            }
                        }
                    };
                    ImGui::TableSetColumnIndex(3);
                    for (const auto& a : b.onPress) actionLine(a);
                    ImGui::TableSetColumnIndex(4);
                    for (const auto& a : b.onRelease) actionLine(a);
                    ImGui::TableSetColumnIndex(5);
                    if (b.input.kind == input::InputKind::Axis) { if (b.axis.mode == AxisMode::Steps) ImGui::TextDisabled("%d%%", static_cast<int>(b.axis.stepPercent + 0.5)); }
                    else if (b.repeat) ImGui::Text("%d ms", b.repeatIntervalMs);
                    ImGui::TableSetColumnIndex(6);
                    if (ImGui::SmallButton("Edit")) openEditor(i);
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Copy")) openEditorCopy(i);
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
                    ImGui::SameLine();
                    ImGui::BeginDisabled(!canReorder);
                    if (ImGui::SmallButton("Up") && pos > 0) { moveFrom = i; moveTo = shown[pos - 1]; }
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Down") && pos + 1 < shown.size()) { moveFrom = i; moveTo = shown[pos + 1]; }
                    ImGui::EndDisabled();
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Delete")) ImGui::OpenPopup("delete_binding");
                    if (ImGui::BeginPopup("delete_binding")) {
                        ImGui::Text("Delete \"%s\"?", b.label.empty() ? ctx_.input.describe(b.input).c_str() : b.label.c_str());
                        if (ImGui::Button("Delete")) { toDelete = i; ImGui::CloseCurrentPopup(); }
                        ImGui::SameLine();
                        if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
                        ImGui::EndPopup();
                    }
                    ImGui::PopID();
                }
            }
            if (!g.title.empty() && open) ImGui::TreePop();
        }
        if (rowsShown == 0 && !filterWords.empty()) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(1);
            ImGui::TextDisabled("No matches");
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
            ImGui::SameLine();
            ImGui::TextDisabled("ms");
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
                    ImGui::SetTooltip(a.kind == ActionKind::SimEvent ? "Parameter"
                                      : a.kind == ActionKind::LVarSet ? "Value"
                                      : a.kind == ActionKind::InputEvent ? "Value"
                                                                         : "Positions");
            }
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Remove")) toDelete = i;
        ImGui::PopID();
    }
    if (toDelete >= 0) actions.erase(actions.begin() + toDelete);
    if (actions.empty()) ImGui::TextDisabled("No actions");
    if (ImGui::SmallButton("Add action")) actions.push_back(Action{});
    ImGui::SameLine();
    // One open-flag per list: BeginPopupModal() writes false into its p_open whenever its popup is not open, so a
    // flag shared by several lists was cleared every frame by the lists that were not open.
    bool& popupOpen = catalogPopupOpen_[std::clamp(listIndex, 0, 5)];
    if (ImGui::SmallButton("From catalog")) { catalogTarget_ = listIndex; popupOpen = true; ImGui::OpenPopup("catalog_pick"); }
    ImGui::SameLine();
    ImGui::BeginDisabled(actions.empty());
    if (ImGui::SmallButton("Run")) {
        if (draft_.input.kind == input::InputKind::Axis && draft_.axis.mode == AxisMode::Value && listIndex == 0) {
            const float now = axisNow(draft_.input);
            const double out = Engine::axisOutput(draft_.axis, Engine::axisPosition(draft_.axis, now >= 0 ? now : 0.5f));
            ctx_.engine.fire(Engine::withValue(actions, out, draft_.axis.integer), "test (axis at its current position)");
        } else {
            ctx_.engine.fire(actions, "test");
        }
    }
    ImGui::EndDisabled();
    ImGui::SetNextWindowSize(ImVec2(ImGui::GetMainViewport()->WorkSize.x * 0.85f, ImGui::GetMainViewport()->WorkSize.y * 0.8f), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("Catalog###catalog_pick", &popupOpen, ImGuiWindowFlags_NoSavedSettings)) {
        drawCatalogBrowser("picker", true);
        ImGui::EndPopup();
    }
    ImGui::PopID();
}

// "Control: [Throttle 1 v]": points an axis at one of the plain flight controls every aircraft has (see AxisTargets.hpp). It
// replaces the press list with the one sim event and sets the range the event takes.
void Ui::drawAxisTargetPicker() {
    const float fs = ImGui::GetFontSize();
    const AxisTarget* current = nullptr;
    if (draft_.onPress.size() == 1 && draft_.onPress[0].kind == ActionKind::SimEvent) current = findAxisTarget(draft_.onPress[0].name);
    fieldLabel("Control");
    ImGui::SetNextItemWidth(fs * 20);
    if (ImGui::BeginCombo("##axis_target", current ? current->label : "Select", ImGuiComboFlags_HeightLargest)) {
        const char* group = nullptr;
        for (const auto& t : axisTargets()) {
            if (!group || std::strcmp(group, t.group) != 0) {
                group = t.group;
                ImGui::SeparatorText(group);
            }
            if (ImGui::Selectable(t.label, &t == current)) {
                Action a;
                a.kind = ActionKind::SimEvent;
                a.name = t.event;
                draft_.onPress = {a};
                draft_.axis.outMin = t.lo;
                draft_.axis.outMax = t.hi;
                draft_.axis.integer = true;
                if (draft_.label.empty()) draft_.label = t.label;
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("K:%s, %g to %g", t.event, t.lo, t.hi);
        }
        ImGui::EndCombo();
    }
}

void Ui::drawBindingEditor() {
    ImGui::SetNextWindowSize(ImVec2(920, 640), ImGuiCond_FirstUseEver);
    const char* title = editIndex_ >= 0 ? "Edit binding###binding_editor" : draftIsCopy_ ? "Copy binding###binding_editor" : "New binding###binding_editor";
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
    if (!draft_.input.device.empty() || !capturing) {  // while listening for it, the prompt on this line says what is wanted
        if (draft_.input.device.empty()) ImGui::TextColored(kYellow, "None");
        else ImGui::Text("%s (%s)", ctx_.input.describe(draft_.input).c_str(), deviceDisplayName(draft_.input.device).c_str());
        ImGui::SameLine();
    }
    if (!capturing) {
        if (ImGui::Button("Detect")) ctx_.engine.beginCapture();
    } else {
        ImGui::TextColored(kYellow, "Press a button or move an axis...");
        ImGui::SameLine();
        if (ImGui::Button("Cancel##listen")) ctx_.engine.cancelCapture();
    }
    fieldLabel("Name");
    ImGui::SetNextItemWidth(fs * 16);
    ImGui::InputTextWithHint("##label", "Name", &draft_.label);
    ImGui::SameLine();
    ImGui::TextDisabled("Group");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(fs * 11);
    ImGui::InputTextWithHint("##group", "", &draft_.group);
    ImGui::SameLine();
    if (ImGui::SmallButton("Pick")) ImGui::OpenPopup("group_pick");
    if (ImGui::BeginPopup("group_pick")) {
        std::set<std::string> names;
        for (const auto& b : ctx_.settings.ensureActive().bindings)
            if (!b.group.empty()) names.insert(b.group);
        names.insert("Throttle");
        if (ImGui::Selectable("None")) draft_.group.clear();
        for (const auto& n : names)
            if (ImGui::Selectable(n.c_str(), n == draft_.group)) draft_.group = n;
        ImGui::EndPopup();
    }

    const bool isAxis = draft_.input.kind == input::InputKind::Axis;
    const bool axisValue = isAxis && draft_.axis.mode == AxisMode::Value;
    if (isAxis) drawAxisOptions();

    if (!isAxis) {
        ImGui::SeparatorText("On press");
        drawActionList("press", draft_.onPress, 0);
        ImGui::SeparatorText("On release");
        drawActionList("release", draft_.onRelease, 1);
    } else if (axisValue) {
        ImGui::SeparatorText("Action");
        if (!draft_.axis.detents.enabled) drawAxisTargetPicker();
        drawActionList("press", draft_.onPress, 0);
        if (draft_.axis.detents.enabled) {
            ImGui::SeparatorText("Gates");
            if (ImGui::BeginTabBar("gates")) {
                if (ImGui::BeginTabItem("Cut-off")) {
                    ImGui::TextUnformatted("Enter cut-off");
                    drawActionList("cutoff", draft_.onCutoff, 2);
                    ImGui::Spacing();
                    ImGui::TextUnformatted("Leave cut-off");
                    drawActionList("cutoff_leave", draft_.onCutoffLeave, 3);
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("Afterburner")) {
                    ImGui::TextUnformatted("Enter afterburner");
                    drawActionList("afterburner", draft_.onAfterburner, 4);
                    ImGui::Spacing();
                    ImGui::TextUnformatted("Leave afterburner");
                    drawActionList("afterburner_leave", draft_.onAfterburnerLeave, 5);
                    ImGui::EndTabItem();
                }
                ImGui::EndTabBar();
            }
        }
    } else {
        ImGui::SeparatorText("Up (+)");
        drawActionList("press", draft_.onPress, 0);
        ImGui::SeparatorText("Down (-)");
        drawActionList("release", draft_.onRelease, 1);
    }

    ImGui::SeparatorText("Options");
    if (!isAxis) ImGui::Checkbox("Repeat while held", &draft_.repeat);
    if (!isAxis && draft_.repeat) {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(fs * 6);
        ImGui::InputInt("Delay (ms)", &draft_.repeatDelayMs);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(fs * 6);
        ImGui::InputInt("Interval (ms)", &draft_.repeatIntervalMs);
        draft_.repeatDelayMs = std::max(0, draft_.repeatDelayMs);
        draft_.repeatIntervalMs = std::max(10, draft_.repeatIntervalMs);
    }
    ImGui::Checkbox("Enabled", &draft_.enabled);

    ImGui::Separator();
    const bool canSave = !draft_.input.device.empty();
    if (!canSave) ImGui::BeginDisabled();
    if (ImGui::Button("Save", ImVec2(fs * 9, 0))) {
        Profile& p = ctx_.settings.ensureActive();
        if (editIndex_ >= 0 && editIndex_ < static_cast<int>(p.bindings.size())) p.bindings[static_cast<size_t>(editIndex_)] = draft_;
        else if (insertAt_ >= 0 && insertAt_ <= static_cast<int>(p.bindings.size())) p.bindings.insert(p.bindings.begin() + insertAt_, draft_);
        else p.bindings.push_back(draft_);
        ctx_.markDirty();
        editorOpen_ = false;
        ctx_.engine.cancelCapture();
    }
    if (!canSave) ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(fs * 9, 0))) { editorOpen_ = false; ctx_.engine.cancelCapture(); }
    if (!canSave) { ImGui::SameLine(); ImGui::TextDisabled("No input"); }
    ImGui::End();
    if (!editorOpen_) ctx_.engine.cancelCapture();
}

}  // namespace lr::app
