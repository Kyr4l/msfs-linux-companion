// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kyr4l
#pragma once
// The tasks with a progress bar that programs report to the desktop. Plain bookkeeping; ProgressWatcher feeds it. Names and percentages only: the text of a notification never gets here.
#include <chrono>
#include <map>
#include <string>
#include <vector>

namespace lr::desktop {

struct ProgressTask {
    std::string key, name;
    int percent = 0;
};

class ProgressTracker {
public:
    using Clock = std::chrono::steady_clock;

    void jobRequested(const std::string& sender, const std::string& application);  // a KDE job is about to start
    void jobUpdate(const std::string& sender, const std::string& path, const std::string& title, int percent, Clock::time_point now);  // percent < 0: not in this update
    void jobEnded(const std::string& sender, const std::string& path);
    void notification(const std::string& sender, const std::string& application, int percent, Clock::time_point now);
    void launcher(const std::string& sender, const std::string& applicationUri, double progress, bool visible, Clock::time_point now);  // progress < 0: not in this update

    void dismiss(const std::string& key);  // hidden until the task ends
    std::vector<ProgressTask> tasks(Clock::time_point now);

private:
    // One program often reports the same work in several ways at once. The most detailed report is the one that is listed.
    enum class Source { Job, Notification, Launcher };
    struct Entry {
        Source source = Source::Job;
        std::string sender, application;
        std::string name;
        int percent = 0;
        uint64_t order = 0;
        bool hidden = false, finished = false;
        Clock::time_point updated{}, finishedAt{};
    };
    void update(const std::string& key, Source source, const std::string& sender, const std::string& application, const std::string& name, int percent, Clock::time_point now);

    std::map<std::string, Entry> entries_;
    std::map<std::string, std::string> applications_;  // bus name -> the application behind it
    uint64_t nextOrder_ = 0;
};

// "application://org.kde.dolphin.desktop" -> "DOLPHIN"
std::string applicationName(const std::string& raw);

}  // namespace lr::desktop
