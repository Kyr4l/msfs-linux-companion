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

// ---------------------------------------------------------------- Guides


std::optional<double> Ui::readProbe(const GuideProbe& p) const {
    if (!p.inputEvent.empty()) return ctx_.bridge.inputEventValue(p.inputEvent);
    return ctx_.bridge.lvarValue(p.code);
}

// One panel section with the controls of a step lit up; the others are dimmed so the eye finds the right one.
void Ui::drawGuideCard(const Catalog& cat, const std::vector<int>& targets) {
    if (targets.empty()) return;
    if (panelModel_.cat != &cat || panelModel_.entryCount != cat.entries.size() || panelModel_.revision != cat.revision) rebuildPanelModel(cat);
    auto holds = [](const PanelGroup& g, int idx) { return std::find(g.entries.begin(), g.entries.end(), idx) != g.entries.end(); };
    const PanelCard* card = nullptr;
    for (const auto& c : panelModel_.cards)
        for (const auto& g : c.groups)
            if (holds(g, targets[0])) card = &c;
    if (!card) return;
    auto isTarget = [&](const PanelGroup& g) {
        for (int t : targets)
            if (holds(g, t)) return true;
        return false;
    };
    // a long section (the CDU keyboard) shows the neighbourhood of the first target only
    size_t first = 0, last = card->groups.size();
    constexpr size_t kMaxTiles = 12;
    if (card->groups.size() > kMaxTiles) {
        size_t at = 0;
        for (size_t i = 0; i < card->groups.size(); ++i)
            if (holds(card->groups[i], targets[0])) at = i;
        first = at > kMaxTiles / 2 ? at - kMaxTiles / 2 : 0;
        first -= first % 2;
        last = std::min(card->groups.size(), first + kMaxTiles);
    }
    const float fs = ImGui::GetFontSize();
    const float pad = fs * 0.45f, cardW = fs * 19.0f, cellW = (cardW - 3 * pad) / 2, cellH = fs * 3.4f, headH = fs * 1.9f;
    const size_t shown = last - first;
    const float height = headH + pad + static_cast<float>((shown + 1) / 2) * (cellH + pad);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    const ImVec2 p1(p0.x + cardW, p0.y + height);
    dl->AddRectFilled(p0, p1, ImGui::GetColorU32(ImVec4(0.13f, 0.14f, 0.17f, 1.0f)), fs * 0.5f);
    dl->AddRect(p0, p1, ImGui::GetColorU32(ImVec4(0.30f, 0.32f, 0.38f, 1.0f)), fs * 0.5f);
    const std::string title = card->region + (card->section.empty() ? "" : "  >  " + card->section);
    const bool hasPhotos = !card->reference.empty();
    const float photoW = hasPhotos ? ImGui::CalcTextSize("photos >").x + pad : 0.0f;
    dl->PushClipRect(p0, ImVec2(p1.x - photoW, p0.y + headH), true);
    dl->AddText(ImVec2(p0.x + pad, p0.y + (headH - fs) * 0.5f), ImGui::GetColorU32(ImGuiCol_Text), title.c_str());
    dl->PopClipRect();
    if (hasPhotos) {  // the FlyByWire docs have real photos of the panel
        ImGui::SetCursorScreenPos(ImVec2(p1.x - photoW - pad * 0.5f, p0.y + 2));
        ImGui::PushID(card);
        const bool clicked = ImGui::InvisibleButton("photos", ImVec2(photoW + pad * 0.5f, headH - 4));
        const bool hov = ImGui::IsItemHovered();
        dl->AddText(ImVec2(p1.x - photoW, p0.y + (headH - fs) * 0.5f), hov ? ImGui::GetColorU32(ImVec4(0.45f, 0.75f, 1.0f, 1.0f)) : ImGui::GetColorU32(ImGuiCol_TextDisabled), "photos >");
        if (hov) ImGui::SetTooltip("Open the real photos of this panel (FlyByWire docs) in your browser:\n%s", card->reference.c_str());
        if (clicked && !openDocsUrl(card->reference)) LOG_WARN("Could not open %s (is xdg-open installed?)", card->reference.c_str());
        ImGui::PopID();
        ImGui::SetCursorScreenPos(p0);
    }
    const ImU32 gold = ImGui::GetColorU32(ImVec4(1.0f, 0.78f, 0.15f, 1.0f));
    for (size_t k = 0; k < shown; ++k) {
        const PanelGroup& g = card->groups[first + k];
        const bool hot = isTarget(g);
        const ImVec2 c0(p0.x + pad + static_cast<float>(k % 2) * (cellW + pad), p0.y + headH + static_cast<float>(k / 2) * (cellH + pad));
        const ImVec2 c1(c0.x + cellW, c0.y + cellH);
        dl->AddRectFilled(c0, c1, hot ? ImGui::GetColorU32(ImVec4(0.42f, 0.32f, 0.08f, 1.0f)) : ImGui::GetColorU32(ImVec4(0.17f, 0.18f, 0.22f, 1.0f)), fs * 0.35f);
        if (hot) dl->AddRect(c0, c1, gold, fs * 0.35f, 0, 3.0f);
        drawControlIcon(dl, ImVec2(c0.x + cellH * 0.5f, c0.y + cellH * 0.5f), cellH * 0.28f, g.style);
        dl->PushClipRect(ImVec2(c0.x + cellH, c0.y), c1, true);
        dl->AddText(ImGui::GetFont(), fs, ImVec2(c0.x + cellH, c0.y + fs * 0.35f), hot ? ImGui::GetColorU32(ImGuiCol_Text) : ImGui::GetColorU32(ImGuiCol_TextDisabled),
                    g.function.c_str(), nullptr, cellW - cellH - pad * 0.5f);
        dl->PopClipRect();
        if (ImGui::IsMouseHoveringRect(c0, c1)) {
            const CatalogEntry& e = cat.entries[static_cast<size_t>(g.entries[0])];
            ImGui::SetTooltip("%s\n%s %s", g.function.c_str(), e.type.c_str(), e.api.c_str());
        }
    }
    if (first > 0) dl->AddText(ImVec2(p1.x - fs * 3.2f, p0.y + (headH - fs) * 0.5f), ImGui::GetColorU32(ImGuiCol_TextDisabled), "... more");
    ImGui::Dummy(ImVec2(cardW, height));
}

