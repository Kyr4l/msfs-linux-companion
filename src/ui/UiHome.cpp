// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kyr4l
// Home (what is going on, what to do next), the rail of pages on the left and the top bar.
#include "ui/Ui.hpp"
#include "ui/UiCommon.hpp"

#include <algorithm>
#include <cmath>

#include <imgui.h>

#include "plugin/PluginManager.hpp"
#include "sim/AircraftId.hpp"

namespace lr::app {

using namespace uidetail;

namespace {

ImU32 col(const ImVec4& c) { return ImGui::GetColorU32(c); }

// Small line icons for the rail, drawn in a square at `p` of side `s`.
void drawIcon(ImDrawList* dl, const char* page, ImVec2 p, float s, ImU32 c) {
    const float t = std::max(1.4f, s * 0.085f);
    auto P = [&](float x, float y) { return ImVec2(p.x + x * s, p.y + y * s); };
    const std::string n = page;
    if (n == "Home") {
        const ImVec2 roof[] = {P(0.12f, 0.5f), P(0.5f, 0.14f), P(0.88f, 0.5f)};
        dl->AddPolyline(roof, 3, c, 0, t);
        dl->AddRect(P(0.24f, 0.46f), P(0.76f, 0.88f), c, s * 0.04f, 0, t);
    } else if (n == "Bindings") {
        dl->AddRect(P(0.12f, 0.16f), P(0.88f, 0.86f), c, s * 0.08f, 0, t);
        for (float y : {0.36f, 0.52f, 0.68f}) dl->AddLine(P(0.28f, y), P(y > 0.6f ? 0.56f : 0.72f, y), c, t);
    } else if (n == "Profiles") {  // two cards, one behind the other
        const ImVec2 back[] = {P(0.3f, 0.36f), P(0.3f, 0.12f), P(0.88f, 0.12f), P(0.88f, 0.62f), P(0.7f, 0.62f)};
        dl->AddPolyline(back, 5, c, 0, t);
        dl->AddRect(P(0.12f, 0.36f), P(0.7f, 0.88f), c, s * 0.08f, 0, t);
    } else if (n == "Devices") {
        dl->AddRect(P(0.3f, 0.1f), P(0.7f, 0.9f), c, s * 0.18f, 0, t);
        dl->AddLine(P(0.5f, 0.24f), P(0.5f, 0.44f), c, t);
    } else if (n == "Guides") {
        const ImVec2 doc[] = {P(0.22f, 0.12f), P(0.62f, 0.12f), P(0.8f, 0.3f), P(0.8f, 0.88f), P(0.22f, 0.88f)};
        dl->AddPolyline(doc, 5, c, ImDrawFlags_Closed, t);
        dl->AddLine(P(0.34f, 0.52f), P(0.68f, 0.52f), c, t);
        dl->AddLine(P(0.34f, 0.68f), P(0.58f, 0.68f), c, t);
    } else if (n == "Catalog") {
        dl->AddCircle(P(0.44f, 0.44f), s * 0.26f, c, 20, t);
        dl->AddLine(P(0.64f, 0.64f), P(0.86f, 0.86f), c, t);
    } else if (n == "Cockpit") {
        dl->PathArcTo(P(0.5f, 0.64f), s * 0.36f, 3.14159f, 6.28318f, 20);
        dl->PathStroke(c, 0, t);
        dl->AddLine(P(0.5f, 0.64f), P(0.72f, 0.4f), c, t);
        dl->AddLine(P(0.14f, 0.64f), P(0.86f, 0.64f), c, t);
    } else if (n == "Vibration") {
        const ImVec2 w[] = {P(0.08f, 0.5f), P(0.26f, 0.5f), P(0.36f, 0.24f), P(0.52f, 0.78f), P(0.62f, 0.5f), P(0.92f, 0.5f)};
        dl->AddPolyline(w, 6, c, 0, t);
    } else if (n == "Log") {
        for (float y : {0.28f, 0.5f, 0.72f}) dl->AddLine(P(0.16f, y), P(y > 0.6f ? 0.6f : 0.84f, y), c, t);
    } else if (n == "Settings") {
        dl->AddCircle(P(0.5f, 0.5f), s * 0.16f, c, 16, t);
        for (int k = 0; k < 8; ++k) {
            const float a = k * 0.785398f;
            dl->AddLine(P(0.5f + std::cos(a) * 0.27f, 0.5f + std::sin(a) * 0.27f), P(0.5f + std::cos(a) * 0.4f, 0.5f + std::sin(a) * 0.4f), c, t);
        }
    } else {  // a plugin's page
        dl->AddRect(P(0.2f, 0.2f), P(0.8f, 0.8f), c, s * 0.1f, 0, t);
        dl->AddLine(P(0.5f, 0.34f), P(0.5f, 0.66f), c, t);
        dl->AddLine(P(0.34f, 0.5f), P(0.66f, 0.5f), c, t);
    }
}

// A bordered card that grows to its content; returns false when it is clipped away (then EndCard is still needed).
void beginCard(const char* id, float width, const ImVec4* border = nullptr) {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ui::palette::kSurface);
    ImGui::PushStyleColor(ImGuiCol_Border, border ? *border : ui::palette::kBorder);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, ImGui::GetFontSize() * 0.6f);
    ImGui::BeginChild(id, ImVec2(width, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding);
}
void endCard() {
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(2);
}

void sectionLabel(const char* text) {
    ImGui::PushStyleColor(ImGuiCol_Text, ui::palette::kTextDim);
    ImGui::PushFont(nullptr, ImGui::GetFontSize() * 0.82f);
    ImGui::TextUnformatted(text);
    ImGui::PopFont();
    ImGui::PopStyleColor();
}

}  // namespace

