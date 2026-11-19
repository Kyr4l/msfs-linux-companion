// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kyr4l
#include "desktop/MediaControl.hpp"

#include <cstring>
#include <vector>

#include "util/Isolated.hpp"

namespace lr::desktop {

namespace {
constexpr const char* kPrefix = "org.mpris.MediaPlayer2.";
constexpr const char* kPlayerPath = "/org/mpris/MediaPlayer2";
constexpr const char* kPlayerInterface = "org.mpris.MediaPlayer2.Player";
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
    return names;
}

// The player chosen with the side key, else the one that is playing, else the first one there is.
std::string MediaControl::choosePlayer(bool* playing, int* count) {
    const std::vector<std::string> names = listPlayers();
    if (count) *count = static_cast<int>(names.size());
    std::string first;
    for (const auto& n : names) {
        sdbus::Message* q = nullptr, *a = nullptr;
        sdbus::Error e;
        if (api_->message_new_method_call(bus_, &q, n.c_str(), kPlayerPath, "org.freedesktop.DBus.Properties", "Get") < 0) continue;
        api_->message_append(q, "ss", kPlayerInterface, "PlaybackStatus");
        const int got = api_->call(bus_, q, kCallTimeoutUs, &e, &a);
        api_->message_unref(q);
        api_->error_free(&e);
        if (got < 0) continue;
        if (first.empty()) first = n;
        const char* status = nullptr;
        const bool isPlaying = api_->message_read(a, "v", "s", &status) > 0 && status && std::strcmp(status, "Playing") == 0;
        api_->message_unref(a);
        if (n == pinned_) {
            *playing = isPlaying;
            return n;
        }
        if (isPlaying && pinned_.empty()) {
            *playing = true;
            return n;
        }
    }
    return first;
}

MediaInfo MediaControl::read() {
    MediaInfo info;
    if (!open()) return info;
    bool playing = false;
    int count = 0;
    player_ = choosePlayer(&playing, &count);
    if (player_.empty()) return info;
    info.present = true;
    info.playing = playing;
    info.players = count;
    info.player = player_.substr(std::strlen(kPrefix));
    if (const size_t dot = info.player.find('.'); dot != std::string::npos) info.player.resize(dot);  // "vlc.instance1234"

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
    auto property = [&](const char* name, const char* type, void* out) {
        sdbus::Message* p = nullptr, *v = nullptr;
        sdbus::Error err;
        if (api_->message_new_method_call(bus_, &p, player_.c_str(), kPlayerPath, "org.freedesktop.DBus.Properties", "Get") < 0) return;
        api_->message_append(p, "ss", kPlayerInterface, name);
        const int rc = api_->call(bus_, p, kCallTimeoutUs, &err, &v);
        api_->message_unref(p);
        api_->error_free(&err);
        if (rc < 0) return;
        api_->message_read(v, "v", type, out);
        api_->message_unref(v);
    };
    int shuffle = 0;
    property("Shuffle", "b", &shuffle);
    const char* loop = nullptr;
    sdbus::Message* p = nullptr, *v = nullptr;
    sdbus::Error err;
    if (api_->message_new_method_call(bus_, &p, player_.c_str(), kPlayerPath, "org.freedesktop.DBus.Properties", "Get") >= 0) {
        api_->message_append(p, "ss", kPlayerInterface, "LoopStatus");
        if (api_->call(bus_, p, kCallTimeoutUs, &err, &v) >= 0) {
            if (api_->message_read(v, "v", "s", &loop) > 0 && loop) repeat_ = std::strcmp(loop, "None") != 0;
            api_->message_unref(v);
        }
        api_->message_unref(p);
        api_->error_free(&err);
    }
    shuffle_ = shuffle != 0;
    info.shuffle = shuffle_;
    info.repeat = repeat_;
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
    if (cmd == MediaCommand::NextPlayer) {
        const std::vector<std::string> names = listPlayers();
        if (names.empty()) return;
        size_t next = 0;
        for (size_t i = 0; i < names.size(); ++i)
            if (names[i] == player_) next = (i + 1) % names.size();
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
