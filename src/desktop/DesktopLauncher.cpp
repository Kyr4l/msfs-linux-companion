// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kyr4l
#include "desktop/DesktopLauncher.hpp"

#include <cctype>
#include <cstdio>

#include "desktop/SdBus.hpp"
#include "util/Isolated.hpp"

namespace lr::desktop {

namespace {
constexpr uint64_t kCallTimeoutUs = 3'000'000;  // KRunner may have to start first

std::string encoded(const std::string& text) {
    std::string out;
    for (unsigned char c : text) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            out.push_back(static_cast<char>(c));
        } else {
            char hex[4];
            snprintf(hex, sizeof hex, "%%%02X", c);
            out += hex;
        }
    }
    return out;
}

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// One call on a connection of its own: these are rare, and the thread that makes them keeps no bus open.
template <typename Fill>
bool callOnce(const char* destination, const char* path, const char* interface, const char* member, Fill&& fill) {
    if (isolatedRun()) return false;
    const sdbus::Api* api = sdbus::api();
    if (!api) return false;
    sdbus::Bus* bus = nullptr;
    if (api->open_user(&bus) < 0) return false;
    sdbus::Message* m = nullptr;
    bool ok = false;
    if (api->message_new_method_call(bus, &m, destination, path, interface, member) >= 0) {
        fill(*api, m);
        sdbus::Error err;
        ok = api->call(bus, m, kCallTimeoutUs, &err, nullptr) >= 0;
        api->error_free(&err);
        api->message_unref(m);
    }
    api->flush_close_unref(bus);
    return ok;
}
}  // namespace

bool showInKRunner(const std::string& text) {
    return callOnce("org.kde.krunner", "/App", "org.kde.krunner.App", "query", [&](const sdbus::Api& a, sdbus::Message* m) { a.message_append(m, "s", text.c_str()); });
}

bool openWebAddress(const std::string& url) {
    return callOnce("org.freedesktop.portal.Desktop", "/org/freedesktop/portal/desktop", "org.freedesktop.portal.OpenURI", "OpenURI", [&](const sdbus::Api& a, sdbus::Message* m) {
        a.message_append(m, "ss", "", url.c_str());
        a.message_open_container(m, 'a', "{sv}");
        a.message_close_container(m);
    });
}

std::string searchAddress(int kind, const std::string& text) {
    const std::string q = encoded(lower(text));
    switch (kind) {
        case 0: return "https://www.google.com/search?q=" + q;
        case 1: return "https://www.youtube.com/results?search_query=" + q;
        case 2: return "https://en.wikipedia.org/w/index.php?search=" + q;
        case 3: return "https://skyvector.com/airport/" + encoded(text);
        case 4: return "https://store.steampowered.com/search/?term=" + q;
        default: return {};
    }
}

}  // namespace lr::desktop
