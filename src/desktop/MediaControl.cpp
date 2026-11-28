// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kyr4l
#include "desktop/MediaControl.hpp"

#include <algorithm>
#include <cstring>
#include <strings.h>
#include <vector>

#include "util/Isolated.hpp"

namespace lr::desktop {

namespace {
constexpr const char* kPrefix = "org.mpris.MediaPlayer2.";
constexpr const char* kPlayerPath = "/org/mpris/MediaPlayer2";
constexpr const char* kPlayerInterface = "org.mpris.MediaPlayer2.Player";
constexpr const char* kRootInterface = "org.mpris.MediaPlayer2";
// KDE's browser integration speaks for the browser a second time: dropped when the browser has a player of its own
constexpr const char* kBrowserIntegration = "org.mpris.MediaPlayer2.plasma-browser-integration";
constexpr uint64_t kCallTimeoutUs = 150'000;  // a player that hangs must not hold up the MCDU
constexpr size_t kMaxPlayers = 16;
}  // namespace

bool MediaControl::open() {
    if (bus_) return true;
    if (isolatedRun()) return false;
    api_ = sdbus::api();
    if (!api_) return false;
    if (api_->open_user(&bus_) < 0) { bus_ = nullptr; return false; }
    return true;
}

void MediaControl::close() {
    if (bus_) api_->flush_close_unref(bus_);
    bus_ = nullptr;
    player_.clear();
}

std::vector<std::string> MediaControl::listPlayers() {
    std::vector<std::string> names;
    sdbus::Message* m = nullptr, *reply = nullptr;
    sdbus::Error err;
    if (api_->message_new_method_call(bus_, &m, "org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus", "ListNames") < 0) return names;
    const int r = api_->call(bus_, m, kCallTimeoutUs, &err, &reply);
    api_->message_unref(m);
    api_->error_free(&err);
    if (r < 0) return names;
    if (api_->message_enter_container(reply, 'a', "s") > 0) {
        const char* name = nullptr;
        while (api_->message_read(reply, "s", &name) > 0)
            if (std::strncmp(name, kPrefix, std::strlen(kPrefix)) == 0 && names.size() < kMaxPlayers) names.emplace_back(name);
        api_->message_exit_container(reply);
    }
    api_->message_unref(reply);
    // The same browser twice: KDE's browser integration and the browser's own player. The integration follows the tab that plays
    // media; the browser's own can keep a tab that stopped long ago (Firefox kept "Scenes | Stash" while YouTube played). Keep the
    // integration, unless only the browser's own is playing.
    const auto it = std::find(names.begin(), names.end(), kBrowserIntegration);
    if (it != names.end()) {
        std::string entry;
        if (getString(*it, kRootInterface, "DesktopEntry", entry) && !entry.empty()) {
            const std::string integration = *it;
            std::string status;
            const bool integrationPlays = getString(integration, kPlayerInterface, "PlaybackStatus", status) && status == "Playing";
            std::vector<std::string> twins;
            for (const auto& n : names) {
                std::string other;
                if (n != integration && getString(n, kRootInterface, "DesktopEntry", other) && other == entry) twins.push_back(n);
            }
            bool twinPlays = false;
            for (const auto& t : twins) twinPlays = twinPlays || (getString(t, kPlayerInterface, "PlaybackStatus", status) && status == "Playing");
            if (!twins.empty() && twinPlays && !integrationPlays) names.erase(std::find(names.begin(), names.end(), integration));
            else
                for (const auto& t : twins) names.erase(std::find(names.begin(), names.end(), t));
        }
    }
    return names;
}

bool MediaControl::getString(const std::string& dest, const char* iface, const char* name, std::string& out) {
    sdbus::Message* q = nullptr, *a = nullptr;
    sdbus::Error e;
    if (api_->message_new_method_call(bus_, &q, dest.c_str(), kPlayerPath, "org.freedesktop.DBus.Properties", "Get") < 0) return false;
    api_->message_append(q, "ss", iface, name);
    const int got = api_->call(bus_, q, kCallTimeoutUs, &e, &a);
    api_->message_unref(q);
    api_->error_free(&e);
    if (got < 0) return false;
    const char* v = nullptr;
    const bool ok = api_->message_read(a, "v", "s", &v) > 0 && v;
    if (ok) out = v;
    api_->message_unref(a);
    return ok;
}

bool MediaControl::getBool(const std::string& dest, const char* iface, const char* name, bool& out) {
    sdbus::Message* q = nullptr, *a = nullptr;
    sdbus::Error e;
    if (api_->message_new_method_call(bus_, &q, dest.c_str(), kPlayerPath, "org.freedesktop.DBus.Properties", "Get") < 0) return false;
    api_->message_append(q, "ss", iface, name);
    const int got = api_->call(bus_, q, kCallTimeoutUs, &e, &a);
    api_->message_unref(q);
    api_->error_free(&e);
    if (got < 0) return false;
    int v = 0;
    const bool ok = api_->message_read(a, "v", "b", &v) > 0;
    if (ok) out = v != 0;
    api_->message_unref(a);
    return ok;
}

// The player chosen with the side key, else the one that is playing, else the first one there is.
std::string MediaControl::choosePlayer(bool* playing, int* count, int* index) {
    const std::vector<std::string> names = listPlayers();
    if (count) *count = static_cast<int>(names.size());
    std::string first;
    int firstAt = 0, at = 0;
    for (const auto& n : names) {
        ++at;
        sdbus::Message* q = nullptr, *a = nullptr;
        sdbus::Error e;
        if (api_->message_new_method_call(bus_, &q, n.c_str(), kPlayerPath, "org.freedesktop.DBus.Properties", "Get") < 0) continue;
        api_->message_append(q, "ss", kPlayerInterface, "PlaybackStatus");
        const int got = api_->call(bus_, q, kCallTimeoutUs, &e, &a);
        api_->message_unref(q);
        api_->error_free(&e);
        if (got < 0) continue;
        if (first.empty()) first = n, firstAt = at;
        const char* status = nullptr;
        const bool isPlaying = api_->message_read(a, "v", "s", &status) > 0 && status && std::strcmp(status, "Playing") == 0;
        api_->message_unref(a);
        if (n == pinned_) {
            *playing = isPlaying;
            if (index) *index = at;
            return n;
        }
        if (isPlaying && pinned_.empty()) {
            *playing = true;
            if (index) *index = at;
            return n;
        }
    }
    if (index) *index = firstAt;
    return first;
}

MediaInfo MediaControl::read() {
    MediaInfo info;
    if (!open()) return info;
    bool playing = false;
    int count = 0;
    int page = 0;
    player_ = choosePlayer(&playing, &count, &page);
    info.page = page;
    if (player_.empty()) return info;
    info.present = true;
    info.playing = playing;
    info.players = count;
    // the name the player gives itself; else its bus name without the instance ("vlc.instance1234"), and without a reversed domain
    // ("de.haeckerfelix.Shortwave": the last part)
    if (!getString(player_, kRootInterface, "Identity", info.player) || info.player.empty()) {
        std::string bus = player_.substr(std::strlen(kPrefix));
        if (const size_t inst = bus.find(".instance"); inst != std::string::npos) bus.resize(inst);
        if (const size_t dot = bus.rfind('.'); dot != std::string::npos) bus = bus.substr(dot + 1);
        info.player = bus;
    }
    // without the maker's name in front ("Mozilla firefox" -> "firefox"): the MCDU line is short
    for (const char* maker : {"mozilla ", "gnome ", "kde "}) {
        const size_t n = std::strlen(maker);
        if (info.player.size() > n && strncasecmp(info.player.c_str(), maker, n) == 0) {
            info.player.erase(0, n);
            break;
        }
    }
    getString(player_, kRootInterface, "DesktopEntry", info.app);
    // MPRIS makes CanGoNext and CanGoPrevious mandatory: a player that does not answer is taken as able to
    info.canNext = info.canPrevious = true;
    getBool(player_, kPlayerInterface, "CanGoNext", info.canNext);
    getBool(player_, kPlayerInterface, "CanGoPrevious", info.canPrevious);

    sdbus::Message* q = nullptr, *a = nullptr;
    sdbus::Error e;
    if (api_->message_new_method_call(bus_, &q, player_.c_str(), kPlayerPath, "org.freedesktop.DBus.Properties", "Get") < 0) return info;
    api_->message_append(q, "ss", kPlayerInterface, "Metadata");
    const int got = api_->call(bus_, q, kCallTimeoutUs, &e, &a);
    api_->message_unref(q);
    api_->error_free(&e);
    if (got < 0) return info;
    if (api_->message_enter_container(a, 'v', "a{sv}") > 0) {
        if (api_->message_enter_container(a, 'a', "{sv}") > 0) {
            while (api_->message_enter_container(a, 'e', "sv") > 0) {
                const char* key = nullptr;
                if (api_->message_read(a, "s", &key) > 0 && key) {
                    if (std::strcmp(key, "xesam:title") == 0) {
                        const char* v = nullptr;
                        if (api_->message_read(a, "v", "s", &v) > 0 && v) info.title = v;
                        else api_->message_skip(a, "v");
                    } else if (std::strcmp(key, "xesam:artist") == 0 && api_->message_enter_container(a, 'v', "as") > 0) {
                        if (api_->message_enter_container(a, 'a', "s") > 0) {
                            const char* v = nullptr;
                            if (api_->message_read(a, "s", &v) > 0 && v) info.artist = v;
                            while (api_->message_read(a, "s", &v) > 0) {}
                            api_->message_exit_container(a);
                        }
                        api_->message_exit_container(a);
                    } else {
                        api_->message_skip(a, "v");
                    }
                }
                api_->message_exit_container(a);
            }
            api_->message_exit_container(a);
        }
        api_->message_exit_container(a);
    }
    api_->message_unref(a);
    bool shuffle = false;
    info.hasShuffle = getBool(player_, kPlayerInterface, "Shuffle", shuffle);
    const char* loop = nullptr;
    sdbus::Message* p = nullptr, *v = nullptr;
    sdbus::Error err;
    if (api_->message_new_method_call(bus_, &p, player_.c_str(), kPlayerPath, "org.freedesktop.DBus.Properties", "Get") >= 0) {
        api_->message_append(p, "ss", kPlayerInterface, "LoopStatus");
        if (api_->call(bus_, p, kCallTimeoutUs, &err, &v) >= 0) {
            if (api_->message_read(v, "v", "s", &loop) > 0 && loop) {
                repeat_ = std::strcmp(loop, "None") != 0;
                info.hasRepeat = true;
            }
            api_->message_unref(v);
        }
        api_->message_unref(p);
        api_->error_free(&err);
    }
    shuffle_ = shuffle;
    info.shuffle = shuffle_;
    info.repeat = repeat_;
    // shuffle and repeat mean something only for a list of tracks: a stream that cannot skip (a radio) lists them and ignores them
    const bool list = info.canNext || info.canPrevious;
    info.hasShuffle = info.hasShuffle && list;
    info.hasRepeat = info.hasRepeat && list;
    return info;
}

void MediaControl::setProperty(const char* name, const char* type, const void* value) {
    sdbus::Message* q = nullptr;
    if (api_->message_new_method_call(bus_, &q, player_.c_str(), kPlayerPath, "org.freedesktop.DBus.Properties", "Set") < 0) return;
    api_->message_append(q, "ss", kPlayerInterface, name);
    if (type[0] == 'b') api_->message_append(q, "v", "b", *static_cast<const int*>(value));
    else api_->message_append(q, "v", "s", static_cast<const char*>(value));
    api_->send(bus_, q, nullptr);
    api_->message_unref(q);
    api_->flush(bus_);
}

void MediaControl::command(MediaCommand cmd) {
    if (!open()) return;
    bool playing = false;
    switch (cmd) {  // the sound server's job (AudioControl), not the player's
        case MediaCommand::Mute: case MediaCommand::AppVolumeUp: case MediaCommand::AppVolumeDown:
        case MediaCommand::PcVolumeUp: case MediaCommand::PcVolumeDown: case MediaCommand::PcMute: return;
        default: break;
    }
    if (cmd == MediaCommand::NextPlayer || cmd == MediaCommand::PrevPlayer) {
        const std::vector<std::string> names = listPlayers();
        if (names.empty()) return;
        const size_t n = names.size(), step = cmd == MediaCommand::NextPlayer ? 1 : n - 1;
        size_t next = 0;
        for (size_t i = 0; i < n; ++i)
            if (names[i] == player_) next = (i + step) % n;
        pinned_ = names[next];
        player_ = pinned_;
        return;
    }
    if (player_.empty()) player_ = choosePlayer(&playing, nullptr);
    if (player_.empty()) return;
    if (cmd == MediaCommand::Shuffle) {
        const int on = shuffle_ ? 0 : 1;
        setProperty("Shuffle", "b", &on);
        return;
    }
    if (cmd == MediaCommand::Repeat) {
        setProperty("LoopStatus", "s", repeat_ ? "None" : "Playlist");
        return;
    }
    const char* member = cmd == MediaCommand::Previous ? "Previous" : cmd == MediaCommand::Next ? "Next" : "PlayPause";
    sdbus::Message* q = nullptr;
    if (api_->message_new_method_call(bus_, &q, player_.c_str(), kPlayerPath, kPlayerInterface, member) < 0) return;
    api_->send(bus_, q, nullptr);
    api_->message_unref(q);
    api_->flush(bus_);
}

}  // namespace lr::desktop
