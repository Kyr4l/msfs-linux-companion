// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kyr4l
#include "mcdu/McduService.hpp"

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstring>
#include <ctime>
#include <fstream>
#include <sstream>

#include <fcntl.h>
#include <poll.h>
#include <sys/eventfd.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <unistd.h>

#include "app/Paths.hpp"
#include "desktop/DesktopLauncher.hpp"
#include "mcdu/McduFont.hpp"
#include "util/Isolated.hpp"
#include "util/Log.hpp"

namespace lr::mcdu {

namespace {
constexpr auto kScreenInterval = std::chrono::milliseconds(150);  // the panel garbles a screen when the next one follows too soon
constexpr auto kReopenDelay = std::chrono::seconds(3);
constexpr auto kWeatherInterval = std::chrono::minutes(10);  // airports publish a report every 30 or 60 minutes
constexpr auto kWeatherRetry = std::chrono::minutes(1);
constexpr auto kMediaInterval = std::chrono::milliseconds(1500);
constexpr int kBrightnessStep = 10;
constexpr int kAvionicsRefreshMs = 250;  // the avionics pages follow the sim's values
constexpr auto kRepeatDelay = std::chrono::milliseconds(500), kRepeatEvery = std::chrono::milliseconds(150);  // BRT / DIM held
constexpr size_t kMaxQueuedKeys = 32;
constexpr int kReadCoalesceMs = 25;  // the light sensors report all the time: reports are read in small batches, not one wake-up each

uint8_t percentToLevel(int percent) { return static_cast<uint8_t>(std::clamp(percent, 0, 100) * 255 / 100); }
}  // namespace

McduService::McduService(Hooks hooks) : hooks_(std::move(hooks)), pages_(*this), mirror_([this] { wake(); }), weather_([this] { wake(); }) {
    wakeFd_ = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
}

McduService::~McduService() {
    stop();
    if (wakeFd_ >= 0) ::close(wakeFd_);
}

void McduService::wake() {
    if (wakeFd_ < 0) return;
    const uint64_t one = 1;
    const ssize_t n = ::write(wakeFd_, &one, sizeof one);
    (void)n;
}

void McduService::configure(const desktop::McduConfig& cfg) {
    {
        std::lock_guard<std::mutex> lock(m_);
        cfg_ = cfg;
        cfgChanged_ = true;
    }
    wake();
}

void McduService::setDevice(const std::string& path, uint16_t productId) {
    {
        std::lock_guard<std::mutex> lock(m_);
        if (wantedPath_ == path && productId_ == productId) return;
        wantedPath_ = path;
        productId_ = productId;
    }
    updateThread();
    wake();
}

void McduService::setSim(const SimState& sim) {
    {
        std::lock_guard<std::mutex> lock(m_);
        if (sim_ == sim) return;
        sim_ = sim;
    }
    wake();
}

void McduService::setPanelOpen(bool open) {
    {
        std::lock_guard<std::mutex> lock(m_);
        if (panelOpen_ == open) return;
        panelOpen_ = open;
    }
    updateThread();
}

void McduService::pressKey(Key key) {
    {
        std::lock_guard<std::mutex> lock(m_);
        if (queuedKeys_.size() < kMaxQueuedKeys) queuedKeys_.push_back(key);
    }
    wake();
}

Screen McduService::screen() const {
    std::lock_guard<std::mutex> lock(m_);
    return shown_;
}

ServiceStatus McduService::status() const {
    std::lock_guard<std::mutex> lock(m_);
    return status_;
}

bool McduService::applyRuntimePrefs(desktop::McduConfig& cfg) {
    std::lock_guard<std::mutex> lock(m_);
    if (!runtimeChanged_) return false;
    runtimeChanged_ = false;
    cfg.screenPercent = runtimePrefs_.screenPercent;
    cfg.autoScreen = runtimePrefs_.autoScreen;
    cfg.diskMount = runtimePrefs_.diskMount;
    cfg.gpuIndex = runtimePrefs_.gpuIndex;
    cfg.refreshSeconds = runtimePrefs_.refreshSeconds;
    cfg.clockUtc = runtimePrefs_.clockUtc;
    cfg.clock12h = runtimePrefs_.clock12h;
    cfg.homeAirport = runtimePrefs_.homeAirport;
    cfg.netBits = runtimePrefs_.netBits;
    cfg_ = cfg;
    return true;
}

void McduService::noteRuntimePrefs() {
    pages_.exportPrefs(active_);
    std::lock_guard<std::mutex> lock(m_);
    runtimePrefs_ = active_;
    runtimeChanged_ = true;
}

void McduService::updateThread() {
    bool want;
    {
        std::lock_guard<std::mutex> lock(m_);
        want = !wantedPath_.empty() || panelOpen_;
    }
    if (want && !running_.load()) {
        if (thread_.joinable()) thread_.join();
        running_ = true;
        thread_ = std::thread([this] { run(); });
    } else if (!want) {
        stop();
    }
}

void McduService::stop() {
    running_ = false;
    wake();
    if (thread_.joinable()) thread_.join();
    mirror_.setWanted(false);  // before the wake-up descriptor goes: the mirror's thread uses it
}

void McduService::tapKeys(const std::vector<int>& keys) {
    if (hooks_.tapKeys) hooks_.tapKeys(keys);
}

void McduService::media(desktop::MediaCommand command) {
    mediaControl_.command(command);
    nextMedia_ = Clock::time_point{};  // the page shows the new state at once
}

void McduService::audio(desktop::AudioCommand command) {
    const std::string& app = active_.commsApp;
    switch (command) {
        case desktop::AudioCommand::ToggleMic:
            audioControl_.toggleMic();
            break;
        case desktop::AudioCommand::ToggleVoice:
            audioControl_.setVoiceMuted(app, !audioState_.voiceMuted);
            deafenMutedVoice_ = false;  // the user decides about the microphone from here on
            break;
        case desktop::AudioCommand::ToggleDeafen: {
            const bool deafen = !audioState_.soundMuted;
            audioControl_.setSoundMuted(app, deafen);
            if (deafen && !audioState_.voiceMuted) {  // as in a voice app: who cannot hear does not talk either
                audioControl_.setVoiceMuted(app, true);
                deafenMutedVoice_ = true;
            } else if (!deafen && deafenMutedVoice_) {
                audioControl_.setVoiceMuted(app, false);
                deafenMutedVoice_ = false;
            }
            break;
        }
    }
    nextAudio_ = Clock::time_point{};  // the page shows the new state at once
}

bool McduService::runnerQuery(const std::string& text) { return desktop::showInKRunner(text); }
bool McduService::openUrl(const std::string& url) { return desktop::openWebAddress(url); }

void McduService::dismissTask(const std::string& key) {
    progress_.tracker().dismiss(key);
    nextRefresh_ = Clock::time_point{};
}

void McduService::sleepScreen() {
    sleeping_ = true;
    nextRefresh_ = Clock::time_point{};
}

void McduService::openDevice(const std::string& path) {
    retryOpenAt_ = Clock::now() + kReopenDelay;
    if (isolatedRun()) return;  // a test run never touches a real unit
    fd_ = ::open(path.c_str(), O_RDWR | O_NONBLOCK | O_CLOEXEC);
    if (fd_ < 0) {
        const std::string why = strerror(errno);
        std::lock_guard<std::mutex> lock(m_);
        if (status_.error != why) LOG_WARN("MCDU: cannot open %s: %s", path.c_str(), why.c_str());
        status_.error = why;
        return;
    }
    openPath_ = path;
    keysDown_.reset();
    sentValid_ = false;
    lampsValid_ = false;
    std::fill(std::begin(lastLevels_), std::end(lastLevels_), -1);
    for (const Report& r : initReports())
        if (!write(r.data(), r.size())) return;
    fontSent_ = "built-in";  // the set-up gives the unit its own font back
    applyLights(true);
    LOG_INFO("MCDU: %s opened", path.c_str());
    std::lock_guard<std::mutex> lock(m_);
    status_.deviceOpen = true;
    status_.devicePath = path;
    status_.seat = seatName(productId_);
    status_.error.clear();
}

void McduService::closeDevice(const char* reason) {
    if (fd_ >= 0) {
        ::close(fd_);
        LOG_INFO("MCDU: closed (%s)", reason);
    }
    fd_ = -1;
    openPath_.clear();
    sentValid_ = false;
    std::lock_guard<std::mutex> lock(m_);
    status_.deviceOpen = false;
    status_.devicePath.clear();
}

bool McduService::write(const uint8_t* data, size_t size) {
    if (fd_ < 0) return false;
    for (int attempt = 0; attempt < 4; ++attempt) {
        const ssize_t n = ::write(fd_, data, size);
        if (n == static_cast<ssize_t>(size)) return true;
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) {
            pollfd p{fd_, POLLOUT, 0};
            ::poll(&p, 1, 20);
            continue;
        }
        break;
    }
    const std::string why = strerror(errno);
    closeDevice(why.c_str());  // unplugged, or it stopped answering: tried again later, never in a loop
    retryOpenAt_ = Clock::now() + kReopenDelay;
    return false;
}

