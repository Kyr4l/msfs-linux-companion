// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kyr4l
#pragma once
// Owns the SimConnect connection on its own thread: reconnects, maps event names to IDs,
// talks to the MobiFlight WASM module (for H: events, L: vars and calculator code) and
// executes queued actions coming from the input engine or the UI.
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <optional>
#include <unordered_map>
#include <vector>

#include "sim/AircraftId.hpp"
#include "sim/SimConnectClient.hpp"

namespace lr::sim {

enum class ConnState { Disconnected, Connecting, Opening, Connected };
enum class ModuleState { Unknown, Probing, Detected, NotFound };

struct SimStatus {
    ConnState state = ConnState::Disconnected;
    ModuleState mobiflight = ModuleState::Unknown;
    std::string host;
    uint16_t port = 0;
    uint16_t detectedPort = 0;    // port found by scanning when the configured one did not answer
    std::string detectedHost;
    std::string simName;         // from RECV_OPEN
    std::string simVersion;      // "11.0.282174.999"
    std::string aircraft;        // path of the loaded aircraft .cfg (AircraftLoaded)
    bool simRunning = false;     // "Sim" system event
    bool paused = false;
    std::string lastError;
    uint64_t commandsSent = 0;
    uint64_t exceptions = 0;
    std::chrono::steady_clock::time_point lastActivity{};
};

// Flight telemetry used by the vibration engine (all requested as FLOAT64, tagged).
struct Telemetry {
    bool valid = false;
    std::chrono::steady_clock::time_point time{};
    uint64_t seq = 0;
    double onGround = 0, groundSpeedKt = 0, gForce = 1, stallWarning = 0, overspeedWarning = 0, gearPct = 1, flapsPct = 0,
           spoilersHandle = 0, n1Eng1 = 0, n1Eng2 = 0, reverse1 = 0, reverse2 = 0, accelBodyY = 0, verticalSpeedFpm = 0,
           brakeLeft = 0, brakeRight = 0, iasKt = 0, altAgl = 0;
};

// What the sim reports about the cockpit of the loaded aircraft (see SimBridge::liveSnapshot()).
struct LiveInputEvent {
    std::string name;
    bool isString = false;
};
struct LiveSnapshot {
    std::string aircraftId, aircraftLabel;  // from AircraftLoaded (empty until the sim told us)
    std::vector<LiveInputEvent> inputEvents;
    std::vector<std::string> lvars;
    bool inputEventsDone = false;           // enumeration answered (even if empty)
    bool lvarsDone = false;
    uint64_t revision = 0;                  // bumped on every change; compare to know when to rebuild a catalog
};

struct SimCommand {
    enum class Type { SimEvent, HEvent, LVarSet, LVarToggle, CalcCode, MobiFlightPing, MobiFlightRaw, InputEventSet, InputEventToggle };
    Type type = Type::SimEvent;
    std::string name;    // event name / L var / H event / calculator code / raw MF command
    double value = 0;    // event parameter, L var / input event value, or the number of toggle positions
    std::string origin;  // free text for the log ("binding: AP1", "test")
};

class SimBridge {
public:
    SimBridge();
    ~SimBridge();

    void setEndpoint(const std::string& host, uint16_t port);
    void setAutoConnect(bool on);
    void connectNow();
    void disconnect();

    void start();
    void stop();

    void enqueue(SimCommand cmd);
    void sendEvent(const std::string& name, double value, const std::string& origin = {});
    void sendHEvent(const std::string& name, const std::string& origin = {});
    void setLVar(const std::string& name, double value, const std::string& origin = {});
    void toggleLVar(const std::string& name, const std::string& origin = {});
    // L: vars to read continuously through the MobiFlight module (replaces the previous list).
    void watchLVars(std::vector<std::string> names);
    std::optional<double> lvarValue(const std::string& name) const;
    std::vector<std::pair<std::string, double>> lvarValues() const;
    void execCalculatorCode(const std::string& code, const std::string& origin = {});
    // B: input events: cockpit switches exposed by the loaded aircraft (SimConnect input events, no add-on needed)
    void setInputEvent(const std::string& name, double value, const std::string& origin = {});
    void toggleInputEvent(const std::string& name, int positions, const std::string& origin = {});
    void watchInputEvents(std::vector<std::string> names);  // polled a few times a second for display
    std::optional<double> inputEventValue(const std::string& name) const;
    void requestInputParams(const std::vector<std::string>& names);  // asks the sim how it describes each event's parameters
    std::optional<std::string> inputParams(const std::string& name) const;
    void requestLiveRefresh();      // re-enumerate input events and L: vars of the loaded aircraft now
    void setInitialScanDelayMs(int ms) { initialScanDelayMs_ = ms; }  // wait after an aircraft loads before listing its controls
    LiveSnapshot liveSnapshot() const;
    uint64_t liveRevision() const { return liveRevision_.load(); }
    void probeMobiFlight();

