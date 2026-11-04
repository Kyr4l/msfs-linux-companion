// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kyr4l
#include "ui/Ui.hpp"
#include "ui/UiCommon.hpp"

#include <algorithm>
#include <cmath>

#include <imgui.h>

#include "util/Log.hpp"

namespace lr::app {

using namespace uidetail;

// ---------------------------------------------------------------- Axis calibration (the gates of a lever)

// The track of a lever with the zones the calibration makes of it (cut-off red, idle blue, dry range green, afterburner orange),
// a line at each gate and the live position of the lever. `gateSet` says which gates were pointed out yet (the wizard); without
// it all four count.
void Ui::drawGateBar(const AxisCalibration& cal, float now, float width, const bool* gateSet) {
    const float fs = ImGui::GetFontSize(), h = fs * 1.5f;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    dl->AddRectFilled(p0, ImVec2(p0.x + width, p0.y + h), ImGui::GetColorU32(ui::palette::kSurfaceHi), 3.0f);
    const bool zones = cal.valid() && (!gateSet || (gateSet[0] && gateSet[1] && gateSet[2] && gateSet[3]));
    if (zones) {
        const AxisDetents probe;  // only the zones matter here
        const int columns = static_cast<int>(width);
        for (int x = 0; x < columns; ++x) {
            const double raw = (x + 0.5) / columns;
            const auto r = Engine::detentOutput(probe, cal, raw, AxisZone::Dry);
            ImVec4 col = withAlpha(kGreen, 0.45f);  // dry range
            if (r.zone == AxisZone::Cutoff) col = withAlpha(kRed, 0.6f);
            else if (r.zone == AxisZone::Afterburner) col = ImVec4(0.95f, 0.55f, 0.20f, 0.7f);
            else if (cal.idleBtn < 0 && std::fabs(raw - cal.idle) <= cal.snap) col = withAlpha(kAccent, 0.7f);
            dl->AddRectFilled(ImVec2(p0.x + x, p0.y), ImVec2(p0.x + x + 1, p0.y + h), ImGui::GetColorU32(col));
        }
    }
    const char* names[4] = {"cut-off", "idle", "max", "afterburner"};
    const double at[4] = {cal.cutoff, cal.idle, cal.mil, cal.ab};
    for (int i = 0; i < 4; ++i) {
        if (gateSet && !gateSet[i]) continue;
        const float x = p0.x + static_cast<float>(std::clamp(at[i], 0.0, 1.0)) * width;
        dl->AddLine(ImVec2(x, p0.y), ImVec2(x, p0.y + h + fs * 0.25f), ImGui::GetColorU32(ImVec4(1, 1, 1, 0.9f)), 1.5f);
        const float lw = ImGui::CalcTextSize(names[i]).x;
        dl->AddText(ImVec2(std::clamp(x - lw * 0.5f, p0.x, p0.x + width - lw), p0.y + h + fs * 0.2f + (i % 2) * fs * 1.05f), ImGui::GetColorU32(ImGuiCol_TextDisabled), names[i]);  // two rows: close gates do not overlap
    }
    if (now >= 0) {
        const float x = p0.x + std::clamp(now, 0.0f, 1.0f) * width;
        dl->AddLine(ImVec2(x, p0.y - 2), ImVec2(x, p0.y + h + 2), ImGui::GetColorU32(ImVec4(1.0f, 0.85f, 0.2f, 1.0f)), 3.0f);
    }
    ImGui::Dummy(ImVec2(width, h + fs * 2.4f));
}

// What a lever sends over its travel: x is the lever (cut-off on the left, whichever way the device reads), y the value. Under it the
// zones; on it the stages with their bands (drag a stage to move it) and the lever where it is now.
bool Ui::drawLeverGraph(AxisDetents& d, const AxisCalibration& cal, float now, float width, float height) {
    const float fs = ImGui::GetFontSize();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    const bool reversed = cal.idle > cal.mil;
    auto rawOf = [&](double x01) { return reversed ? 1.0 - x01 : x01; };   // x on the graph -> device position
    auto xOfRaw = [&](double raw) { return reversed ? 1.0 - raw : raw; };
    const float padL = fs * 0.8f, padR = fs * 0.8f, padT = fs * 1.6f, strip = fs * 0.45f, padB = fs * 1.5f + strip;
    const ImVec2 a(p0.x + padL, p0.y + padT), b(p0.x + width - padR, p0.y + height - padB);
    const float pw = b.x - a.x, ph = b.y - a.y;
    dl->AddRectFilled(p0, ImVec2(p0.x + width, p0.y + height), ImGui::GetColorU32(ui::palette::kSurfaceHi), fs * 0.4f);

    double vmin = std::min({d.outCutoff, d.outIdle, d.outMil}), vmax = std::max({d.outCutoff, d.outIdle, d.outMil});
    for (const auto& st : d.stages) { vmin = std::min(vmin, st.value); vmax = std::max(vmax, st.value); }
    if (vmax - vmin < 1e-9) { vmin -= 1; vmax += 1; }
    auto yOf = [&](double v) { return b.y - static_cast<float>((v - vmin) / (vmax - vmin)) * ph; };
    auto pxOf = [&](double x01) { return a.x + static_cast<float>(std::clamp(x01, 0.0, 1.0)) * pw; };

    // grid: the lowest, highest and zero value
    const ImU32 grid = ImGui::GetColorU32(ImVec4(1, 1, 1, 0.06f));
    dl->AddLine(ImVec2(a.x, yOf(vmin)), ImVec2(b.x, yOf(vmin)), grid);
    dl->AddLine(ImVec2(a.x, yOf(vmax)), ImVec2(b.x, yOf(vmax)), grid);
    if (vmin < 0 && vmax > 0) dl->AddLine(ImVec2(a.x, yOf(0)), ImVec2(b.x, yOf(0)), grid);
    {   // the value range at the left
        char lo[32], hi[32];
        snprintf(lo, sizeof lo, "%g", vmin);
        snprintf(hi, sizeof hi, "%g", vmax);
        const ImU32 dim = ImGui::GetColorU32(withAlpha(ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled), 0.8f));
        ImGui::PushFont(nullptr, fs * 0.8f);
        dl->AddText(ImVec2(a.x + 3, yOf(vmax) + 2), dim, hi);
        dl->AddText(ImVec2(a.x + 3, yOf(vmin) - fs * 0.85f), dim, lo);
        ImGui::PopFont();
    }