void McduService::readDevice() {
    uint8_t buf[64];
    KeyBits previous = keysDown_;
    bool got = false;
    InputState input;
    for (int i = 0; i < 32; ++i) {
        const ssize_t n = ::read(fd_, buf, sizeof buf);
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) break;
            closeDevice("read failed");
            retryOpenAt_ = Clock::now() + kReopenDelay;
            return;
        }
        if (n == 0) break;
        InputState s;
        if (!parseInputReport(buf, static_cast<size_t>(n), s)) continue;
        // every report is looked at: a key pressed and let go within one batch still counts
        const KeyBits pressed = s.keys & ~previous;
        for (int k = 0; k < kKeyCount; ++k)
            if (pressed.test(static_cast<size_t>(k))) handleKey(static_cast<Key>(k));
        previous = s.keys;
        input = s;
        got = true;
        if (fd_ < 0) return;
    }
    if (!got) return;
    keysDown_ = previous;
    // The sensors span a huge range (about 2,700 of 4,095 in afternoon light indoors, 38 to 61 in the evening, seen on a
    // unit with firmware 1.09): a logarithmic scale, as the eye judges light, keeps ordinary rooms off the bottom of it.
    const double raw = (input.lightLeft + input.lightRight) / 2.0;
    ambient_ = static_cast<int>(std::lround(100.0 * std::log1p(std::max(raw, 0.0)) / std::log1p(4095.0)));
}

