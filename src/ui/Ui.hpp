// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kyr4l
#pragma once
#include <deque>
#include <map>
#include <optional>
#include <set>
// Dear ImGui front-end. Everything here runs on the main (GLFW) thread.
#include <memory>
#include <string>
#include <vector>

#include "app/Catalog.hpp"
#include "app/CockpitWatch.hpp"
#include "app/Guide.hpp"
#include "app/LeverZones.hpp"
#include "app/MsfsControls.hpp"
#include "app/Keypad.hpp"
#include "app/Engine.hpp"
#include "app/Profile.hpp"
#include "app/SimPackages.hpp"
#include "haptics/Haptics.hpp"
#include "haptics/WinwingHid.hpp"
#include "input/Evdev.hpp"
#include "sim/SimBridge.hpp"
#include "util/Log.hpp"

namespace lr::plugin { class PluginManager; }

namespace lr::app {

struct AppContext {
    Settings settings;
    std::string configPath;
    CatalogSet catalogs;  // one per aircraft: data/*_catalog.json, what the sim reported, and the standard events
    std::deque<Guide> guides;               // data/*_guide.json
    std::vector<LeverRecipe> leverRecipes;  // data/*levers.json, then the user's own <config>/my_levers.json (userMade)
    std::string userLeverRecipesPath() const;
    KeypadSet keypads;                      // data/*_keypads.json: the keyboards the guides point at
    std::vector<GuideProbe> guideProbes;    // what the open guide page wants read from the sim
    const Guide* guideFor(const std::string& aircraft) const;
    void refreshWatches();                  // the profile's L: vars / input events plus the open guide page's probes
    sim::SimBridge& bridge;
    Engine& engine;
    input::InputManager& input;
    haptics::HapticsEngine& haptics;
    haptics::WinwingHidManager& winwing;
    plugin::PluginManager* plugins = nullptr;  // what the plugins added (tabs, menus, commands ...); set by main()
    bool dirty = false;
    std::string startTab;  // --tab NAME: tab selected on the first frame
    int startEditBinding = -1;  // --edit-binding N: the editor opens on binding N of the active profile
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
    // When the loaded aircraft changes and the active profile is not made for it: the first profile with "switch automatically" that is.
    void autoSwitchProfile();
    std::string autoSwitchSeen;  // the aircraft the last check was for
    std::string autoSwitchedTo;  // the profile the last automatic switch chose (Home says "switched automatically")
    std::vector<MsfsControlProfile> msfsProfiles;  // the sim's own control profiles (app/MsfsControls.hpp), read when first needed
    bool msfsProfilesRead = false;
    void forgetLive(const std::string& aircraft);
    // An aircraft without a hand-made catalog: labels its controls from their names and its package's tooltips, and makes a guide from
    // its package's checklist when there is one (app/AutoCatalog.hpp)
    void autoLabel(const std::string& aircraft);
    std::optional<std::vector<std::string>> packageRoots;                    // found on first use (app/SimPackages.hpp)
    std::map<std::string, std::shared_ptr<const PackageText>> packageTexts;  // by aircraft key; null when the package was not found
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
    // the page shown: a built-in one by name ("Bindings"), or a plugin's tab (pagePlugin_ = its plugin id)
    std::string page_ = "Home", pagePlugin_;
    void drawHomeTab();      // status, "bind a control", what needs a look
    void drawRail();         // the pages, as icons down the left
    void drawTopBar();       // the brand, the state of everything, the profile, the bindings switch
    bool bindingChecked(const Binding& b) const;            // every action matches a catalog entry tried on the aircraft
    std::vector<DoubleBinding> doubleBindings();             // buttons the sim's own control profile also binds
    void drawBindingsTab();
    void drawProfilesTab();  // every profile in a table: its aircraft, the automatic switch, its theme
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
    void drawPluginsSection();
    bool pluginsChanged_ = false;  // a plugin was switched on or off: takes effect at the next start
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
    void drawDetentOptions();  // lever with gates: calibration state, range, stages, presets
    void drawLeverRecipes(const AxisCalibration* cal);  // recipe cards, "changed since the recipe", save as recipe
    void drawCutoffZone();     // what the cut-off zone does: nothing, reverse thrust, custom actions
    // The output of a lever over its travel, with the zones, the stages (dragged to move them) and the live position; true when a stage moved.
    bool drawLeverGraph(AxisDetents& d, const AxisCalibration& cal, float now, float width, float height);
    int leverDrag_ = -1;  // the stage being dragged in the lever graph
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
    bool homeListening_ = false;  // Home's "bind a control" waits for a press
    int pendingSelect_ = -2;      // a row clicked while the open binding had unsaved changes (-2: none)
    bool draftChanged() const;    // the open binding differs from what is saved
    void saveDraft();             // writes the open binding into the profile and keeps it open
    void selectBinding(int index);  // opens it in the inspector, asking first when the open one has changes
    std::string bindingsFilter_;  // the Bindings list shows only: "" all, "twice", "untried", "reverse" (set from Home)
    int editIndex_ = -1;      // the binding being edited, or -1 for a new one
    int insertAt_ = -1;       // where a new binding goes in the list (a copy goes right after its original); -1 = the end
    bool draftIsCopy_ = false;
    Binding draft_;
    bool cutoffCustom_ = false;        // the cut-off zone was set to "Custom actions" (its lists may still be empty)
    int recipeEngine_ = 0;             // the engine the recipes fill in ({n}); 0 = from the binding
    std::string newRecipeTitle_;
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
    std::string profRenameOf_, profRenameText_;  // Profiles page: the row whose name is being edited
    bool profRenameFocus_ = false;
    std::string profAircraftSearch_;
    std::string profDelete_;  // the profile the delete question is about

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
