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
#include <set>
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
    int edition = 0;             // 2020 or 2024 (from the name and version in RECV_OPEN: "KittyHawk" 11.x / "SunRise" 12.x), 0 = not known yet
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
           brakeLeft = 0, brakeRight = 0, iasKt = 0, altAgl = 0,
           rotorRpmPct = 0, collective = 0,  // helicopter: main rotor speed and collective, both 0..1 (1 = 100 %); 0 on an aircraft without a rotor
           engineType = 0,                   // ENGINE TYPE: 0 piston, 1 jet, 3 turbine helicopter, 5 turboprop
           torqueEng1 = 0, torqueEng2 = 0,   // percent of the rated torque
           cameraState = 0,                  // CAMERA STATE: 2 cockpit ... 10 drone aircraft are views of a flight; 11 and up are menus, the world map, hangars
           spoilerAvailable = 0;             // SPOILER AVAILABLE: the aircraft has spoilers. Its handle variable moves without them (a lever bound in the sim)

    // MSFS 2024 runs a world with the menu's aircraft behind its menus and says "in a flight" there, so only a flight camera tells a flight
    // (seen: camera 34 in the menu, with the menu's aircraft airborne at 188 kt and an overspeed warning). 0 = not reported yet.
    bool flightCamera() const { return cameraState == 0 || (cameraState >= 2 && cameraState <= 10); }

    // N1 of the engines that turn. A single-engine aircraft (the Vision Jet) reports 0 for engine 2, which must not halve engine 1.
    double turningN1() const { return meanTurning(n1Eng1, n1Eng2); }
    double turningTorque() const { return meanTurning(torqueEng1, torqueEng2); }

    // How hard the engines pull, 0..100. A turboprop's N1 is its gas generator, already 55-70 % at idle: its power is the torque.
    double enginePower() const { return engineType == 3 || engineType == 5 ? turningTorque() : turningN1(); }

private:
    static double meanTurning(double v1, double v2) {
        const double a = v1 > 1.0 ? v1 : 0.0, b = v2 > 1.0 ? v2 : 0.0;
        const int turning = (a > 0.0) + (b > 0.0);
        return turning ? (a + b) / turning : 0.0;
    }
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
};

// What is asked every frame (the Catalog tab): only whether the lists have arrived, not the lists, which are a thousand strings to copy.
struct LiveStatus {
    std::string aircraftId;
    bool inputEventsDone = false;
    bool lvarsDone = false;
};

// A change of the value of a cockpit input event while the cockpit trace is on (see SimBridge::setInputTrace).
struct InputChange {
    uint64_t serial = 0;  // 1, 2, 3 ... in the order they were seen
    std::chrono::steady_clock::time_point time;
    std::string name;
    double oldValue = 0, newValue = 0;
};

struct TraceStatus {
    bool on = false;
    size_t events = 0;           // input events that are read back
    bool subscribed = false;     // the subscription was sent
    bool refused = false;        // the sim answered it with an exception: only the read-back finds changes
    uint64_t pushed = 0;         // changes the sim announced
    uint64_t polled = 0;         // changes found by reading the values back
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
    void execCalculatorCode(const std::string& code, const std::string& origin = {});
    // B: input events: cockpit switches exposed by the loaded aircraft (SimConnect input events, no add-on needed)
    void setInputEvent(const std::string& name, double value, const std::string& origin = {});
    void toggleInputEvent(const std::string& name, int positions, const std::string& origin = {});
    void watchInputEvents(std::vector<std::string> names);  // polled a few times a second for display
    std::optional<double> inputEventValue(const std::string& name) const;
    void requestInputParams(const std::vector<std::string>& names);  // asks the sim how it describes each event's parameters
    std::optional<std::string> inputParams(const std::string& name) const;
    // Watching the cockpit: while on, every change of the value of a cockpit input event (a switch clicked with the mouse, a knob turned) is recorded,
    // found by the sim's own announcement and by reading every value back a few times a second. The first value of each event is only the baseline.
    void setInputTrace(bool on);
    std::vector<InputChange> inputChanges(uint64_t afterSerial) const;  // the recorded changes with a serial above this one (the last 1000 are kept)
    void clearInputChanges();
    TraceStatus traceStatus() const;
    void requestLiveRefresh();      // re-enumerate input events and L: vars of the loaded aircraft now
    void setInitialScanDelayMs(int ms) { initialScanDelayMs_ = ms; }  // wait after an aircraft loads before listing its controls
    LiveSnapshot liveSnapshot() const;
    LiveStatus liveStatus() const;
    uint64_t liveRevision() const { return liveRevision_.load(); }
    void probeMobiFlight();

    SimStatus status() const;
    // The user is flying: connected, the sim reports that the aircraft is under the user's control (not a menu or a loading screen) and is not paused.
    // The data of the sim objects keeps coming in a menu on MSFS 2024 (with whatever the menu's aircraft reports), so it is not a sign of a flight.
    bool inFlight() const;
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
    bool warnUnknownInput(const std::string& name, const std::string& where);  // true the first time for this name and aircraft
    void sendMobiFlightCommand(const std::string& text, const std::string& desc);
    void setupMobiFlight();
    void registerMobiFlightClient();
    void onMobiFlightClientReady();
    void applyLVarWatch();
    void setupLVarDefinition();
    void handleLVarData(const RecvClientData& d);
    void onAircraftPath(const std::string& cfgPath);
    void scheduleLiveScan(int delayMs);
    void pumpInputTrace(std::chrono::steady_clock::time_point now);  // subscription and read-back of the cockpit trace
    void traceValue(const std::string& name, double value, bool announced);
    void pumpLiveScan();                 // sends the enumeration requests when due and publishes finished results
    void publishLive();
    void handleInputEventList(const RecvInputEventList& l);
    void handleInputEventValue(const RecvInputEventValue& v);
    void abandonToggles(const std::string& name);  // the sim could not tell the position of this input event
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
    std::atomic<bool> userDisconnected_{false};  // "Disconnect" was pressed: no automatic attempt until "Connect now"
    std::atomic<bool> forceProbe_{false};

