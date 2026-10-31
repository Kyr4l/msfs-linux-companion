// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kyr4l
#include "ui/UiLint.hpp"

#include <cstdarg>
#include <cstdio>
#include <set>
#include <string>
#include <unordered_map>

#include <imgui.h>
#include <imgui_internal.h>

// The interface Dear ImGui offers its test engine (built with IMGUI_ENABLE_TEST_ENGINE): it tells every widget it adds. The functions
// below are what this program does with that: count the widgets per identifier, remember their labels.
namespace {

struct Seen {
    int count = 0;
    std::string window;
};
std::unordered_map<ImGuiID, Seen> g_items;           // this frame
std::unordered_map<ImGuiID, std::string> g_labels;   // last known label of an identifier
std::set<std::string> g_reported;

}  // namespace

void ImGuiTestEngineHook_ItemAdd(ImGuiContext* ctx, ImGuiID id, const ImRect&, const ImGuiLastItemData* item_data) {
    if (!item_data || id == 0) return;  // windows are registered without item data
    Seen& s = g_items[id];
    if (++s.count == 1 && ctx->CurrentWindow) s.window = ctx->CurrentWindow->Name;
}

void ImGuiTestEngineHook_ItemInfo(ImGuiContext*, ImGuiID id, const char* label, ImGuiItemStatusFlags) {
    if (label) g_labels[id] = label;
}

void ImGuiTestEngineHook_Log(ImGuiContext*, const char*, ...) {}

const char* ImGuiTestEngine_FindItemDebugLabel(ImGuiContext*, ImGuiID id) {
    auto it = g_labels.find(id);
    return it == g_labels.end() ? nullptr : it->second.c_str();
}

namespace lr::ui {

static void report(const std::string& line) {
    if (g_reported.insert(line).second) fprintf(stderr, "[ui-lint] %s\n", line.c_str());
}

void lintInstall() {
    ImGuiContext& g = *ImGui::GetCurrentContext();
    g.TestEngineHookItems = true;
    g.IO.ConfigErrorRecoveryEnableAssert = false;  // report and repair, do not abort
    g.ErrorCallback = [](ImGuiContext*, void*, const char* msg) { report(std::string("error: ") + msg); };
}

void lintEndFrame() {
    for (const auto& [id, s] : g_items) {
        if (s.count < 2) continue;
        auto it = g_labels.find(id);
        char head[64];
        snprintf(head, sizeof head, "%d widgets share the id 0x%08X", s.count, id);
        report(std::string(head) + " in window \"" + s.window + "\", label \"" + (it == g_labels.end() ? "?" : it->second) + "\"");
    }
    g_items.clear();
}

}  // namespace lr::ui