void Ui::drawGuidesTab() {
    if (ctx_.guides.empty()) {
        ImGui::TextDisabled("No guide found (expected data/<aircraft>_guide.json).");
        return;
    }
    const Guide* g = ctx_.guideFor(guideAircraft_);
    if (!g) {
        g = ctx_.guideFor(ctx_.settings.ensureActive().aircraft);  // follow the active profile until the user picks one
        if (!g) g = &ctx_.guides.front();
        guideAircraft_ = g->aircraft;
    }
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Aircraft");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 22);
    if (ImGui::BeginCombo("##guideAircraft", g->title.c_str())) {
        for (const auto& o : ctx_.guides)
            if (ImGui::Selectable(o.title.c_str(), sim::sameAircraft(o.aircraft, g->aircraft)) && !sim::sameAircraft(o.aircraft, g->aircraft)) {
                guideAircraft_ = o.aircraft;
                guideRun_ = GuideRun{};
                guideTicked_.clear();
                g = &o;
            }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    if (ImGui::Checkbox("Autodetect answers", &ctx_.settings.guideAutodetect)) ctx_.markDirty();
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("When the simulator tells the answer to a question (on the ground? engines running?), the guide takes it\nand moves on. The next page lists what was detected; Back returns to the question so you can change it.");
    ImGui::SameLine();
    ImGui::TextDisabled("Ticks come from the simulator when it reports the state; you can always tick by hand.");
    ImGui::Separator();

    const Catalog* cat = ctx_.catalogFor(g->aircraft);
    if (cat && !sim::sameAircraft(cat->aircraft, g->aircraft)) cat = nullptr;  // never show another aircraft's panels

    ImGui::BeginChild("guide_body", ImVec2(0, 0));
    if (!guideRun_.active() || guideRun_.guide() != g) {
        ImGui::SetWindowFontScale(1.25f);
        ImGui::TextUnformatted(g->title.c_str());
        ImGui::SetWindowFontScale(1.0f);
        if (!g->notice.empty()) {
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + std::min(ImGui::GetContentRegionAvail().x, ImGui::GetFontSize() * 60));
            ImGui::TextColored(kYellow, "%s", g->notice.c_str());
            ImGui::PopTextWrapPos();
            ImGui::Spacing();
        }
        ImGui::TextDisabled("What do you want to do?");
        ImGui::Spacing();
        const float w = std::min(ImGui::GetContentRegionAvail().x, ImGui::GetFontSize() * 34);
        for (const auto& t : g->topics) {
            ImGui::PushID(t.id.c_str());
            if (ImGui::Button(t.title.c_str(), ImVec2(w, ImGui::GetFrameHeight() * 1.6f))) {
                guideRun_.start(*g, t.id);
                guideTicked_.clear();
                guideDetected_.clear();
            }
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + w);
            ImGui::TextDisabled("%s", t.summary.c_str());
            ImGui::PopTextWrapPos();
            ImGui::Spacing();
            ImGui::PopID();
        }
        ImGui::Spacing();
        ImGui::SeparatorText("Sources");
        for (const auto& s : g->sources) {
            ImGui::PushTextWrapPos(0);
            ImGui::TextDisabled("- %s", s.c_str());
            ImGui::PopTextWrapPos();
        }
        if (ctx_.guideProbes.size()) { ctx_.guideProbes.clear(); ctx_.refreshWatches(); guideWatchSig_.clear(); }
    } else {
        drawGuideNode(*g, cat);
    }
    ImGui::EndChild();
}