    void setupTelemetry();
    void stopTelemetry();
    void requestTelemetry();
    void handleTelemetry(const RecvClientData& d);

    SimConnectClient client_;
    SimStatus status_;
    Telemetry telemetry_;
    std::atomic<bool> telemetryWanted_{false};
    int telemetryInterval_ = 2;       // sim frames skipped between two telemetry updates (adapted to about 20 updates a second)
    int telemetryRetunes_ = 0;        // adaptations made on this connection (a few at most)
    int telemetryWindowCount_ = 0;
    std::chrono::steady_clock::time_point telemetryWindowStart_{};
    bool telemetryRetune_ = false;
    bool telemetryDefined_ = false;  // the data definition exists on this connection (defined once)
    int wakeFd_ = -1;                // eventfd: written when another thread has work for the bridge thread
    int failedConnects_ = 0;         // consecutive failed attempts, for the retry pace
    void wake();

    bool telemetryRequested_ = false;
    std::deque<SimCommand> queue_;

    std::unordered_map<std::string, uint32_t> eventIds_;
    uint32_t nextEventId_ = 1000;
    std::deque<std::pair<uint32_t, std::string>> sentLog_;

    std::atomic<std::chrono::steady_clock::time_point> nextConnectAttempt_{};  // written by the UI thread (new endpoint) and the bridge thread
    std::chrono::steady_clock::time_point openSentAt_{};
    std::chrono::steady_clock::time_point mfPingSentAt_{};
    bool mfSetupDone_ = false;
    bool mfClientReady_ = false;      // our own "<name>.Command/.Response/.LVars" areas exist
    bool mfClientRequested_ = false;
    std::vector<std::string> lvarWatch_;         // wanted
    std::vector<std::string> lvarRegistered_;    // what the module currently has, index = datum id
    std::unordered_map<std::string, double> lvarValues_;
    std::chrono::steady_clock::time_point lvarRegisteredAt_{};
    std::chrono::steady_clock::time_point lvarAcceptFrom_{};  // values before this are the old content of new slots
    bool lvarDefSent_ = false;   // the fixed SimConnect definition and requests exist on this connection
    bool lvarRebuild_ = true;    // empty the module's list and add everything again (also once at the start of a connection)
    std::atomic<int> lvarRetries_{0};  // rebuilds because values never arrived
    int lvarUpdatesLogged_ = 0;
    int lvarChangesLogged_ = 0;  // value changes written to the log since the last registration (bounded)  // debug log of the first updates after each registration
    std::atomic<bool> lvarWatchDirty_{false};  // the watched names changed (UI thread) or must be registered again (bridge thread)

    // --- input events (bridge thread owns these; the UI reads copies through liveSnapshot())
    std::unordered_map<std::string, uint64_t> inputHash_;      // name -> hash for the loaded aircraft
    std::unordered_map<std::string, std::string> inputByLower_;  // lower-case name -> the name as the sim spells it (MSFS 2024 upper-cases them, 2020 does not)
    std::unordered_map<std::string, uint64_t>::const_iterator findInput(const std::string& name) const;  // exact, else ignoring the case
    std::unordered_map<std::string, bool> inputIsString_;
    std::vector<LiveInputEvent> inputEventsPending_;           // collected until the replies stop arriving
    bool inputEnumInFlight_ = false;
    std::chrono::steady_clock::time_point inputEnumLastPacket_{};
    std::chrono::steady_clock::time_point nextLiveScanAt_{};
    std::atomic<bool> liveScanPending_{false};  // set by the UI thread (Refresh) as well as the bridge thread
    std::atomic<int> liveScanTries_{0};
    std::atomic<bool> lvarListRequested_{false};
    bool lvarCollecting_ = false;
    std::vector<std::string> lvarNamesPending_;
    std::chrono::steady_clock::time_point lvarListSentAt_{};
    uint32_t nextGetRequest_ = 5000;
    struct PendingToggle { std::string name; uint64_t hash; int positions; std::string origin; };
    std::unordered_map<uint32_t, PendingToggle> pendingToggles_;
    // cockpit trace (bridge thread unless noted)
    std::atomic<bool> traceInputs_{false};
    std::atomic<bool> traceReset_{false};          // set by setInputTrace(true): start from a new baseline
    std::atomic<bool> traceSubscribed_{false};
    std::atomic<bool> traceRefused_{false};
    std::atomic<size_t> traceEventCount_{0};
    std::unordered_map<uint64_t, std::string> inputNameByHash_;
    std::unordered_map<std::string, double> traceLast_;
    std::vector<std::string> traceNames_;           // what is read back, in turns
    std::set<std::string> traceBroken_;             // events the sim would not read: left out
    std::unordered_map<uint32_t, std::string> pendingTraceGets_;
    size_t tracePollCursor_ = 0;
    std::chrono::steady_clock::time_point nextTracePollAt_{};
    std::deque<InputChange> inputChanges_;          // guarded by m_
    uint64_t inputChangeSerial_ = 0, tracePushed_ = 0, tracePolled_ = 0;  // guarded by m_
    std::set<std::string> unknownInputWarned_;  // input events already reported as unknown for the loaded aircraft
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