    // zones under the plot, and the curve
    const int columns = std::max(2, static_cast<int>(pw / 2));
    AxisZone z = AxisZone::Dry;
    std::vector<ImVec2> line;
    line.reserve(static_cast<size_t>(columns) + 1);
    for (int i = 0; i <= columns; ++i) {
        const double x01 = static_cast<double>(i) / columns;
        const auto r = Engine::detentOutput(d, cal, rawOf(x01), z);
        z = r.zone;
        const float x = a.x + static_cast<float>(x01) * pw;
        ImVec4 col = withAlpha(kGreen, 0.45f);
        if (r.zone == AxisZone::Cutoff) col = withAlpha(kRed, 0.6f);
        else if (r.zone == AxisZone::Afterburner) col = ImVec4(0.95f, 0.55f, 0.20f, 0.7f);
        if (i < columns) dl->AddRectFilled(ImVec2(x, b.y + fs * 0.35f), ImVec2(x + pw / columns + 0.5f, b.y + fs * 0.35f + strip), ImGui::GetColorU32(col));
        line.emplace_back(x, yOf(r.value));
    }

    // stage bands and markers, dragged sideways
    bool moved = false;
    const ImVec2 back = ImGui::GetCursorScreenPos();
    for (size_t i = 0; i < d.stages.size(); ++i) {
        LeverStage& st = d.stages[i];
        const float x = pxOf(xOfRaw(Engine::leverRaw(d, cal, st.at)));
        const float x0 = pxOf(xOfRaw(Engine::leverRaw(d, cal, st.at - st.band))), x1 = pxOf(xOfRaw(Engine::leverRaw(d, cal, st.at + st.band)));
        dl->AddRectFilled(ImVec2(std::min(x0, x1), a.y), ImVec2(std::max(x0, x1), b.y), ImGui::GetColorU32(ui::palette::kAccentSoft));
        ImGui::SetCursorScreenPos(ImVec2(x - fs * 0.45f, a.y - padT * 0.9f));
        ImGui::PushID(static_cast<int>(i));
        ImGui::InvisibleButton("##stage", ImVec2(fs * 0.9f, ph + padT * 0.9f));
        const bool hot = ImGui::IsItemHovered() || ImGui::IsItemActive();
        if (hot) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
        if (ImGui::IsItemActive()) {
            leverDrag_ = static_cast<int>(i);
            const double x01 = std::clamp((ImGui::GetIO().MousePos.x - a.x) / pw, 0.0f, 1.0f);
            const double at = Engine::leverAt(d, cal, rawOf(x01));
            if (std::fabs(at - st.at) > 1e-6) { st.at = at; moved = true; }
        }
        if (ImGui::IsItemDeactivated()) leverDrag_ = -1;
        ImGui::PopID();
        dl->AddLine(ImVec2(x, a.y), ImVec2(x, b.y), ImGui::GetColorU32(hot ? kAccent : withAlpha(kAccent, 0.6f)), hot ? 2.0f : 1.0f);
        const char* name = st.name.empty() ? "Stage" : st.name.c_str();
        const float tw = ImGui::CalcTextSize(name).x;
        dl->AddText(ImVec2(std::clamp(x - tw * 0.5f, p0.x + 2, p0.x + width - tw - 2), p0.y + fs * 0.3f), ImGui::GetColorU32(hot ? ImGuiCol_Text : ImGuiCol_TextDisabled), name);
    }
    dl->AddPolyline(line.data(), static_cast<int>(line.size()), ImGui::GetColorU32(kAccent), ImDrawFlags_None, 2.0f);