// ---------------------------------------------------------------- rail and top bar

void Ui::drawRail() {
    const float fs = ImGui::GetFontSize();
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ui::palette::kSurface);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(fs * 0.35f, fs * 0.6f));
    ImGui::BeginChild("##rail", ImVec2(fs * 4.6f, 0), ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoScrollbar);
    ImGui::PopStyleVar();
    auto item = [&](const char* name, const char* label, const std::string& plugin) {
        const bool selected = page_ == name && pagePlugin_ == plugin;
        ImGui::PushID(name);
        ImGui::PushID(plugin.c_str());
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float w = ImGui::GetContentRegionAvail().x, h = fs * 3.0f;
        if (ImGui::InvisibleButton("page", ImVec2(w, h))) { page_ = name; pagePlugin_ = plugin; }
        const bool hovered = ImGui::IsItemHovered();
        if (hovered && std::string(label) != name) ImGui::SetTooltip("%s", name);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        if (selected || hovered) dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), col(selected ? ui::palette::kAccentSoft : ImGui::GetStyleColorVec4(ImGuiCol_FrameBg)), fs * 0.45f);
        const ImU32 c = col(selected ? kAccent : hovered ? ui::palette::kText : ui::palette::kTextDim);
        const float s = fs * 1.25f;
        drawIcon(dl, plugin.empty() ? name : "plugin", ImVec2(p.x + (w - s) * 0.5f, p.y + fs * 0.35f), s, c);
        ImGui::PushFont(nullptr, fs * 0.74f);
        const ImVec2 ts = ImGui::CalcTextSize(label);
        dl->AddText(ImVec2(p.x + (w - ts.x) * 0.5f, p.y + fs * 1.85f), c, label);
        ImGui::PopFont();
        ImGui::PopID();
        ImGui::PopID();
    };
    item("Home", "Home", "");
    item("Bindings", "Bind", "");
    item("Profiles", "Profiles", "");
    item("Devices", "Devices", "");
    item("Guides", "Guides", "");
    item("Catalog", "Catalog", "");
    item("Cockpit", "Cockpit", "");
    item("Vibration", "Vibrate", "");
    if (ctx_.plugins)
        for (const auto& h : ctx_.plugins->tabs()) {
            const std::string shortName = h.name.size() > 8 ? h.name.substr(0, 7) + "." : h.name;
            item(h.name.c_str(), shortName.c_str(), h.plugin);
        }
    ImGui::Dummy(ImVec2(0, fs * 0.2f));
    item("Log", "Log", "");
    item("Settings", "Settings", "");
    // version at the bottom
    ImGui::PushFont(nullptr, fs * 0.72f);
    const float bottom = ImGui::GetWindowHeight() - fs * 1.6f;
    if (ImGui::GetCursorPosY() < bottom) ImGui::SetCursorPosY(bottom);
    const ImVec2 vs = ImGui::CalcTextSize(COMPANION_VERSION);
    ImGui::SetCursorPosX((ImGui::GetWindowWidth() - vs.x) * 0.5f);
    ImGui::TextDisabled("%s", COMPANION_VERSION);
    ImGui::PopFont();
    ImGui::EndChild();
    ImGui::PopStyleColor();
}

