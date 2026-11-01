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

// ---------------------------------------------------------------- Catalog

// One or two actions for a catalog entry: the entry itself, plus its first-officer twin when
// "both sides" is ticked and the catalog has that twin.
const Catalog& Ui::pickerCatalog() const {
    static const Catalog empty;
    const Profile* p = ctx_.settings.active();
    const Catalog* c = ctx_.catalogFor(p ? p->aircraft : kDefaultAircraft);
    return c ? *c : empty;
}

const Catalog& Ui::currentCatalog() const {
    static const Catalog empty;
    if (viewAircraft_.empty()) return pickerCatalog();
    const Catalog* c = ctx_.catalogFor(viewAircraft_);
    return c ? *c : empty;
}

std::string Ui::detectAircraft(const std::string& cfgPath) const { return sim::identifyAircraft(cfgPath).id; }


std::string Ui::uniqueProfileName(const std::string& wanted) const {
    auto taken = [&](const std::string& n) {
        for (const auto& p : ctx_.settings.profiles)
            if (p.name == n) return true;
        return false;
    };
    if (!taken(wanted)) return wanted;
    for (int i = 2;; ++i) {
        std::string c = wanted + " " + std::to_string(i);
        if (!taken(c)) return c;
    }
}

// The curated catalogs and the guides of Asobo's and Microsoft's aircraft were made and checked on MSFS 2020. The 2024 version of an aircraft
// may differ (controls, polarity, the names of its variables), so say so instead of letting the "verified" marks pass for 2024.
// The FlyByWire aircraft are one add-on for both and are left out.
std::string Ui::editionCaveat(const std::string& aircraftId, bool guide) const {
    if (status_.edition != 2024) return {};
    if (aircraftId == "A32NX" || aircraftId == "A380X" || aircraftId == kStandardCatalogId) return {};
    const Catalog* c = ctx_.catalogs.find(aircraftId);
    if (!c || !c->curated) return {};
    return std::string(guide ? "Guide" : "Catalog") + " made on MSFS 2020: it may differ on 2024.";
}

std::vector<Action> Ui::catalogActions(const CatalogEntry& e) const {
    std::vector<Action> out = e.toActions();
    if (catalogBothSides_ && e.isCaptainSide()) {
        const std::string twin = e.firstOfficerApi();
        for (const auto& o : currentCatalog().entries)
            if (o.api == twin) { out.push_back(o.toAction()); break; }
    }
    return out;
}

static void applyEntryRangeToAxisDraft(Binding& draft, const CatalogEntry& e) {
    if (draft.input.kind == input::InputKind::Axis && draft.axis.mode == AxisMode::Value && e.hasAxisRange &&
        draft.axis.outMin == 0 && draft.axis.outMax == 100) {
        draft.axis.outMin = e.axisMin;
        draft.axis.outMax = e.axisMax;
        draft.axis.integer = true;
    }
}

std::vector<Action>& Ui::catalogTargetList() {
    switch (catalogTarget_) {
        case 1: return draft_.onRelease;
        case 2: return draft_.onCutoff;
        case 3: return draft_.onCutoffLeave;
        case 4: return draft_.onAfterburner;
        case 5: return draft_.onAfterburnerLeave;
        default: return draft_.onPress;
    }
}

const char* Ui::catalogTargetName(int target) {
    static const char* names[] = {"PRESS", "RELEASE", "ENTER CUT-OFF", "LEAVE CUT-OFF", "ENTER AFTERBURNER", "LEAVE AFTERBURNER"};
    return names[target >= 0 && target < 6 ? target : 0];
}

void Ui::newBindingFrom(const CatalogEntry& e) {
    openEditor(-1);
    for (const auto& a : catalogActions(e)) draft_.onPress.push_back(a);
    draft_.label = e.function;
    if (e.isIncDec()) draft_.repeat = true;  // knobs on buttons: keep clicking while held
}


// ---------------------------------------------------------------- Catalog panels view

