// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kyr4l
#pragma once
// Mutes the microphone and the voice app's own audio streams through the sound server (pactl). A keyboard shortcut only reaches the window in focus; this works whatever is
// in focus, and tells the real state. Used only while the Comms page of the MCDU is on screen.
#include <optional>
#include <string>
#include <vector>

namespace lr::desktop {

enum class AudioCommand { ToggleMic, ToggleVoice, ToggleDeafen };

struct AudioState {
    bool known = false;      // the sound server was asked at least once
    bool available = false;  // and it answered
    bool micMuted = false;   // the default microphone, for every program
    int voiceStreams = 0, soundStreams = 0;  // the voice app's microphone and playback streams: none outside a call
    bool voiceMuted = false, soundMuted = false;
};

struct StreamSummary {
    std::vector<int> indexes;
    bool allMuted = false;
    int volume = -1;  // the loudest of its streams, in percent (-1: none)
};
// The streams of one application in the JSON of `pactl --format=json list sink-inputs` (or source-outputs).
StreamSummary summarizeStreams(const std::string& json, const std::string& app);
// The first percentage in pactl's text ("Volume: front-left: 45875 /  70% / -9.29 dB, ..." -> 70); -1 when there is none.
int parseVolumePercent(const std::string& text);

struct PcSound {  // the PC's output (the default sink)
    bool known = false;
    int volume = -1;
    bool muted = false;
};

class AudioControl {
public:
    AudioState read(const std::string& app);
    void toggleMic();
    void setVoiceMuted(const std::string& app, bool muted);
    void setSoundMuted(const std::string& app, bool muted);
    // The playback streams of one program, found by any of its names (desktop entry, name, binary). toggleAppMuted() mutes them
    // all, or unmutes them all, and says which it did; nullopt when it has none (not playing).
    std::optional<bool> toggleAppMuted(const std::vector<std::string>& names);
    StreamSummary appSound(const std::vector<std::string>& names);  // its streams, whether they are muted, their volume
    // the program's volume by `delta` percent (kept within 0..100); the new volume, nullopt when it has no stream
    std::optional<int> stepAppVolume(const std::vector<std::string>& names, int delta);
    PcSound pcSound();
    std::optional<int> stepPcVolume(int delta);  // the PC's output by `delta` percent, within 0..100
    bool togglePcMute();                          // false when the sound server did not answer
};

}  // namespace lr::desktop
