// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kyr4l
#pragma once
// The media player of the desktop (MPRIS over the session bus). Used from one thread; the bus is only open while the media page is on screen.
#include <string>
#include <vector>

#include "desktop/SdBus.hpp"

namespace lr::desktop {

struct MediaInfo {
    bool present = false, playing = false, shuffle = false, repeat = false;
    // what the player can do: a radio stream cannot skip, and has no shuffle or repeat even when it lists them
    bool canPrevious = false, canNext = false, hasShuffle = false, hasRepeat = false;
    int players = 0;
    int page = 0;  // which of them this is, from 1 (the Airbus pages show "page/players")
    std::string player, title, artist;  // player: the name it gives itself ("Shortwave", "Firefox")
    std::string app;     // its desktop entry ("firefox", "de.haeckerfelix.Shortwave"): how its sound streams are found
    // filled in by the MCDU service from the sound server, not by read():
    bool muted = false;   // its sound streams are muted
    int appVolume = -1;   // their volume in percent (-1: none, the player makes no sound)
    int pcVolume = -1;    // the PC's output
    bool pcMuted = false;
    bool operator==(const MediaInfo&) const = default;
};

// Mute and AppVolume*: the player's program only; Pc*: the PC's output. Those go to the sound server (the MCDU service), not to the player.
enum class MediaCommand { Previous, PlayPause, Next, Shuffle, Repeat, NextPlayer, PrevPlayer, Mute, AppVolumeUp, AppVolumeDown, PcVolumeUp, PcVolumeDown, PcMute };

class MediaControl {
public:
    MediaControl() = default;
    ~MediaControl() { close(); }
    MediaControl(const MediaControl&) = delete;
    MediaControl& operator=(const MediaControl&) = delete;

    MediaInfo read();                // every call asks the player: at most a fraction of a second
    void command(MediaCommand cmd);
    void close();                    // gives the connection back
    void unpin() { pinned_.clear(); }  // the page opens again: on the player that is playing

private:
    bool open();
    std::vector<std::string> listPlayers();
    std::string choosePlayer(bool* playing, int* count, int* index = nullptr);
    void setProperty(const char* name, const char* type, const void* value);
    bool getString(const std::string& dest, const char* iface, const char* name, std::string& out);
    bool getBool(const std::string& dest, const char* iface, const char* name, bool& out);
    const sdbus::Api* api_ = nullptr;
    sdbus::Bus* bus_ = nullptr;
    std::string player_, pinned_;  // pinned_: the player chosen with the side key
    bool shuffle_ = false, repeat_ = false;
};

}  // namespace lr::desktop
