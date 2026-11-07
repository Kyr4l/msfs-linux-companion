// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kyr4l
// The Profiles page: every profile as a row of a table, with its aircraft, the automatic switch, its theme and its menu.
#include "ui/Ui.hpp"
#include "ui/UiCommon.hpp"
#include "ui/Theme.hpp"

#include <algorithm>
#include <cfloat>

#include <imgui.h>
#include <imgui_internal.h>  // BeginComboPreview: the aircraft cell draws its chips
#include <imgui_stdlib.h>

#include "sim/AircraftId.hpp"

namespace lr::app {

using namespace uidetail;

namespace {

// The aircraft of a profile; the first one is the one whose catalog the editor offers.
std::vector<std::string> aircraftOf(const Profile& p) {
    std::vector<std::string> v{p.aircraft};
    v.insert(v.end(), p.alsoAircraft.begin(), p.alsoAircraft.end());
    return v;
}

bool containsNoCase(const std::string& text, const std::string& word) {
    if (word.empty()) return true;
    auto low = [](std::string s) {
        for (auto& ch : s) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        return s;
    };
    return low(text).find(low(word)) != std::string::npos;
}

// Six dots: the handle a row is dragged by.
bool gripHandle(const char* id, float h) {
    const float w = ImGui::GetFontSize() * 0.9f;
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton(id, ImVec2(w, h));
    const bool hot = ImGui::IsItemHovered() || ImGui::IsItemActive();
    if (hot) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
    const ImU32 c = ImGui::GetColorU32(hot ? ImGuiCol_Text : ImGuiCol_TextDisabled);
    const float r = h * 0.06f + 0.5f;
    for (int row = -1; row <= 1; ++row)
        for (int k = 0; k < 2; ++k)
            ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(p.x + w * (0.35f + 0.3f * k), p.y + h * 0.5f + row * h * 0.2f), r, c, 8);
    return ImGui::IsItemActive();
}

}  // namespace

