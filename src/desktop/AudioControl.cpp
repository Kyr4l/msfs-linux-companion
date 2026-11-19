// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kyr4l
#include "desktop/AudioControl.hpp"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstring>

#include <fcntl.h>
#include <poll.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#include <nlohmann/json.hpp>

#include "util/Isolated.hpp"

extern char** environ;

namespace lr::desktop {

namespace {
using json = nlohmann::json;

constexpr int kTimeoutMs = 1500;           // a sound server that hangs must not hold up the MCDU
constexpr size_t kMaxOutput = 512u << 10;  // far more than any list of streams

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// Runs pactl and collects what it prints. False when it is missing, fails or takes too long; it never outlives this call.
bool pactl(const std::vector<std::string>& args, std::string* output) {
    if (isolatedRun()) return false;  // a test run never touches the real sound server
    int fds[2];
    if (pipe2(fds, O_CLOEXEC) != 0) return false;

    std::vector<std::string> text{"pactl"};
    text.insert(text.end(), args.begin(), args.end());
    std::vector<char*> argv;
    for (auto& a : text) argv.push_back(a.data());
    argv.push_back(nullptr);

    // always the same language and number format, whatever the desktop's
    std::vector<std::string> envText{"LC_ALL=C"};
    for (char** e = environ; e && *e; ++e)
        if (std::strncmp(*e, "LC_ALL=", 7) != 0) envText.emplace_back(*e);
    std::vector<char*> envp;
    for (auto& e : envText) envp.push_back(e.data());
    envp.push_back(nullptr);

    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, fds[1], STDOUT_FILENO);
    posix_spawn_file_actions_addopen(&actions, STDIN_FILENO, "/dev/null", O_RDONLY, 0);
    posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, "/dev/null", O_WRONLY, 0);
    pid_t pid = -1;
    const int spawned = posix_spawnp(&pid, "pactl", &actions, nullptr, argv.data(), envp.data());
    posix_spawn_file_actions_destroy(&actions);
    ::close(fds[1]);
    if (spawned != 0) {
        ::close(fds[0]);
        return false;
    }

    bool ok = true;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(kTimeoutMs);
    char buf[8192];
    for (;;) {
        const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()).count();
        if (left <= 0) { ok = false; break; }
        pollfd p{fds[0], POLLIN, 0};
        const int rc = ::poll(&p, 1, static_cast<int>(left));
        if (rc < 0 && errno == EINTR) continue;
        if (rc <= 0) { ok = false; break; }
        const ssize_t n = ::read(fds[0], buf, sizeof buf);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) break;
        if (output) {
            if (output->size() + static_cast<size_t>(n) > kMaxOutput) { ok = false; break; }
            output->append(buf, static_cast<size_t>(n));
        }
    }
    ::close(fds[0]);
    if (!ok) ::kill(pid, SIGKILL);
    int status = 0;
    // it has closed its output or was killed: this does not wait for long
    while (::waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
    return ok && WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

StreamSummary streamsOf(const char* kind, const std::string& app, bool* answered) {
    std::string text;
    if (!pactl({"--format=json", "list", kind}, &text)) {
        *answered = false;
        return {};
    }
    return summarizeStreams(text, app);
}

void setMuted(const char* command, const char* kind, const std::string& app, bool muted) {
    bool answered = true;
    for (int index : streamsOf(kind, app, &answered).indexes) pactl({command, std::to_string(index), muted ? "1" : "0"}, nullptr);
}
}  // namespace

StreamSummary summarizeStreams(const std::string& text, const std::string& app) {
    StreamSummary out;
    const json doc = json::parse(text, nullptr, false);
    if (!doc.is_array() || app.empty()) return out;
    const std::string wanted = lower(app);
    bool allMuted = true;
    for (const json& stream : doc) {
        if (!stream.is_object() || !stream.contains("index") || !stream["index"].is_number_integer()) continue;
        const auto props = stream.find("properties");
        if (props == stream.end() || !props->is_object()) continue;
        auto property = [&](const char* key) {
            const auto it = props->find(key);
            return it != props->end() && it->is_string() ? lower(it->get<std::string>()) : std::string();
        };
        const bool match = property("application.name") == wanted || property("application.process.binary") == wanted ||
                           property("pipewire.access.portal.app_id").find(wanted) != std::string::npos;
        if (!match) continue;
        out.indexes.push_back(stream["index"].get<int>());
        const auto mute = stream.find("mute");
        allMuted = allMuted && mute != stream.end() && mute->is_boolean() && mute->get<bool>();
    }
    out.allMuted = !out.indexes.empty() && allMuted;
    return out;
}

AudioState AudioControl::read(const std::string& app) {
    AudioState s;
    s.known = true;
    std::string mic;
    if (!pactl({"get-source-mute", "@DEFAULT_SOURCE@"}, &mic)) return s;
    s.micMuted = mic.find("yes") != std::string::npos;
    bool answered = true;
    const StreamSummary voice = streamsOf("source-outputs", app, &answered), sound = streamsOf("sink-inputs", app, &answered);
    s.available = answered;
    s.voiceStreams = static_cast<int>(voice.indexes.size());
    s.voiceMuted = voice.allMuted;
    s.soundStreams = static_cast<int>(sound.indexes.size());
    s.soundMuted = sound.allMuted;
    return s;
}

void AudioControl::toggleMic() { pactl({"set-source-mute", "@DEFAULT_SOURCE@", "toggle"}, nullptr); }
void AudioControl::setVoiceMuted(const std::string& app, bool muted) { setMuted("set-source-output-mute", "source-outputs", app, muted); }
void AudioControl::setSoundMuted(const std::string& app, bool muted) { setMuted("set-sink-input-mute", "sink-inputs", app, muted); }

}  // namespace lr::desktop
