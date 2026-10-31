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

#include "app/Paths.hpp"
#include "util/Log.hpp"

namespace lr::app {

using namespace uidetail;

// ---------------------------------------------------------------- Bindings

void Ui::drawDetentOptions() {
    AxisConfig& c = draft_.axis;
    AxisDetents& d = c.detents;
    const AxisCalibration* cal = ctx_.settings.calibrationFor(draft_.input);
    const bool ok = cal && cal->valid();
    ImGui::Indent();
    if (ok) {
        const float now = axisNow(draft_.input);
        drawGateBar(*cal, now, 460);
        if (now >= 0) {
            const auto r = Engine::detentOutput(d, *cal, now, AxisZone::Dry);
            ImGui::TextDisabled("The lever is in the %s zone.", r.zone == AxisZone::Cutoff ? "cut-off" : r.zone == AxisZone::Afterburner ? "afterburner" : "dry range (idle to military power)");
        }
    } else {
        ImGui::TextColored(kYellow, "This axis is not calibrated%s: until it is, the lever acts as a plain axis.", cal ? " properly" : " yet");
    }
    if (ImGui::Button(ok ? "Calibrate again..." : "Calibrate this axis...")) openCalibration(draft_.input);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Move the lever to its cut-off, idle, end-of-dry-range and afterburner positions and say when it is there.\nDone once per axis; every profile uses it.");

    ImGui::SetNextItemWidth(110);
    ImGui::InputDouble("Value at the cut-off", &d.outCutoff, 0, 0, "%g");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(110);
    ImGui::InputDouble("at idle", &d.outIdle, 0, 0, "%g");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(110);
    ImGui::InputDouble("at the end of the dry range", &d.outMil, 0, 0, "%g");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Also the value while in the afterburner zone. Between idle and here the value follows the lever linearly.");
    const bool fractional = d.outIdle != std::round(d.outIdle) || d.outMil != std::round(d.outMil) || d.outCutoff != std::round(d.outCutoff);
    if (c.integer && fractional) {
        ImGui::TextColored(kYellow, "'Whole numbers' is on: these values would be rounded.");
        ImGui::SameLine();
        if (ImGui::SmallButton("Turn it off")) c.integer = false;
    }
    ImGui::TextUnformatted("Preset");
    for (int engine = 1; engine <= 2; ++engine) {
        ImGui::SameLine();
        char label[64];
        snprintf(label, sizeof label, "F/A-18E throttle %d", engine);
        if (ImGui::SmallButton(label)) {
            // Asobo F/A-18E: the lever is a fraction of its travel (0 off stop, 0.1 idle stop, 0.8 military power); afterburner is its own event
            d.outCutoff = 0.0; d.outIdle = 0.1; d.outMil = 0.8;
            c.integer = false;
            const std::string n = std::to_string(engine);
            Action thr, burn, burnOff;
            thr.kind = burn.kind = burnOff.kind = ActionKind::InputEvent;
            thr.name = "ENGINE_Throttle_" + n;
            burn.name = burnOff.name = "ENGINE_Throttle_" + n + "_Afterburner";
            burn.value = 1;
            burnOff.value = 0;
            draft_.onPress = {thr};
            draft_.onAfterburner = {burn};
            draft_.onAfterburnerLeave = {burnOff};
            if (draft_.label.empty()) draft_.label = "F/A-18E throttle " + n;
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("0 / 0.1 / 0.8 on ENGINE_Throttle_%d (verified on the aircraft), and the afterburner event on entering and leaving the afterburner zone.\n"
                              "That afterburner event has NOT been tried on a real aircraft, and the engine cut-off is not understood there yet:\nthe cut-off lists are left empty.", engine);
    }
    ImGui::Unindent();
}

void Ui::drawAxisOptions() {
    AxisConfig& c = draft_.axis;
    ImGui::SeparatorText("Axis");
    const float now = axisNow(draft_.input);
    ImGui::TextUnformatted("Position now");
    ImGui::SameLine();
    const double t = Engine::axisPosition(c, now >= 0 ? now : 0.0f);
    const AxisCalibration* cal = c.detents.enabled && c.mode == AxisMode::Value ? ctx_.settings.calibrationFor(draft_.input) : nullptr;
    const bool gates = cal && cal->valid();  // the lever's gates decide the output instead of the range below
    double shown = Engine::axisOutput(c, t);
    if (gates && now >= 0) shown = Engine::detentOutput(c.detents, *cal, now, AxisZone::Dry).value;
    char overlay[96];
    if (now < 0) snprintf(overlay, sizeof overlay, "device not connected");
    else if (c.mode == AxisMode::Value) snprintf(overlay, sizeof overlay, "%d%% of travel  ->  %s", static_cast<int>(now * 100 + 0.5f), Engine::formatValue(shown, c.integer && !gates).c_str());
    else snprintf(overlay, sizeof overlay, "%d%% of travel", static_cast<int>(now * 100 + 0.5f));
    ImGui::ProgressBar(now < 0 ? 0.0f : now, ImVec2(360, 0), overlay);

    int mode = c.mode == AxisMode::Steps ? 1 : 0;
    ImGui::SetNextItemWidth(420);
    if (ImGui::Combo("Mode", &mode, "Value: the position is sent as the parameter\0Steps: up runs the + actions, down the - actions\0")) c.mode = mode == 1 ? AxisMode::Steps : AxisMode::Value;
    if (c.mode == AxisMode::Value) {
        ImGui::Checkbox("Lever with gates (cut-off, idle, afterburner)", &c.detents.enabled);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("For a throttle with mechanical gates: the calibration learns where they are on the axis, and the value sent\nfollows them (a flat idle, a linear dry range, a fixed value in the afterburner), with actions at the gates.");
    } else {
        c.detents.enabled = false;
    }
    if (c.detents.enabled) drawDetentOptions();
    if (!gates) ImGui::Checkbox("Invert", &c.invert);

    float from = static_cast<float>(c.inMin * 100.0), to = static_cast<float>(c.inMax * 100.0);
    if (!gates) {
        ImGui::SetNextItemWidth(160);
        if (ImGui::SliderFloat("Use travel from (%)", &from, 0.0f, 98.0f, "%.0f")) c.inMin = std::min<double>(from / 100.0, c.inMax - 0.02);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(160);
        if (ImGui::SliderFloat("to (%)", &to, 2.0f, 100.0f, "%.0f")) c.inMax = std::max<double>(to / 100.0, c.inMin + 0.02);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Everything outside this part of the physical travel is dead zone: it reads as the minimum or the maximum.");
    }

    if (c.mode == AxisMode::Value && gates) {
        ImGui::Checkbox("Whole numbers", &c.integer);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(90);
        ImGui::InputInt("min ms between updates", &c.minIntervalMs, 0, 0);
        c.minIntervalMs = std::clamp(c.minIntervalMs, 5, 1000);
    } else if (c.mode == AxisMode::Value) {
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
        ImGui::SetNextItemWidth(380);
        if (ImGui::BeginCombo("Output range", current)) {
            for (const auto& pr : presets)
                if (ImGui::Selectable(pr.label, c.outMin == pr.lo && c.outMax == pr.hi)) { c.outMin = pr.lo; c.outMax = pr.hi; c.integer = pr.integer; }
            ImGui::EndCombo();
        }
        double lo, hi;
        if (ctx_.suggestedAxisRange(draft_, lo, hi)) {
            if (axisRangeLooksWrong(c, lo, hi)) {
                ImGui::TextColored(kYellow, "This axis event expects %g to %g, but the output range is %g to %g: the control would barely move.", lo, hi, c.outMin, c.outMax);
                ImGui::SameLine();
                if (ImGui::SmallButton("Set range")) { c.outMin = lo; c.outMax = hi; c.integer = true; }
            } else {
                ImGui::TextDisabled("This axis event expects %g to %g.", lo, hi);
            }
        }
        ImGui::SetNextItemWidth(110);
        ImGui::InputDouble("Output at start", &c.outMin, 0, 0, "%g");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(110);
        ImGui::InputDouble("at end", &c.outMax, 0, 0, "%g");
        ImGui::SameLine();
        ImGui::Checkbox("Whole numbers", &c.integer);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(90);
        ImGui::InputInt("min ms between updates", &c.minIntervalMs, 0, 0);
        c.minIntervalMs = std::clamp(c.minIntervalMs, 5, 1000);
    } else {
        float step = static_cast<float>(c.stepPercent);
        ImGui::SetNextItemWidth(220);
        if (ImGui::SliderFloat("One step per (% of travel)", &step, 1.0f, 50.0f, "%.0f")) c.stepPercent = step;
        ImGui::TextDisabled("Example: 5%% = 20 steps over the full travel. Sweeping fast sends at most 10 steps per movement.");
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
    if (ImGui::Button("+ Add binding")) openEditor(-1);
    ImGui::SameLine();
    ImGui::TextDisabled("Rows light up while their input is held. Buttons bound here should stay unassigned in the MSFS controls menu.");

    Profile& prof = s.ensureActive();
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

        int toDelete = -1, moveFrom = -1, moveTo = -1;
        for (int i = 0; i < static_cast<int>(prof.bindings.size()); ++i) {
            Binding& b = prof.bindings[static_cast<size_t>(i)];
            ImGui::PushID(i);
            ImGui::TableNextRow();
            if (inputIsPressed(b.input)) ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, ImGui::GetColorU32(ImVec4(0.2f, 0.45f, 0.2f, 0.6f)));
            ImGui::TableSetColumnIndex(0);
            if (ImGui::Checkbox("##on", &b.enabled)) ctx_.markDirty();
            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(ctx_.input.describe(b.input).c_str());
            if (b.input.kind == input::InputKind::Axis) {
                const float now = axisNow(b.input);
                ImGui::TextDisabled("%s  %s%s%s", deviceDisplayName(b.input.device).c_str(), b.axis.mode == AxisMode::Steps ? "steps" : "value",
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
            if (ImGui::SmallButton("Up") && i > 0) { moveFrom = i; moveTo = i - 1; }
            ImGui::SameLine();
            if (ImGui::SmallButton("Down") && i + 1 < static_cast<int>(prof.bindings.size())) { moveFrom = i; moveTo = i + 1; }
            ImGui::SameLine();
            if (ImGui::SmallButton("X")) toDelete = i;
            ImGui::PopID();
        }
        ImGui::EndTable();
        if (toDelete >= 0) { prof.bindings.erase(prof.bindings.begin() + toDelete); ctx_.markDirty(); }
        if (moveFrom >= 0) { std::swap(prof.bindings[static_cast<size_t>(moveFrom)], prof.bindings[static_cast<size_t>(moveTo)]); ctx_.markDirty(); }
    }
}

void Ui::drawActionList(const char* id, std::vector<Action>& actions, int listIndex) {
    ImGui::PushID(id);
    int toDelete = -1;
    for (int i = 0; i < static_cast<int>(actions.size()); ++i) {
        Action& a = actions[static_cast<size_t>(i)];
        ImGui::PushID(i);
        ImGui::SetNextItemWidth(260);
        actionKindCombo("##kind", a.kind);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(330);
        const char* hint = a.kind == ActionKind::SimEvent ? "A32NX.FCU_AP_1_PUSH or GEAR_UP" :
                           a.kind == ActionKind::HEvent ? "A32NX_EFIS_L_CHRONO_PUSHED" :
                           a.kind == ActionKind::LVarSet ? "A32NX_AUTOBRAKES_ARMED_MODE_SET" :
                           a.kind == ActionKind::LVarToggle ? "A32NX_OVHD_APU_MASTER_SW_PB_IS_ON" :
                           (a.kind == ActionKind::InputEvent || a.kind == ActionKind::InputEventToggle) ? "LIGHTING_LANDING_1" : "1 (>L:A32NX_SOME_VAR)";
        ImGui::InputTextWithHint("##name", hint, &a.name);
        if (a.kind == ActionKind::SimEvent || a.kind == ActionKind::LVarSet || a.kind == ActionKind::InputEvent || a.kind == ActionKind::InputEventToggle) {
            ImGui::SameLine();
            ImGui::SetNextItemWidth(110);
            ImGui::InputDouble("##value", &a.value, 0, 0, "%g");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(a.kind == ActionKind::SimEvent ? "Event parameter (0 for plain push buttons)"
                                  : a.kind == ActionKind::LVarSet ? "Value written to the L: var"
                                  : a.kind == ActionKind::InputEvent ? "Value the cockpit input event is set to (switch position)"
                                                                     : "Number of positions to cycle through (2 = on/off, 3 = a three-position switch)");
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("x")) toDelete = i;
        ImGui::PopID();
    }
    if (toDelete >= 0) actions.erase(actions.begin() + toDelete);
    if (ImGui::SmallButton("+ action")) actions.push_back(Action{});
    ImGui::SameLine();
    // One open-flag per list: BeginPopupModal() writes false into its p_open whenever its popup is not open, so a
    // flag shared by the press and release lists was cleared every frame by the list that was not open.
    bool& popupOpen = catalogPopupOpen_[std::clamp(listIndex, 0, 5)];
    if (ImGui::SmallButton("+ from catalog")) { catalogTarget_ = listIndex; popupOpen = true; ImGui::OpenPopup("catalog_pick"); }
    ImGui::SameLine();
    if (ImGui::SmallButton("test")) {
        if (draft_.input.kind == input::InputKind::Axis && draft_.axis.mode == AxisMode::Value && listIndex == 0) {
            const float now = axisNow(draft_.input);
            const double out = Engine::axisOutput(draft_.axis, Engine::axisPosition(draft_.axis, now >= 0 ? now : 0.5f));
            ctx_.engine.fire(Engine::withValue(actions, out, draft_.axis.integer), "test (axis at its current position)");
        } else {
            ctx_.engine.fire(actions, "test");
        }
    }
    ImGui::SetNextWindowSize(ImVec2(ImGui::GetMainViewport()->WorkSize.x * 0.85f, ImGui::GetMainViewport()->WorkSize.y * 0.8f), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("catalog_pick", &popupOpen, ImGuiWindowFlags_NoSavedSettings)) {
        drawCatalogBrowser("picker", true);
        ImGui::EndPopup();
    }
    ImGui::PopID();
}

void Ui::drawBindingEditor() {
    ImGui::SetNextWindowSize(ImVec2(900, 560), ImGuiCond_FirstUseEver);
    const char* title = editIndex_ >= 0 ? "Edit binding###binding_editor" : "New binding###binding_editor";
    if (!ImGui::Begin(title, &editorOpen_)) { ImGui::End(); return; }

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
    ImGui::TextUnformatted("Input:");
    ImGui::SameLine();
    if (draft_.input.device.empty()) ImGui::TextColored(kYellow, "none yet");
    else ImGui::Text("%s  on  %s", ctx_.input.describe(draft_.input).c_str(), deviceDisplayName(draft_.input.device).c_str());
    ImGui::SameLine();
    if (!capturing) {
        if (ImGui::Button("Listen for input")) ctx_.engine.beginCapture();
    } else {
        ImGui::TextColored(kYellow, "Press a button, move a hat, or move an axis about a quarter of its travel...");
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) ctx_.engine.cancelCapture();
    }
    ImGui::InputTextWithHint("Label", "e.g. AP1 push", &draft_.label);

    const bool isAxis = draft_.input.kind == input::InputKind::Axis;
    const bool axisValue = isAxis && draft_.axis.mode == AxisMode::Value;
    if (isAxis) drawAxisOptions();

    ImGui::SeparatorText(!isAxis ? "On press" : axisValue ? "Actions (the axis value is the parameter)" : "Axis moves up (+)");
    if (axisValue) ImGui::TextDisabled("K: events get the value as their parameter, L: var set uses it as the value to write, and RPN code can contain {v}.");
    drawActionList("press", draft_.onPress, 0);
    if (axisValue && draft_.axis.detents.enabled) {
        ImGui::SeparatorText("Lever gates: what to do at the cut-off and the afterburner");
        ImGui::TextDisabled("Each list runs once when the lever enters or leaves that zone (after the calibration is done).");
        ImGui::TextUnformatted("Enters the cut-off zone");
        drawActionList("cutoff", draft_.onCutoff, 2);
        ImGui::TextUnformatted("Leaves the cut-off zone");
        drawActionList("cutoff_leave", draft_.onCutoffLeave, 3);
        ImGui::TextUnformatted("Enters the afterburner zone");
        drawActionList("afterburner", draft_.onAfterburner, 4);
        ImGui::TextUnformatted("Leaves the afterburner zone");
        drawActionList("afterburner_leave", draft_.onAfterburnerLeave, 5);
    }
    if (!axisValue) {
        ImGui::SeparatorText(!isAxis ? "On release" : "Axis moves down (-)");
        if (!isAxis) ImGui::TextDisabled("Useful for 2-position switches: set a value on press, another on release.");
        drawActionList("release", draft_.onRelease, 1);
    }

    ImGui::SeparatorText("Options");
    if (!isAxis) ImGui::Checkbox("Repeat press actions while held", &draft_.repeat);
    if (!isAxis && draft_.repeat) {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(100);
        ImGui::InputInt("delay ms", &draft_.repeatDelayMs);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(100);
        ImGui::InputInt("interval ms", &draft_.repeatIntervalMs);
        draft_.repeatDelayMs = std::max(0, draft_.repeatDelayMs);
        draft_.repeatIntervalMs = std::max(10, draft_.repeatIntervalMs);
    }
    ImGui::Checkbox("Enabled", &draft_.enabled);

    ImGui::Separator();
    const bool canSave = !draft_.input.device.empty();
    if (!canSave) ImGui::BeginDisabled();
    if (ImGui::Button("Save binding", ImVec2(140, 0))) {
        Profile& p = ctx_.settings.ensureActive();
        if (editIndex_ >= 0 && editIndex_ < static_cast<int>(p.bindings.size())) p.bindings[static_cast<size_t>(editIndex_)] = draft_;
        else p.bindings.push_back(draft_);
        ctx_.markDirty();
        editorOpen_ = false;
        ctx_.engine.cancelCapture();
    }
    if (!canSave) ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(100, 0))) { editorOpen_ = false; ctx_.engine.cancelCapture(); }
    if (!canSave) { ImGui::SameLine(); ImGui::TextDisabled("Capture an input first."); }
    ImGui::End();
    if (!editorOpen_) ctx_.engine.cancelCapture();
}

}  // namespace lr::app
