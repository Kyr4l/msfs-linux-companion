// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kyr4l
#include "sim/SimBridge.hpp"
#include "sim/ProbeSelect.hpp"

#include <arpa/inet.h>
#include <dirent.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/eventfd.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <cstdlib>
#include <cmath>
#include <cstring>
#include <fstream>
#include <set>
#include <sstream>
#include <thread>

#include "util/Log.hpp"
#include "util/Isolated.hpp"

namespace lr::sim {

namespace {
using clock = std::chrono::steady_clock;

// Our client-side IDs.
constexpr uint32_t EV_SYS_SIM = 1;
constexpr uint32_t EV_SYS_PAUSE = 2;
constexpr uint32_t EV_SYS_AIRCRAFT_LOADED = 3;
constexpr uint32_t REQ_SYS_AIRCRAFT_LOADED = 1;
constexpr uint32_t REQ_SYS_SIM = 2;
constexpr uint32_t REQ_ENUM_INPUT_EVENTS = 200;
constexpr uint32_t DEF_TELEMETRY = 10;
constexpr uint32_t REQ_TELEMETRY = 10;

struct TelemetryVar { const char* name; const char* units; double Telemetry::*field; };
const TelemetryVar kTelemetryVars[] = {
    {"SIM ON GROUND", "Bool", &Telemetry::onGround},
    {"GROUND VELOCITY", "Knots", &Telemetry::groundSpeedKt},
    {"G FORCE", "GForce", &Telemetry::gForce},
    {"STALL WARNING", "Bool", &Telemetry::stallWarning},
    {"OVERSPEED WARNING", "Bool", &Telemetry::overspeedWarning},
    {"GEAR TOTAL PCT EXTENDED", "Percent Over 100", &Telemetry::gearPct},
    {"TRAILING EDGE FLAPS LEFT PERCENT", "Percent Over 100", &Telemetry::flapsPct},
    {"SPOILERS HANDLE POSITION", "Percent Over 100", &Telemetry::spoilersHandle},
    {"TURB ENG N1:1", "Percent", &Telemetry::n1Eng1},
    {"TURB ENG N1:2", "Percent", &Telemetry::n1Eng2},
    {"TURB ENG REVERSE NOZZLE PERCENT:1", "Percent Over 100", &Telemetry::reverse1},
    {"TURB ENG REVERSE NOZZLE PERCENT:2", "Percent Over 100", &Telemetry::reverse2},
    {"ACCELERATION BODY Y", "Feet per second squared", &Telemetry::accelBodyY},
    {"VERTICAL SPEED", "Feet per minute", &Telemetry::verticalSpeedFpm},
    {"BRAKE LEFT POSITION", "Position", &Telemetry::brakeLeft},
    {"BRAKE RIGHT POSITION", "Position", &Telemetry::brakeRight},
    {"AIRSPEED INDICATED", "Knots", &Telemetry::iasKt},
    {"PLANE ALT ABOVE GROUND", "Feet", &Telemetry::altAgl},
};
constexpr size_t kTelemetryCount = sizeof(kTelemetryVars) / sizeof(kTelemetryVars[0]);

// MobiFlight WASM module protocol (https://github.com/MobiFlight/MobiFlight-WASM-Module).
constexpr uint32_t CD_MF_COMMAND = 100;
constexpr uint32_t CD_MF_RESPONSE = 101;
constexpr uint32_t DEF_MF_STRING = 100;
constexpr uint32_t REQ_MF_RESPONSE = 100;
constexpr uint32_t MF_MESSAGE_SIZE = 1024;
// Our private MobiFlight client (registered with "MF.Clients.Add.<name>"): own command/response areas and an
// LVars area the module fills with the variables we ask for, so other MobiFlight clients cannot shift our indices.
constexpr const char* MF_CLIENT_NAME = "msfs-linux-companion";
constexpr uint32_t CD_OUR_COMMAND = 110;
constexpr uint32_t CD_OUR_RESPONSE = 111;
constexpr uint32_t CD_OUR_LVARS = 112;
constexpr uint32_t REQ_OUR_RESPONSE = 111;
constexpr uint32_t REQ_OUR_LVARS = 112;
constexpr uint32_t REQ_OUR_LVARS_SNAPSHOT = 113;  // every watched value, once a second (see setupLVarDefinition)
constexpr uint32_t DEF_OUR_LVARS = 112;
constexpr size_t MF_LVAR_SLOTS = 512;  // datums defined once per connection (the module's area holds 1024 floats)

std::string formatNumber(double v) {
    char buf[64];
    if (std::fabs(v - std::round(v)) < 1e-9 && std::fabs(v) < 1e15) {
        snprintf(buf, sizeof buf, "%lld", static_cast<long long>(std::llround(v)));
    } else {
        snprintf(buf, sizeof buf, "%.6g", v);
    }
    return buf;
}

uint32_t eventParam(double v) {
    // SimConnect event parameters are DWORDs; negative values travel as two's complement.
    long long ll = std::llround(v);
    return static_cast<uint32_t>(static_cast<int64_t>(ll));
}

std::string stripPrefix(std::string s, const char* prefix) {
    const size_t n = strlen(prefix);
    if (s.size() >= n && strncasecmp(s.c_str(), prefix, n) == 0) s.erase(0, n);
    return s;
}
}  // namespace

SimBridge::SimBridge() {
    status_.host = "127.0.0.1";
    status_.port = 500;
    wakeFd_ = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
}

SimBridge::~SimBridge() {
    stop();
    if (wakeFd_ >= 0) ::close(wakeFd_);
}

// The bridge thread sleeps until the sim sends something or somebody calls this: commands and setting changes are
// not delayed by a short polling interval, and an idle bridge costs next to nothing.
void SimBridge::wake() {
    cv_.notify_all();
    if (wakeFd_ >= 0) {
        const uint64_t one = 1;
        (void)!::write(wakeFd_, &one, sizeof one);
    }
}

void SimBridge::setEndpoint(const std::string& host, uint16_t port) {
    std::lock_guard<std::mutex> lock(m_);
    if (status_.host == host && status_.port == port) return;
    status_.host = host;
    status_.port = port;
    forceDisconnect_ = true;
    nextConnectAttempt_ = clock::now();
    wake();
}

void SimBridge::setAutoConnect(bool on) {
    wantConnect_ = on;
    wake();
}

void SimBridge::connectNow() {
    forceConnect_ = true;
    wake();
}

void SimBridge::disconnect() {
    forceDisconnect_ = true;
    wake();
}

void SimBridge::probeMobiFlight() {
    forceProbe_ = true;
    wake();
}

void SimBridge::start() {
    if (running_) return;
    running_ = true;
    thread_ = std::thread([this] { threadMain(); });
}

void SimBridge::stop() {
    if (!running_) return;
    running_ = false;
    wake();
    if (thread_.joinable()) thread_.join();
}

void SimBridge::enqueue(SimCommand cmd) {
    {
        std::lock_guard<std::mutex> lock(m_);
        queue_.push_back(std::move(cmd));
    }
    wake();
}

void SimBridge::sendEvent(const std::string& name, double value, const std::string& origin) {
    enqueue({SimCommand::Type::SimEvent, name, value, origin});
}
void SimBridge::sendHEvent(const std::string& name, const std::string& origin) {
    enqueue({SimCommand::Type::HEvent, name, 0, origin});
}
void SimBridge::setLVar(const std::string& name, double value, const std::string& origin) {
    enqueue({SimCommand::Type::LVarSet, name, value, origin});
}
void SimBridge::execCalculatorCode(const std::string& code, const std::string& origin) {
    enqueue({SimCommand::Type::CalcCode, code, 0, origin});
}
void SimBridge::setInputEvent(const std::string& name, double value, const std::string& origin) {
    enqueue({SimCommand::Type::InputEventSet, name, value, origin});
}
void SimBridge::toggleInputEvent(const std::string& name, int positions, const std::string& origin) {
    enqueue({SimCommand::Type::InputEventToggle, name, static_cast<double>(positions), origin});
}

void SimBridge::watchInputEvents(std::vector<std::string> names) {
    std::vector<std::string> clean;
    for (auto& n : names) {
        std::string v = stripPrefix(n, "B:");
        if (!v.empty() && std::find(clean.begin(), clean.end(), v) == clean.end()) clean.push_back(v);
    }
    std::lock_guard<std::mutex> lock(m_);
    if (clean == inputWatch_) return;
    inputWatch_ = std::move(clean);
    inputPollCursor_ = 0;
    nextInputPollAt_ = {};
}

std::optional<double> SimBridge::inputEventValue(const std::string& name) const {
    std::lock_guard<std::mutex> lock(m_);
    auto it = inputValues_.find(stripPrefix(name, "B:"));
    if (it == inputValues_.end()) return std::nullopt;
    return it->second;
}

LiveSnapshot SimBridge::liveSnapshot() const {
    std::lock_guard<std::mutex> lock(m_);
    return live_;
}

void SimBridge::requestInputParams(const std::vector<std::string>& names) {
    std::lock_guard<std::mutex> lock(m_);
    for (const auto& n : names) inputParamQueue_.push_back(stripPrefix(n, "B:"));
    wake();
}

std::optional<std::string> SimBridge::inputParams(const std::string& name) const {
    std::lock_guard<std::mutex> lock(m_);
    auto it = inputParams_.find(stripPrefix(name, "B:"));
    if (it == inputParams_.end()) return std::nullopt;
    return it->second;
}

void SimBridge::requestLiveRefresh() {
    std::lock_guard<std::mutex> lock(m_);
    liveScanPending_ = true;
    liveScanTries_ = 0;
    nextLiveScanAt_ = clock::now();
    live_.inputEventsDone = live_.lvarsDone = false;
    wake();
}

void SimBridge::toggleLVar(const std::string& name, const std::string& origin) {
    enqueue({SimCommand::Type::LVarToggle, name, 0, origin});
}

// A watched name is an L: variable name, or raw calculator code in parentheses such as "(A:LIGHT NAV, Bool)".
static bool isRawCode(const std::string& n) { return !n.empty() && n.front() == '('; }

void SimBridge::watchLVars(std::vector<std::string> names) {
    std::vector<std::string> clean;
    for (auto& n : names) {
        std::string v = isRawCode(n) ? n : stripPrefix(n, "L:");
        if (!v.empty() && std::find(clean.begin(), clean.end(), v) == clean.end()) clean.push_back(v);
    }
    std::lock_guard<std::mutex> lock(m_);
    if (clean == lvarWatch_) return;
    lvarRetries_ = 0;
    lvarWatch_ = std::move(clean);
    lvarWatchDirty_ = true;
    wake();
}

std::optional<double> SimBridge::lvarValue(const std::string& name) const {
    std::lock_guard<std::mutex> lock(m_);
    auto it = lvarValues_.find(isRawCode(name) ? name : stripPrefix(name, "L:"));
    if (it == lvarValues_.end()) return std::nullopt;
    return it->second;
}

std::vector<std::pair<std::string, double>> SimBridge::lvarValues() const {
    std::lock_guard<std::mutex> lock(m_);
    return {lvarValues_.begin(), lvarValues_.end()};
}

SimStatus SimBridge::status() const {
    std::lock_guard<std::mutex> lock(m_);
    return status_;
}

Telemetry SimBridge::telemetry() const {
    std::lock_guard<std::mutex> lock(m_);
    return telemetry_;
}

void SimBridge::setTelemetryEnabled(bool on) {
    telemetryWanted_ = on;
    wake();
}

void SimBridge::setupTelemetry() {
    if (!telemetryDefined_) {  // the definition is made once per connection; stopping and restarting only touches the request
        for (size_t i = 0; i < kTelemetryCount; ++i) {
            rememberSend(client_.addToDataDefinition(DEF_TELEMETRY, kTelemetryVars[i].name, kTelemetryVars[i].units, DATATYPE_FLOAT64, 0,
                                                     static_cast<uint32_t>(i)),
                         std::string("define telemetry ") + kTelemetryVars[i].name);
        }
        telemetryDefined_ = true;
    }
    requestTelemetry();
    LOG_INFO("Telemetry subscription sent (%zu variables)", kTelemetryCount);
}

// Tagged format: each datum arrives as (uint32 id, float64 value), so a rejected variable cannot shift the others.
void SimBridge::requestTelemetry() {
    rememberSend(client_.requestDataOnSimObject(REQ_TELEMETRY, DEF_TELEMETRY, OBJECT_ID_USER, PERIOD_SIM_FRAME, DATA_REQUEST_FLAG_TAGGED, 0,
                                                static_cast<uint32_t>(telemetryInterval_), 0),
                 "request telemetry (every " + std::to_string(telemetryInterval_ + 1) + ". frame)");
    telemetryRequested_ = true;
    telemetryWindowStart_ = clock::now();
    telemetryWindowCount_ = 0;
}

// Vibration is off (or no device is plugged in): the sim stops being asked for flight data every few frames.
void SimBridge::stopTelemetry() {
    rememberSend(client_.requestDataOnSimObject(REQ_TELEMETRY, DEF_TELEMETRY, OBJECT_ID_USER, PERIOD_NEVER, DATA_REQUEST_FLAG_TAGGED, 0, 0, 0),
                 "stop telemetry");
    telemetryRequested_ = false;
    {
        std::lock_guard<std::mutex> lock(m_);
        telemetry_.valid = false;
    }
    LOG_INFO("Telemetry subscription stopped");
}

void SimBridge::handleTelemetry(const RecvClientData& d) {
    Telemetry t;
    {
        std::lock_guard<std::mutex> lock(m_);
        t = telemetry_;
    }
    const uint8_t* p = d.data.data();
    size_t n = d.data.size();
    size_t off = 0;
    for (uint32_t i = 0; i < d.defineCount && off + 12 <= n; ++i) {
        uint32_t id;
        double v;
        std::memcpy(&id, p + off, 4);
        std::memcpy(&v, p + off + 4, 8);
        off += 12;
        if (id < kTelemetryCount) t.*(kTelemetryVars[id].field) = v;
    }
    if (!t.valid) LOG_INFO("Telemetry flowing (%u datums per update)", d.defineCount);
    t.valid = true;
    t.time = clock::now();
    t.seq++;
    // The updates come every Nth sim frame: at 180 fps that is far more often than vibration needs (about 20 a second).
    // After a few seconds the interval is adjusted once or twice; it is a plain re-request of the same data.
    ++telemetryWindowCount_;
    if (t.time - telemetryWindowStart_ >= std::chrono::seconds(3) && telemetryRetunes_ < 3) {
        const double secs = std::chrono::duration<double>(t.time - telemetryWindowStart_).count();
        const double fps = telemetryWindowCount_ / secs * (telemetryInterval_ + 1);
        const int wanted = std::clamp(static_cast<int>(std::lround(fps / 20.0)) - 1, 0, 40);
        if (std::abs(wanted - telemetryInterval_) >= 2) {
            LOG_INFO("Telemetry: about %.0f sim frames a second, updates every %d. frame instead of every %d.", fps, wanted + 1, telemetryInterval_ + 1);
            telemetryInterval_ = wanted;
            ++telemetryRetunes_;
            telemetryRetune_ = true;
        } else {
            telemetryRetunes_ = 3;  // close enough: leave it
        }
        telemetryWindowStart_ = t.time;
        telemetryWindowCount_ = 0;
    }
    std::lock_guard<std::mutex> lock(m_);
    telemetry_ = t;
}

void SimBridge::rememberSend(uint32_t sendId, std::string desc) {
    // everything sent to the sim goes to the session log (except the repetitive polling), so that after a crash of the
    // sim the last commands can be matched to the time in its crash report
    if (desc.rfind("poll ", 0) != 0 && desc != "MobiFlight dummy") LOG_DEBUG("-> %s", desc.c_str());
    sentLog_.emplace_back(sendId, std::move(desc));
    while (sentLog_.size() > 512) sentLog_.pop_front();
}

std::string SimBridge::describeSend(uint32_t sendId) {
    for (const auto& [id, desc] : sentLog_)
        if (id == sendId) return desc;
    return "packet #" + std::to_string(sendId);
}

void SimBridge::dropConnection(const std::string& why) {
    client_.close();
    std::lock_guard<std::mutex> lock(m_);
    if (status_.state != ConnState::Disconnected) LOG_WARN("SimConnect: disconnected (%s)", why.c_str());
    status_.state = ConnState::Disconnected;
    status_.mobiflight = ModuleState::Unknown;
    status_.lastError = why;
    status_.simRunning = false;
    status_.aircraft.clear();
    telemetry_ = Telemetry{};
    telemetryRequested_ = false;
    eventIds_.clear();
    sentLog_.clear();
    inputHash_.clear();
    inputIsString_.clear();
    inputEventsPending_.clear();
    inputParamQueue_.clear();
    pendingToggles_.clear();
    deferredToggles_.clear();
    pendingWatchGets_.clear();
    inputEnumInFlight_ = false;
    liveScanPending_ = false;
    lvarListRequested_ = false;
    lvarCollecting_ = false;
    {
        // the cached lists in live_ stay readable (the UI keeps showing the last aircraft) but are no longer "done"
        live_.inputEventsDone = live_.lvarsDone = false;
        inputValues_.clear();
    }
    telemetryRetunes_ = 0;
    telemetryInterval_ = 2;
    telemetryDefined_ = false;
    mfSetupDone_ = false;
    mfClientReady_ = false;
    mfClientRequested_ = false;
    lvarRegistered_.clear();
    lvarValues_.clear();
    lvarDefSent_ = false;  // the definition belongs to the connection
    lvarRebuild_ = true;   // and the module's list is emptied first
    lvarWatchDirty_ = true;
    nextConnectAttempt_ = clock::now() + std::chrono::seconds(3);
}

namespace {
// The wineserver processes (MSFS under Proton) and the socket inodes they hold: the SimConnect listener is one of them.
struct WineSockets {
    bool processFound = false;  // a wineserver is running at all
    std::set<unsigned long> inodes;
};
WineSockets wineserverSockets() {
    WineSockets w;
    DIR* proc = opendir("/proc");
    if (!proc) return w;
    while (dirent* e = readdir(proc)) {
        if (e->d_name[0] < '0' || e->d_name[0] > '9') continue;
        std::ifstream comm(std::string("/proc/") + e->d_name + "/comm");
        std::string name;
        std::getline(comm, name);
        if (name != "wineserver") continue;
        w.processFound = true;
        const std::string fdDir = std::string("/proc/") + e->d_name + "/fd";
        DIR* fds = opendir(fdDir.c_str());
        if (!fds) continue;
        while (dirent* f = readdir(fds)) {
            char link[64];
            ssize_t n = readlink((fdDir + "/" + f->d_name).c_str(), link, sizeof link - 1);
            if (n <= 0) continue;
            link[n] = 0;
            unsigned long ino = 0;
            if (sscanf(link, "socket:[%lu]", &ino) == 1) w.inodes.insert(ino);
        }
        closedir(fds);
    }
    closedir(proc);
    return w;
}

// LISTEN sockets on loopback / any address from /proc/net/tcp{,6}: (host, port, inode).
using Listener = ProbeTarget;
std::vector<Listener> loopbackListeners() {
    std::vector<Listener> out;
    for (const char* file : {"/proc/net/tcp", "/proc/net/tcp6"}) {
        std::ifstream in(file);
        std::string line;
        std::getline(in, line);  // header
        const bool v6 = strstr(file, "tcp6") != nullptr;
        while (std::getline(in, line)) {
            std::istringstream ss(line);
            std::string sl, local, remote, st;
            ss >> sl >> local >> remote >> st;
            // remaining columns: tx_queue:rx_queue tr:tm->when retrnsmt uid timeout inode
            std::string txrx, trtm, retr, uid, timeout, inodeStr;
            ss >> txrx >> trtm >> retr >> uid >> timeout >> inodeStr;
            if (st != "0A") continue;  // LISTEN
            size_t colon = local.find(':');
            if (colon == std::string::npos) continue;
            const std::string addr = local.substr(0, colon);
            const uint16_t port = static_cast<uint16_t>(strtoul(local.substr(colon + 1).c_str(), nullptr, 16));
            std::string host;
            if (!v6) {
                if (addr == "0100007F") host = "127.0.0.1";
                else if (addr == "00000000") host = "127.0.0.1";
                else continue;
            } else {
                if (addr == "00000000000000000000000001000000") host = "::1";
                else if (addr == "00000000000000000000000000000000") host = "::1";
                else continue;
            }
            out.push_back({host, port, strtoul(inodeStr.c_str(), nullptr, 10)});
        }
    }
    return out;
}

// Sends a SimConnect Open to host:port and reports whether a RECV_OPEN comes back within the timeout.
bool simConnectAnswers(const std::string& host, uint16_t port, int timeoutMs) {
    SimConnectClient probe;
    std::string err;
    if (!probe.connect(host, port, 300, err)) return false;
    probe.open("msfs-linux-companion-probe");
    const auto deadline = clock::now() + std::chrono::milliseconds(timeoutMs);
    std::vector<RecvMessage> msgs;
    while (clock::now() < deadline) {
        msgs.clear();
        if (!probe.poll(50, msgs, err)) return false;
        for (const auto& m : msgs)
            if (static_cast<RecvId>(m.id) == RecvId::Open) return true;
    }
    return false;
}
}  // namespace

std::vector<SimBridge::Endpoint> SimBridge::scanForSimConnect() {
    std::vector<Endpoint> found;
    if (isolatedRun()) return found;  // a test run next to a real simulator must never probe or connect to it
    const WineSockets wine = wineserverSockets();
    const auto candidates = chooseProbeCandidates(loopbackListeners(), wine.processFound, wine.inodes);
    if (candidates.empty()) return found;  // the simulator is not running (or has not opened its port yet)
    LOG_INFO("SimConnect: configured port did not answer, probing %zu loopback listener(s)%s", candidates.size(), wine.inodes.empty() ? "" : " owned by wineserver");
    for (const auto& l : candidates) {
        if (simConnectAnswers(l.host, l.port, 700)) {
            LOG_INFO("SimConnect: found the simulator on %s:%u", l.host.c_str(), l.port);
            found.push_back({l.host, l.port});
            break;
        }
    }
    return found;
}

bool SimBridge::tryConnect() {
    std::string host;
    uint16_t port;
    {
        std::lock_guard<std::mutex> lock(m_);
        host = status_.host;
        port = status_.port;
        status_.state = ConnState::Connecting;
    }
    std::string err;
    bool connected = port != 0 && client_.connect(host, port, 2000, err);
    if (!connected) {
        // Typical under Proton: MSFS cannot bind a port < 1024 (SimConnect.xml says 500) and silently falls back
        // to a dynamic port that changes every launch. Find it.
        auto eps = scanForSimConnect();
        if (!eps.empty() && client_.connect(eps[0].host, eps[0].port, 2000, err)) {
            connected = true;
            std::lock_guard<std::mutex> lock(m_);
            status_.detectedHost = eps[0].host;
            status_.detectedPort = eps[0].port;
            host = eps[0].host;
            port = eps[0].port;
        }
    }
    if (!connected) {
        std::lock_guard<std::mutex> lock(m_);
        status_.state = ConnState::Disconnected;
        status_.lastError = port == 0 ? "no SimConnect listener found" : err;
        status_.detectedPort = 0;
        nextConnectAttempt_ = clock::now() + std::chrono::seconds(++failedConnects_ <= 10 ? 3 : 10);
        return false;
    }
    failedConnects_ = 0;
    LOG_INFO("SimConnect: TCP connected to %s:%u, sending Open", host.c_str(), port);
    uint32_t id = client_.open("msfs-linux-companion");
    rememberSend(id, "Open");
    openSentAt_ = clock::now();
    std::lock_guard<std::mutex> lock(m_);
    status_.state = ConnState::Opening;
    status_.lastError.clear();
    return true;
}

void SimBridge::onConnected() {
    rememberSend(client_.subscribeToSystemEvent(EV_SYS_SIM, "Sim"), "subscribe Sim");
    rememberSend(client_.subscribeToSystemEvent(EV_SYS_PAUSE, "Pause"), "subscribe Pause");
    rememberSend(client_.subscribeToSystemEvent(EV_SYS_AIRCRAFT_LOADED, "AircraftLoaded"), "subscribe AircraftLoaded");
    rememberSend(client_.requestSystemState(REQ_SYS_AIRCRAFT_LOADED, "AircraftLoaded"), "request AircraftLoaded");
    rememberSend(client_.requestSystemState(REQ_SYS_SIM, "Sim"), "request Sim state");
    setupMobiFlight();
    if (telemetryWanted_) setupTelemetry();
}

void SimBridge::setupMobiFlight() {
    // The module creates "MobiFlight.Command" / "MobiFlight.Response" (1024-byte strings).
    rememberSend(client_.mapClientDataNameToID("MobiFlight.Command", CD_MF_COMMAND), "map MobiFlight.Command");
    rememberSend(client_.mapClientDataNameToID("MobiFlight.Response", CD_MF_RESPONSE), "map MobiFlight.Response");
    rememberSend(client_.addToClientDataDefinition(DEF_MF_STRING, 0, MF_MESSAGE_SIZE), "define MobiFlight string");
    rememberSend(client_.requestClientData(CD_MF_RESPONSE, REQ_MF_RESPONSE, DEF_MF_STRING, CD_PERIOD_ON_SET, CD_REQUEST_FLAG_CHANGED),
                 "request MobiFlight.Response");
    mfSetupDone_ = true;
    {
        std::lock_guard<std::mutex> lock(m_);
        status_.mobiflight = ModuleState::Probing;
    }
    sendMobiFlightCommand("MF.Ping", "MobiFlight ping");
    mfPingSentAt_ = clock::now();
}

void SimBridge::sendMobiFlightCommand(const std::string& text, const std::string& desc) {
    if (!mfSetupDone_) return;
    // Give the module a frame between commands that execute code: the aircraft systems poll their
    // events once per update, and two events for the same system in one frame can lose one.
    // The same goes for registering watches (Clear / Add): a burst of them in one frame was followed by no values at all.
    static clock::time_point lastExec{};
    if (text.rfind("MF.SimVars.Set.", 0) == 0 || text.rfind("MF.SimVars.Add.", 0) == 0 || text == "MF.SimVars.Clear") {
        const auto now = clock::now();
        if (now - lastExec < std::chrono::milliseconds(30)) std::this_thread::sleep_for(std::chrono::milliseconds(30) - (now - lastExec));
        lastExec = clock::now();
    }
    // Commands that execute code go through our own area once it exists (keeps the shared one quiet);
    // registration/ping always use the shared "MobiFlight.Command".
    const bool own = mfClientReady_ && text.rfind("MF.Clients.Add.", 0) != 0 && text != "MF.Ping";
    const uint32_t area = own ? CD_OUR_COMMAND : CD_MF_COMMAND;
    std::string payload = text;
    if (payload.size() >= MF_MESSAGE_SIZE) payload.resize(MF_MESSAGE_SIZE - 1);
    rememberSend(client_.setClientData(area, DEF_MF_STRING, MF_MESSAGE_SIZE, payload.data(), payload.size() + 1), desc);
    // The module only receives the area when its content changes, so follow every command with
    // a dummy one. Otherwise sending the same command twice in a row would be ignored.
    static const char dummy[] = "MF.DummyCmd";
    rememberSend(client_.setClientData(area, DEF_MF_STRING, MF_MESSAGE_SIZE, dummy, sizeof dummy), "MobiFlight dummy");
}

void SimBridge::registerMobiFlightClient() {
    if (mfClientRequested_) return;
    mfClientRequested_ = true;
    sendMobiFlightCommand(std::string("MF.Clients.Add.") + MF_CLIENT_NAME, "MobiFlight client registration");
}

void SimBridge::onMobiFlightClientReady() {
    const std::string n = MF_CLIENT_NAME;
    rememberSend(client_.mapClientDataNameToID(n + ".Command", CD_OUR_COMMAND), "map own MobiFlight command area");
    rememberSend(client_.mapClientDataNameToID(n + ".Response", CD_OUR_RESPONSE), "map own MobiFlight response area");
    rememberSend(client_.mapClientDataNameToID(n + ".LVars", CD_OUR_LVARS), "map own MobiFlight LVars area");
    rememberSend(client_.requestClientData(CD_OUR_RESPONSE, REQ_OUR_RESPONSE, DEF_MF_STRING, CD_PERIOD_ON_SET, CD_REQUEST_FLAG_CHANGED),
                 "request own MobiFlight response");
    mfClientReady_ = true;
    LOG_INFO("MobiFlight: private client \"%s\" registered", MF_CLIENT_NAME);
    lvarWatchDirty_ = true;
}

// The SimConnect side is defined ONCE per connection: MF_LVAR_SLOTS float datums (tag = slot number) and two requests that
// are never touched again (changes as they happen, and every value once a second). What is watched is decided only on
// the MobiFlight side, by appending codes to the module's list for our private client: slot n is the n-th code added.
// Earlier versions stopped the request, cleared the definition and defined it again whenever the watched set changed;
// the simulator stopped answering a few seconds after such a change (and later died) in two sessions, so the definition
// is never changed any more.
void SimBridge::setupLVarDefinition() {
    for (size_t i = 0; i < MF_LVAR_SLOTS; ++i)
        client_.addToClientDataDefinition(DEF_OUR_LVARS, static_cast<uint32_t>(i * 4), 4, 0, static_cast<uint32_t>(i));
    rememberSend(client_.requestClientData(CD_OUR_LVARS, REQ_OUR_LVARS, DEF_OUR_LVARS, CD_PERIOD_ON_SET, CD_REQUEST_FLAG_CHANGED | CD_REQUEST_FLAG_TAGGED),
                 "request LVars (changes)");
    rememberSend(client_.requestClientData(CD_OUR_LVARS, REQ_OUR_LVARS_SNAPSHOT, DEF_OUR_LVARS, CD_PERIOD_SECOND, CD_REQUEST_FLAG_TAGGED),
                 "request LVars (every second)");
    lvarDefSent_ = true;
}

// Appends the watched codes the module does not have yet. Nothing is removed (the list only grows within a connection);
// it is emptied and rebuilt from the wanted set when it would not fit, or when values never arrive. With nothing to
// watch nothing is defined or requested: the sim is not asked for anything this tool does not use.
void SimBridge::applyLVarWatch() {
    std::vector<std::string> wanted;
    {
        std::lock_guard<std::mutex> lock(m_);
        lvarWatchDirty_ = false;
        wanted = lvarWatch_;
    }
    if (!mfClientReady_) return;
    auto known = [&](const std::string& c) { return std::find(lvarRegistered_.begin(), lvarRegistered_.end(), c) != lvarRegistered_.end(); };
    std::vector<std::string> add;
    for (const auto& w : wanted)
        if (!known(w)) add.push_back(w);
    if (!lvarRebuild_ && lvarRegistered_.size() + add.size() > MF_LVAR_SLOTS) {
        LOG_INFO("MobiFlight: the watch list is full (%zu), rebuilding it from what is wanted now", lvarRegistered_.size());
        lvarRebuild_ = true;
    }
    if (lvarRebuild_) {  // also true once per connection: the module may still hold the list of an earlier run of this tool
        sendMobiFlightCommand("MF.SimVars.Clear", "MobiFlight clear vars");
        lvarRegistered_.clear();
        {
            std::lock_guard<std::mutex> lock(m_);
            lvarValues_.clear();
        }
        add = wanted;
        if (add.size() > MF_LVAR_SLOTS) add.resize(MF_LVAR_SLOTS);
        lvarRebuild_ = false;
    }
    if (add.empty()) return;
    if (!lvarDefSent_) setupLVarDefinition();
    for (const auto& c : add) {
        sendMobiFlightCommand("MF.SimVars.Add." + (isRawCode(c) ? c : "(L:" + c + ")"), "watch " + c);
        lvarRegistered_.push_back(c);
    }
    lvarUpdatesLogged_ = 0;
    lvarChangesLogged_ = 0;
    lvarRegisteredAt_ = clock::now();
    // values that arrive before the module has evaluated the new codes would be the old content of those slots
    lvarAcceptFrom_ = lvarRegisteredAt_ + std::chrono::milliseconds(400);
    LOG_INFO("MobiFlight: watching %zu L: var(s), %zu new", lvarRegistered_.size(), add.size());
}

void SimBridge::handleLVarData(const RecvClientData& d) {
    const uint8_t* p = d.data.data();
    const size_t n = d.data.size();
    size_t off = 0;
    std::lock_guard<std::mutex> lock(m_);
    if (clock::now() < lvarAcceptFrom_) return;
    static bool first = true;
    if (first) { LOG_INFO("MobiFlight: L: var values flowing (%u datum(s) in first update)", d.defineCount); first = false; }
    if (lvarUpdatesLogged_ < 5) { ++lvarUpdatesLogged_; LOG_DEBUG("L: var update: %u datum(s), %zu byte(s), %zu registered", d.defineCount, n, lvarRegistered_.size()); }
    for (uint32_t i = 0; i < d.defineCount && off + 8 <= n; ++i) {
        uint32_t id;
        float v;
        std::memcpy(&id, p + off, 4);
        std::memcpy(&v, p + off + 4, 4);
        off += 8;
        if (id < lvarRegistered_.size()) {
            auto& slot = lvarValues_[lvarRegistered_[id]];
            if (slot != v && lvarChangesLogged_ < 60) { ++lvarChangesLogged_; LOG_DEBUG("L:%s = %g", lvarRegistered_[id].c_str(), v); }
            slot = v;
        }
    }
}

void SimBridge::execute(const SimCommand& cmd) {
    const std::string where = cmd.origin.empty() ? std::string() : " [" + cmd.origin + "]";
    switch (cmd.type) {
        case SimCommand::Type::SimEvent: {
            std::string name = stripPrefix(cmd.name, "K:");
            auto it = eventIds_.find(name);
            uint32_t id;
            if (it == eventIds_.end()) {
                id = nextEventId_++;
                eventIds_[name] = id;
                rememberSend(client_.mapClientEventToSimEvent(id, name), "map event " + name);
            } else {
                id = it->second;
            }
            const uint32_t param = eventParam(cmd.value);
            rememberSend(client_.transmitClientEvent(OBJECT_ID_USER, id, param, GROUP_PRIORITY_HIGHEST, EVENT_FLAG_GROUPID_IS_PRIORITY),
                         "event " + name + " " + formatNumber(cmd.value));
            LOG_INFO("-> K:%s %s%s", name.c_str(), formatNumber(cmd.value).c_str(), where.c_str());
            break;
        }
        case SimCommand::Type::HEvent: {
            std::string name = stripPrefix(cmd.name, "H:");
            sendMobiFlightCommand("MF.SimVars.Set.(>H:" + name + ")", "H event " + name);
            LOG_INFO("-> H:%s%s", name.c_str(), where.c_str());
            break;
        }
        case SimCommand::Type::LVarSet: {
            std::string name = stripPrefix(cmd.name, "L:");
            const std::string v = formatNumber(cmd.value);
            sendMobiFlightCommand("MF.SimVars.Set." + v + " (>L:" + name + ")", "set L:" + name);
            LOG_INFO("-> L:%s = %s%s", name.c_str(), v.c_str(), where.c_str());
            break;
        }
        case SimCommand::Type::LVarToggle: {
            std::string name = stripPrefix(cmd.name, "L:");
            // Evaluated inside the sim, so it is correct whatever changed the switch in the meantime.
            sendMobiFlightCommand("MF.SimVars.Set.(L:" + name + ") ! (>L:" + name + ")", "toggle L:" + name);
            LOG_INFO("-> L:%s toggle%s", name.c_str(), where.c_str());
            break;
        }
        case SimCommand::Type::InputEventSet: {
            const std::string name = stripPrefix(cmd.name, "B:");
            auto it = inputHash_.find(name);
            if (it == inputHash_.end()) {
                LOG_WARN("B:%s is not an input event of the loaded aircraft (%zu known)%s", name.c_str(), inputHash_.size(), where.c_str());
                if (!liveScanPending_) requestLiveRefresh();
                break;
            }
            rememberSend(client_.setInputEvent(it->second, cmd.value), "input event " + name + " = " + formatNumber(cmd.value));
            LOG_INFO("-> B:%s = %s%s", name.c_str(), formatNumber(cmd.value).c_str(), where.c_str());
            break;
        }
        case SimCommand::Type::InputEventToggle: {
            const std::string name = stripPrefix(cmd.name, "B:");
            auto it = inputHash_.find(name);
            if (it == inputHash_.end()) {
                LOG_WARN("B:%s is not an input event of the loaded aircraft (%zu known)%s", name.c_str(), inputHash_.size(), where.c_str());
                if (!liveScanPending_) requestLiveRefresh();
                break;
            }
            // one toggle in flight per event: a second one would read the same old position and undo nothing
            bool busy = false;
            for (const auto& [rq, pt] : pendingToggles_) busy |= pt.name == name;
            if (busy) { deferredToggles_.push_back(cmd); break; }
            // read the current position from the sim, then write the next one when the answer arrives
            const uint32_t req = nextGetRequest_++;
            pendingToggles_[req] = {name, it->second, cmd.value >= 2 ? static_cast<int>(cmd.value) : 2, cmd.origin};
            rememberSend(client_.getInputEvent(req, it->second), "get input event " + name);
            break;
        }
        case SimCommand::Type::CalcCode: {
            sendMobiFlightCommand("MF.SimVars.Set." + cmd.name, "calculator code");
            LOG_INFO("-> calc: %s%s", cmd.name.c_str(), where.c_str());
            break;
        }
        case SimCommand::Type::MobiFlightPing: {
            sendMobiFlightCommand("MF.Ping", "MobiFlight ping");
            mfPingSentAt_ = clock::now();
            break;
        }
        case SimCommand::Type::MobiFlightRaw: {
            sendMobiFlightCommand(cmd.name, "MobiFlight raw");
            LOG_INFO("-> MF: %s%s", cmd.name.c_str(), where.c_str());
            break;
        }
    }
    std::lock_guard<std::mutex> lock(m_);
    status_.commandsSent++;
    status_.lastActivity = clock::now();
}

void SimBridge::drainQueue() {
    for (;;) {
        SimCommand cmd;
        {
            std::lock_guard<std::mutex> lock(m_);
            if (queue_.empty()) return;
            cmd = std::move(queue_.front());
            queue_.pop_front();
        }
        if (!client_.isConnected()) {
            LOG_WARN("Dropped command (not connected to the simulator): %s", cmd.name.c_str());
            continue;
        }
        execute(cmd);
    }
}

void SimBridge::handleMessage(const RecvMessage& m) {
    switch (static_cast<RecvId>(m.id)) {
        case RecvId::Open: {
            RecvOpen o = SimConnectClient::parseOpen(m);
            char ver[64];
            snprintf(ver, sizeof ver, "%u.%u.%u.%u", o.appVersionMajor, o.appVersionMinor, o.appBuildMajor, o.appBuildMinor);
            LOG_INFO("SimConnect: open OK, simulator \"%s\" %s (SimConnect %u.%u.%u.%u)", o.applicationName.c_str(), ver,
                     o.simConnectVersionMajor, o.simConnectVersionMinor, o.simConnectBuildMajor, o.simConnectBuildMinor);
            {
                std::lock_guard<std::mutex> lock(m_);
                status_.state = ConnState::Connected;
                status_.simName = o.applicationName;
                status_.simVersion = ver;
                status_.lastError.clear();
            }
            onConnected();
            break;
        }
        case RecvId::Quit:
            dropConnection("simulator quit");
            break;
        case RecvId::Exception: {
            RecvException e = SimConnectClient::parseException(m);
            const std::string what = describeSend(e.sendId);
            bool mfRelated = what.rfind("MobiFlight", 0) == 0 || what.find("MobiFlight") != std::string::npos ||
                             what.rfind("H event", 0) == 0 || what.rfind("set L:", 0) == 0 || what.rfind("calculator", 0) == 0;
            {
                std::lock_guard<std::mutex> lock(m_);
                status_.exceptions++;
                if (mfRelated && status_.mobiflight != ModuleState::Detected) {
                    status_.mobiflight = ModuleState::NotFound;
                }
            }
            if (mfRelated) {
                LOG_WARN("SimConnect exception %s on \"%s\" (MobiFlight WASM module missing?)", exceptionName(e.exception), what.c_str());
            } else {
                LOG_ERROR("SimConnect exception %s (index %u) on \"%s\"", exceptionName(e.exception), e.index, what.c_str());
            }
            if (what.rfind("map event ", 0) == 0) eventIds_.erase(what.substr(10));
            break;
        }
        case RecvId::Event: {
            RecvEvent e = SimConnectClient::parseEvent(m);
            std::lock_guard<std::mutex> lock(m_);
            if (e.eventId == EV_SYS_SIM) status_.simRunning = e.data != 0;
            else if (e.eventId == EV_SYS_PAUSE) status_.paused = e.data != 0;
            break;
        }
        case RecvId::EventFilename: {
            RecvEventFilename e = SimConnectClient::parseEventFilename(m);
            if (e.eventId == EV_SYS_AIRCRAFT_LOADED) {
                LOG_INFO("Aircraft loaded: %s", e.fileName.c_str());
                {
                    std::lock_guard<std::mutex> lock(m_);
                    status_.aircraft = e.fileName;
                }
                onAircraftPath(e.fileName);
            }
            break;
        }
        case RecvId::SystemState: {
            RecvSystemState s = SimConnectClient::parseSystemState(m);
            std::string aircraftFromState;
            {
                std::lock_guard<std::mutex> lock(m_);
                if (s.requestId == REQ_SYS_AIRCRAFT_LOADED) { status_.aircraft = s.dataString; aircraftFromState = s.dataString; }
                else if (s.requestId == REQ_SYS_SIM) status_.simRunning = s.dataInteger != 0;
            }
            if (!aircraftFromState.empty()) onAircraftPath(aircraftFromState);
            break;
        }
        case RecvId::EnumerateInputEvents:
            handleInputEventList(SimConnectClient::parseInputEventList(m));
            break;
        case RecvId::EnumerateInputEventParams: {
            const RecvInputEventParams p = SimConnectClient::parseInputEventParams(m);
            for (const auto& [name, hash] : inputHash_)
                if (hash == p.hash) {
                    std::lock_guard<std::mutex> lock(m_);
                    inputParams_[name] = p.text;
                    break;
                }
            break;
        }
        case RecvId::GetInputEvent:
            handleInputEventValue(SimConnectClient::parseGetInputEvent(m));
            break;
        case RecvId::SimObjectData: {
            RecvClientData d = SimConnectClient::parseSimObjectData(m);
            if (d.requestId == REQ_TELEMETRY) handleTelemetry(d);
            break;
        }
        case RecvId::ClientData: {
            RecvClientData c = SimConnectClient::parseClientData(m);
            if (c.requestId == REQ_MF_RESPONSE || c.requestId == REQ_OUR_RESPONSE) {
                std::string text(reinterpret_cast<const char*>(c.data.data()), strnlen(reinterpret_cast<const char*>(c.data.data()), c.data.size()));
                handleMobiFlightResponse(text);
            } else if (c.requestId == REQ_OUR_LVARS || c.requestId == REQ_OUR_LVARS_SNAPSHOT) {
                handleLVarData(c);
            }
            break;
        }
        default:
            break;
    }
}

void SimBridge::handleMobiFlightResponse(const std::string& text) {
    if (text == "MF.Pong") {
        {
            std::lock_guard<std::mutex> lock(m_);
            if (status_.mobiflight != ModuleState::Detected) LOG_INFO("MobiFlight WASM module detected");
            status_.mobiflight = ModuleState::Detected;
        }
        registerMobiFlightClient();
    } else if (text == std::string("MF.Clients.Add.") + MF_CLIENT_NAME + ".Finished") {
        if (!mfClientReady_) onMobiFlightClientReady();
        if (liveScanPending_ || !live_.lvarsDone) liveScanPending_ = true;  // the L: var list needs our client
    } else if (text == "MF.LVars.List.Start") {
        lvarCollecting_ = true;
        lvarNamesPending_.clear();
    } else if (text == "MF.LVars.List.End") {
        lvarCollecting_ = false;
        std::sort(lvarNamesPending_.begin(), lvarNamesPending_.end());
        lvarNamesPending_.erase(std::unique(lvarNamesPending_.begin(), lvarNamesPending_.end()), lvarNamesPending_.end());
        {
            std::lock_guard<std::mutex> lock(m_);
            live_.lvars = lvarNamesPending_;
            live_.lvarsDone = true;
        }
        LOG_INFO("Sim reports %zu L: variables for the loaded aircraft", lvarNamesPending_.size());
        publishLive();
    } else if (lvarCollecting_ && text.rfind("MF.", 0) != 0 && !text.empty()) {
        lvarNamesPending_.push_back(text);
    } else {
        LOG_DEBUG("MobiFlight response: %s", text.c_str());
    }
}

void SimBridge::onAircraftPath(const std::string& cfgPath) {
    const AircraftId id = identifyAircraft(cfgPath);
    bool changed;
    {
        std::lock_guard<std::mutex> lock(m_);
        changed = id.id != live_.aircraftId;
        live_.aircraftId = id.id;
        live_.aircraftLabel = id.label;
        if (changed) {
            live_.inputEvents.clear();
            live_.lvars.clear();
            live_.inputEventsDone = live_.lvarsDone = false;
        }
    }
    inputHash_.clear();
    inputIsString_.clear();
    lvarListRequested_ = false;
    if (changed) LOG_INFO("Aircraft id: %s", id.id.empty() ? "(unknown)" : id.id.c_str());
    // the cockpit registers its input events while it loads: ask after a short delay and retry while the list is empty
    scheduleLiveScan(initialScanDelayMs_);
    publishLive();
}

void SimBridge::scheduleLiveScan(int delayMs) {
    std::lock_guard<std::mutex> lock(m_);
    liveScanPending_ = true;
    liveScanTries_ = 0;
    nextLiveScanAt_ = clock::now() + std::chrono::milliseconds(delayMs);
}

void SimBridge::publishLive() {
    liveRevision_.fetch_add(1);
}

void SimBridge::handleInputEventList(const RecvInputEventList& l) {
    for (const auto& d : l.events) {
        if (d.name.empty()) continue;
        inputHash_[d.name] = d.hash;
        inputIsString_[d.name] = d.type == 1;
        inputEventsPending_.push_back({d.name, d.type == 1});
    }
    inputEnumInFlight_ = true;
    inputEnumLastPacket_ = clock::now();
}

void SimBridge::handleInputEventValue(const RecvInputEventValue& v) {
    if (auto t = pendingToggles_.find(v.requestId); t != pendingToggles_.end()) {
        const PendingToggle pt = t->second;
        pendingToggles_.erase(t);
        if (v.type == 1) {
            LOG_WARN("B:%s holds text, cannot toggle it", pt.name.c_str());
            return;
        }
        const long long cur = std::llround(v.number);
        const long long next = ((cur % pt.positions) + pt.positions + 1) % pt.positions;
        const std::string where = pt.origin.empty() ? std::string() : " [" + pt.origin + "]";
        rememberSend(client_.setInputEvent(pt.hash, static_cast<double>(next)), "input event " + pt.name + " = " + std::to_string(next));
        LOG_INFO("-> B:%s %lld -> %lld (of %d)%s", pt.name.c_str(), cur, next, pt.positions, where.c_str());
        {
            std::lock_guard<std::mutex> lock(m_);
            inputValues_[pt.name] = static_cast<double>(next);
            status_.commandsSent++;
            status_.lastActivity = clock::now();
        }
        // the next waiting toggle of this event can read the new position now
        for (auto it = deferredToggles_.begin(); it != deferredToggles_.end(); ++it) {
            if (stripPrefix(it->name, "B:") != pt.name) continue;
            SimCommand again = *it;
            deferredToggles_.erase(it);
            execute(again);
            break;
        }
        return;
    }
    if (auto w = pendingWatchGets_.find(v.requestId); w != pendingWatchGets_.end()) {
        if (v.type == 0) {
            std::lock_guard<std::mutex> lock(m_);
            inputValues_[w->second] = v.number;
        }
        pendingWatchGets_.erase(w);
    }
}

// Sends the enumeration requests when they are due and publishes results once the reply packets stop arriving.
void SimBridge::pumpLiveScan() {
    const auto now = clock::now();
    // a finished (quiet for 300 ms) input event list becomes the published one
    if (inputEnumInFlight_ && now - inputEnumLastPacket_ > std::chrono::milliseconds(300)) {
        inputEnumInFlight_ = false;
        std::sort(inputEventsPending_.begin(), inputEventsPending_.end(), [](const LiveInputEvent& a, const LiveInputEvent& b) { return a.name < b.name; });
        inputEventsPending_.erase(std::unique(inputEventsPending_.begin(), inputEventsPending_.end(),
                                              [](const LiveInputEvent& a, const LiveInputEvent& b) { return a.name == b.name; }),
                                  inputEventsPending_.end());
        {
            std::lock_guard<std::mutex> lock(m_);
            // an empty answer usually means the cockpit has not registered its events yet: keep waiting and retrying
            if (!inputEventsPending_.empty() || live_.inputEvents.empty()) live_.inputEvents = inputEventsPending_;
            live_.inputEventsDone = !inputEventsPending_.empty();
        }
        if (inputEventsPending_.empty()) LOG_INFO("Sim reported no cockpit input events yet (try %d)", liveScanTries_);
        else LOG_INFO("Sim reports %zu cockpit input events for the loaded aircraft", inputEventsPending_.size());
        inputEventsPending_.clear();
        publishLive();
    }

    bool due;
    {
        std::lock_guard<std::mutex> lock(m_);
        due = liveScanPending_ && now >= nextLiveScanAt_ && !live_.aircraftId.empty();
    }
    if (due && !inputEnumInFlight_) {
        bool haveEvents;
        {
            std::lock_guard<std::mutex> lock(m_);
            haveEvents = live_.inputEventsDone && !live_.inputEvents.empty();
        }
        if (!haveEvents) {
            inputEventsPending_.clear();
            rememberSend(client_.enumerateInputEvents(REQ_ENUM_INPUT_EVENTS), "enumerate input events");
            ++liveScanTries_;
        }
        // L: variables through our own MobiFlight client, once it exists
        if (mfClientReady_ && !lvarListRequested_) {
            lvarListRequested_ = true;
            lvarListSentAt_ = now;
            sendMobiFlightCommand("MF.LVars.List", "list L: variables");
        }
        std::lock_guard<std::mutex> lock(m_);
        const bool eventsOk = live_.inputEventsDone && !live_.inputEvents.empty();
        const bool lvarsOk = live_.lvarsDone || status_.mobiflight == ModuleState::NotFound;
        if ((eventsOk && lvarsOk) || liveScanTries_ >= 8) {
            liveScanPending_ = false;
            // an empty answer is a real answer after the last try: do not leave the UI waiting
            if (!live_.inputEventsDone) live_.inputEventsDone = true;
            if (!live_.lvarsDone && status_.mobiflight == ModuleState::NotFound) live_.lvarsDone = true;
        } else {
            nextLiveScanAt_ = now + std::chrono::seconds(4);
        }
    }

    // parameter descriptions asked for with requestInputParams(), a few per tick
    for (int i = 0; i < 40; ++i) {
        std::string name;
        {
            std::lock_guard<std::mutex> lock(m_);
            if (inputParamQueue_.empty()) break;
            name = inputParamQueue_.front();
            inputParamQueue_.pop_front();
        }
        auto it = inputHash_.find(name);
        if (it != inputHash_.end()) rememberSend(client_.enumerateInputEventParams(it->second), "input event params " + name);
    }

    // poll the values of watched input events. A handful (the ones bindings use) are all polled twice a second; a long
    // list (a full dump) goes through in batches so the sim is never flooded.
    constexpr size_t kPollBatch = 48;
    std::vector<std::string> batch;
    {
        std::lock_guard<std::mutex> lock(m_);
        if (now >= nextInputPollAt_ && !inputWatch_.empty()) {
            if (inputPollCursor_ >= inputWatch_.size()) inputPollCursor_ = 0;
            const size_t end = std::min(inputWatch_.size(), inputPollCursor_ + kPollBatch);
            batch.assign(inputWatch_.begin() + static_cast<long>(inputPollCursor_), inputWatch_.begin() + static_cast<long>(end));
            inputPollCursor_ = end >= inputWatch_.size() ? 0 : end;
            const bool wrapped = inputPollCursor_ == 0;
            nextInputPollAt_ = now + std::chrono::milliseconds(wrapped ? 500 : 60);
        }
    }
    for (const auto& n : batch) {
        auto it = inputHash_.find(n);
        if (it == inputHash_.end() || inputIsString_[n]) continue;
        const uint32_t req = nextGetRequest_++;
        pendingWatchGets_[req] = n;
        rememberSend(client_.getInputEvent(req, it->second), "poll input event " + n);
    }
    if (pendingWatchGets_.size() > 4096) pendingWatchGets_.clear();  // never let lost replies pile up

    // Codes that still have no value 2.5 s after they were added mean the module did not take them (or did not answer):
    // empty its list and add everything again, at most twice per change of the watched set.
    if (mfClientReady_ && lvarDefSent_ && !lvarRegistered_.empty() && now - lvarRegisteredAt_ > std::chrono::milliseconds(2500) && lvarRetries_ < 2) {
        bool missing = false;
        {
            std::lock_guard<std::mutex> lock(m_);
            for (const auto& c : lvarRegistered_)
                if (!lvarValues_.count(c)) { missing = true; break; }
        }
        if (missing) {
            ++lvarRetries_;
            LOG_WARN("MobiFlight: no value yet for some of the %zu watched code(s), adding them again (try %d)", lvarRegistered_.size(), lvarRetries_);
            lvarRebuild_ = true;
            lvarRegisteredAt_ = now;
            std::lock_guard<std::mutex> lock(m_);
            lvarWatchDirty_ = true;
        }
    }
}

void SimBridge::threadMain() {
    LOG_INFO("SimBridge thread started");
    std::vector<RecvMessage> msgs;
    while (running_) {
        if (forceDisconnect_.exchange(false)) {
            dropConnection("disconnect requested");
            {
                std::lock_guard<std::mutex> lock(m_);
                status_.lastError.clear();
                // a deliberate disconnect (new endpoint, user request) must not delay the next attempt by the failure back-off
                nextConnectAttempt_ = clock::now();
            }
        }
        if (!client_.isConnected()) {
            const bool force = forceConnect_.exchange(false);
            const bool due = clock::now() >= nextConnectAttempt_;
            if ((wantConnect_ && due) || force) {
                if (!tryConnect()) {
                    // keep draining so queued commands do not pile up forever
                    drainQueue();
                }
            } else {
                drainQueue();
                std::unique_lock<std::mutex> lock(m_);
                cv_.wait_for(lock, std::chrono::milliseconds(200));
            }
            continue;
        }

        msgs.clear();
        std::string err;
        if (!client_.poll(100, msgs, err, wakeFd_)) {
            dropConnection(err);
            continue;
        }
        for (const auto& m : msgs) handleMessage(m);

        ConnState st;
        ModuleState mf;
        {
            std::lock_guard<std::mutex> lock(m_);
            st = status_.state;
            mf = status_.mobiflight;
        }
        if (st == ConnState::Opening && clock::now() - openSentAt_ > std::chrono::seconds(8)) {
            dropConnection("no Open reply from simulator (is this really the SimConnect port?)");
            continue;
        }
        if (st == ConnState::Connected) {
            drainQueue();
            if (telemetryWanted_ && !telemetryRequested_) setupTelemetry();
            else if (!telemetryWanted_ && telemetryRequested_) stopTelemetry();
            else if (telemetryRetune_ && telemetryRequested_) { telemetryRetune_ = false; requestTelemetry(); }
            pumpLiveScan();
            bool dirty;
            {
                std::lock_guard<std::mutex> lock(m_);
                dirty = lvarWatchDirty_;
            }
            if (dirty && mfClientReady_) applyLVarWatch();
            const auto now = clock::now();
            if (mf == ModuleState::Probing && now - mfPingSentAt_ > std::chrono::seconds(4)) {
                std::lock_guard<std::mutex> lock(m_);
                status_.mobiflight = ModuleState::NotFound;
                LOG_WARN("MobiFlight WASM module not detected: H: events, L: vars and calculator code are unavailable until it is installed in the Community folder");
            }
            const bool probe = forceProbe_.exchange(false);
            if (probe || (mf == ModuleState::NotFound && now - mfPingSentAt_ > std::chrono::seconds(15))) {
                {
                    std::lock_guard<std::mutex> lock(m_);
                    status_.mobiflight = ModuleState::Probing;
                }
                sendMobiFlightCommand("MF.Ping", "MobiFlight ping");
                mfPingSentAt_ = now;
            }
        }
    }
    client_.close();
    LOG_INFO("SimBridge thread stopped");
}

}  // namespace lr::sim