void Ui::drawProfilesTab() {
    Settings& s = ctx_.settings;
    s.ensureActive();
    const float fs = ImGui::GetFontSize();
    const std::string loaded = detectAircraft(status_.aircraft);
    auto label = [&](const std::string& id) {
        const Catalog* c = ctx_.catalogs.find(id);
        return c ? c->label() : id;
    };

    if (ImGui::Button("New profile")) {
        Profile created;
        created.name = uniqueProfileName("New profile");
        created.aircraft = !loaded.empty() ? ctx_.catalogs.canonicalId(loaded) : s.ensureActive().aircraft;
        s.profiles.push_back(created);  // the switch starts off: it does not take over from a profile of the same aircraft
        profRenameOf_ = created.name;
        profRenameText_ = created.name;
        profRenameFocus_ = true;
        ctx_.markDirty();
    }

    // Aircraft that several switching profiles want: the first in the list is the one used.
    {
        std::vector<std::string> seen;
        for (const auto& p : s.profiles) {
            if (!p.autoSwitch) continue;
            for (const auto& a : aircraftOf(p)) {
                if (std::any_of(seen.begin(), seen.end(), [&](const std::string& x) { return sim::sameAircraft(x, a); })) continue;
                seen.push_back(a);
                std::vector<const Profile*> who;
                for (const auto& q : s.profiles)
                    if (q.autoSwitch && profileFitsAircraft(q, a)) who.push_back(&q);
                if (who.size() < 2) continue;
                ImGui::TextColored(kYellow, "%s: %zu profiles switch to it. %s is used (higher in the list).", label(a).c_str(), who.size(), who.front()->name.c_str());
            }
        }
    }
    ImGui::Spacing();

    const int n = static_cast<int>(s.profiles.size());
    int moveFrom = -1, moveTo = -1, duplicateAt = -1;
    std::string askDelete;
    const ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_PadOuterX | ImGuiTableFlags_SizingFixedFit;
    if (ImGui::BeginTable("##profiles", 7, flags)) {
        ImGui::TableSetupColumn("##order", ImGuiTableColumnFlags_WidthFixed, fs * 0.9f);
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn("Aircraft", ImGuiTableColumnFlags_WidthStretch, 2.2f);
        ImGui::TableSetupColumn("Switch automatically", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn("Theme", ImGuiTableColumnFlags_WidthFixed, fs * 9.0f);
        ImGui::TableSetupColumn("Bindings", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn("##menu", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFrameHeight());
        ImGui::TableHeadersRow();

        for (int i = 0; i < n; ++i) {
            Profile& p = s.profiles[static_cast<size_t>(i)];
            ImGui::PushID(p.name.c_str());  // by name: a row being dragged keeps its id when it changes place
            ImGui::TableNextRow(0, ImGui::GetFrameHeight() + fs * 0.4f);
            const bool current = p.name == s.activeProfile;
            if (current) ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1, ImGui::GetColorU32(ui::palette::kAccentSoft));
            const std::vector<std::string> mine = aircraftOf(p);

            // order
            ImGui::TableNextColumn();
            if (gripHandle("##grip", ImGui::GetFrameHeight()) && !ImGui::IsItemHovered()) {
                const int next = i + (ImGui::GetMouseDragDelta(0).y < 0.0f ? -1 : 1);
                if (next >= 0 && next < n) { moveFrom = i; moveTo = next; ImGui::ResetMouseDragDelta(); }
            }
            if (ImGui::IsItemHovered() && !ImGui::IsItemActive()) ImGui::SetTooltip("Drag to change the order");

            // name
            ImGui::TableNextColumn();
            bool startRename = false;
            if (profRenameOf_ == p.name) {
                if (profRenameFocus_) { ImGui::SetKeyboardFocusHere(); profRenameFocus_ = false; }
                ImGui::SetNextItemWidth(-FLT_MIN);
                const bool enter = ImGui::InputText("##rename", &profRenameText_, ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
                if (enter || ImGui::IsItemDeactivated()) {
                    std::string wanted = profRenameText_;
                    while (!wanted.empty() && std::isspace(static_cast<unsigned char>(wanted.back()))) wanted.pop_back();
                    while (!wanted.empty() && std::isspace(static_cast<unsigned char>(wanted.front()))) wanted.erase(wanted.begin());
                    if (!ImGui::IsKeyPressed(ImGuiKey_Escape) && !wanted.empty() && wanted != p.name) {
                        const std::string old = p.name;
                        p.name = uniqueProfileName(wanted);
                        if (s.activeProfile == old) s.activeProfile = p.name;
                        if (ctx_.autoSwitchedTo == old) ctx_.autoSwitchedTo = p.name;
                        ctx_.markDirty();
                    }
                    profRenameOf_.clear();
                }
            } else {
                ImGui::AlignTextToFramePadding();
                ImGui::Selectable(p.name.c_str(), false, ImGuiSelectableFlags_AllowDoubleClick, ImGui::CalcTextSize(p.name.c_str()));
                if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) startRename = true;
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Double-click to rename");
                if (current) {
                    ImGui::SameLine();
                    ImGui::TextDisabled(ctx_.autoSwitchedTo == p.name ? "in use, switched automatically" : "in use");
                }
            }

            // aircraft
            ImGui::TableNextColumn();
            {
                std::string toAdd;
                int toRemove = -1;
                ImGui::SetNextItemWidth(-FLT_MIN);
                ImGui::SetNextWindowSizeConstraints(ImVec2(fs * 22, 0), ImVec2(FLT_MAX, fs * 26));
                if (ImGui::BeginCombo("##aircraft", "", ImGuiComboFlags_CustomPreview)) {
                    if (ImGui::IsWindowAppearing()) { profAircraftSearch_.clear(); ImGui::SetKeyboardFocusHere(); }
                    ImGui::SetNextItemWidth(-FLT_MIN);
                    ImGui::InputTextWithHint("##search", "Search aircraft", &profAircraftSearch_);
                    auto item = [&](const std::string& id, int index) {  // index in `mine`, -1 when not ticked
                        const std::string name = label(id);
                        if (!containsNoCase(name + " " + id, profAircraftSearch_)) return;
                        ImGui::PushID(id.c_str());
                        bool on = index >= 0;
                        const bool onlyOne = on && mine.size() == 1;
                        ImGui::BeginDisabled(onlyOne);
                        if (ImGui::Checkbox(name.c_str(), &on)) {
                            if (on) toAdd = id;
                            else toRemove = index;
                        }
                        ImGui::EndDisabled();
                        if (onlyOne && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("A profile needs one aircraft");
                        if (!loaded.empty() && sim::sameAircraft(id, loaded)) {
                            ImGui::SameLine(0, fs);
                            ImGui::TextColored(kGreen, "loaded");
                        } else {
                            std::string others;
                            for (const auto& q : s.profiles)
                                if (&q != &p && profileFitsAircraft(q, id)) others += (others.empty() ? "" : ", ") + q.name;
                            if (!others.empty()) {
                                ImGui::SameLine(0, fs);
                                ImGui::TextDisabled("%s", others.c_str());
                            }
                        }
                        ImGui::PopID();
                    };
                    for (size_t k = 0; k < mine.size(); ++k) item(mine[k], static_cast<int>(k));
                    ImGui::Separator();
                    if (!loaded.empty() && !profileFitsAircraft(p, loaded) && !ctx_.catalogs.find(loaded)) item(loaded, -1);
                    for (const auto& c : ctx_.catalogs)
                        if (c.aircraft != kStandardCatalogId && !profileFitsAircraft(p, c.aircraft)) item(c.aircraft, -1);
                    ImGui::EndCombo();
                }
                if (ImGui::BeginComboPreview()) {  // the aircraft as chips
                    ImDrawList* dl = ImGui::GetWindowDrawList();
                    const ImVec2 at = ImGui::GetCursorScreenPos();
                    const float padY = ImGui::GetStyle().FramePadding.y * 0.6f, limit = ImGui::GetItemRectMax().x - ImGui::GetFrameHeight() - fs * 0.3f;
                    float x = at.x - fs * 0.2f;
                    for (size_t k = 0; k < mine.size(); ++k) {
                        const std::string t = label(mine[k]);
                        const float w = ImGui::CalcTextSize(t.c_str()).x + fs * 0.9f;
                        if (k > 0 && x + w > limit) {
                            dl->AddText(ImVec2(x, at.y), ImGui::GetColorU32(ImGuiCol_TextDisabled), ("+" + std::to_string(mine.size() - k)).c_str());
                            break;
                        }
                        dl->AddRectFilled(ImVec2(x, at.y - padY), ImVec2(x + w, at.y + fs + padY), ImGui::GetColorU32(ui::palette::kSurfaceHi), fs);
                        dl->AddRect(ImVec2(x, at.y - padY), ImVec2(x + w, at.y + fs + padY), ImGui::GetColorU32(ui::palette::kBorder), fs);
                        dl->AddText(ImVec2(x + fs * 0.45f, at.y), ImGui::GetColorU32(ImGuiCol_Text), t.c_str());
                        x += w + fs * 0.3f;
                    }
                    ImGui::EndComboPreview();
                }
                if (!toAdd.empty()) {
                    p.alsoAircraft.push_back(ctx_.catalogs.canonicalId(toAdd));
                    ctx_.markDirty();
                } else if (toRemove == 0 && !p.alsoAircraft.empty()) {  // the first one goes: the next one names the catalog
                    p.aircraft = p.alsoAircraft.front();
                    p.alsoAircraft.erase(p.alsoAircraft.begin());
                    if (current) viewAircraft_.clear();
                    ctx_.markDirty();
                } else if (toRemove > 0) {
                    p.alsoAircraft.erase(p.alsoAircraft.begin() + (toRemove - 1));
                    ctx_.markDirty();
                }
                // profiles sharing an aircraft with this one; yellow when both switch and this one loses
                std::string shared;
                bool loses = false;
                for (int j = 0; j < n; ++j) {
                    const Profile& q = s.profiles[static_cast<size_t>(j)];
                    if (j == i) continue;
                    const bool share = std::any_of(mine.begin(), mine.end(), [&](const std::string& a) { return profileFitsAircraft(q, a); });
                    if (!share) continue;
                    shared += (shared.empty() ? "" : ", ") + q.name;
                    if (j < i && p.autoSwitch && q.autoSwitch) loses = true;
                }
                if (!shared.empty()) {
                    if (loses) ImGui::TextColored(kYellow, "also in %s", shared.c_str());
                    else ImGui::TextDisabled("also in %s", shared.c_str());
                }
            }

            // switch automatically
            ImGui::TableNextColumn();
            if (toggleSwitch("##auto", &p.autoSwitch)) ctx_.markDirty();
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Use this profile when one of its aircraft is loaded");

            // theme
            ImGui::TableNextColumn();
            {
                int count = 0;
                const ui::ThemeInfo* list = ui::themes(count);
                const char* currentTheme = "App theme";
                for (int k = 0; k < count; ++k)
                    if (p.uiTheme == list[k].id) currentTheme = list[k].name;
                ImGui::SetNextItemWidth(-FLT_MIN);
                if (ImGui::BeginCombo("##theme", currentTheme)) {
                    if (ImGui::Selectable("App theme", p.uiTheme.empty())) { p.uiTheme.clear(); ctx_.markDirty(); }
                    for (int k = 0; k < count; ++k)
                        if (ImGui::Selectable(list[k].name, p.uiTheme == list[k].id)) { p.uiTheme = list[k].id; ctx_.markDirty(); }
                    ImGui::EndCombo();
                }
            }

            // bindings
            ImGui::TableNextColumn();
            if (p.bindings.empty()) {
                if (ImGui::Button("Copy from...")) ImGui::OpenPopup("copy_from");
                if (ImGui::BeginPopup("copy_from")) {
                    ImGui::TextDisabled("Copy the bindings of");
                    for (const auto& q : s.profiles) {
                        if (&q == &p || q.bindings.empty()) continue;
                        if (ImGui::Selectable((q.name + "  (" + std::to_string(q.bindings.size()) + ")").c_str())) {
                            p.bindings = q.bindings;
                            ctx_.markDirty();
                        }
                    }
                    ImGui::EndPopup();
                }
            } else {
                ImGui::AlignTextToFramePadding();
                ImGui::Text("%zu", p.bindings.size());
            }

            // menu
            ImGui::TableNextColumn();
            if (dotsButton("##more")) ImGui::OpenPopup("row_menu");
            if (ImGui::BeginPopup("row_menu")) {
                if (ImGui::MenuItem("Edit bindings")) {
                    s.activeProfile = p.name;
                    ctx_.autoSwitchedTo.clear();
                    viewAircraft_.clear();
                    page_ = "Bindings";
                    ctx_.markDirty();
                }
                if (ImGui::MenuItem("Use this profile", nullptr, false, !current)) {
                    s.activeProfile = p.name;
                    ctx_.autoSwitchedTo.clear();
                    viewAircraft_.clear();
                    ctx_.markDirty();
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Rename")) startRename = true;
                if (ImGui::MenuItem("Duplicate")) duplicateAt = i;
                if (ImGui::MenuItem("Move up", nullptr, false, i > 0)) { moveFrom = i; moveTo = i - 1; }
                if (ImGui::MenuItem("Move down", nullptr, false, i + 1 < n)) { moveFrom = i; moveTo = i + 1; }
                ImGui::Separator();
                if (ImGui::MenuItem("Delete", nullptr, false, n > 1)) askDelete = p.name;  // the last profile cannot be deleted
                ImGui::EndPopup();
            }
            if (startRename) { profRenameOf_ = p.name; profRenameText_ = p.name; profRenameFocus_ = true; }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    // changes to the list, after the rows that point into it
    if (moveFrom >= 0 && moveTo >= 0 && moveFrom != moveTo) {
        std::swap(s.profiles[static_cast<size_t>(moveFrom)], s.profiles[static_cast<size_t>(moveTo)]);
        ctx_.markDirty();
    }
    if (duplicateAt >= 0) {
        Profile copy = s.profiles[static_cast<size_t>(duplicateAt)];
        copy.name = uniqueProfileName(copy.name + " copy");
        copy.autoSwitch = false;  // two switching profiles for the same aircraft would compete
        s.profiles.insert(s.profiles.begin() + duplicateAt + 1, copy);
        profRenameOf_ = copy.name;
        profRenameText_ = copy.name;
        profRenameFocus_ = true;
        ctx_.markDirty();
    }
    if (!askDelete.empty()) { profDelete_ = askDelete; ImGui::OpenPopup("Delete profile"); }
    if (ImGui::BeginPopupModal("Delete profile", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        Profile* doomed = nullptr;
        for (auto& p : s.profiles) if (p.name == profDelete_) doomed = &p;
        if (!doomed) ImGui::CloseCurrentPopup();
        else {
            ImGui::Text("Delete \"%s\" and its %zu bindings?", doomed->name.c_str(), doomed->bindings.size());
            ImGui::Spacing();
            if (ImGui::Button("Delete") && s.profiles.size() > 1) {
                const std::string name = profDelete_;
                s.profiles.erase(std::remove_if(s.profiles.begin(), s.profiles.end(), [&](const Profile& x) { return x.name == name; }), s.profiles.end());
                if (s.activeProfile == name) { s.activeProfile = s.profiles.front().name; viewAircraft_.clear(); }
                if (ctx_.autoSwitchedTo == name) ctx_.autoSwitchedTo.clear();
                ctx_.markDirty();
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    ImGui::Spacing();
    ImGui::TextDisabled("When an aircraft is in several profiles that switch automatically, the highest one is used. Drag the dots to change the order.");
    ImGui::TextDisabled("The first aircraft of a profile decides which catalog the binding editor shows.");
    if (!s.themeFromProfile && std::any_of(s.profiles.begin(), s.profiles.end(), [](const Profile& p) { return !p.uiTheme.empty(); })) {
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(kYellow, "Profile themes are off in Settings.");
        ImGui::SameLine();
        if (ImGui::Button("Turn on")) { s.themeFromProfile = true; ctx_.markDirty(); }
    }
}

}  // namespace lr::app