void McduService::handleKey(Key key) {
    lastKey_ = Clock::now();
    if (sleeping_) {  // the key that wakes the unit does nothing else
        sleeping_ = false;
        nextRefresh_ = Clock::time_point{};
        return;
    }
    if (key == Key::Brt || key == Key::Dim) {
        stepBrightness(key == Key::Brt);
        holdStart_ = Clock::now();  // held on: more steps from kRepeatDelay (see run())
        nextRepeat_ = holdStart_ + kRepeatDelay;
        return;
    }
    if (avionicsMode()) {
        const auto read = [this](const std::string& code) { return hooks_.readCode ? hooks_.readCode(code) : std::nullopt; };
        for (const auto& code : avionics_.onKey(key, read))
            if (hooks_.fireCode) hooks_.fireCode(code);
        nextRefresh_ = Clock::time_point{};
        return;
    }
    if (activeSim_.active) {
        if (activeSim_.iniBuildsKeys) {
            uint16_t product;
            {
                std::lock_guard<std::mutex> lock(m_);
                product = productId_;
            }
            const std::string code = iniBuildsKeyCode(key, product);
            if (!code.empty() && hooks_.fireCode) hooks_.fireCode(code);
            mirror_.boost();
            return;
        }
        const char* id = keyId(key);
        if (!*id) return;
        if (link_.connected()) link_.sendKey(id);
        else if (!activeSim_.hEventPrefix.empty() && hooks_.fireHEvent) hooks_.fireHEvent(activeSim_.hEventPrefix + id);
        return;
    }
    pages_.onKey(key);
    if (pages_.takePrefsChanged()) noteRuntimePrefs();
    nextRefresh_ = Clock::time_point{};  // the page shows the key at once
}

bool McduService::avionicsMode() const {
    return activeSim_.active && !activeSim_.aircraft.empty() && !activeSim_.simBridge && !activeSim_.iniBuildsKeys && activeSim_.hEventPrefix.empty();
}