    SimStatus status() const;
    Telemetry telemetry() const;
    void setTelemetryEnabled(bool on);  // subscribe to flight data (needed for vibration)

private:
    void threadMain();
    bool tryConnect();
    struct Endpoint { std::string host; uint16_t port; };
    std::vector<Endpoint> scanForSimConnect();  // probes loopback listeners (wineserver's first) with an Open handshake
    void onConnected();
    void handleMessage(const RecvMessage& m);
    void drainQueue();
    void execute(const SimCommand& cmd);
    void sendMobiFlightCommand(const std::string& text, const std::string& desc);
    void setupMobiFlight();
    void registerMobiFlightClient();
    void onMobiFlightClientReady();
    void applyLVarWatch();
    void handleLVarData(const RecvClientData& d);
    void onAircraftPath(const std::string& cfgPath);
    void scheduleLiveScan(int delayMs);
    void pumpLiveScan();                 // sends the enumeration requests when due and publishes finished results
    void publishLive();
    void handleInputEventList(const RecvInputEventList& l);
    void handleInputEventValue(const RecvInputEventValue& v);
    void handleMobiFlightResponse(const std::string& text);
    void rememberSend(uint32_t sendId, std::string desc);
    std::string describeSend(uint32_t sendId);
    void dropConnection(const std::string& why);

    mutable std::mutex m_;
    std::condition_variable cv_;
    std::thread thread_;
    std::atomic<bool> running_{false};
    std::atomic<bool> wantConnect_{true};
    std::atomic<bool> forceConnect_{false};
    std::atomic<bool> forceDisconnect_{false};
    std::atomic<bool> forceProbe_{false};

    void setupTelemetry();
    void handleTelemetry(const RecvClientData& d);

    SimConnectClient client_;
    SimStatus status_;
    Telemetry telemetry_;
    std::atomic<bool> telemetryWanted_{false};
    bool telemetryRequested_ = false;
    std::deque<SimCommand> queue_;

    std::unordered_map<std::string, uint32_t> eventIds_;
    uint32_t nextEventId_ = 1000;
    std::deque<std::pair<uint32_t, std::string>> sentLog_;

    std::chrono::steady_clock::time_point nextConnectAttempt_{};
    std::chrono::steady_clock::time_point openSentAt_{};
    std::chrono::steady_clock::time_point lastPing_{};
    std::chrono::steady_clock::time_point mfPingSentAt_{};
    bool mfSetupDone_ = false;
    bool mfClientReady_ = false;      // our own "<name>.Command/.Response/.LVars" areas exist
    bool mfClientRequested_ = false;
    std::vector<std::string> lvarWatch_;         // wanted
    std::vector<std::string> lvarRegistered_;    // what the module currently has, index = datum id
    std::unordered_map<std::string, double> lvarValues_;
    bool lvarWatchDirty_ = false;

    // --- input events (bridge thread owns these; the UI reads copies through liveSnapshot())
    std::unordered_map<std::string, uint64_t> inputHash_;      // name -> hash for the loaded aircraft
    std::unordered_map<std::string, bool> inputIsString_;
    std::vector<LiveInputEvent> inputEventsPending_;           // collected until the replies stop arriving
    bool inputEnumInFlight_ = false;
    std::chrono::steady_clock::time_point inputEnumLastPacket_{};
    std::chrono::steady_clock::time_point nextLiveScanAt_{};
    bool liveScanPending_ = false;
    int liveScanTries_ = 0;
    bool lvarListRequested_ = false;
    bool lvarCollecting_ = false;
    std::vector<std::string> lvarNamesPending_;
    std::chrono::steady_clock::time_point lvarListSentAt_{};
    uint32_t nextGetRequest_ = 5000;
    struct PendingToggle { std::string name; uint64_t hash; int positions; std::string origin; };
    std::unordered_map<uint32_t, PendingToggle> pendingToggles_;
    std::deque<SimCommand> deferredToggles_;   // toggles of an event whose previous toggle has not been answered yet
    std::atomic<int> initialScanDelayMs_{3000};
    std::unordered_map<uint32_t, std::string> pendingWatchGets_;  // request id -> input event name
    std::vector<std::string> inputWatch_;
    size_t inputPollCursor_ = 0;
    std::unordered_map<std::string, double> inputValues_;         // guarded by m_
    std::unordered_map<std::string, std::string> inputParams_;    // guarded by m_
    std::deque<std::string> inputParamQueue_;                     // names whose parameters are still to be requested
    std::chrono::steady_clock::time_point nextInputPollAt_{};
    LiveSnapshot live_;                                          // guarded by m_
    std::atomic<uint64_t> liveRevision_{0};
};

}  // namespace lr::sim
