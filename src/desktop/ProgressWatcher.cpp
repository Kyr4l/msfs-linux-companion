// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kyr4l
#include "desktop/ProgressWatcher.hpp"

#include <cstdlib>
#include <cstring>

#include "util/Isolated.hpp"
#include "util/Log.hpp"

namespace lr::desktop {

namespace {
constexpr auto kRetry = std::chrono::seconds(30);
constexpr uint64_t kCallTimeoutUs = 2'000'000;
constexpr int kMaxMessagesPerCall = 64;

constexpr const char* kRules[] = {
    "type='method_call',interface='org.freedesktop.Notifications',member='Notify'",
    "type='signal',interface='com.canonical.Unity.LauncherEntry',member='Update'",
    "type='method_call',interface='org.kde.JobViewServerV2',member='requestView'",
    "type='method_call',interface='org.kde.JobViewServer',member='requestView'",
    "type='method_call',interface='org.kde.JobViewV3'",
    "type='method_call',interface='org.kde.JobViewV2'",
};

// The number inside a variant, whichever integer type the sender chose. False: it is something else, and it was skipped.
bool readVariantNumber(const sdbus::Api& a, sdbus::Message* m, double& out) {
    char type = 0;
    const char* contents = nullptr;
    if (a.message_peek_type(m, &type, &contents) <= 0 || type != 'v' || !contents) return false;
    int ok = 0;
    switch (contents[0]) {
        case 'y': { uint8_t v = 0; ok = a.message_read(m, "v", "y", &v); out = v; break; }
        case 'n': { int16_t v = 0; ok = a.message_read(m, "v", "n", &v); out = v; break; }
        case 'q': { uint16_t v = 0; ok = a.message_read(m, "v", "q", &v); out = v; break; }
        case 'i': { int32_t v = 0; ok = a.message_read(m, "v", "i", &v); out = v; break; }
        case 'u': { uint32_t v = 0; ok = a.message_read(m, "v", "u", &v); out = v; break; }
        case 'x': { int64_t v = 0; ok = a.message_read(m, "v", "x", &v); out = static_cast<double>(v); break; }
        case 't': { uint64_t v = 0; ok = a.message_read(m, "v", "t", &v); out = static_cast<double>(v); break; }
        case 'd': { double v = 0; ok = a.message_read(m, "v", "d", &v); out = v; break; }
        default: a.message_skip(m, "v"); return false;
    }
    return ok > 0;
}

// Calls `field` for each entry of an a{sv}. It reads the value or leaves it: what it leaves is skipped here.
template <typename Field>
void readDictionary(const sdbus::Api& a, sdbus::Message* m, Field&& field) {
    if (a.message_enter_container(m, 'a', "{sv}") <= 0) return;
    while (a.message_enter_container(m, 'e', "sv") > 0) {
        const char* key = nullptr;
        if (a.message_read(m, "s", &key) > 0 && key && !field(key)) a.message_skip(m, "v");
        a.message_exit_container(m);
    }
    a.message_exit_container(m);
}
}  // namespace

void ProgressWatcher::setWanted(bool wanted) {
    if (wanted == wanted_) return;
    wanted_ = wanted;
    if (!wanted) disconnect();
    nextAttempt_ = Clock::time_point{};
}

int ProgressWatcher::fd() const { return bus_ ? api_->get_fd(bus_) : -1; }

short ProgressWatcher::events() const {
    if (!bus_) return 0;
    const int e = api_->get_events(bus_);
    return e < 0 ? 0 : static_cast<short>(e);
}

void ProgressWatcher::disconnect() {
    if (bus_) api_->flush_close_unref(bus_);
    bus_ = nullptr;
}

bool ProgressWatcher::connect() {
    if (isolatedRun()) return false;
    api_ = sdbus::api();
    if (!api_) return false;
    std::string address;
    if (const char* env = std::getenv("DBUS_SESSION_BUS_ADDRESS"); env && *env) address = env;
    else if (const char* run = std::getenv("XDG_RUNTIME_DIR"); run && *run) address = std::string("unix:path=") + run + "/bus";
    else return false;

    sdbus::Bus* bus = nullptr;
    if (api_->bus_new(&bus) < 0) return false;
    if (api_->set_address(bus, address.c_str()) < 0 || api_->set_monitor(bus, 1) < 0 || api_->set_bus_client(bus, 1) < 0 || api_->start(bus) < 0) {
        api_->flush_close_unref(bus);
        return false;
    }
    sdbus::Message* m = nullptr;
    if (api_->message_new_method_call(bus, &m, "org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus.Monitoring", "BecomeMonitor") < 0) {
        api_->flush_close_unref(bus);
        return false;
    }
    api_->message_open_container(m, 'a', "s");
    for (const char* rule : kRules) api_->message_append(m, "s", rule);
    api_->message_close_container(m);
    api_->message_append(m, "u", 0u);
    sdbus::Error err;
    const int rc = api_->call(bus, m, kCallTimeoutUs, &err, nullptr);
    api_->message_unref(m);
    if (rc < 0) LOG_INFO("Progress: the session bus does not allow watching (%s)", err.name ? err.name : "error");
    api_->error_free(&err);
    if (rc < 0) {
        api_->flush_close_unref(bus);
        return false;
    }
    bus_ = bus;
    return true;
}

void ProgressWatcher::process() {
    if (!wanted_) return;
    if (!bus_) {
        const auto now = Clock::now();
        if (now < nextAttempt_) return;
        nextAttempt_ = now + kRetry;
        if (!connect()) return;
    }
    for (int i = 0; i < kMaxMessagesPerCall; ++i) {
        sdbus::Message* m = nullptr;
        const int r = api_->process(bus_, &m);
        if (r < 0) {
            disconnect();
            return;
        }
        if (m) {
            const bool gone = api_->message_is_signal(m, "org.freedesktop.DBus.Local", "Disconnected") > 0;
            if (!gone) handle(m);
            api_->message_unref(m);
            if (gone) {
                disconnect();
                return;
            }
        }
        if (r == 0) break;
    }
}

void ProgressWatcher::handle(sdbus::Message* m) {
    const sdbus::Api& a = *api_;
    const auto now = Clock::now();
    const char* senderText = a.message_get_sender(m), *pathText = a.message_get_path(m);
    const std::string sender = senderText ? senderText : "", path = pathText ? pathText : "";

    if (a.message_is_method_call(m, "org.freedesktop.Notifications", "Notify") > 0) {
        const char* app = nullptr, *icon = nullptr, *summary = nullptr, *body = nullptr;
        uint32_t replaces = 0;
        if (a.message_read(m, "susss", &app, &replaces, &icon, &summary, &body) <= 0 || !app) return;
        a.message_skip(m, "as");
        double value = -1;
        readDictionary(a, m, [&](const char* key) { return std::strcmp(key, "value") == 0 && readVariantNumber(a, m, value); });
        if (value >= 0) tracker_.notification(sender, app, static_cast<int>(value), now);
    } else if (a.message_is_signal(m, "com.canonical.Unity.LauncherEntry", "Update") > 0) {
        const char* uri = nullptr;
        if (a.message_read(m, "s", &uri) <= 0 || !uri) return;
        double progress = -1;
        int visible = -1;
        readDictionary(a, m, [&](const char* key) {
            if (std::strcmp(key, "progress") == 0) return readVariantNumber(a, m, progress);
            if (std::strcmp(key, "progress-visible") == 0) return a.message_read(m, "v", "b", &visible) > 0;
            return false;
        });
        if (visible == 0) tracker_.launcher(sender, uri, -1, false, now);
        else if (visible > 0 || progress >= 0) tracker_.launcher(sender, uri, progress, true, now);
    } else if (a.message_is_method_call(m, "org.kde.JobViewServerV2", "requestView") > 0 || a.message_is_method_call(m, "org.kde.JobViewServer", "requestView") > 0) {
        const char* application = nullptr;
        if (a.message_read(m, "s", &application) > 0 && application) tracker_.jobRequested(sender, application);
    } else if (a.message_is_method_call(m, "org.kde.JobViewV3", "update") > 0) {
        double percent = -1;
        std::string title;
        readDictionary(a, m, [&](const char* key) {
            if (std::strcmp(key, "percent") == 0) return readVariantNumber(a, m, percent);
            if (std::strcmp(key, "title") == 0) {
                const char* text = nullptr;
                if (a.message_read(m, "v", "s", &text) <= 0) return false;
                if (text) title = text;
                return true;
            }
            return false;
        });
        tracker_.jobUpdate(sender, path, title, static_cast<int>(percent), now);
    } else if (a.message_is_method_call(m, "org.kde.JobViewV2", "setPercent") > 0) {
        uint32_t percent = 0;
        if (a.message_read(m, "u", &percent) > 0) tracker_.jobUpdate(sender, path, "", static_cast<int>(percent), now);
    } else if (a.message_is_method_call(m, "org.kde.JobViewV3", "terminate") > 0 || a.message_is_method_call(m, "org.kde.JobViewV2", "terminate") > 0) {
        tracker_.jobEnded(sender, path);
    }
}

}  // namespace lr::desktop
