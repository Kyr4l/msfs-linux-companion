// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kyr4l
#include "ui/Ui.hpp"
#include "app/AxisTargets.hpp"
#include "ui/UiCommon.hpp"
#include "ui/DeviceLayouts.hpp"
#include "ui/Theme.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <ctime>
#include <filesystem>

#include <imgui.h>
#include <imgui_stdlib.h>

#include "app/BindingView.hpp"
#include "app/Paths.hpp"
#include "plugin/PluginManager.hpp"
#include "util/Log.hpp"

namespace lr::app {

using namespace uidetail;

// ---------------------------------------------------------------- Bindings

// Lever presets. Values only; the press list is kept, except the F/A-18E's, which is the cockpit's own input event.
enum class LeverPreset { Linear, AirbusFbw, Fa18Left, Fa18Right };

static void applyLeverPreset(LeverPreset which, Binding& draft, const AxisCalibration* cal, AppContext& ctx) {
    AxisConfig& c = draft.axis;
    AxisDetents& d = c.detents;
    d.stages.clear();
    switch (which) {
        case LeverPreset::Linear: {
            // idle to the end of the travel, the whole range of the event; the cut-off sends idle
            double lo = c.outMin, hi = c.outMax;
            if (ctx.suggestedAxisRange(draft, lo, hi)) c.integer = true;
            d.top = LeverTop::Full;
            d.outCutoff = lo; d.outIdle = lo; d.outMil = hi;
            break;
        }
        case LeverPreset::AirbusFbw: {
            // FlyByWire A32NX / A380X, THROTTLEn_AXIS_SET_EX1 (-16384..16384). The defaults of its throttle mapping (fbw-common
            // ThrottleAxisMapping.cpp, axis -1..1): idle -0.55..-0.45, CL -0.05..0.05, FLX/MCT 0.45..0.55, TOGA 0.95..1. The middle of each region
            // is sent; FLX/MCT goes on the max gate of the lever, TOGA on its end, CL a little past half way to the gate.
            d.top = LeverTop::Full;
            d.outCutoff = -8192; d.outIdle = -8192; d.outMil = 16384;
            c.integer = true;
            const double gate = cal && cal->valid() ? Engine::leverAt(d, *cal, cal->mil) : 0.75;
            d.stages = {{"CL", gate * 0.55, 0, 0.05}, {"FLX/MCT", gate, 8192, 0.03}, {"TOGA", 1.0, 16384, 0.03}};
            break;
        }
        case LeverPreset::Fa18Left:
        case LeverPreset::Fa18Right: {
            // Asobo F/A-18E (the cockpit's own template, read from the sim files and checked on the aircraft): the throttle input event is a
            // fraction of the lever travel; the sim's own Idle preset sets 0 and military power is 0.8.
            //  * The cut-off needs the throttle at exactly 0 when STOP is set, and a fast pull from above lands on the idle gate (10 %) first,
            //    so the sequence is: 0, wait, 0 again, wait, STOP. A throttle value that changes the throttle clears STOP by itself, but
            //    leaving the cut-off at 0 changes nothing, so STOP is cleared explicitly.
            //  * The afterburner sets the throttle to L:XMLVAR_LastAB<n>ThrottleSet, which is 0 until the game's own throttle is used, so
            //    it is set to 1 first; leaving sends the throttle value again.
            const int engine = which == LeverPreset::Fa18Left ? 1 : 2;
            d.top = LeverTop::Max;
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
            draft.onPress = {event(thr, 0)};
            draft.onCutoff = {wait(100), event(thr, 0), wait(100), event(thr + "_STOP", 1)};
            draft.onCutoffLeave = {event(thr + "_STOP", 0)};
            draft.onAfterburner = {lastAb, wait(100), event(thr + "_Afterburner", 1)};
            draft.onAfterburnerLeave = {event(thr + "_Afterburner", 0)};
            if (draft.label.empty()) draft.label = engine == 1 ? "Left throttle" : "Right throttle";
            break;
        }
    }
}

static void sortStages(AxisDetents& d) {
    std::stable_sort(d.stages.begin(), d.stages.end(), [](const LeverStage& a, const LeverStage& b) { return a.at < b.at; });
}

// Lever recipes (app/LeverZones.hpp): the ones for the profile's aircraft as cards, the others in a list. Applying one fills in the
// lever below (range, stages, what the zones do); the editor then says whether the lever still is what the recipe made.
void Ui::drawLeverRecipes(const AxisCalibration* cal) {
    const float fs = ImGui::GetFontSize();
    const bool ok = cal && cal->valid();
    const double gateAt = ok ? Engine::leverAt(draft_.axis.detents, *cal, cal->mil) : -1.0;
    const int engine = recipeEngine_ > 0 ? recipeEngine_ : leverEngine(draft_);
    const std::string aircraft = ctx_.settings.ensureActive().aircraft;
    auto forThis = [&](const LeverRecipe& r) {
        for (const auto& a : r.aircraft) if (sim::sameAircraft(a, aircraft)) return true;
        return false;
    };
    auto apply = [&](const LeverRecipe& r) {
        applyLeverRecipe(r, engine, gateAt, draft_);
        cutoffCustom_ = r.cutoffMode == ZoneMode::Actions && (!r.onCutoff.empty() || !r.onCutoffLeave.empty());
    };

    fieldLabel("Start from");
    ImGui::TextDisabled("a recipe fills in the lever below");
    ImGui::SameLine();
    ImGui::TextDisabled("· engine");
    ImGui::SameLine();
    int eng = engine;
    ImGui::SetNextItemWidth(fs * 2.5f);
    if (ImGui::InputInt("##recipe_engine", &eng, 0)) recipeEngine_ = std::clamp(eng, 1, 8);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("The engine this lever moves: {n} in the recipe's event and variable names");

    // cards for this aircraft (and the user's own), side by side, wrapping
    const float cardW = fs * 15.5f;
    int shown = 0;
    for (size_t i = 0; i < ctx_.leverRecipes.size(); ++i) {
        const LeverRecipe& r = ctx_.leverRecipes[i];
        if (!forThis(r) && !r.userMade) continue;
        if (shown > 0) {
            const float next = ImGui::GetItemRectMax().x + ImGui::GetStyle().ItemSpacing.x + cardW;
            if (next < ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x) ImGui::SameLine();
        }
        ++shown;
        ImGui::PushID(static_cast<int>(i));
        const bool current = draft_.recipe == r.id;
        if (current) ImGui::PushStyleColor(ImGuiCol_Border, kAccent);
        ImGui::BeginChild("card", ImVec2(cardW, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY);
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextUnformatted(r.title.c_str());
        ImGui::PopTextWrapPos();
        ImGui::TextColored(r.verified ? kGreen : kYellow, "%s", r.verified ? "checked" : "not tried");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", r.checked.c_str());
        ImGui::SameLine();
        if (current) ImGui::TextDisabled("applied");
        else if (ImGui::SmallButton("Apply")) apply(r);
        ImGui::EndChild();
        if (current) ImGui::PopStyleColor();
        ImGui::PopID();
    }
    if (shown > 0) ImGui::SameLine();
    ImGui::SetNextItemWidth(fs * 11);
    if (ImGui::BeginCombo("##other_recipes", shown ? "Other recipes" : "Recipes", ImGuiComboFlags_HeightLarge)) {
        if (ImGui::Selectable("Plain lever (idle to full)")) { applyLeverPreset(LeverPreset::Linear, draft_, cal, ctx_); draft_.recipe.clear(); cutoffCustom_ = false; }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("No stages, the whole range of the event");
        for (size_t i = 0; i < ctx_.leverRecipes.size(); ++i) {
            const LeverRecipe& r = ctx_.leverRecipes[i];
            if (forThis(r) || r.userMade) continue;
            ImGui::PushID(static_cast<int>(i));
            if (ImGui::Selectable(r.title.c_str())) apply(r);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", r.checked.c_str());
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }

    // where the lever came from, and whether it still is that
    const LeverRecipe* from = nullptr;
    for (const auto& r : ctx_.leverRecipes) if (r.id == draft_.recipe) from = &r;
    if (from) {
        fieldLabel("");
        ImGui::TextDisabled("From \"%s\"", from->title.c_str());
        if (!leverMatchesRecipe(*from, engine, gateAt, draft_)) {
            ImGui::SameLine();
            ImGui::TextColored(kYellow, "changed");
            ImGui::SameLine();
            if (ImGui::SmallButton("Back to the recipe")) apply(*from);
        }
    }
    fieldLabel("");
    if (ImGui::SmallButton("Save as recipe...")) {
        newRecipeTitle_ = draft_.label.empty() ? "My lever" : draft_.label;
        ImGui::OpenPopup("save_recipe");
    }
    if (ImGui::BeginPopup("save_recipe")) {
        ImGui::TextUnformatted("Name");
        ImGui::SetNextItemWidth(fs * 18);
        ImGui::InputText("##recipe_title", &newRecipeTitle_);
        ImGui::TextDisabled("Kept in my_levers.json in the settings folder;\nengine numbers become {n}.");
        const bool named = !newRecipeTitle_.empty();
        if (!named) ImGui::BeginDisabled();
        if (ImGui::Button("Save")) {
            std::string id = "my-" + std::to_string(static_cast<long long>(std::time(nullptr)));
            LeverRecipe r = recipeFromBinding(draft_, id, newRecipeTitle_);
            r.aircraft = {aircraft};
            ctx_.leverRecipes.push_back(r);
            std::string err;
            if (!saveUserLeverRecipes(ctx_.userLeverRecipesPath(), ctx_.leverRecipes, err)) LOG_ERROR("Saving your lever recipes failed: %s", err.c_str());
            else draft_.recipe = id;
            ImGui::CloseCurrentPopup();
        }
        if (!named) ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

// The cut-off zone: nothing (the lever only sends its cut-off value), reverse thrust run by the program, or the user's own actions.
void Ui::drawCutoffZone() {
    const float fs = ImGui::GetFontSize();
    ReverseZone& r = draft_.reverse;
    enum { kNothing, kReverse, kCustom };
    const int mode = draft_.cutoffMode == ZoneMode::Reverse ? kReverse
                   : (cutoffCustom_ || !draft_.onCutoff.empty() || !draft_.onCutoffLeave.empty()) ? kCustom : kNothing;
    const char* names[] = {"Nothing", "Reverse thrust", "Custom actions"};
    fieldLabel("What it does");
    ImGui::SetNextItemWidth(fs * 13);
    if (ImGui::BeginCombo("##cutoff_mode", names[mode])) {
        if (ImGui::Selectable(names[kNothing], mode == kNothing)) {
            draft_.cutoffMode = ZoneMode::Actions;
            draft_.onCutoff.clear();
            draft_.onCutoffLeave.clear();
            cutoffCustom_ = false;
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("The lever only sends its cut-off value");
        if (ImGui::Selectable(names[kReverse], mode == kReverse)) {
            draft_.cutoffMode = ZoneMode::Reverse;
            cutoffCustom_ = false;
            r.engine = leverEngine(draft_);
            if (r.reversedCode.empty())  // the aircraft's values from a recipe for it, when there is one
                for (const auto& rec : ctx_.leverRecipes) {
                    bool forThis = false;
                    for (const auto& a : rec.aircraft) forThis |= sim::sameAircraft(a, ctx_.settings.ensureActive().aircraft);
                    if (!forThis || rec.cutoffMode != ZoneMode::Reverse) continue;
                    r = rec.reverse;
                    r.engine = leverEngine(draft_);
                    r.otherEngine = rec.reverseShared ? (r.engine == 1 ? 2 : 1) : 0;
                    break;
                }
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("The program switches reverse on and sends the reverse amount");
        if (ImGui::Selectable(names[kCustom], mode == kCustom)) {
            draft_.cutoffMode = ZoneMode::Actions;
            cutoffCustom_ = true;
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Your own actions when the lever enters and leaves the cut-off");
        ImGui::EndCombo();
    }

    if (mode == kCustom) {
        ImGui::TextUnformatted("Enter cut-off");
        drawActionList("cutoff", draft_.onCutoff, 2);
        ImGui::Spacing();
        ImGui::TextUnformatted("Leave cut-off");
        drawActionList("cutoff_leave", draft_.onCutoffLeave, 3);
        return;
    }
    if (mode == kNothing) {
        ImGui::TextDisabled("The lever sends its cut-off value (%g) and nothing else.", draft_.axis.detents.outCutoff);
        return;
    }

    // reverse thrust
    bool shared = r.otherEngine > 0;
    fieldLabel("");
    ImGui::Checkbox("Only on the ground", &r.groundOnly);
    fieldLabel("");
    if (ImGui::Checkbox("Shared with lever", &shared)) r.otherEngine = shared ? (r.engine == 1 ? 2 : 1) : 0;
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("One reverse switch for both engines (an Airbus):\nreverse stays on while either lever is in its cut-off");
    if (shared) {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(fs * 2.5f);
        ImGui::InputInt("##other_engine", &r.otherEngine, 0);
        r.otherEngine = std::clamp(r.otherEngine, 1, 8);
    }
    fieldLabel("Amount");
    ImGui::SetNextItemWidth(fs * 8);
    ImGui::InputDouble("##rev_amount", &r.amount, 0, 0, "%.0f");
    ImGui::SameLine();
    ImGui::TextDisabled("axis value in reverse (maximum: the top of the event)");
    fieldLabel("Reverse idle");
    ImGui::SetNextItemWidth(fs * 8);
    ImGui::InputDouble("##rev_idle", &r.idle, 0, 0, "%.0f");
    ImGui::SameLine();
    ImGui::TextDisabled("axis value when the lever leaves");
    if (r.reversedCode.empty()) {
        fieldLabel("");
        ImGui::TextColored(kYellow, "Set \"Reverse is on when\" for this aircraft below: until then the zone does nothing.");
    }
    if (ImGui::TreeNode("Aircraft values")) {
        fieldLabel("Reverse switch");
        ImGui::SetNextItemWidth(fs * 20);
        ImGui::InputText("##rev_toggle", &r.toggleEvent);
        ImGui::SameLine();
        ImGui::TextDisabled("K: event");
        fieldLabel("Reverse is on when");
        ImGui::SetNextItemWidth(fs * 24);
        ImGui::InputTextWithHint("##rev_reversed", "(L:INI_AUTOTHRUST_TLA:{n}, Number) -1 <", &r.reversedCode);
        ImGui::SameLine();
        ImGui::TextDisabled("{n} = engine");
        ImGui::TreePop();
    }

    ImGui::Spacing();
    ImGui::TextUnformatted("What happens");
    ImGui::Indent(fs);
    ImGui::TextWrapped("1. The lever enters the cut-off: reverse switches on%s, if it is off.", r.groundOnly ? " (on the ground only)" : "");
    ImGui::TextWrapped("2. Once the aircraft reports reverse, engine %d goes to %.0f.", r.engine, r.amount);
    ImGui::TextWrapped("3. The lever leaves: engine %d goes to %.0f (reverse idle).", r.engine, r.idle);
    if (r.otherEngine > 0) ImGui::TextWrapped("4. Reverse switches off once neither lever is in its cut-off.");
    else ImGui::TextWrapped("4. Reverse switches off.");
    ImGui::TextDisabled("It never sends forward thrust: without reverse the lever stays at its cut-off value.");
    ImGui::Unindent(fs);
    if (ImGui::TreeNode("What it sends")) {
        const std::string axis = leverAxisEvent(draft_);
        for (int pass = 0; pass < 2; ++pass) {
            ImGui::TextDisabled(pass == 0 ? "Entering" : "Leaving");
            for (const auto& a : reverseZoneActions(r, axis, pass == 0)) {
                ImGui::Bullet();
                ImGui::TextWrapped("%s", a.describe().c_str());
            }
        }
        ImGui::TreePop();
    }
}

// The throttle with gates: calibration, the part of the lever that is used, presets, the graph and the points of the curve.
void Ui::drawDetentOptions() {
    AxisConfig& c = draft_.axis;
    AxisDetents& d = c.detents;
    const AxisCalibration* cal = ctx_.settings.calibrationFor(draft_.input);
    const bool ok = cal && cal->valid();
    const float fs = ImGui::GetFontSize();
    const float now = axisNow(draft_.input);

    fieldLabel("Lever");
    dot(ok ? kGreen : kYellow);
    ImGui::TextUnformatted(ok ? "Calibrated" : "Not calibrated");
    if (ok && ImGui::IsItemHovered()) {
        std::string gates;
        const int codes[4] = {cal->cutoffBtn, cal->idleBtn, cal->milBtn, cal->abBtn};
        const char* names[4] = {"Cut-off", "Idle", "Max", "Afterburner"};
        for (int g = 0; g < 4; ++g)
            if (codes[g] >= 0) gates += std::string(gates.empty() ? "" : "\n") + names[g] + ": " + ctx_.input.describe({draft_.input.device, input::InputKind::Button, codes[g], 0});
        if (!gates.empty()) ImGui::SetTooltip("%s", gates.c_str());
    }
    ImGui::SameLine();
    if (ImGui::SmallButton(ok ? "Recalibrate" : "Calibrate")) openCalibration(draft_.input);

    fieldLabel("Range");
    if (ImGui::RadioButton("Idle to max gate", d.top == LeverTop::Max)) d.top = LeverTop::Max;
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Afterburner zone stays at max");
    ImGui::SameLine();
    if (ImGui::RadioButton("Idle to end", d.top == LeverTop::Full)) d.top = LeverTop::Full;

    drawLeverRecipes(cal);

    if (ok) {
        ImGui::Spacing();
        drawLeverGraph(d, *cal, now, std::min(ImGui::GetContentRegionAvail().x, fs * 44), fs * 9.5f);
    }

    // the points of the curve: cut-off and idle, the stages, the top
    ImGui::Spacing();
    const ImGuiTableFlags tf = ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_NoHostExtendX;
    int removeStage = -1;
    bool resort = false;
    const Engine::DetentResult live = ok && now >= 0 ? Engine::detentOutput(d, *cal, now, AxisZone::Dry, gateStateNow(*cal)) : Engine::DetentResult{0, AxisZone::Dry, -2, 0};
    if (ImGui::BeginTable("lever_points", 5, tf)) {
        ImGui::TableSetupColumn("Point", ImGuiTableColumnFlags_WidthFixed, fs * 9);
        ImGui::TableSetupColumn("Position", ImGuiTableColumnFlags_WidthFixed, fs * 5.5f);
        ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthFixed, fs * 7);
        ImGui::TableSetupColumn("Band", ImGuiTableColumnFlags_WidthFixed, fs * 4.5f);
        ImGui::TableSetupColumn("##do", ImGuiTableColumnFlags_WidthFixed, fs * 8);
        ImGui::TableHeadersRow();
        auto fixedRow = [&](const char* name, const char* where, double& value, bool active) {
            ImGui::TableNextRow();
            if (active) ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, ImGui::GetColorU32(ui::palette::kAccentSoft));
            ImGui::TableSetColumnIndex(0);
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(name);
            ImGui::TableSetColumnIndex(1);
            ImGui::AlignTextToFramePadding();
            ImGui::TextDisabled("%s", where);
            ImGui::TableSetColumnIndex(2);
            ImGui::SetNextItemWidth(-FLT_MIN);
            ImGui::PushID(name);
            ImGui::InputDouble("##v", &value, 0, 0, "%g");
            ImGui::PopID();
        };
        fixedRow("Cut-off", "", d.outCutoff, live.zone == AxisZone::Cutoff);
        fixedRow("Idle", "0 %", d.outIdle, live.stage == -1 && live.zone != AxisZone::Cutoff && live.at <= 0.0);
        for (size_t i = 0; i < d.stages.size(); ++i) {
            LeverStage& st = d.stages[i];
            ImGui::PushID(static_cast<int>(i));
            ImGui::TableNextRow();
            if (live.stage == static_cast<int>(i)) ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, ImGui::GetColorU32(ui::palette::kAccentSoft));
            ImGui::TableSetColumnIndex(0);
            ImGui::SetNextItemWidth(-FLT_MIN);
            ImGui::InputTextWithHint("##name", "Name", &st.name);
            ImGui::TableSetColumnIndex(1);
            float pct = static_cast<float>(st.at * 100.0);
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::DragFloat("##at", &pct, 0.2f, 0.0f, 100.0f, "%.0f %%", ImGuiSliderFlags_AlwaysClamp)) st.at = pct / 100.0;
            if (ImGui::IsItemDeactivatedAfterEdit()) resort = true;
            ImGui::TableSetColumnIndex(2);
            ImGui::SetNextItemWidth(-FLT_MIN);
            ImGui::InputDouble("##v", &st.value, 0, 0, "%g");
            ImGui::TableSetColumnIndex(3);
            float band = static_cast<float>(st.band * 100.0);
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::DragFloat("##band", &band, 0.1f, 0.0f, 20.0f, "%.0f %%", ImGuiSliderFlags_AlwaysClamp)) st.band = band / 100.0;
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Snap width");
            ImGui::TableSetColumnIndex(4);
            ImGui::BeginDisabled(!ok || now < 0);
            if (ImGui::SmallButton("Set")) { st.at = Engine::leverAt(d, *cal, now); resort = true; }
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Lever position");
            ImGui::SameLine();
            if (ImGui::SmallButton("Remove")) removeStage = static_cast<int>(i);
            ImGui::PopID();
        }
        // a stage at the very top (TOGA) is what the top sends: the row of the top would only be a second, unused value
        const bool stageAtTop = std::any_of(d.stages.begin(), d.stages.end(), [](const LeverStage& st) { return st.at >= 0.999; });
        if (!stageAtTop) fixedRow(d.top == LeverTop::Full ? "End" : "Max", "100 %", d.outMil, live.stage == -1 && live.zone != AxisZone::Cutoff && live.at >= 1.0);
        ImGui::EndTable();
    }
    if (removeStage >= 0) d.stages.erase(d.stages.begin() + removeStage);
    if (resort && leverDrag_ < 0) sortStages(d);
    ImGui::BeginDisabled(d.stages.size() >= 16);
    if (ImGui::SmallButton("Add stage")) {
        LeverStage st;
        st.name = "Stage " + std::to_string(d.stages.size() + 1);
        if (ok && now >= 0) st.at = Engine::leverAt(d, *cal, now);
        st.value = Engine::leverCurve(d, st.at).value;  // where the curve already is: adding a stage changes nothing until it is edited
        d.stages.push_back(st);
        sortStages(d);
    }
    ImGui::EndDisabled();
    const bool fractional = d.outIdle != std::round(d.outIdle) || d.outMil != std::round(d.outMil) || d.outCutoff != std::round(d.outCutoff) ||
                            std::any_of(d.stages.begin(), d.stages.end(), [](const LeverStage& st) { return st.value != std::round(st.value); });
    if (c.integer && fractional) {
        ImGui::SameLine();
        ImGui::TextColored(kYellow, "Values are rounded");
        ImGui::SameLine();
        if (ImGui::SmallButton("Allow decimals")) c.integer = false;
    }
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
    else if (c.mode == AxisMode::Value) snprintf(overlay, sizeof overlay, "%d%%  ->  %s", static_cast<int>(now * 100 + 0.5f), Engine::formatValue(shown, c.integer).c_str());
    else snprintf(overlay, sizeof overlay, "%d%%", static_cast<int>(now * 100 + 0.5f));
    fieldLabel("Position");
    ImGui::ProgressBar(now < 0 ? 0.0f : now, ImVec2(fs * 26, 0), overlay);

    fieldLabel("Mode");
    if (ImGui::RadioButton("Value", c.mode == AxisMode::Value)) c.mode = AxisMode::Value;
    ImGui::SameLine();
    if (ImGui::RadioButton("Steps", c.mode == AxisMode::Steps)) { c.mode = AxisMode::Steps; c.detents.enabled = false; }
    if (!gates) {  // a calibrated lever with gates is read the right way round by its calibration
        ImGui::SameLine();
        ImGui::Checkbox("Invert", &c.invert);
    }

    if (c.mode == AxisMode::Value) {
        fieldLabel("Throttle");
        if (ImGui::Checkbox("Gates", &c.detents.enabled) && c.detents.enabled) {
            // switched on for the first time: idle to the end with the range of the event, not the old F/A-18E numbers
            const AxisDetents fresh;
            if (c.detents.outCutoff == fresh.outCutoff && c.detents.outIdle == fresh.outIdle && c.detents.outMil == fresh.outMil && c.detents.stages.empty())
                applyLeverPreset(LeverPreset::Linear, draft_, ctx_.settings.calibrationFor(draft_.input), ctx_);
        }
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
    page_ = "Bindings";
    pagePlugin_.clear();
    Profile& p = ctx_.settings.ensureActive();
    editIndex_ = bindingIndex;
    if (bindingIndex >= 0 && bindingIndex < static_cast<int>(p.bindings.size())) draft_ = p.bindings[static_cast<size_t>(bindingIndex)];
    else draft_ = Binding{};
    cutoffCustom_ = false;
    recipeEngine_ = 0;
    insertAt_ = -1;
    draftIsCopy_ = false;
    editorOpen_ = true;
    ctx_.engine.cancelCapture();
}

void Ui::openEditorWith(std::vector<Action> press, const std::string& label) {
    page_ = "Bindings";
    pagePlugin_.clear();
    ctx_.settings.ensureActive();
    draft_ = Binding{};
    cutoffCustom_ = false;
    recipeEngine_ = 0;
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
    page_ = "Bindings";
    pagePlugin_.clear();
    const Profile& p = ctx_.settings.ensureActive();
    if (bindingIndex < 0 || bindingIndex >= static_cast<int>(p.bindings.size())) return;
    draft_ = p.bindings[static_cast<size_t>(bindingIndex)];
    cutoffCustom_ = false;
    recipeEngine_ = 0;
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
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 13);
    if (ImGui::BeginCombo("##profile", p.name.c_str())) {
        for (const auto& prof : s.profiles) {
            if (ImGui::Selectable(prof.name.c_str(), prof.name == p.name)) {
                s.activeProfile = prof.name;
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
    if (ImGui::Button("Manage profiles...")) page_ = "Profiles";
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Aircraft of each profile, automatic switching, themes");
    ImGui::SameLine(0, ImGui::GetFontSize() * 0.3f);
    if (dotsButton("##profile_more")) ImGui::OpenPopup("profile_menu");
    {
        bool rename = false, remove = false;
        if (ImGui::BeginPopup("profile_menu")) {
            if (ImGui::MenuItem("Rename")) rename = true;
            if (ImGui::MenuItem("Duplicate")) {
                Profile copy = p;
                copy.name = uniqueProfileName(p.name + " copy");
                s.profiles.push_back(copy);
                s.activeProfile = copy.name;
                ctx_.markDirty();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Delete", nullptr, false, s.profiles.size() > 1)) remove = true;  // the last profile cannot be deleted
            ImGui::EndPopup();
        }
        // opened here, outside the menu, where their BeginPopup below looks for them
        if (rename) { newProfileName_ = p.name; renamingProfile_ = true; ImGui::OpenPopup("profile_name"); }
        if (remove) ImGui::OpenPopup("confirm_delete_profile");
    }
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
    // Duplicate / New / Delete above may have resized settings.profiles: `p` can dangle from here on
    Profile& prof = s.ensureActive();

    {
        const std::string det = detectAircraft(status_.aircraft);
        if (!det.empty() && !profileFitsAircraft(prof, det)) {
            const sim::AircraftId loaded = sim::identifyAircraft(status_.aircraft);
            const Catalog* profCat = ctx_.catalogFor(prof.aircraft);
            const std::string profLabel = (profCat && sim::sameAircraft(profCat->aircraft, prof.aircraft)) ? profCat->label() : prof.aircraft;
            ImGui::TextColored(kYellow, "Loaded: %s. Profile: %s.", loaded.label.c_str(), profLabel.c_str());
            ImGui::SameLine();
            const Profile* match = nullptr;
            for (const auto& prof : s.profiles)
                if (profileFitsAircraft(prof, det)) { match = &prof; break; }
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
        for (const auto& b : prof.bindings) {
            double lo, hi;
            if (b.input.kind == input::InputKind::Axis && ctx_.suggestedAxisRange(b, lo, hi) && axisRangeLooksWrong(b.axis, lo, hi)) ++wrong;
        }
        if (wrong) {
            ImGui::TextColored(kYellow, "%d axis binding%s with the wrong range", wrong, wrong == 1 ? "" : "s");
            ImGui::SameLine();
            if (ImGui::SmallButton("Fix")) ctx_.fixAxisRanges(prof);
        }
    }
    ImGui::Separator();
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

    const std::vector<std::string> filterWords = searchWords(bindingFilter_);
    const BindingsGroupBy by = s.bindingsGroupBy;
    const bool canReorder = !s.bindingsAutoSort && filterWords.empty() && bindingsFilter_.empty();
    const auto groups = arrangeBindings(prof, by, s.bindingsAutoSort, [this](const std::string& key) { return deviceDisplayName(key); });
    const auto twice = doubleBindings();
    auto twiceFor = [&](int i) -> const DoubleBinding* {
        for (const auto& d : twice) if (static_cast<int>(d.binding) == i) return &d;
        return nullptr;
    };
    const float fsz = ImGui::GetFontSize();
    {   // what the list shows
        const char* names[] = {"All", "Not tried", "Bound twice", "Reverse"};
        const char* keys[] = {"", "untried", "twice", "reverse"};
        for (int k = 0; k < 4; ++k) {
            if (k == 3 && bindingsFilter_ != "reverse") continue;  // only reached from Home
            if (k) ImGui::SameLine(0, fsz * 0.3f);
            const bool on = bindingsFilter_ == keys[k];
            if (on) ImGui::PushStyleColor(ImGuiCol_Button, ui::palette::kAccentSoft);
            if (ImGui::SmallButton(names[k])) bindingsFilter_ = keys[k];
            if (on) ImGui::PopStyleColor();
        }
        if (!twice.empty()) {
            ImGui::SameLine(0, fsz);
            ImGui::TextColored(kRed, "%zu bound twice with MSFS", twice.size());
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Your MSFS control profile \"%s\" also acts on these buttons:\none press, two actions. Remove them there.", twice[0].msfsProfile.c_str());
        }
    }
    auto shownByFilter = [&](int i) {
        const Binding& b = prof.bindings[static_cast<size_t>(i)];
        if (!bindingMatches(b, filterWords)) return false;
        if (bindingsFilter_ == "untried") return b.enabled && !bindingChecked(b);
        if (bindingsFilter_ == "twice") return twiceFor(i) != nullptr;
        if (bindingsFilter_ == "reverse") return b.cutoffMode == ZoneMode::Reverse;
        return true;
    };

    // the list on the left, the open binding on the right
    const float listW = std::clamp(ImGui::GetContentRegionAvail().x * 0.36f, fsz * 19, fsz * 28);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ui::palette::kSurface);
    ImGui::BeginChild("##blist", ImVec2(listW, 0), ImGuiChildFlags_Borders);
    ImGui::PopStyleColor();
    int toDelete = -1, moveFrom = -1, moveTo = -1;
    int rowsShown = 0;
    for (const auto& g : groups) {
        // within a device of known layout, one section per part of it (the throttle's base, grip, EX panel)
        std::vector<std::pair<std::string, std::vector<int>>> parts;
        for (int i : g.indices) {
            if (!shownByFilter(i)) continue;
            const Binding& b = prof.bindings[static_cast<size_t>(i)];
            const ui::ControlName known = ui::knownControl(b.input.device, b.input.kind == input::InputKind::Axis, b.input.code);
            // a part of a known device: its name ("Base"), with the device in front when the list is grouped by device
            const std::string title = known.section.empty() ? g.title : by == BindingsGroupBy::Device && !g.title.empty() ? g.title + "  \xc2\xb7  " + known.section : known.section;
            auto it = std::find_if(parts.begin(), parts.end(), [&](const auto& x) { return x.first == title; });
            if (it == parts.end()) { parts.push_back({title, {}}); it = parts.end() - 1; }
            it->second.push_back(i);
        }
        if (by == BindingsGroupBy::Device || by == BindingsGroupBy::None) {  // known sections in the order of the hardware
            auto rank = [](const std::string& t) {
                for (const char* k : {"Thrust levers", "Base", "Grip", "EX panel"}) if (t.find(k) != std::string::npos) return static_cast<int>(std::string("Thrust levers|Base|Grip|EX panel").find(k));
                return 1000;
            };
            std::stable_sort(parts.begin(), parts.end(), [&](const auto& a, const auto& b) { return rank(a.first) < rank(b.first); });
        }
        for (const auto& [title, rows] : parts) {
            if (!title.empty()) {
                ImGui::Dummy(ImVec2(0, fsz * 0.15f));
                ImGui::PushStyleColor(ImGuiCol_Text, ui::palette::kTextDim);
                ImGui::PushFont(nullptr, fsz * 0.8f);
                ImGui::TextUnformatted(title.c_str());
                ImGui::PopFont();
                ImGui::PopStyleColor();
            }
            for (size_t pos = 0; pos < rows.size(); ++pos) {
                const int i = rows[pos];
                Binding& b = prof.bindings[static_cast<size_t>(i)];
                ++rowsShown;
                ImGui::PushID(i);
                const bool selected = editorOpen_ && editIndex_ == i;
                const ImVec2 p0 = ImGui::GetCursorScreenPos();
                const float rowW = ImGui::GetContentRegionAvail().x, rowH = ImGui::GetTextLineHeight() * 2 + fsz * 0.5f;
                if (ImGui::Selectable("##row", selected, ImGuiSelectableFlags_AllowOverlap, ImVec2(rowW, rowH))) selectBinding(i);
                if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) ImGui::OpenPopup("row_menu");
                ImDrawList* dl = ImGui::GetWindowDrawList();
                if (inputIsPressed(b.input)) dl->AddRectFilled(p0, ImVec2(p0.x + rowW, p0.y + rowH), ImGui::GetColorU32(withAlpha(kGreen, 0.2f)), fsz * 0.3f);
                if (selected) dl->AddRectFilled(p0, ImVec2(p0.x + 3, p0.y + rowH), ImGui::GetColorU32(kAccent));
                // key cap
                const ui::ControlName known = ui::knownControl(b.input.device, b.input.kind == input::InputKind::Axis, b.input.code);
                std::string cap = known.name.empty() ? ctx_.input.describe(b.input) : known.name;
                const float capW = fsz * 5.4f, capH = fsz * 1.35f;
                const ImVec2 c0(p0.x + fsz * 0.5f, p0.y + (rowH - capH) * 0.5f);
                dl->AddRectFilled(c0, ImVec2(c0.x + capW, c0.y + capH), ImGui::GetColorU32(ui::palette::kGlass), fsz * 0.25f);
                dl->AddRect(c0, ImVec2(c0.x + capW, c0.y + capH), ImGui::GetColorU32(ui::palette::kBorder), fsz * 0.25f);
                ImGui::PushFont(nullptr, fsz * 0.8f);
                ImVec2 ts = ImGui::CalcTextSize(cap.c_str());
                while (ts.x > capW - fsz * 0.4f && cap.size() > 3) { cap = cap.substr(0, cap.size() - 2) + "."; ts = ImGui::CalcTextSize(cap.c_str()); }
                dl->AddText(ImVec2(c0.x + (capW - ts.x) * 0.5f, c0.y + (capH - ts.y) * 0.5f), ImGui::GetColorU32(ui::palette::kTextDim), cap.c_str());
                ImGui::PopFont();
                // name, what it does, status
                const float tx = c0.x + capW + fsz * 0.6f, dotX = p0.x + rowW - fsz * 0.8f, textW = dotX - tx - fsz * 0.5f;
                std::string sub;
                if (b.input.kind == input::InputKind::Axis && b.axis.detents.enabled) {
                    for (const auto& st : b.axis.detents.stages) sub += (sub.empty() ? "" : " \xc2\xb7 ") + st.name;
                    if (b.cutoffMode == ZoneMode::Reverse) sub += (sub.empty() ? "" : " \xc2\xb7 ") + std::string("reverse in cut-off");
                    if (sub.empty()) sub = "lever with gates";
                } else if (!b.onPress.empty()) {
                    sub = b.onPress[0].describe();
                    if (b.onPress.size() > 1) sub += "  +" + std::to_string(b.onPress.size() - 1);
                }
                const std::string name = b.label.empty() ? ctx_.input.describe(b.input) : b.label;
                const ImVec4 clip(tx, p0.y, tx + textW, p0.y + rowH);
                dl->AddText(nullptr, 0.0f, ImVec2(tx, p0.y + fsz * 0.2f), ImGui::GetColorU32(b.enabled ? ui::palette::kText : ui::palette::kTextDim), name.c_str(), nullptr, 0.0f, &clip);
                ImGui::PushFont(nullptr, fsz * 0.84f);
                dl->AddText(nullptr, 0.0f, ImVec2(tx, p0.y + fsz * 0.2f + ImGui::GetTextLineHeight() * 1.25f), ImGui::GetColorU32(ui::palette::kTextDim), sub.c_str(), nullptr, 0.0f, &clip);
                ImGui::PopFont();
                const DoubleBinding* dbl = twiceFor(i);
                const ImVec2 dc(dotX, p0.y + rowH * 0.5f);
                if (dbl) dl->AddCircleFilled(dc, fsz * 0.28f, ImGui::GetColorU32(kRed));
                else if (bindingChecked(b)) dl->AddCircleFilled(dc, fsz * 0.28f, ImGui::GetColorU32(kGreen));
                else dl->AddCircle(dc, fsz * 0.26f, ImGui::GetColorU32(ui::palette::kTextDim), 0, 1.5f);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s%s", dbl ? ("MSFS also: " + dbl->msfsAction + " (\"" + dbl->msfsProfile + "\")").c_str() : bindingChecked(b) ? "Checked on the aircraft" : "Not tried on the aircraft",
                                      b.enabled ? "" : "\nSwitched off");
                bool askDelete = false;
                if (ImGui::BeginPopup("row_menu")) {
                    if (ImGui::MenuItem("Edit")) selectBinding(i);
                    if (ImGui::MenuItem(b.enabled ? "Switch off" : "Switch on")) { b.enabled = !b.enabled; ctx_.markDirty(); }
                    if (ImGui::MenuItem("Test")) {
                        if (b.input.kind == input::InputKind::Axis && b.axis.mode == AxisMode::Value) {
                            const float now = axisNow(b.input);
                            const AxisCalibration* cal = b.axis.detents.enabled ? ctx_.settings.calibrationFor(b.input) : nullptr;
                            const double out = cal && cal->valid() ? Engine::detentOutput(b.axis.detents, *cal, now >= 0 ? now : 0.5, AxisZone::Dry, gateStateNow(*cal)).value
                                                                   : Engine::axisOutput(b.axis, Engine::axisPosition(b.axis, now >= 0 ? now : 0.5f));
                            ctx_.engine.fire(Engine::withValue(b.onPress, out, b.axis.integer), "test " + b.label);
                        } else {
                            ctx_.engine.fire(b.onPress, "test " + b.label);
                        }
                    }
                    if (ImGui::MenuItem("Copy")) openEditorCopy(i);
                    ImGui::Separator();
                    if (ImGui::MenuItem("Move up", nullptr, false, canReorder && pos > 0)) { moveFrom = i; moveTo = rows[pos - 1]; }
                    if (ImGui::MenuItem("Move down", nullptr, false, canReorder && pos + 1 < rows.size())) { moveFrom = i; moveTo = rows[pos + 1]; }
                    ImGui::Separator();
                    if (ImGui::MenuItem("Delete")) askDelete = true;
                    ImGui::EndPopup();
                }
                if (askDelete) ImGui::OpenPopup("delete_binding");
                if (ImGui::BeginPopup("delete_binding")) {
                    ImGui::Text("Delete \"%s\"?", name.c_str());
                    if (ImGui::Button("Delete")) { toDelete = i; ImGui::CloseCurrentPopup(); }
                    ImGui::SameLine();
                    if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
                    ImGui::EndPopup();
                }
                ImGui::PopID();
            }
        }
    }
    if (rowsShown == 0) ImGui::TextDisabled(prof.bindings.empty() ? "No bindings yet: Add binding, or Home > Bind a control." : "Nothing matches.");
    ImGui::EndChild();
    if (toDelete >= 0) {
        if (editorOpen_ && editIndex_ == toDelete) editorOpen_ = false;
        else if (editorOpen_ && editIndex_ > toDelete) --editIndex_;
        prof.bindings.erase(prof.bindings.begin() + toDelete);
        ctx_.markDirty();
    }
    if (moveFrom >= 0) {
        std::swap(prof.bindings[static_cast<size_t>(moveFrom)], prof.bindings[static_cast<size_t>(moveTo)]);
        if (editorOpen_ && editIndex_ == moveFrom) editIndex_ = moveTo;
        else if (editorOpen_ && editIndex_ == moveTo) editIndex_ = moveFrom;
        ctx_.markDirty();
    }

    ImGui::SameLine();
    ImGui::BeginChild("##inspector", ImVec2(0, 0), ImGuiChildFlags_AlwaysUseWindowPadding);
    if (editorOpen_) drawBindingEditor();
    else {
        ImGui::Dummy(ImVec2(0, fsz * 2));
        ImGui::TextDisabled("Select a binding on the left to see and change it.");
        ImGui::TextDisabled("Add binding, or Home > Bind a control, makes a new one.");
    }
    ImGui::EndChild();

    // a row clicked while the open binding has changes
    if (pendingSelect_ != -2) ImGui::OpenPopup("unsaved_changes");
    if (ImGui::BeginPopupModal("unsaved_changes", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar)) {
        ImGui::Text("Save the changes to \"%s\"?", draft_.label.empty() ? ctx_.input.describe(draft_.input).c_str() : draft_.label.c_str());
        const int next = pendingSelect_;
        auto then = [&] { pendingSelect_ = -2; if (next >= 0) openEditor(next); else { editorOpen_ = false; ctx_.engine.cancelCapture(); } };
        if (ImGui::Button("Save")) { if (!draft_.input.device.empty()) saveDraft(); then(); ImGui::CloseCurrentPopup(); }
        ImGui::SameLine();
        if (ImGui::Button("Discard")) { then(); ImGui::CloseCurrentPopup(); }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) { pendingSelect_ = -2; ImGui::CloseCurrentPopup(); }
        ImGui::EndPopup();
    }
}

bool Ui::draftChanged() const {
    const Profile* p = ctx_.settings.active();
    if (!p) return false;
    if (editIndex_ >= 0 && editIndex_ < static_cast<int>(p->bindings.size())) return bindingFingerprint(draft_) != bindingFingerprint(p->bindings[static_cast<size_t>(editIndex_)]);
    return !draft_.input.device.empty() || !draft_.onPress.empty() || !draft_.label.empty();
}

void Ui::selectBinding(int index) {
    if (editorOpen_ && editIndex_ == index) return;
    if (editorOpen_ && draftChanged()) { pendingSelect_ = index; return; }
    openEditor(index);
}

void Ui::saveDraft() {
    Profile& p = ctx_.settings.ensureActive();
    if (editIndex_ >= 0 && editIndex_ < static_cast<int>(p.bindings.size())) p.bindings[static_cast<size_t>(editIndex_)] = draft_;
    else if (insertAt_ >= 0 && insertAt_ <= static_cast<int>(p.bindings.size())) { p.bindings.insert(p.bindings.begin() + insertAt_, draft_); editIndex_ = insertAt_; }
    else { p.bindings.push_back(draft_); editIndex_ = static_cast<int>(p.bindings.size()) - 1; }
    insertAt_ = -1;
    draftIsCopy_ = false;
    ctx_.markDirty();
    ctx_.engine.cancelCapture();
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
        } else if (a.kind == ActionKind::Plugin) {
            // the commands of the loaded plugins; a name no plugin has (its plugin is off or gone) is kept and shown as it is
            const plugin::Command* cur = ctx_.plugins ? ctx_.plugins->findCommand(a.name) : nullptr;
            ImGui::SetNextItemWidth(fs * 22);
            if (ImGui::BeginCombo("##command", cur ? cur->label.c_str() : a.name.empty() ? "Command" : a.name.c_str())) {
                if (ctx_.plugins)
                    for (const auto& c : ctx_.plugins->commands()) {
                        if (ImGui::Selectable(c.label.c_str(), &c == cur)) a.name = c.name;
                        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", c.name.c_str());
                    }
                if (!ctx_.plugins || ctx_.plugins->commands().empty()) ImGui::TextDisabled("None");
                ImGui::EndCombo();
            }
            ImGui::SameLine();
            ImGui::SetNextItemWidth(fs * 6);
            ImGui::InputDouble("##value", &a.value, 0, 0, "%g");
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Value");
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
            const AxisCalibration* cal = draft_.axis.detents.enabled ? ctx_.settings.calibrationFor(draft_.input) : nullptr;
            const double out = cal && cal->valid() ? Engine::detentOutput(draft_.axis.detents, *cal, now >= 0 ? now : 0.5, AxisZone::Dry, gateStateNow(*cal)).value
                                                   : Engine::axisOutput(draft_.axis, Engine::axisPosition(draft_.axis, now >= 0 ? now : 0.5f));
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
            ImGui::PushID(&t);  // the same label is in two groups ("Mixture 1": MIXTURE1_SET and AXIS_MIXTURE1_SET)
            const bool picked = ImGui::Selectable(t.label, &t == current);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("K:%s, %g to %g", t.event, t.lo, t.hi);
            ImGui::PopID();
            if (picked) {
                Action a;
                a.kind = ActionKind::SimEvent;
                a.name = t.event;
                draft_.onPress = {a};
                draft_.axis.outMin = t.lo;
                draft_.axis.outMax = t.hi;
                draft_.axis.integer = true;
                if (draft_.label.empty()) draft_.label = t.label;
            }
        }
        ImGui::EndCombo();
    }
}

void Ui::drawBindingEditor() {
    {   // heading of the inspector
        const char* title = editIndex_ >= 0 ? "Edit binding" : draftIsCopy_ ? "Copy of a binding" : "New binding";
        ImGui::PushFont(nullptr, ImGui::GetFontSize() * 1.15f);
        ImGui::TextUnformatted(title);
        ImGui::PopFont();
        if (editIndex_ >= 0 && draftChanged()) { ImGui::SameLine(); ImGui::TextColored(kYellow, "unsaved"); }
        const float closeW = ImGui::CalcTextSize("Close").x + ImGui::GetStyle().FramePadding.x * 2;
        ImGui::SameLine(std::max(ImGui::GetCursorPosX(), ImGui::GetContentRegionMax().x - closeW));
        if (ImGui::SmallButton("Close")) {
            if (draftChanged()) pendingSelect_ = -1;  // asks first; -1 = nothing selected after
            else editorOpen_ = false;
        }
        ImGui::Separator();
    }
    const float fs = ImGui::GetFontSize();

    // Input capture
    const bool capturing = ctx_.engine.capturing();
    // while the calibration wizard is open the capture is the wizard's (it learns the gate buttons): taking it here turned the axis
    // being edited into a button binding and the wizard never heard the button
    if (auto captured = calOpen_ || homeListening_ ? std::nullopt : ctx_.engine.takeCaptured()) {
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
                if (ImGui::BeginTabItem("Cut-off zone")) {
                    drawCutoffZone();
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("Afterburner zone")) {
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
    if (ImGui::Button("Save", ImVec2(fs * 9, 0))) saveDraft();
    if (!canSave) ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button(editIndex_ >= 0 ? "Undo changes" : "Cancel", ImVec2(fs * 9, 0))) {
        if (editIndex_ >= 0) openEditor(editIndex_);  // back to what is saved
        else { editorOpen_ = false; ctx_.engine.cancelCapture(); }
    }
    if (!canSave) { ImGui::SameLine(); ImGui::TextDisabled("No input"); }
    if (!editorOpen_) ctx_.engine.cancelCapture();
}

}  // namespace lr::app