    // the gates under the zones
    auto gateLabel = [&](double raw, const char* text, int row) {
        const float x = pxOf(xOfRaw(raw));
        dl->AddLine(ImVec2(x, b.y + fs * 0.2f), ImVec2(x, b.y + fs * 0.35f + strip), ImGui::GetColorU32(ImVec4(1, 1, 1, 0.5f)), 1.0f);
        const float tw = ImGui::CalcTextSize(text).x;
        (void)row;
        dl->AddText(ImVec2(std::clamp(x - tw * 0.5f, p0.x + 2, p0.x + width - tw - 2), b.y + fs * 0.45f + strip), ImGui::GetColorU32(ImGuiCol_TextDisabled), text);
    };
    gateLabel(cal.idle, "Idle", 0);
    gateLabel(cal.mil, "Max", 1);

    // the lever now
    if (now >= 0) {
        const auto r = Engine::detentOutput(d, cal, now, AxisZone::Dry, gateStateNow(cal));
        const float x = pxOf(xOfRaw(now)), y = yOf(r.value);
        dl->AddLine(ImVec2(x, a.y), ImVec2(x, b.y + fs * 0.35f + strip), ImGui::GetColorU32(withAlpha(kYellow, 0.7f)), 1.5f);
        dl->AddCircleFilled(ImVec2(x, y), fs * 0.32f, ImGui::GetColorU32(kYellow), 16);
        char label[96];
        const char* where = r.zone == AxisZone::Cutoff ? "Cut-off" : r.zone == AxisZone::Afterburner && d.top == LeverTop::Max ? "Afterburner"
                            : r.stage >= 0 ? d.stages[static_cast<size_t>(r.stage)].name.c_str() : nullptr;
        snprintf(label, sizeof label, "%s%s%s", where ? where : "", where ? "  " : "", Engine::formatValue(r.value, draft_.axis.integer).c_str());
        const ImVec2 ts = ImGui::CalcTextSize(label);
        const float lx = std::clamp(x + fs * 0.5f, a.x, b.x - ts.x), ly = std::clamp(y - ts.y - fs * 0.3f, a.y, b.y - ts.y);
        dl->AddRectFilled(ImVec2(lx - 4, ly - 1), ImVec2(lx + ts.x + 4, ly + ts.y + 1), ImGui::GetColorU32(ImVec4(0, 0, 0, 0.55f)), 3.0f);
        dl->AddText(ImVec2(lx, ly), ImGui::GetColorU32(ImGuiCol_Text), label);
    }
    ImGui::SetCursorScreenPos(back);
    ImGui::Dummy(ImVec2(width, height));
    return moved;
}

Engine::GateState Ui::gateStateNow(const AxisCalibration& cal) const {
    auto closed = [&](int code) { return code >= 0 && inputIsPressed({cal.device, input::InputKind::Button, code, 0}); };
    Engine::GateState g;
    g.cutoff = closed(cal.cutoffBtn);
    g.idle = closed(cal.idleBtn);
    g.mil = closed(cal.milBtn);
    g.ab = closed(cal.abBtn);
    return g;
}