namespace {
std::string behaviorText(const CatalogEntry& e) {
    if (e.behavior == "latched") return e.positions >= 3 ? "Switch, " + std::to_string(e.positions) + " positions" : "Switch";
    if (e.behavior == "knob_up") return "Knob up";
    if (e.behavior == "knob_down") return "Knob down";
    if (e.behavior == "press") return "Push button";
    if (e.behavior == "level") return "Axis" + (e.hasAxisRange ? " (" + std::to_string(static_cast<int>(e.axisMin)) + " to " + std::to_string(static_cast<int>(e.axisMax)) + ")" : std::string());
    return e.behavior;
}

bool endsWith(const std::string& s, const char* suffix) {
    const size_t n = strlen(suffix);
    return s.size() >= n && s.compare(s.size() - n, n, suffix) == 0;
}

// Which entry a click on a control should pick: switches toggle, push buttons push, knobs click up.
int entryScore(const CatalogEntry& e, bool knob) {
    if (e.kind == "calc_code") return 95;  // recipes are complete on their own
    if (e.kind == "input_event") return e.behavior == "knob_up" ? 88 : (e.behavior == "knob_down" ? 87 : 86);
    if (e.kind == "lvar" && e.values == "0|1") return 80;
    if (e.kind == "sim_event") {
        if (endsWith(e.api, "_PUSH")) return 90;
        if (knob && endsWith(e.api, "_INC")) return 88;
        if (endsWith(e.api, "_SET") || endsWith(e.api, "_SET_EX1") || endsWith(e.api, "_DEC") || endsWith(e.api, "_INC")) return 45;
        return e.api.rfind("A32NX.", 0) == 0 ? 70 : 60;
    }
    if (e.kind == "h_event") return 50;
    if (e.kind == "lvar") return 40;
    return 30;
}

bool containsAny(const std::string& hay, std::initializer_list<const char*> needles) {
    for (const char* n : needles)
        if (hay.find(n) != std::string::npos) return true;
    return false;
}

std::string upper(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}
}  // namespace

void Ui::rebuildPanelModel(const Catalog& cat) {
    panelModel_ = PanelModel{};
    panelModel_.cat = &cat;
    panelModel_.entryCount = cat.entries.size();
    panelModel_.revision = cat.revision;
    for (int i = 0; i < static_cast<int>(cat.entries.size()); ++i) {
        const CatalogEntry& e = cat.entries[static_cast<size_t>(i)];
        if (std::find(panelModel_.regions.begin(), panelModel_.regions.end(), e.panel) == panelModel_.regions.end()) panelModel_.regions.push_back(e.panel);
        PanelCard* card = nullptr;
        for (auto& c : panelModel_.cards)
            if (c.region == e.panel && c.section == e.section) { card = &c; break; }
        if (!card) {
            panelModel_.cards.push_back({e.panel, e.section, e.reference, {}});
            card = &panelModel_.cards.back();
        }
        if (card->reference.empty()) card->reference = e.reference;
        PanelGroup* g = nullptr;
        const std::string fn = e.function.empty() ? std::string("(other)") : e.function;
        for (auto& x : card->groups)
            if (x.function == fn) { g = &x; break; }
        if (!g) {
            card->groups.push_back({fn, {}, -1, 0});
            g = &card->groups.back();
        }
        g->entries.push_back(i);
    }
    for (auto& card : panelModel_.cards) {
        for (auto& g : card.groups) {
            const std::string fn = upper(g.function);
            bool incdec = false, steps = false;
            for (int idx : g.entries) {
                const CatalogEntry& e = cat.entries[static_cast<size_t>(idx)];
                if (endsWith(e.api, "_INC") || endsWith(e.api, "_DEC") || e.behavior == "knob_up" || e.behavior == "knob_down") incdec = true;
                if (e.behavior == "latched" && e.positions >= 3) steps = true;
                if (e.behavior == "level") incdec = true;
                if (e.kind == "lvar" && e.values.find("..") != std::string::npos && e.values != "0..1") steps = true;
            }
            if (incdec || containsAny(fn, {"KNOB", "ROTARY", "DIAL", "BRT", "BRIGHT"})) g.style = 1;
            else if (steps || containsAny(fn, {"SELECTOR", "SWITCH", "MODE"})) g.style = 2;
            int best = -1, bestScore = -1;
            for (int idx : g.entries) {
                const int sc = entryScore(cat.entries[static_cast<size_t>(idx)], g.style == 1);
                if (sc > bestScore) { bestScore = sc; best = idx; }
            }
            g.preferred = best;
        }
    }
}

