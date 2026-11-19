// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kyr4l
#pragma once
// Mutes the microphone and the voice app's own audio streams through the sound server (pactl). A keyboard shortcut only reaches the window in focus; this works whatever is
// in focus, and tells the real state. Used only while the Comms page of the MCDU is on screen.
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
};
// The streams of one application in the JSON of `pactl --format=json list sink-inputs` (or source-outputs).
StreamSummary summarizeStreams(const std::string& json, const std::string& app);

class AudioControl {
public:
    AudioState read(const std::string& app);
    void toggleMic();
    void setVoiceMuted(const std::string& app, bool muted);
    void setSoundMuted(const std::string& app, bool muted);
};

}  // namespace lr::desktop
