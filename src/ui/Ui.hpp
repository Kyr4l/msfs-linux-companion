// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kyr4l
#pragma once
#include <deque>
#include <optional>
#include <set>
// Dear ImGui front-end. Everything here runs on the main (GLFW) thread.
#include <memory>
#include <string>
#include <vector>

#include "app/Catalog.hpp"
#include "app/CockpitWatch.hpp"
#include "app/Guide.hpp"
#include "app/Keypad.hpp"
#include "app/Engine.hpp"
#include "app/Profile.hpp"
#include "haptics/Haptics.hpp"
#include "haptics/WinwingHid.hpp"
#include "input/Evdev.hpp"
#include "sim/SimBridge.hpp"
#include "util/Log.hpp"

namespace lr::app {

struct AppContext {
    Settings settings;
    std::string configPath;
    CatalogSet catalogs;  // one per aircraft: data/*_catalog.json, what the sim reported, and the standard events
    std::deque<Guide> guides;               // data/*_guide.json
    KeypadSet keypads;                      // data/*_keypads.json: the keyboards the guides point at
    std::vector<GuideProbe> guideProbes;    // what the open guide page wants read from the sim
    const Guide* guideFor(const std::string& aircraft) const;
    void refreshWatches();                  // the profile's L: vars / input events plus the open guide page's probes
    sim::SimBridge& bridge;
    Engine& engine;
    input::InputManager& input;
    haptics::HapticsEngine& haptics;
    haptics::WinwingHidManager& winwing;
    bool dirty = false;
    std::string startTab;  // --tab NAME: tab selected on the first frame
    bool quitRequested = false;
    std::string lastSaveError;

    AppContext(sim::SimBridge& b, Engine& e, input::InputManager& i, haptics::HapticsEngine& h, haptics::WinwingHidManager& w)
        : bridge(b), engine(e), input(i), haptics(h), winwing(w) {}
    // Range the first sim event of an axis binding expects (catalog first, then the name), if it is an axis event.
    bool suggestedAxisRange(const Binding& b, double& lo, double& hi) const;
    int fixAxisRanges(Profile& p);              // applies the suggestion to every wrong binding, returns how many
    void warnAxisRanges() const;                // log one line per wrong binding (startup)
    const Catalog* catalogFor(const std::string& aircraft) const;  // exact match, else the standard events, else the first
    void loadLiveCaches();                                          // ~/.config/msfs-linux-companion/live/*.json from earlier sessions
    void syncLive();                                                // folds the sim's current lists into the catalog, saves the cache
    void forgetLive(const std::string& aircraft);
    uint64_t liveSeen = ~0ull;
    std::string pushedFingerprint_;  // what the engine was last given (see engineFingerprint)
    void pushProfileToEngine();
    void pushHapticsConfig();
    void markDirty();
    bool save();
};

class Ui {
public:
    explicit Ui(AppContext& ctx);
    void draw();

private:
    void drawMenuBar();
    void drawStatusBar();
    void drawBindingsTab();
    void drawDevicesTab();
    void drawCatalogTab();
    void drawCockpitTab();     // which input event is the control I just clicked in the cockpit?
    void stopCockpitWatch();
    void drawGuidesTab();
    void drawGuideNode(const Guide& g, const Catalog* cat);
    void drawGuideCard(const Catalog& cat, const std::vector<int>& targets);
    void drawKeypad(const Keypad& pad, const std::vector<std::string>& sequence);
    std::optional<double> readProbe(const GuideProbe& p) const;
    void drawLogTab();
    void drawSettingsTab();
    void drawVibrationTab();
    void drawSplash();
    void drawBindingEditor();
    void drawActionList(const char* id, std::vector<Action>& actions, int listIndex);
    void drawCatalogBrowser(const char* id, bool pickMode);
    void openEditor(int bindingIndex);
    void openEditorWith(std::vector<Action> press, const std::string& label);  // a new binding that does this; its button or axis is chosen next
    void openEditorCopy(int bindingIndex);  // a new binding with everything the given one does; its button or axis is chosen next
    bool bindingMatches(const Binding& b, const std::vector<std::string>& words) const;  // the filter box of the Bindings tab
    std::string deviceDisplayName(const std::string& key) const;
    bool inputIsPressed(const input::InputId& id) const;
    float axisNow(const input::InputId& id) const;  // live position 0..1 of an axis input, or -1 when the device is absent
    std::string editionCaveat(const std::string& aircraftId, bool guide) const;  // shown when connected to MSFS 2024 and the data was made on MSFS 2020
    void drawAxisOptions();
    void drawAxisTargetPicker();  // quick setup: one of the flight controls every aircraft has, with its range
    void drawDetentOptions();  // lever with gates: calibration state, values at the gates, presets
    void drawGateBar(const AxisCalibration& cal, float now, float width, const bool* gateSet = nullptr);  // the zones of a calibrated lever with its live position
    Engine::GateState gateStateNow(const AxisCalibration& cal) const;  // which gate buttons of a calibrated lever are closed right now
    void openCalibration(const input::InputId& axis);
    void drawCalibrationWizard();
    std::vector<Action>& catalogTargetList();  // the draft list the catalog picker adds to