void Ui::drawTopBar() {
    const float fs = ImGui::GetFontSize();
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ui::palette::kSurface);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(fs * 0.9f, fs * 0.45f));
    ImGui::BeginChild("##topbar", ImVec2(0, ImGui::GetFrameHeight() + fs * 0.9f), ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoScrollbar);
    ImGui::PopStyleVar();
    ImGui::AlignTextToFramePadding();
    ImGui::PushFont(nullptr, fs * 1.08f);
    ImGui::TextUnformatted("MSFS Companion");
    ImGui::PopFont();
    ImGui::SameLine(0, fs * 1.2f);
    drawStatusBar();
    ImGui::EndChild();
    ImGui::PopStyleColor();
    // a line under the bar
    const ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
    ImGui::GetWindowDrawList()->AddLine(ImVec2(a.x, b.y - 1), ImVec2(b.x, b.y - 1), col(ui::palette::kBorder));
}

// ---------------------------------------------------------------- status of bindings

bool Ui::bindingChecked(const Binding& b) const {
    if (b.cutoffMode == ZoneMode::Reverse) {  // a lever from a checked recipe
        for (const auto& r : ctx_.leverRecipes) if (r.id == b.recipe) return r.verified;
        return false;
    }
    const Catalog* cat = ctx_.catalogFor(ctx_.settings.ensureActive().aircraft);
    if (!cat || b.onPress.empty()) return false;
    auto same = [](const std::string& x, const std::string& y) {
        if (x.size() != y.size()) return false;
        for (size_t i = 0; i < x.size(); ++i)
            if (std::tolower(static_cast<unsigned char>(x[i])) != std::tolower(static_cast<unsigned char>(y[i]))) return false;
        return true;
    };
    for (const Action& a : b.onPress) {
        if (a.kind == ActionKind::Delay) continue;
        bool ok = false;
        for (const auto& e : cat->entries)
            if (e.verified && (same(e.name, a.name) || same(e.api, a.name))) { ok = true; break; }
        if (!ok) return false;
    }
    return true;
}

std::vector<DoubleBinding> Ui::doubleBindings() {
    if (!ctx_.msfsProfilesRead) {
        ctx_.msfsProfiles = readMsfsControlProfiles();
        ctx_.msfsProfilesRead = true;
    }
    std::vector<DeviceButtons> devs;
    for (const auto& d : devices_) devs.push_back({d.key, d.name, d.buttonCodes});
    return findDoubleBindings(ctx_.settings.ensureActive(), devs, ctx_.msfsProfiles, status_.state == sim::ConnState::Connected ? status_.edition : 0);
}

// ---------------------------------------------------------------- Home