void Ui::drawPanelsView(const Catalog& cat, bool pickMode, const std::vector<std::string>& words, float reserveBelow) {
    if (panelModel_.cat != &cat || panelModel_.entryCount != cat.entries.size() || panelModel_.revision != cat.revision) rebuildPanelModel(cat);

    auto entryVisible = [&](int idx) {
        const CatalogEntry& e = cat.entries[static_cast<size_t>(idx)];
        for (const auto& w : words)
            if (e.searchKey.find(w) == std::string::npos) return false;
        return true;
    };

    const float fs = ImGui::GetFontSize();
    const float pad = fs * 0.45f;
    const float cardW = fs * 19.0f;
    const float cellW = (cardW - 3 * pad) / 2;
    const float cellH = fs * 3.4f;
    const float headH = fs * 1.9f;
    const float gap = fs * 0.7f;

    ImGui::BeginChild("panels_view", ImVec2(0, -reserveBelow), ImGuiChildFlags_Borders);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float availW = ImGui::GetContentRegionAvail().x;
    const int cols = std::max(1, static_cast<int>((availW + gap) / (cardW + gap)));
    const ImU32 colCard = ImGui::GetColorU32(ImVec4(0.13f, 0.14f, 0.17f, 1.0f));
    const ImU32 colCardEdge = ImGui::GetColorU32(ImVec4(0.30f, 0.32f, 0.38f, 1.0f));
    const ImU32 colText = ImGui::GetColorU32(ImGuiCol_Text);
    const ImU32 colDim = ImGui::GetColorU32(ImGuiCol_TextDisabled);
    bool anything = false;

    for (const std::string& region : panelModel_.regions) {
        // visible cards of the region and their heights
        struct Vis { const PanelCard* card; std::vector<const PanelGroup*> groups; float height; };
        std::vector<Vis> vis;
        for (const auto& card : panelModel_.cards) {
            if (card.region != region) continue;
            Vis v{&card, {}, 0};
            for (const auto& g : card.groups) {
                bool any = false;
                for (int idx : g.entries) any |= entryVisible(idx);
                if (any) v.groups.push_back(&g);
            }
            if (v.groups.empty()) continue;
            const int rows = static_cast<int>((v.groups.size() + 1) / 2);
            v.height = headH + pad + rows * (cellH + pad);
            vis.push_back(std::move(v));
        }
        if (vis.empty()) continue;
        anything = true;
        ImGui::SeparatorText(region.c_str());
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        std::vector<float> colH(static_cast<size_t>(cols), 0.0f);
        for (const auto& v : vis) {
            const size_t col = static_cast<size_t>(std::min_element(colH.begin(), colH.end()) - colH.begin());
            const ImVec2 p0(origin.x + static_cast<float>(col) * (cardW + gap), origin.y + colH[col]);
            const ImVec2 p1(p0.x + cardW, p0.y + v.height);
            colH[col] += v.height + gap;
            dl->AddRectFilled(p0, p1, colCard, fs * 0.5f);
            dl->AddRect(p0, p1, colCardEdge, fs * 0.5f);
            // header: panel name + link to the real photos
            const std::string title = v.card->section.empty() ? v.card->region : v.card->section;
            const bool hasRef = !v.card->reference.empty();
            const float photoW = hasRef ? ImGui::CalcTextSize("photos >").x + pad : 0.0f;
            dl->PushClipRect(ImVec2(p0.x, p0.y), ImVec2(p1.x - photoW, p0.y + headH), true);
            dl->AddText(ImVec2(p0.x + pad, p0.y + (headH - fs) * 0.5f), colText, title.c_str());
            dl->PopClipRect();
            if (hasRef) {
                ImGui::SetCursorScreenPos(ImVec2(p1.x - photoW - pad * 0.5f, p0.y + 2));
                ImGui::PushID(&v);
                const bool clicked = ImGui::InvisibleButton("photos", ImVec2(photoW + pad * 0.5f, headH - 4));
                const bool hov = ImGui::IsItemHovered();
                dl->AddText(ImVec2(p1.x - photoW, p0.y + (headH - fs) * 0.5f), hov ? ImGui::GetColorU32(ImVec4(0.45f, 0.75f, 1.0f, 1.0f)) : colDim, "photos >");
                if (hov) ImGui::SetTooltip("Open photos in the browser");
                if (clicked && !openDocsUrl(v.card->reference)) LOG_WARN("Could not open %s (is xdg-open installed?)", v.card->reference.c_str());
                ImGui::PopID();
            }
            // controls
            for (size_t gi = 0; gi < v.groups.size(); ++gi) {
                const PanelGroup& g = *v.groups[gi];
                const ImVec2 c0(p0.x + pad + static_cast<float>(gi % 2) * (cellW + pad), p0.y + headH + static_cast<float>(gi / 2) * (cellH + pad));
                const ImVec2 c1(c0.x + cellW, c0.y + cellH);
                ImGui::SetCursorScreenPos(c0);
                ImGui::PushID(static_cast<int>(gi + 1) * 100003 + static_cast<int>(&v - vis.data()) + static_cast<int>(region.size()));
                ImGui::PushID(v.card->section.c_str());
                ImGui::PushID(g.function.c_str());
                const bool clicked = ImGui::InvisibleButton("ctl", ImVec2(cellW, cellH));
                const bool hovered = ImGui::IsItemHovered();
                const bool doubled = hovered && ImGui::IsMouseDoubleClicked(0);
                bool selected = false;
                for (int idx : g.entries) selected |= idx == catalogSelected_;
                const ImU32 fill = selected ? ImGui::GetColorU32(ImVec4(0.18f, 0.40f, 0.72f, 1.0f))
                                            : hovered ? ImGui::GetColorU32(ImVec4(0.26f, 0.29f, 0.36f, 1.0f))
                                                      : ImGui::GetColorU32(ImVec4(0.20f, 0.22f, 0.27f, 1.0f));
                dl->AddRectFilled(c0, c1, fill, fs * 0.35f);
                drawControlIcon(dl, ImVec2(c0.x + cellH * 0.5f, c0.y + cellH * 0.5f), cellH * 0.28f, g.style);
                // verified on the real simulator: a small green tick in the corner
                {
                    bool anyVerified = false;
                    for (int idx : g.entries) anyVerified |= cat.entries[static_cast<size_t>(idx)].verified;
                    if (anyVerified) {
                        const ImVec2 t(c1.x - fs * 0.7f, c0.y + fs * 0.7f);
                        dl->AddCircleFilled(t, fs * 0.32f, ImGui::GetColorU32(ImVec4(0.25f, 0.75f, 0.35f, 1)), 12);
                        dl->AddLine(ImVec2(t.x - fs * 0.16f, t.y), ImVec2(t.x - fs * 0.03f, t.y + fs * 0.13f), ImGui::GetColorU32(ImVec4(0.05f, 0.1f, 0.05f, 1)), 1.6f);
                        dl->AddLine(ImVec2(t.x - fs * 0.03f, t.y + fs * 0.13f), ImVec2(t.x + fs * 0.17f, t.y - fs * 0.12f), ImGui::GetColorU32(ImVec4(0.05f, 0.1f, 0.05f, 1)), 1.6f);
                    }
                }
                // label (wraps over at most two lines, clipped)
                dl->PushClipRect(ImVec2(c0.x + cellH, c0.y), c1, true);
                dl->AddText(ImGui::GetFont(), fs, ImVec2(c0.x + cellH, c0.y + fs * 0.35f), colText, g.function.c_str(), nullptr, cellW - cellH - pad * 0.5f);
                dl->PopClipRect();
                if (hovered) {
                    ImGui::BeginTooltip();
                    ImGui::TextUnformatted(g.function.c_str());
                    ImGui::Separator();
                    int shown = 0;
                    for (int idx : g.entries) {
                        const CatalogEntry& e = cat.entries[static_cast<size_t>(idx)];
                        if (shown++ >= 8) { ImGui::TextDisabled("..."); break; }
                        ImGui::TextDisabled("%s", e.type.c_str());
                        ImGui::SameLine();
                        ImGui::TextUnformatted(e.api.size() > 70 ? (e.api.substr(0, 67) + "...").c_str() : e.api.c_str());
                    }
                    const CatalogEntry& pe = cat.entries[static_cast<size_t>(g.preferred >= 0 ? g.preferred : g.entries[0])];
                    if (!pe.behavior.empty()) {
                        ImGui::Spacing();
                        ImGui::TextDisabled("%s", behaviorText(pe).c_str());
                        if (pe.verified) ImGui::TextColored(kGreen, "Verified");
                        else if (pe.kind == "input_event" && !pe.note.empty()) ImGui::TextColored(kYellow, "Not verified: %s", pe.note.c_str());
                        else if (pe.kind == "input_event") ImGui::TextColored(kYellow, "Not verified");
                    }
                    ImGui::TextDisabled("Double-click to bind");
                    ImGui::EndTooltip();
                }
                if (clicked) catalogSelected_ = g.preferred;
                if (doubled && g.preferred >= 0) {
                    catalogSelected_ = g.preferred;
                    const CatalogEntry& e = cat.entries[static_cast<size_t>(g.preferred)];
                    if (pickMode) {
                        for (const auto& a : catalogActions(e)) catalogTargetList().push_back(a);
                        ImGui::CloseCurrentPopup();
                    } else {
                        newBindingFrom(e);
                    }
                }
                ImGui::PopID();
                ImGui::PopID();
                ImGui::PopID();
            }
        }
        float maxH = 0;
        for (float h : colH) maxH = std::max(maxH, h);
        ImGui::SetCursorScreenPos(origin);
        ImGui::Dummy(ImVec2(availW, maxH));
    }
    if (!anything) ImGui::TextDisabled("No matches");
    ImGui::EndChild();

    // the other ways to drive the selected control
    if (catalogSelected_ >= 0 && catalogSelected_ < static_cast<int>(cat.entries.size())) {
        const CatalogEntry& sel = cat.entries[static_cast<size_t>(catalogSelected_)];
        const PanelGroup* group = nullptr;
        for (const auto& card : panelModel_.cards)
            for (const auto& g : card.groups)
                if (std::find(g.entries.begin(), g.entries.end(), catalogSelected_) != g.entries.end()) group = &g;
        if (group && group->entries.size() > 1) {
            ImGui::AlignTextToFramePadding();
            ImGui::TextDisabled("%s:", sel.function.c_str());
            for (int idx : group->entries) {
                const CatalogEntry& e = cat.entries[static_cast<size_t>(idx)];
                ImGui::SameLine();
                if (ImGui::GetContentRegionAvail().x < ImGui::CalcTextSize(e.api.c_str()).x + 30) ImGui::NewLine();
                ImGui::PushID(idx);
                if (idx == catalogSelected_) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.40f, 0.72f, 1.0f));
                if (ImGui::SmallButton(e.api.size() > 46 ? (e.api.substr(0, 43) + "...").c_str() : e.api.c_str())) catalogSelected_ = idx;
                if (idx == catalogSelected_) ImGui::PopStyleColor();
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s  %s  %s", e.type.c_str(), e.values.c_str(), e.remark.c_str());
                ImGui::PopID();
            }
        }
    }
}