void McduService::applyLights(bool force) {
    if (fd_ < 0) return;
    int screen = active_.screenPercent, keys = active_.keysPercent, leds = active_.ledPercent;
    if (active_.autoBrightness && ambient_ >= 0) {  // each light on its curve; BRT / DIM move the screen curve
        screen = std::clamp(desktop::curveAt(active_.autoScreen, ambient_), 5, 100);
        keys = std::clamp(desktop::curveAt(active_.autoKeys, ambient_), 0, 100);
        leds = std::clamp(desktop::curveAt(active_.autoLeds, ambient_), 0, 100);
    }
    if (sleeping_) screen = keys = leds = 0;
    screenShown_ = screen;
    const int levels[3] = {percentToLevel(keys), percentToLevel(screen), percentToLevel(leds)};
    const Light ids[3] = {Light::Keys, Light::Screen, Light::Leds};
    for (int i = 0; i < 3; ++i) {
        if (!force && levels[i] == lastLevels_[i]) continue;
        const auto report = encodeLight(ids[i], static_cast<uint8_t>(levels[i]));
        if (!write(report.data(), report.size())) return;
        lastLevels_[i] = levels[i];
    }
    LampState lamps{};
    if (activeSim_.active && link_.hasScreen()) lamps = link_.lamps();  // otherwise every lamp stays off: only an aircraft lights them
    else if (activeSim_.active && activeSim_.hasLamps) lamps = activeSim_.lamps;
    if (force || !lampsValid_ || lamps != lastLamps_) {
        for (size_t i = 0; i < kLamps.size(); ++i) {
            const auto report = encodeLight(kLamps[i], lamps[i] ? 1 : 0);
            if (!write(report.data(), report.size())) return;
        }
        lastLamps_ = lamps;
        lampsValid_ = true;
    }
}

// The lamps as the on-screen panel draws them, with or without a unit plugged in.
static LampState panelLamps(const SimState& simState, bool sim, const SimLink& link) {
    LampState lamps{};
    if (sim && link.hasScreen()) lamps = link.lamps();
    else if (sim && simState.hasLamps) lamps = simState.lamps;
    return lamps;
}

void McduService::renderScreen(Clock::time_point now) {
    Screen next;
    if (sleeping_) {
        next.clear();
    } else if (activeSim_.active) {
        mirrorShown_ = false;
        if (link_.hasScreen()) next = link_.screen();
        else if (activeSim_.iniBuildsKeys && mirror_.screen(next)) mirrorShown_ = true;
        else if (avionicsMode()) avionics_.render(next, [this](const std::string& code) { return hooks_.readCode ? hooks_.readCode(code) : std::nullopt; });
        else {
            next.putCentered(0, "MSFS");
            next.putCentered(4, activeSim_.aircraft.empty() ? "NO AIRCRAFT" : activeSim_.aircraft, Color::Green);
            if (activeSim_.simBridge) next.putCentered(7, link_.connected() ? "WAITING FOR AIRCRAFT" : "WAITING FOR SIMBRIDGE", Color::Amber);
            else next.putCentered(7, activeSim_.hEventPrefix.empty() && !activeSim_.iniBuildsKeys ? "NO MCDU DATA" : "KEYS ONLY", Color::Amber);
            if (activeSim_.iniBuildsKeys) {
                const std::string hint = mirror_.hint();
                next.putCentered(9, hint.empty() ? "READING THE SCREEN" : hint, Color::White, true);
                if (hint == "POP OUT THE MCDU") next.putCentered(10, "RIGHT ALT + CLICK IT", Color::White, true);
                if (hint == "POP-OUT TOO SMALL") next.putCentered(10, "MAKE THE WINDOW BIGGER", Color::White, true);
            }
        }
    } else {
        if (pages_.wantsSystem()) {
            if (now >= nextSystem_) {
                pages_.setSystem(systemInfo_.read());
                nextSystem_ = now + std::chrono::seconds(std::clamp(active_.refreshSeconds, 1, 5));
            }
        } else {
            systemInfo_.release();
            nextSystem_ = Clock::time_point{};
        }
        if (pages_.wantsTasks()) pages_.setTasks(progress_.tracker().tasks(now), progress_.available());
        if (pages_.wantsAudio()) {
            if (now >= nextAudio_) {
                audioState_ = audioControl_.read(active_.commsApp);
                pages_.setAudio(audioState_);
                nextAudio_ = now + std::chrono::seconds(std::clamp(active_.refreshSeconds, 1, 5));
            }
        } else {
            nextAudio_ = Clock::time_point{};
        }
        if (pages_.page() == PageId::Media && now >= nextMedia_) {
            pages_.setMedia(mediaControl_.read());
            nextMedia_ = now + kMediaInterval;
        } else if (pages_.page() != PageId::Media) {
            mediaControl_.close();
        }
        const std::time_t t = std::time(nullptr);
        std::tm tm{};
        localtime_r(&t, &tm);
        pages_.render(next, tm);
    }
    desired_ = next;
    std::lock_guard<std::mutex> lock(m_);
    shown_ = next;
    status_.sideLabels = sleeping_ || activeSim_.active ? SideLabels{} : pages_.sideLabels();
    status_.lamps = panelLamps(activeSim_, activeSim_.active, link_);
}