void Ui::drawHomeTab() {
    const float fs = ImGui::GetFontSize();
    const ImGuiStyle& style = ImGui::GetStyle();
    Settings& s = ctx_.settings;
    const Profile& p = s.ensureActive();
    const std::string loadedId = status_.aircraft.empty() ? std::string() : sim::identifyAircraft(status_.aircraft).id;
    const Catalog* loadedCat = loadedId.empty() ? nullptr : ctx_.catalogs.find(loadedId);
    const std::string loadedName = loadedId.empty() ? std::string("No aircraft") : loadedCat ? loadedCat->label() : sim::identifyAircraft(status_.aircraft).label;
    const bool connected = status_.state == sim::ConnState::Connected;
    const bool fits = !loadedId.empty() && profileFitsAircraft(p, loadedId);

    // what is going on
    const float avail = ImGui::GetContentRegionAvail().x;
    const float tileW = (avail - style.ItemSpacing.x * 3) / 4.0f;
    auto tile = [&](const char* id, const char* label, const std::string& big, const ImVec4& bigColor, const std::string& small, const ImVec4& smallColor) {
        beginCard(id, tileW);
        sectionLabel(label);
        ImGui::PushFont(nullptr, fs * 1.18f);
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(bigColor, "%s", big.c_str());
        ImGui::PopTextWrapPos();
        ImGui::PopFont();
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(smallColor, "%s", small.c_str());
        ImGui::PopTextWrapPos();
        endCard();
    };
    std::string flyingSmall = connected ? (status_.edition == 2024 ? "MSFS 2024" : status_.edition == 2020 ? "MSFS 2020" : "MSFS") : "the sim is not connected";
    if (connected && status_.paused) flyingSmall += " · paused";
    tile("##t_ac", "Flying", loadedName, ui::palette::kText, flyingSmall, ui::palette::kTextDim);
    ImGui::SameLine();
    const char* profSmall = loadedId.empty() ? "no aircraft loaded" : !fits ? "not made for this aircraft" : ctx_.autoSwitchedTo == p.name ? "switched automatically" : "picked by hand";
    tile("##t_prof", "Profile", p.name, ui::palette::kText, profSmall, loadedId.empty() || fits ? (ctx_.autoSwitchedTo == p.name ? kGreen : ui::palette::kTextDim) : kYellow);
    ImGui::SameLine();
    const char* mf = status_.mobiflight == sim::ModuleState::Detected ? "MobiFlight ready" : status_.mobiflight == sim::ModuleState::NotFound ? "MobiFlight not found" : "MobiFlight not checked yet";
    tile("##t_sim", "Sim", connected ? "Connected" : connStateText(status_.state), connected ? kGreen : kYellow, mf, status_.mobiflight == sim::ModuleState::NotFound ? kYellow : ui::palette::kTextDim);
    ImGui::SameLine();
    std::string devNames;
    for (const auto& d : devices_) devNames += (devNames.empty() ? "" : " · ") + d.name;
    tile("##t_dev", "Devices", std::to_string(devices_.size()), ui::palette::kText, devNames.empty() ? "none found" : devNames, ui::palette::kTextDim);

    ImGui::Spacing();
    // what to do
    const float wide = (avail - style.ItemSpacing.x * 2) * 2.0f / 3.0f + style.ItemSpacing.x, narrow = (avail - style.ItemSpacing.x * 2) / 3.0f;
    {
        ImVec4 accent = kAccent;
        beginCard("##bind", wide, &accent);
        ImGui::PushFont(nullptr, fs * 1.1f);
        ImGui::TextUnformatted("Bind a control");
        ImGui::PopFont();
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextDisabled("Press the button, switch or lever you want to use. The companion finds it and opens a new binding for it, with this aircraft's controls to pick from.");
        ImGui::PopTextWrapPos();
        ImGui::Spacing();
        if (!homeListening_) {
            if (ImGui::Button("Press a control")) {
                homeListening_ = true;
                ctx_.engine.beginCapture();
            }
        } else {
            const ImVec2 c = ImGui::GetCursorScreenPos();
            const float pulse = 0.5f + 0.5f * std::sin(static_cast<float>(ImGui::GetTime()) * 4.0f);
            ImDrawList* dl = ImGui::GetWindowDrawList();
            dl->AddCircleFilled(ImVec2(c.x + fs * 0.5f, c.y + ImGui::GetFrameHeight() * 0.5f), fs * (0.32f + 0.12f * pulse), col(withAlpha(kAccent, 0.35f)));
            dl->AddCircleFilled(ImVec2(c.x + fs * 0.5f, c.y + ImGui::GetFrameHeight() * 0.5f), fs * 0.28f, col(kAccent));
            ImGui::Dummy(ImVec2(fs * 1.1f, ImGui::GetFrameHeight()));
            ImGui::SameLine();
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("Listening: press a button, flip a switch or move a lever");
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) {
                homeListening_ = false;
                ctx_.engine.cancelCapture();
            }
            if (auto got = ctx_.engine.takeCaptured()) {
                homeListening_ = false;
                openEditor(-1);
                draft_.input = *got;
                page_ = "Bindings";
                pagePlugin_.clear();
            }
        }
        endCard();
    }
    ImGui::SameLine();
    beginCard("##tasks_side", narrow);
    auto link = [&](const char* title, const char* text, const char* page) {
        if (ImGui::Selectable(title, false, ImGuiSelectableFlags_None)) { page_ = page; pagePlugin_.clear(); }
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextDisabled("%s", text);
        ImGui::PopTextWrapPos();
        ImGui::Spacing();
    };
    link("Follow a guide", "Procedures for the loaded aircraft, step by step.", "Guides");
    link("Calibrate levers", "Where the gates of your throttle are.", "Devices");
    link("Vibration", "What makes the grips vibrate.", "Vibration");
    endCard();

    ImGui::Spacing();
    sectionLabel("NEEDS A LOOK");
    struct Item { ImVec4 c; std::string title, text, button; int what; std::string arg; };
    std::vector<Item> items;
    if (!loadedId.empty() && !fits) {
        const Profile* match = nullptr;
        for (const auto& q : s.profiles) if (profileFitsAircraft(q, loadedId)) { match = &q; break; }
        items.push_back({kYellow, "The active profile is not made for the " + loadedName,
                         match ? "\"" + match->name + "\" is, but does not switch by itself" : "no profile is made for it yet",
                         match ? "Use " + match->name : "Bindings", match ? 1 : 2, match ? match->name : ""});
    }
    const auto twice = doubleBindings();
    if (!twice.empty()) {
        std::string which;
        for (size_t i = 0; i < twice.size() && i < 6; ++i) which += (i ? ", " : "") + std::to_string(twice[i].button);
        if (twice.size() > 6) which += "...";
        items.push_back({kRed, std::to_string(twice.size()) + (twice.size() == 1 ? " button bound twice" : " buttons bound twice"),
                         "your MSFS control profile \"" + twice[0].msfsProfile + "\" also uses button" + (twice.size() == 1 ? " " : "s ") + which, "Show", 3, "twice"});
    }
    int untried = 0;
    for (const auto& b : p.bindings) if (b.enabled && !bindingChecked(b)) ++untried;
    if (untried > 0)
        items.push_back({kYellow, std::to_string(untried) + (untried == 1 ? " binding not tried on this aircraft" : " bindings not tried on this aircraft"),
                         "they were not marked as checked in the sim yet", "List", 3, "untried"});
    for (const auto& b : p.bindings)
        if (b.cutoffMode == ZoneMode::Reverse && bindingChecked(b)) {
            items.push_back({kGreen, "Reverse in the cut-off is ready", "\"" + b.label + "\" and its pair, from a checked recipe", "Open", 3, "reverse"});
            break;
        }
    if (items.empty()) ImGui::TextDisabled("Nothing: everything looks fine.");
    for (size_t i = 0; i < items.size(); ++i) {
        const Item& it = items[i];
        ImGui::PushID(static_cast<int>(i));
        beginCard("##attn", avail);
        const ImVec2 a = ImGui::GetWindowPos();
        ImGui::GetWindowDrawList()->AddRectFilled(a, ImVec2(a.x + fs * 0.28f, a.y + ImGui::GetWindowHeight()), col(it.c), fs * 0.6f, ImDrawFlags_RoundCornersLeft);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(it.title.c_str());
        ImGui::SameLine();
        ImGui::TextDisabled("· %s", it.text.c_str());
        const float bw = ImGui::CalcTextSize(it.button.c_str()).x + style.FramePadding.x * 2;
        ImGui::SameLine(std::max(ImGui::GetCursorPosX(), ImGui::GetWindowContentRegionMax().x - bw));
        if (ImGui::Button(it.button.c_str())) {
            if (it.what == 1) { s.activeProfile = it.arg; ctx_.autoSwitchedTo.clear(); ctx_.markDirty(); }
            else { page_ = "Bindings"; pagePlugin_.clear(); bindingsFilter_ = it.arg; }
        }
        endCard();
        ImGui::PopID();
    }
}

}  // namespace lr::app