void Ui::drawCatalogBrowser(const char* id, bool pickMode) {
    ImGui::PushID(id);
    ImGui::SetNextItemWidth(400);
    ImGui::InputTextWithHint("##filter", "Filter", &catalogFilter_);
    ImGui::SameLine();
    ImGui::TextDisabled("%zu entries", currentCatalog().entries.size());
    ImGui::SameLine();
    if (ImGui::RadioButton("Panels", catalogView_ == CatalogView::Panels)) catalogView_ = CatalogView::Panels;
    ImGui::SameLine();
    if (ImGui::RadioButton("List", catalogView_ == CatalogView::List)) catalogView_ = CatalogView::List;
    if (pickMode) {
        ImGui::SameLine();
        ImGui::Text("Adding to: %s", catalogTargetName(catalogTarget_));
    }

    const std::vector<std::string> words = searchWords(catalogFilter_);

    const float footer = pickMode ? ImGui::GetFrameHeightWithSpacing() * 3.5f : ImGui::GetFrameHeightWithSpacing() * 2.5f;
    ImGuiTableFlags tflags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable;
    if (catalogView_ == CatalogView::Panels) {
        drawPanelsView(pickMode ? pickerCatalog() : currentCatalog(), pickMode, words, footer + ImGui::GetFrameHeightWithSpacing() * 1.4f);
    } else if (ImGui::BeginTable("catalog", 5, tflags, ImVec2(0, -footer))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Panel", ImGuiTableColumnFlags_WidthStretch, 1.2f);
        ImGui::TableSetupColumn("Function", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn("API", ImGuiTableColumnFlags_WidthStretch, 1.8f);
        ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthStretch, 0.8f);
        ImGui::TableSetupColumn("Values / remark", ImGuiTableColumnFlags_WidthStretch, 1.5f);
        ImGui::TableHeadersRow();
        const Catalog& cat = pickMode ? pickerCatalog() : currentCatalog();
        for (int i = 0; i < static_cast<int>(cat.entries.size()); ++i) {
            const CatalogEntry& e = cat.entries[static_cast<size_t>(i)];
            if (!std::all_of(words.begin(), words.end(), [&](const std::string& w) { return e.searchKey.find(w) != std::string::npos; })) continue;
            ImGui::PushID(i);
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            std::string panel = e.section.empty() ? e.panel : e.panel + " / " + e.section;
            if (ImGui::Selectable(panel.c_str(), catalogSelected_ == i, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick)) {
                catalogSelected_ = i;
                if (ImGui::IsMouseDoubleClicked(0)) {
                    if (pickMode) {
                        for (const auto& a : catalogActions(e)) catalogTargetList().push_back(a);
                        applyEntryRangeToAxisDraft(draft_, e);
                        ImGui::CloseCurrentPopup();
                    } else {
                        newBindingFrom(e);
                    }
                }
            }
            if (!e.description.empty() && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", e.description.c_str());
            ImGui::TableSetColumnIndex(1); ImGui::TextUnformatted(e.function.c_str());
            ImGui::TableSetColumnIndex(2); ImGui::TextUnformatted(e.api.c_str());
            ImGui::TableSetColumnIndex(3); ImGui::TextUnformatted(e.type.c_str());
            ImGui::TableSetColumnIndex(4);
            ImGui::TextUnformatted(e.values.c_str());
            if (!e.remark.empty()) { ImGui::SameLine(); ImGui::TextDisabled("%s", e.remark.c_str()); }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    const Catalog& selCat = pickMode ? pickerCatalog() : currentCatalog();
    const CatalogEntry* sel = (catalogSelected_ >= 0 && catalogSelected_ < static_cast<int>(selCat.entries.size())) ? &selCat.entries[static_cast<size_t>(catalogSelected_)] : nullptr;
    if (sel) {
        const std::vector<Action> acts = catalogActions(*sel);
        std::string desc;
        for (size_t i = 0; i < acts.size(); ++i) desc += (i ? "  +  " : "") + acts[i].describe();
        ImGui::Text("%s", desc.c_str());
        if (actionNeedsMobiFlight(acts[0].kind)) { ImGui::SameLine(); ImGui::TextColored(kYellow, "(needs MobiFlight)"); }
        if (!sel->description.empty()) ImGui::TextWrapped("%s", sel->description.c_str());
        if (!sel->behavior.empty()) {
            ImGui::TextDisabled("%s", behaviorText(*sel).c_str());
            if (sel->verified) { ImGui::SameLine(); ImGui::TextColored(kGreen, "Verified"); }
            else if (sel->kind == "input_event") { ImGui::SameLine(); ImGui::TextColored(kYellow, "Not verified%s%s", sel->note.empty() ? "" : ": ", sel->note.c_str()); }
        }
        if (sel->isCaptainSide() && !sel->firstOfficerApi().empty()) {
            ImGui::Checkbox("Both sides (L and R)", &catalogBothSides_);
        }
        if (pickMode) {
            if (ImGui::Button("Add to press")) {
                for (const auto& a : acts) draft_.onPress.push_back(a);
                applyEntryRangeToAxisDraft(draft_, *sel);
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Add to release")) { for (const auto& a : acts) draft_.onRelease.push_back(a); ImGui::CloseCurrentPopup(); }
            ImGui::SameLine();
        }
        if (ImGui::Button("Test")) ctx_.engine.fire(acts, "catalog test");
        if (!pickMode) {
            ImGui::SameLine();
            if (ImGui::Button("New binding")) newBindingFrom(*sel);
        }
    } else {
        ImGui::TextDisabled("Select an entry");
    }
    if (pickMode) {
        if (ImGui::Button("Close")) ImGui::CloseCurrentPopup();
    }
    ImGui::PopID();
}

void Ui::drawCatalogTab() {
    if (ctx_.catalogs.empty()) {
        ImGui::TextColored(kYellow, "No catalog loaded");
        return;
    }
    {
        const Profile* active = ctx_.settings.active();
        const std::string shown = currentCatalog().aircraft;
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Aircraft");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(300);
        if (ImGui::BeginCombo("##catalog_aircraft", currentCatalog().label().c_str())) {
            for (const auto& c : ctx_.catalogs) {
                const bool sel = sim::sameAircraft(c.aircraft, shown);
                if (ImGui::Selectable(c.label().c_str(), sel)) {
                    viewAircraft_ = (active && sim::sameAircraft(c.aircraft, active->aircraft)) ? std::string() : c.aircraft;
                    catalogSelected_ = -1;
                }
            }
            ImGui::EndCombo();
        }
        if (const std::string caveat = editionCaveat(shown, false); !caveat.empty()) ImGui::TextColored(kYellow, "%s", caveat.c_str());
        if (active && shown != active->aircraft) {
            ImGui::SameLine();
            ImGui::TextColored(kYellow, "Not the profile's aircraft (%s)", active->aircraft.c_str());
            ImGui::SameLine();
            if (ImGui::SmallButton("Back")) { viewAircraft_.clear(); catalogSelected_ = -1; }
        }
    }
    {
        // what the sim told us about the aircraft being browsed (any aircraft, not only the loaded one: the lists are cached)
        const Catalog& shownCat = currentCatalog();
        const sim::LiveStatus live = ctx_.bridge.liveStatus();
        const bool isLoaded = sim::sameAircraft(live.aircraftId, shownCat.aircraft);
        size_t nInput = 0, nLVar = 0;
        for (const auto& e : shownCat.entries) {
            if (e.live && e.kind == "input_event") ++nInput;
            if (e.live && e.kind == "lvar") ++nLVar;
        }
        if (shownCat.aircraft == kStandardCatalogId) {
            ImGui::TextDisabled("K: events for every aircraft");
        } else {
            ImGui::TextDisabled("%zu input events, %zu L: variables%s", nInput, nLVar, isLoaded ? "" : " (cached)");
            ImGui::SameLine();
            if (ImGui::SmallButton("Refresh")) ctx_.bridge.requestLiveRefresh();
            if (nInput + nLVar > 0) {
                ImGui::SameLine();
                if (ImGui::SmallButton("Forget")) { ctx_.forgetLive(shownCat.aircraft); catalogSelected_ = -1; }
            }
            if (isLoaded && live.inputEventsDone && nInput == 0)
                ImGui::TextColored(kYellow, "No input events yet. Wait for the flight to load, then Refresh.");
            if (isLoaded && status_.mobiflight == sim::ModuleState::NotFound)
                ImGui::TextColored(kYellow, "L: variables need MobiFlight");
            if (!shownCat.curated && nInput + nLVar == 0 && !isLoaded)
                ImGui::TextColored(kYellow, "Load this aircraft in MSFS to list its controls");
        }
    }
    ImGui::TextDisabled("Double-click an entry to bind it.");
    drawCatalogBrowser("tab", false);
}

}  // namespace lr::app