void Ui::drawGuideNode(const Guide& g, const Catalog* cat) {
    const GuideNode* node = guideRun_.current();
    if (!node) { guideRun_ = GuideRun{}; return; }
    auto read = [&](const GuideProbe& p) { return readProbe(p); };

    // keep the simulator watching what this page needs
    {
        std::string sig = node->id;
        std::vector<GuideProbe> probes = g.probesOf(*node);
        if (guideWatchSig_ != sig) {
            guideWatchSig_ = sig;
            guideNodeSince_ = ImGui::GetTime();
            ctx_.guideProbes = std::move(probes);
            ctx_.refreshWatches();
        }
    }

    const GuideTopic* topic = g.topic(guideRun_.topic());
    if (ImGui::Button("< Back") && guideRun_.canGoBack()) guideRun_.back();
    ImGui::SameLine();
    if (ImGui::Button("Restart")) { guideRun_.restart(); guideTicked_.clear(); guideDetected_.clear(); }
    ImGui::SameLine();
    if (ImGui::Button("All topics")) { guideRun_ = GuideRun{}; return; }
    ImGui::SameLine();
    ImGui::TextDisabled("%s  -  page %zu", topic ? topic->title.c_str() : "", guideRun_.depth());
    ImGui::Separator();
    ImGui::Spacing();
    const float wrap = std::min(ImGui::GetContentRegionAvail().x, ImGui::GetFontSize() * 60);

    {   // answers the simulator gave: keep those on the path that led here (Back drops the ones after it)
        const auto& hist = guideRun_.history();
        guideDetected_.erase(std::remove_if(guideDetected_.begin(), guideDetected_.end(),
                                            [&](const auto& e) { return e.first == node->id || std::find(hist.begin(), hist.end(), e.first) == hist.end(); }),
                             guideDetected_.end());
        if (!guideDetected_.empty()) {
            ImGui::TextDisabled("Detected from the simulator (Back to change):");
            for (const auto& e : guideDetected_) ImGui::TextColored(kGreen, "  %s", e.second.c_str());
            ImGui::Spacing();
        }
    }

    if (guideRun_.finished()) {
        ImGui::SetWindowFontScale(1.25f);
        ImGui::TextColored(kGreen, "Done");
        ImGui::SetWindowFontScale(1.0f);
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + wrap);
        if (!node->text.empty()) ImGui::TextUnformatted(node->text.c_str());
        else ImGui::TextUnformatted("You reached the end of this guide.");
        ImGui::PopTextWrapPos();
        ImGui::Spacing();
        if (ImGui::Button("Back to the topics")) guideRun_ = GuideRun{};
        return;
    }

    ImGui::SetWindowFontScale(1.25f);
    ImGui::TextUnformatted(node->title.c_str());
    ImGui::SetWindowFontScale(1.0f);
    ImGui::Spacing();

    if (node->type == "question") {
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + wrap);
        ImGui::TextUnformatted(node->text.c_str());
        if (!node->help.empty()) ImGui::TextDisabled("%s", node->help.c_str());
        ImGui::PopTextWrapPos();
        ImGui::Spacing();
        const auto suggested = node->autoChoice(read);
        if (ctx_.settings.guideAutodetect && suggested && guideRun_.autoAllowed()) {
            for (const auto& c : node->choices)
                if (c.id == *suggested) {
                    guideDetected_.emplace_back(node->id, node->text + "  ->  " + c.label);
                    guideRun_.choose(c.id);
                    return;
                }
        }
        if (node->autoProbe.valid() && !suggested) {
            const bool asking = ctx_.settings.guideAutodetect && guideRun_.autoAllowed() && status_.state == sim::ConnState::Connected &&
                                ImGui::GetTime() - guideNodeSince_ < 3.0;
            if (asking) ImGui::TextDisabled("Asking the simulator...");
            else ImGui::TextDisabled("The simulator did not report this (not connected, or the MobiFlight module is missing): answer by hand.");
        }
        const float bw = std::min(ImGui::GetContentRegionAvail().x, ImGui::GetFontSize() * 30);
        for (const auto& c : node->choices) {
            ImGui::PushID(c.id.c_str());
            const bool hint = suggested && *suggested == c.id;
            if (hint) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.45f, 0.25f, 1.0f));
            const bool clicked = ImGui::Button(c.label.c_str(), ImVec2(bw, ImGui::GetFrameHeight() * 1.5f));
            if (hint) ImGui::PopStyleColor();
            if (hint) { ImGui::SameLine(); ImGui::TextColored(kGreen, "the simulator says this"); }
            if (!c.hint.empty()) ImGui::TextDisabled("%s", c.hint.c_str());
            if (clicked) { guideRun_.choose(c.id); ImGui::PopID(); return; }
            ImGui::PopID();
            ImGui::Spacing();
        }
        return;
    }

    if (node->type == "steps") {
        if (!node->source.empty()) { ImGui::TextDisabled("Source: %s", node->source.c_str()); ImGui::Spacing(); }
        // which step is next: the first one that is neither confirmed by the sim nor ticked by hand
        size_t current = node->steps.size();
        std::vector<bool> done(node->steps.size(), false);
        for (size_t i = 0; i < node->steps.size(); ++i) {
            const auto ok = node->steps[i].check.evaluate(read);
            done[i] = (ok && *ok) || guideTicked_.count(node->id + ":" + std::to_string(i));
            if (!done[i] && current == node->steps.size()) current = i;
        }
        for (size_t i = 0; i < node->steps.size(); ++i) {
            const GuideStep& s = node->steps[i];
            ImGui::PushID(static_cast<int>(i));
            const auto sim = s.check.evaluate(read);
            const bool byHand = guideTicked_.count(node->id + ":" + std::to_string(i)) > 0;
            bool ticked = done[i];
            ImGui::BeginDisabled(sim && *sim);  // the simulator's confirmation cannot be unticked
            if (ImGui::Checkbox("##tick", &ticked)) {
                const std::string key = node->id + ":" + std::to_string(i);
                if (ticked) guideTicked_.insert(key); else guideTicked_.erase(key);
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + wrap);
            if (i == current) ImGui::TextColored(ImVec4(1.0f, 0.82f, 0.30f, 1.0f), "%zu. %s", i + 1, s.text.c_str());
            else if (done[i]) ImGui::TextDisabled("%zu. %s", i + 1, s.text.c_str());
            else ImGui::Text("%zu. %s", i + 1, s.text.c_str());
            ImGui::PopTextWrapPos();
            if (sim && *sim) { ImGui::SameLine(); ImGui::TextColored(kGreen, "confirmed by the simulator"); }
            else if (byHand) { ImGui::SameLine(); ImGui::TextDisabled("(ticked by hand)"); }
            if (i == current || (!done[i] && node->steps.size() <= 3)) {
                ImGui::Indent(ImGui::GetFontSize() * 1.6f);
                ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + wrap - ImGui::GetFontSize() * 1.6f);
                if (!s.where.empty()) ImGui::TextDisabled("Where: %s", s.where.c_str());
                if (!s.why.empty()) ImGui::TextDisabled("%s", s.why.c_str());
                if (!s.warn.empty()) ImGui::TextColored(kYellow, "Caution: %s", s.warn.c_str());
                ImGui::PopTextWrapPos();
                for (const auto& r : s.readouts) {
                    const auto v = readProbe(r.probe);
                    if (v) ImGui::Text("%s: %.4g %s", r.label.c_str(), *v, r.unit.c_str());
                    else ImGui::TextDisabled("%s: -", r.label.c_str());
                }
                if (!s.source.empty()) ImGui::TextDisabled("Source: %s", s.source.c_str());
                if (cat && !s.controls.empty()) {
                    // the sections the step's controls live in, side by side, with the controls lit
                    std::vector<std::vector<int>> groups;
                    for (const auto& ref : s.controls) {
                        int idx = -1;
                        for (int k = 0; k < static_cast<int>(cat->entries.size()) && idx < 0; ++k) {
                            const CatalogEntry& e = cat->entries[static_cast<size_t>(k)];
                            if (e.api == ref.api && (ref.function.empty() || e.function == ref.function)) idx = k;
                        }
                        if (idx < 0) continue;
                        bool placed = false;
                        for (auto& grp : groups) {
                            const CatalogEntry& a = cat->entries[static_cast<size_t>(grp[0])];
                            const CatalogEntry& b = cat->entries[static_cast<size_t>(idx)];
                            if (a.panel == b.panel && a.section == b.section) { grp.push_back(idx); placed = true; break; }
                        }
                        if (!placed) groups.push_back({idx});
                    }
                    ImGui::Spacing();
                    for (size_t k = 0; k < groups.size() && k < 4; ++k) {
                        if (k) ImGui::SameLine();
                        ImGui::BeginGroup();
                        drawGuideCard(*cat, groups[k]);
                        ImGui::EndGroup();
                    }
                }
                ImGui::Unindent(ImGui::GetFontSize() * 1.6f);
            }
            ImGui::PopID();
            ImGui::Spacing();
        }
        ImGui::Separator();
        const bool last = node->next.empty();
        if (ImGui::Button(last ? "Finish" : "Next >", ImVec2(ImGui::GetFontSize() * 9, ImGui::GetFrameHeight() * 1.4f))) guideRun_.advance();
        if (current < node->steps.size()) { ImGui::SameLine(); ImGui::TextDisabled("%zu step(s) not confirmed yet", node->steps.size() - current); }
        return;
    }

    // info and end
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + wrap);
    ImGui::TextUnformatted(node->text.c_str());
    ImGui::PopTextWrapPos();
    if (!node->source.empty()) ImGui::TextDisabled("Source: %s", node->source.c_str());
    ImGui::Spacing();
    if (ImGui::Button(node->type == "end" || node->next.empty() ? "Finish" : "Next >", ImVec2(ImGui::GetFontSize() * 9, ImGui::GetFrameHeight() * 1.4f))) guideRun_.advance();
}

}  // namespace lr::app