    AppContext& ctx_;
    std::vector<input::DeviceSnapshot> devices_;
    sim::SimStatus status_;

    // binding editor state
    bool editorOpen_ = false;
    int editIndex_ = -1;      // the binding being edited, or -1 for a new one
    int insertAt_ = -1;       // where a new binding goes in the list (a copy goes right after its original); -1 = the end
    bool draftIsCopy_ = false;
    Binding draft_;
    std::string bindingFilter_;  // Bindings tab: only the rows with all of these words

    // cockpit tab
    CockpitWatch cockpit_;
    uint64_t cockpitSerial_ = 0;       // the last change of the bridge that was taken over
    bool cockpitShowNoisy_ = false;
    bool cockpitDrawn_ = false;        // the tab was drawn in this frame: watching ends when it is not
    // 0 = press, 1 = release, 2 / 3 = enters / leaves the cut-off zone, 4 / 5 = enters / leaves the afterburner zone
    int catalogTarget_ = 0;
    bool catalogPopupOpen_[6] = {false, false, false, false, false, false};  // one per list
    static const char* catalogTargetName(int target);

    // axis calibration wizard (gates of a lever)
    bool calOpen_ = false, calJustOpened_ = false, calWasEnabled_ = true;
    input::InputId calAxis_;
    AxisCalibration calDraft_;
    bool calSet_[4] = {false, false, false, false};  // which gates the user has pointed out in this run of the wizard
    int calListening_ = -1;                          // the gate whose button is being listened for (0 cut-off .. 3 afterburner), or -1
    int calSig_ = 0;                                 // the gate buttons the engine is learning positions for
    std::string calMessage_;

    // catalog panel view: the catalog regrouped into panels (cards) of controls, rebuilt when the catalog changes
    enum class CatalogView { List, Panels };
    struct PanelGroup {
        std::string function;
        std::vector<int> entries;  // indices into Catalog::entries
        int preferred = -1;        // entry used when the control is clicked
        int style = 0;             // 0 button, 1 knob, 2 selector
    };
    struct PanelCard {
        std::string region, section, reference;
        std::vector<PanelGroup> groups;
    };
    struct PanelModel {
        const Catalog* cat = nullptr;
        size_t entryCount = 0;
        unsigned revision = 0;
        std::vector<std::string> regions;  // in order of first appearance
        std::vector<PanelCard> cards;
    };
    CatalogView catalogView_ = CatalogView::Panels;
    PanelModel panelModel_;
    void rebuildPanelModel(const Catalog& cat);
    void drawPanelsView(const Catalog& cat, bool pickMode, const std::vector<std::string>& words, float reserveBelow);

    // catalog state
    std::string viewAircraft_;  // catalog tab: aircraft being browsed; empty = follow the active profile
    const Catalog& currentCatalog() const;
    const Catalog& pickerCatalog() const;  // the active profile's aircraft
    std::string detectAircraft(const std::string& cfgPath) const;  // "A32NX"/"A380X" from the loaded .cfg path, or empty
    std::string uniqueProfileName(const std::string& wanted) const;
    std::string newProfileAircraft_ = kDefaultAircraft;
    std::string catalogFilter_;
    int catalogSelected_ = -1;
    bool catalogBothSides_ = false;  // add the first-officer twin of captain-side EFIS entries too
    std::vector<Action> catalogActions(const CatalogEntry& e) const;
    void newBindingFrom(const CatalogEntry& e);

    // profile management
    std::string newProfileName_;
    bool renamingProfile_ = false;

    bool splashDone_ = false;
    std::string startTab_;

    // guides
    GuideRun guideRun_;
    std::string guideAircraft_;
    std::set<std::string> guideTicked_;  // steps the user ticked by hand: "node:index"
    std::string guideWatchSig_;
    std::vector<std::pair<std::string, std::string>> guideDetected_;  // questions answered from the simulator: node id, "question  ->  answer"
    double guideNodeSince_ = 0;                                       // when the open page appeared (ImGui time)

    // log
    int logMinLevel_ = 1;
    bool logAutoScroll_ = true;
    uint64_t logSerialSeen_ = 0;
    std::vector<LogEntry> logEntries_;  // the log as it was when it last changed: copying it is not done every frame
    std::vector<size_t> logShown_;      // the entries at or above logMinLevel_
    uint64_t logCacheSerial_ = ~0ull;
    int logCacheLevel_ = -1;
};

}  // namespace lr::app
