// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kyr4l
#include "mcdu/ScreenMirror.hpp"

#include <sys/resource.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <memory>
#include <vector>

#include "mcdu/ScreenReader.hpp"
#include "mcdu/X11Capture.hpp"
#include "util/Log.hpp"

namespace lr::mcdu {

namespace {
constexpr auto kFrame = std::chrono::milliseconds(125);
constexpr auto kFastFrame = std::chrono::milliseconds(30);   // for a moment after a key: the new page shows sooner
constexpr auto kBoost = std::chrono::milliseconds(1500);
constexpr auto kRescan = std::chrono::seconds(2);
constexpr auto kRetry = std::chrono::seconds(10);
constexpr auto kStale = std::chrono::seconds(3);
constexpr float kMinScale = 0.7f;  // smaller than this, the characters are too small to read reliably
constexpr int kMinGlyphs = 12;  // a pop-out with fewer characters than this is not an MCDU (a PFD, an ND, ...)
}  // namespace

ScreenMirror::ScreenMirror(std::function<void()> changed) : notify_(std::move(changed)) {}

ScreenMirror::~ScreenMirror() { setWanted(false); }

void ScreenMirror::setWanted(bool wanted) {
    if (wanted == thread_.joinable()) return;
    if (wanted) {
        {
            std::lock_guard<std::mutex> lock(m_);
            stop_ = false;
            have_ = false;
            hint_.clear();
        }
        thread_ = std::thread([this] { run(); });
        return;
    }
    {
        std::lock_guard<std::mutex> lock(m_);
        stop_ = true;
    }
    cv_.notify_all();
    thread_.join();
    std::lock_guard<std::mutex> lock(m_);
    have_ = false;
    hint_.clear();
}

bool ScreenMirror::screen(Screen& out) const {
    std::lock_guard<std::mutex> lock(m_);
    if (!have_ || Clock::now() - latestAt_ > kStale) return false;
    out = latest_;
    return true;
}

std::string ScreenMirror::hint() const {
    std::lock_guard<std::mutex> lock(m_);
    return hint_;
}

bool ScreenMirror::idle(std::chrono::milliseconds wait) {
    std::unique_lock<std::mutex> lock(m_);
    const auto start = Clock::now();
    // a key press (boost() wakes this) shortens a frame wait to the fast one (not the long retry waits); a stop ends it at once
    for (;;) {
        if (stop_) return false;
        auto deadline = start + wait;
        if (wait <= kFrame && Clock::now() < boostUntil_) deadline = std::min(deadline, start + std::chrono::duration_cast<Clock::duration>(kFastFrame));
        if (Clock::now() >= deadline) return true;
        cv_.wait_until(lock, deadline);
    }
}

void ScreenMirror::boost() {
    {
        std::lock_guard<std::mutex> lock(m_);
        boostUntil_ = Clock::now() + kBoost;
    }
    cv_.notify_all();
}

void ScreenMirror::publish(const Screen* screen, const char* hint) {
    bool changed;
    {
        std::lock_guard<std::mutex> lock(m_);
        changed = hint_ != hint || (screen && (!have_ || !(latest_ == *screen))) || (!screen && have_);
        hint_ = hint;
        if (screen) {
            latest_ = *screen;
            have_ = true;
            latestAt_ = Clock::now();
        } else {
            have_ = false;
        }
    }
    if (changed) {
        changed_ = true;
        if (notify_) notify_();
    }
}

void ScreenMirror::run() {
    setpriority(PRIO_PROCESS, static_cast<id_t>(syscall(SYS_gettid)), 19);  // the simulator always comes first
    GlyphBank bank;
    std::string err;
    const std::string path = defaultGlyphBankPath();
    std::string logged;
    auto note = [&logged](const std::string& what) {  // each message once, not every retry
        if (what != logged) LOG_INFO("MCDU mirror: %s", what.c_str());
        logged = what;
    };
    while (!bank.load(path, err)) {
        publish(nullptr, "NO CHARACTER SAMPLES");
        note(err);
        if (!idle(kRetry)) return;
    }
    X11Capture capture;
    const ScreenReader prepared(bank);  // the samples made ready once; every reader below is a copy sharing them
    std::unique_ptr<ScreenReader> reader;
    std::vector<uint8_t> frame, previous;
    int previousW = 0, previousH = 0;
    ReadResult last;
    uint32_t window = 0;
    auto nextScan = Clock::time_point{};
    LOG_INFO("MCDU mirror: started (%zu character samples)", bank.samples().size());
    for (;;) {
        if (!capture.isOpen() && !capture.open(err)) {
            note(err);
            publish(nullptr, "NO X11 ACCESS");
            if (!idle(kRetry)) return;
            continue;
        }
        const auto now = Clock::now();
        if (!window || now >= nextScan) {
            // several pop-outs can be open: the MCDU is the one with the most characters on the grid
            nextScan = now + kRescan;
            const auto windows = capture.popOuts();
            uint32_t best = 0;
            int bestGlyphs = 0;
            bool tooSmall = false;
            for (uint32_t w : windows) {
                if (w == window && reader) { best = w, bestGlyphs = kMinGlyphs; break; }
                int width = 0, height = 0;
                if (!capture.grab(w, frame, width, height)) continue;
                ScreenReader probe = prepared;
                const ReadResult r = probe.read(frame.data(), width, height, width * 4);
                if (r.scale < kMinScale) { tooSmall = true; continue; }
                const int sure = r.glyphs - r.unsure;
                if (sure > bestGlyphs) best = w, bestGlyphs = sure;
            }
            if (bestGlyphs < kMinGlyphs) best = 0;
            if (best != window) {
                window = best;
                reader = best ? std::make_unique<ScreenReader>(prepared) : nullptr;
                previous.clear();
                note(best ? "reading the MCDU pop-out" : "no MCDU pop-out");
            }
            if (!window) {
                publish(nullptr, tooSmall ? "POP-OUT TOO SMALL" : "POP OUT THE MCDU");
                if (!idle(kRescan)) return;
                continue;
            }
        }
        int width = 0, height = 0;
        if (!capture.grab(window, frame, width, height)) {
            window = 0;  // closed: look again
            reader.reset();
            if (!idle(kFrame)) return;
            continue;
        }
        // an unchanged picture (most of the time) is not looked at again
        const bool same = width == previousW && height == previousH && frame == previous;
        if (!same) {
            last = reader->read(frame.data(), width, height, width * 4);
            previous.swap(frame);
            previousW = width, previousH = height;
        }
        const ReadResult& r = last;
        if (r.scale < kMinScale) {
            window = 0;  // made too small while being read
            reader.reset();
            publish(nullptr, "POP-OUT TOO SMALL");
            if (!idle(kRescan)) return;
            continue;
        }
        publish(&r.screen, "");
        bool fast;
        {
            std::lock_guard<std::mutex> lock(m_);
            fast = Clock::now() < boostUntil_;
        }
        if (!idle(fast ? kFastFrame : kFrame)) return;
    }
}

}  // namespace lr::mcdu