void McduService::sendScreen(Clock::time_point now) {
    if (fd_ < 0 || (sentValid_ && sent_ == desired_) || now < nextSend_) return;
    for (const Report& r : encodeScreen(desired_))
        if (!write(r.data(), r.size())) return;
    sent_ = desired_;
    sentValid_ = true;
    nextSend_ = now + kScreenInterval;
    std::lock_guard<std::mutex> lock(m_);
    ++status_.screensSent;
}

void McduService::stepBrightness(bool up) {
    const int step = up ? kBrightnessStep : -kBrightnessStep;
    if (active_.autoBrightness) {  // the whole screen curve up or down, its shape kept as far as it goes
        for (int& v : active_.autoScreen) v = std::clamp(v + step, 5, 100);
    } else {
        active_.screenPercent = std::clamp(active_.screenPercent + step, 10, 100);
    }
    noteRuntimePrefs();
    applyLights(false);
}

void McduService::applyFont() {
    if (fd_ < 0 || active_.font == fontSent_) return;
    const std::string wanted = active_.font;
    fontSent_ = wanted;  // tried once: a font that cannot be sent is not tried again on every turn
    if (wanted == "b612" || wanted == "aircraft") {
        auto readFile = [](const std::string& suffix, std::string& out) {
            for (const auto& path : app::listDataFiles(suffix)) {
                std::ifstream f(path, std::ios::binary);
                std::stringstream ss;
                ss << f.rdbuf();
                out = ss.str();
                if (!out.empty()) return true;
            }
            return false;
        };
        std::string fontText, mapText, err;
        FontUpload up;
        // B612 ships with the program; "aircraft" is made on this machine from captures of the aircraft's own MCDU (never shipped)
        if (!readFile(wanted + "-font-21x31.json", fontText) || !readFile("WinWingFontPacketMap-3x31.json", mapText)) err = "font files not found";
        else if (!buildFontUpload(fontText, mapText, percentToLevel(active_.screenPercent), up, err)) err = "font files: " + err;
        if (!err.empty()) {
            LOG_WARN("MCDU: font %s not sent: %s", wanted.c_str(), err.c_str());
            return;
        }
        Screen empty;  // the unit shows the font as it arrives: an empty screen meanwhile
        for (const Report& r : encodeScreen(empty))
            if (!write(r.data(), r.size())) return;
        for (const auto& p : up.packets)
            if (!write(p.data(), p.size())) return;
        LOG_INFO("MCDU: font %s sent (%zu packets)", up.name.c_str(), up.packets.size());
    } else {
        // The unit keeps a sent font until it loses power: its set-up sent again does not bring its own font back cleanly (the
        // screen moved by a few pixels, stray lines at the top, seen on firmware 1.09). Nothing is sent: unplugging it does.
        LOG_INFO("MCDU: built-in font from the next time the unit is plugged in");
        return;
    }
    sentValid_ = false;  // the screen and the lights again, as after opening
    std::fill(std::begin(lastLevels_), std::end(lastLevels_), -1);
    lampsValid_ = false;
    applyLights(true);
}

void McduService::blankDevice() {
    if (fd_ < 0) return;
    Screen empty;
    for (const Report& r : encodeScreen(empty))
        if (!write(r.data(), r.size())) return;
    for (Light l : {Light::Screen, Light::Keys, Light::Leds}) {
        const auto report = encodeLight(l, 0);
        if (!write(report.data(), report.size())) return;
    }
}