void Ui::openCalibration(const input::InputId& axis) {
    if (calOpen_) return;
    calAxis_ = axis;
    const AxisCalibration* current = ctx_.settings.calibrationFor(axis);
    calDraft_ = current ? *current : AxisCalibration{};
    calDraft_.device = axis.device;
    calDraft_.code = axis.code;
    for (bool& s : calSet_) s = current != nullptr;
    calListening_ = -1;
    calMessage_.clear();
    calOpen_ = true;
    calJustOpened_ = true;
    calWasEnabled_ = ctx_.engine.enabled();
    ctx_.engine.setEnabled(false);  // the lever must not move the simulator while it is being taught
    ctx_.engine.beginGateLearning(calDraft_);
    calSig_ = calDraft_.cutoffBtn * 3 + calDraft_.idleBtn * 5 + calDraft_.milBtn * 7 + calDraft_.abBtn * 11;
}

void Ui::drawCalibrationWizard() {
    if (!calOpen_) return;
    if (calJustOpened_) { ImGui::OpenPopup("Calibrate"); calJustOpened_ = false; }
    bool open = true;
    bool close = false;
    const float fs = ImGui::GetFontSize();
    ImGui::SetNextWindowSize(ImVec2(fs * 50, 0), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("Calibrate", &open, ImGuiWindowFlags_NoSavedSettings)) {
        const float now = axisNow(calAxis_);
        int* buttons[4] = {&calDraft_.cutoffBtn, &calDraft_.idleBtn, &calDraft_.milBtn, &calDraft_.abBtn};
        double* positions[4] = {&calDraft_.cutoff, &calDraft_.idle, &calDraft_.mil, &calDraft_.ab};

        // a button heard while listening, and the positions the engine noted when the gate buttons moved
        if (calListening_ >= 0) {
            if (auto heard = ctx_.engine.takeCaptured()) {
                if (heard->kind == input::InputKind::Button && heard->device == calAxis_.device) {
                    *buttons[calListening_] = heard->code;
                    calMessage_.clear();
                    calListening_ = -1;
                } else {
                    calMessage_ = "Button is on another device";
                    ctx_.engine.beginCapture(true);
                }
            }
        }
        const int sig = calDraft_.cutoffBtn * 3 + calDraft_.idleBtn * 5 + calDraft_.milBtn * 7 + calDraft_.abBtn * 11;
        if (sig != calSig_) { calSig_ = sig; ctx_.engine.beginGateLearning(calDraft_); }
        const auto learned = ctx_.engine.takeGateLearned();
        for (int g = 0; g < 4; ++g)
            if (learned[static_cast<size_t>(g)] && *buttons[g] >= 0) { *positions[g] = *learned[static_cast<size_t>(g)]; calSet_[g] = true; }

        ImGui::Text("%s (%s)", ctx_.input.describe(calAxis_).c_str(), deviceDisplayName(calAxis_.device).c_str());
        ImGui::TextDisabled("Bindings are paused.");
        if (now < 0) ImGui::TextColored(kYellow, "No device");
        drawGateBar(calDraft_, now, fs * 44, calSet_);

        // 1. the buttons that mark the gates
        ImGui::SeparatorText("1. Gate buttons");
        const char* gateName[4] = {"Cut-off", "Idle", "Max", "Afterburner"};
        for (int g = 0; g < 4; ++g) {
            ImGui::PushID(g);
            const bool hasButton = *buttons[g] >= 0;
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(gateName[g]);
            ImGui::SameLine(fs * 12);
            if (hasButton) {
                const bool closed = inputIsPressed({calAxis_.device, input::InputKind::Button, *buttons[g], 0});
                ImGui::Text("%s", ctx_.input.describe({calAxis_.device, input::InputKind::Button, *buttons[g], 0}).c_str());
                ImGui::SameLine();
                dot(closed ? kGreen : kGrey);
                ImGui::TextDisabled("%s", closed ? "closed" : "open");
            } else {
                ImGui::TextDisabled("None");
            }
            ImGui::SameLine(fs * 25);
            const bool listening = calListening_ == g;
            if (ImGui::SmallButton(listening ? "Stop" : "Detect")) {
                if (listening) { calListening_ = -1; ctx_.engine.cancelCapture(); }
                else { calListening_ = g; calMessage_.clear(); ctx_.engine.beginCapture(true); }
            }
            if (hasButton) {
                ImGui::SameLine();
                if (ImGui::SmallButton("Clear")) *buttons[g] = -1;
            }
            // by number, as the Devices tab shows it
            ImGui::SameLine();
            ImGui::SetNextItemWidth(fs * 4);
            int number = 0;
            for (const auto& d : devices_)
                if (d.key == calAxis_.device && hasButton) {
                    const auto it = std::find(d.buttonCodes.begin(), d.buttonCodes.end(), *buttons[g]);
                    if (it != d.buttonCodes.end()) number = static_cast<int>(it - d.buttonCodes.begin()) + 1;
                }
            if (ImGui::InputInt("##number", &number, 0, 0)) {
                for (const auto& d : devices_)
                    if (d.key == calAxis_.device) *buttons[g] = number >= 1 && number <= static_cast<int>(d.buttonCodes.size()) ? d.buttonCodes[static_cast<size_t>(number - 1)] : -1;
            }
            if (listening) {
                ImGui::SameLine();
                ImGui::TextColored(kYellow, "Move the lever to this gate");
            }
            ImGui::PopID();
        }
        if (!calMessage_.empty()) ImGui::TextColored(kYellow, "%s", calMessage_.c_str());

        // 2. the two positions the linear part needs
        ImGui::SeparatorText("2. Positions");
        auto positionRow = [&](int g, const char* label) {
            ImGui::PushID(100 + g);
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(label);
            ImGui::SameLine(fs * 12);
            float pct = static_cast<float>(*positions[g] * 100.0);
            ImGui::SetNextItemWidth(fs * 7);
            if (ImGui::DragFloat("##pct", &pct, 0.1f, 0.0f, 100.0f, "%.1f %%")) { *positions[g] = pct / 100.0; calSet_[g] = true; }
            ImGui::SameLine();
            ImGui::BeginDisabled(now < 0);
            if (ImGui::SmallButton("Set")) { *positions[g] = now; calSet_[g] = true; }
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (calSet_[g]) ImGui::TextColored(kGreen, "set"); else ImGui::TextDisabled("not set");
            ImGui::PopID();
        };
        positionRow(1, "Idle ends at");
        positionRow(2, "Max starts at");

        // the gates that have no button are judged from their position
        if (calDraft_.cutoffBtn < 0 || calDraft_.abBtn < 0 || calDraft_.idleBtn < 0) {
            ImGui::SeparatorText("Gates without button");
            if (calDraft_.cutoffBtn < 0) positionRow(0, "Cut-off at");
            if (calDraft_.abBtn < 0) positionRow(3, "Afterburner at");
            if (calDraft_.idleBtn < 0) {
                float snapPct = static_cast<float>(calDraft_.snap * 100.0);
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted("Idle band");
                ImGui::SameLine(fs * 12);
                ImGui::SetNextItemWidth(fs * 7);
                if (ImGui::SliderFloat("##snap", &snapPct, 0.0f, 5.0f, "%.1f %%")) calDraft_.snap = snapPct / 100.0;
            }
        }

        ImGui::Separator();
        const bool allSet = (calDraft_.cutoffBtn >= 0 || calSet_[0]) && calSet_[1] && calSet_[2] && (calDraft_.abBtn >= 0 || calSet_[3]);
        const bool valid = calDraft_.valid();
        if (!allSet) ImGui::TextDisabled("Set the positions above");
        else if (!valid) {
            ImGui::PushTextWrapPos(fs * 48);
            ImGui::TextColored(kYellow, "Positions are out of order");
            ImGui::PopTextWrapPos();
        } else ImGui::TextColored(kGreen, "Ready");
        ImGui::Spacing();
        ImGui::BeginDisabled(!(allSet && valid));
        if (ImGui::Button("Save", ImVec2(fs * 9, 0))) {
            ctx_.settings.setCalibration(calDraft_);
            ctx_.markDirty();
            close = true;
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(fs * 9, 0))) close = true;
        if (ctx_.settings.calibrationFor(calAxis_)) {
            ImGui::SameLine();
            if (ImGui::Button("Remove")) {
                ctx_.settings.clearCalibration(calAxis_);
                ctx_.markDirty();
                close = true;
            }
        }
        if (close) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    if (!open || close) {
        calOpen_ = false;
        calListening_ = -1;
        ctx_.engine.cancelCapture();
        ctx_.engine.endGateLearning();
        ctx_.engine.setEnabled(calWasEnabled_);
    }
}

}  // namespace lr::app