void McduService::run() {
    setpriority(PRIO_PROCESS, static_cast<id_t>(syscall(SYS_gettid)), 10);  // the simulator goes first
    lastKey_ = Clock::now();
    nextRefresh_ = Clock::time_point{};
    {
        std::lock_guard<std::mutex> lock(m_);
        status_.running = true;
        cfgChanged_ = true;
    }
    while (running_) {
        const auto now = Clock::now();
        std::string wanted;
        std::deque<Key> keys;
        bool reconfigure = false, simChanged = false;
        {
            std::lock_guard<std::mutex> lock(m_);
            wanted = wantedPath_;
            keys.swap(queuedKeys_);
            if (cfgChanged_) { active_ = cfg_; cfgChanged_ = false; reconfigure = true; }
            if (!(activeSim_ == sim_)) { activeSim_ = sim_; simChanged = true; }
            status_.simMode = activeSim_.active;
            status_.simScreen = activeSim_.active && (link_.hasScreen() || mirrorShown_ || avionicsMode());
            status_.sleeping = sleeping_;
            status_.ambientPercent = ambient_;
            status_.screenLevelPercent = screenShown_;
        }
        if (reconfigure) {
            pages_.configure(active_);
            avionics_.setTemperatures(active_.avionicsTemp);
            avionics_.setFuelUnit(active_.avionicsFuel);
            applyLights(false);
            nextRefresh_ = Clock::time_point{};
        }
        progress_.setWanted(!activeSim_.active);  // nothing of this runs next to the simulator
        progress_.process();
        if (simChanged) {
            link_.setWanted(activeSim_.active && activeSim_.simBridge);
            if (activeSim_.active) {
                mediaControl_.close();
                systemInfo_.release();
            }
            sleeping_ = false;
            lastKey_ = now;
            nextRefresh_ = Clock::time_point{};
            avionics_.reset();
            avionics_.setG3000(activeSim_.g3000);
        }
        // the avionics pages' values: read only while they can be on screen
        if (avionicsMode() != avionicsWatched_) {
            avionicsWatched_ = avionicsMode();
            if (hooks_.watchCodes) hooks_.watchCodes(avionicsWatched_ ? AvionicsPages::codes() : std::vector<std::string>{});
        }
        if (!openPath_.empty() && openPath_ != wanted) closeDevice("unplugged");
        if (fd_ < 0 && !wanted.empty() && now >= retryOpenAt_) openDevice(wanted);

        for (Key k : keys) handleKey(k);

        // sleep: never while the aircraft owns the unit
        const bool canSleep = !activeSim_.active && active_.sleepMinutes > 0;
        const auto sleepAt = lastKey_ + std::chrono::minutes(active_.sleepMinutes);
        if (canSleep && !sleeping_ && now >= sleepAt) {
            sleeping_ = true;
            nextRefresh_ = Clock::time_point{};
        }
        if (link_.takeChanged()) nextRefresh_ = Clock::time_point{};
        bool panelOpen;
        {
            std::lock_guard<std::mutex> lock(m_);
            panelOpen = panelOpen_;
        }
        mirror_.setWanted(activeSim_.active && activeSim_.iniBuildsKeys && (fd_ >= 0 || panelOpen));
        if (mirror_.takeChanged()) nextRefresh_ = Clock::time_point{};

        // the weather: asked for only while its page is on screen
        WeatherResult weatherResult;
        if (weather_.take(weatherResult)) {
            pages_.setWeather(weatherResult);
            nextWeather_ = now + (weatherResult.status == WeatherStatus::Ok || weatherResult.status == WeatherStatus::NoReport ? kWeatherInterval : kWeatherRetry);
            nextRefresh_ = Clock::time_point{};
        }
        if (!activeSim_.active && !sleeping_ && pages_.wantsWeather() && !pages_.homeAirport().empty() && (pages_.takeWeatherRequest() || now >= nextWeather_)) {
            WeatherResult loading;
            loading.status = WeatherStatus::Loading;
            pages_.setWeather(loading);
            weather_.fetch(pages_.homeAirport());
            nextWeather_ = now + kWeatherRetry;
            nextRefresh_ = Clock::time_point{};
        }

        if (now >= nextRefresh_) {
            renderScreen(now);
            applyLights(false);
            const int refresh = sleeping_ ? 0 : avionicsMode() ? kAvionicsRefreshMs : activeSim_.active ? 0 : pages_.refreshMs();
            nextRefresh_ = refresh > 0 ? now + std::chrono::milliseconds(refresh) : Clock::time_point::max();
        }
        // BRT or DIM held down: a step every kRepeatEvery after kRepeatDelay
        const bool brtHeld = keysDown_.test(static_cast<size_t>(Key::Brt)), dimHeld = keysDown_.test(static_cast<size_t>(Key::Dim));
        if (fd_ >= 0 && (brtHeld != dimHeld) && !sleeping_ && holdStart_ != Clock::time_point{}) {
            if (now >= nextRepeat_) {
                stepBrightness(brtHeld);
                nextRepeat_ = now + kRepeatEvery;
            }
        } else {
            holdStart_ = Clock::time_point{};
        }
        applyLights(false);  // the room light changes on its own: only a changed level is sent
        applyFont();
        sendScreen(now);

        // Sleep until something is due.
        auto until = nextRefresh_;
        if (fd_ >= 0 && (!sentValid_ || !(sent_ == desired_))) until = std::min(until, nextSend_);
        if (fd_ < 0 && !wanted.empty()) until = std::min(until, retryOpenAt_);
        if (canSleep && !sleeping_) until = std::min(until, sleepAt);
        if (holdStart_ != Clock::time_point{}) until = std::min(until, nextRepeat_);
        if (fd_ >= 0 && active_.autoBrightness) until = std::min(until, now + std::chrono::seconds(1));  // the room light, once a second
        if (!activeSim_.active && !sleeping_ && pages_.wantsWeather() && !pages_.homeAirport().empty()) until = std::min(until, std::max(nextWeather_, Clock::now() + std::chrono::seconds(1)));
        if (activeSim_.active && activeSim_.simBridge && !link_.connected()) until = std::min(until, now + std::chrono::seconds(1));
        const auto waitMs = std::chrono::duration_cast<std::chrono::milliseconds>(until - Clock::now()).count();
        const int timeout = static_cast<int>(std::clamp<long long>(waitMs, 0, 60000));

        pollfd pfds[4];
        nfds_t count = 0;
        pfds[count++] = {wakeFd_, POLLIN, 0};
        const int hidIndex = fd_ >= 0 ? static_cast<int>(count) : -1;
        if (fd_ >= 0) pfds[count++] = {fd_, POLLIN, 0};
        const int linkIndex = link_.fd() >= 0 ? static_cast<int>(count) : -1;
        if (link_.fd() >= 0) pfds[count++] = {link_.fd(), link_.pollEvents(), 0};
        if (progress_.fd() >= 0) pfds[count++] = {progress_.fd(), progress_.events(), 0};
        const int rc = ::poll(pfds, count, timeout);
        if (rc > 0 && (pfds[0].revents & POLLIN)) {
            uint64_t drained;
            const ssize_t n = ::read(wakeFd_, &drained, sizeof drained);
            (void)n;
        }
        if (hidIndex >= 0 && pfds[hidIndex].revents) {
            if (pfds[hidIndex].revents & (POLLERR | POLLHUP | POLLNVAL)) {
                closeDevice("unplugged");
                retryOpenAt_ = Clock::now() + kReopenDelay;
            } else {
                readDevice();
                // The sensors report without pause: wait a moment so that they cost a few wake-ups a second, not hundreds.
                if (fd_ >= 0) {
                    timespec ts{0, kReadCoalesceMs * 1000000L};
                    nanosleep(&ts, nullptr);
                }
            }
        }
        link_.process(linkIndex >= 0 ? pfds[linkIndex].revents : 0);
    }
    if (avionicsWatched_ && hooks_.watchCodes) hooks_.watchCodes({});
    blankDevice();
    closeDevice("stopped");
    link_.setWanted(false);
    mediaControl_.close();
    systemInfo_.release();
    weather_.cancel();
    progress_.setWanted(false);
    std::lock_guard<std::mutex> lock(m_);
    status_.running = false;
}

}  // namespace lr::mcdu
