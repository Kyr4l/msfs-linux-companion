// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kyr4l
#include "desktop/ProgressTracker.hpp"

#include <algorithm>
#include <cctype>

#include "util/Log.hpp"

namespace lr::desktop {

namespace {
constexpr size_t kMaxEntries = 32, kMaxApplications = 32, kNameLength = 22;
constexpr auto kKeepFinished = std::chrono::seconds(5);
constexpr auto kForgetSilent = std::chrono::seconds(90);  // a program that stopped reporting without saying so

std::string screenText(const std::string& raw) {
    std::string out;
    for (unsigned char c : raw) {
        if (out.size() >= kNameLength) break;
        out.push_back(c < 0x20 || c >= 0x7f ? '?' : static_cast<char>(std::toupper(c)));
    }
    return out;
}
}  // namespace

std::string applicationName(const std::string& raw) {
    std::string s = raw;
    if (const size_t slash = s.find_last_of('/'); slash != std::string::npos) s.erase(0, slash + 1);
    const std::string suffix = ".desktop";
    if (s.size() > suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0) s.resize(s.size() - suffix.size());
    if (const size_t dot = s.find_last_of('.'); dot != std::string::npos && dot + 1 < s.size()) s.erase(0, dot + 1);
    std::replace(s.begin(), s.end(), '-', ' ');
    std::replace(s.begin(), s.end(), '_', ' ');
    s = screenText(s);
    return s.empty() ? "TASK" : s;
}

void ProgressTracker::update(const std::string& key, Source source, const std::string& sender, const std::string& application, const std::string& name, int percent, Clock::time_point now) {
    auto it = entries_.find(key);
    if (it == entries_.end()) {
        if (entries_.size() >= kMaxEntries) {
            auto oldest = std::min_element(entries_.begin(), entries_.end(), [](const auto& a, const auto& b) { return a.second.updated < b.second.updated; });
            entries_.erase(oldest);
        }
        it = entries_.emplace(key, Entry{}).first;
        it->second.order = nextOrder_++;
        it->second.source = source;
        it->second.sender = sender;
        it->second.application = application;
        static constexpr const char* kSources[] = {"job", "notification", "taskbar"};
        LOG_INFO("Progress: new task from %s: %s", kSources[static_cast<int>(source)], name.c_str());
    }
    Entry& e = it->second;
    if (!name.empty()) e.name = name;
    if (percent >= 0) e.percent = std::min(percent, 100);
    e.updated = now;
    if (e.percent >= 100 && !e.finished) {
        e.finished = true;
        e.finishedAt = now;
    } else if (e.percent < 100) {
        e.finished = false;
    }
}

void ProgressTracker::jobRequested(const std::string& sender, const std::string& application) {
    if (applications_.size() >= kMaxApplications && !applications_.count(sender)) applications_.clear();
    applications_[sender] = applicationName(application);
}

void ProgressTracker::jobUpdate(const std::string& sender, const std::string& path, const std::string& title, int percent, Clock::time_point now) {
    const std::string key = "job:" + sender + path;
    const auto app = applications_.find(sender);
    const std::string application = app != applications_.end() ? app->second : "";
    std::string name = screenText(title);
    if (name.empty() && !entries_.count(key)) name = application.empty() ? "TASK" : application;
    update(key, Source::Job, sender, application, name, percent, now);
}

void ProgressTracker::jobEnded(const std::string& sender, const std::string& path) { entries_.erase("job:" + sender + path); }

void ProgressTracker::notification(const std::string& sender, const std::string& application, int percent, Clock::time_point now) {
    const std::string name = applicationName(application);
    update("note:" + sender + ":" + application, Source::Notification, sender, name, name, percent, now);
}

void ProgressTracker::launcher(const std::string& sender, const std::string& uri, double progress, bool visible, Clock::time_point now) {
    const std::string key = "app:" + uri;
    if (!visible) {
        entries_.erase(key);
        return;
    }
    const std::string name = applicationName(uri);
    update(key, Source::Launcher, sender, name, name, progress < 0 ? -1 : static_cast<int>(progress * 100.0 + 0.5), now);
}

void ProgressTracker::dismiss(const std::string& key) {
    if (const auto it = entries_.find(key); it != entries_.end()) it->second.hidden = true;
}

std::vector<ProgressTask> ProgressTracker::tasks(Clock::time_point now) {
    std::vector<std::pair<uint64_t, ProgressTask>> shown;
    for (auto it = entries_.begin(); it != entries_.end();) {
        const Entry& e = it->second;
        if ((e.finished && now - e.finishedAt > kKeepFinished) || now - e.updated > kForgetSilent) {
            it = entries_.erase(it);
            continue;
        }
        ++it;
    }
    for (const auto& [key, e] : entries_) {
        // the same work reported a second way by the same program is not a second task
        const bool repeated = std::any_of(entries_.begin(), entries_.end(), [&e = e](const auto& other) {
            const Entry& o = other.second;
            return o.source < e.source && ((!o.sender.empty() && o.sender == e.sender) || (!o.application.empty() && o.application == e.application));
        });
        if (!e.hidden && !repeated) shown.push_back({e.order, ProgressTask{key, e.name, e.percent}});
    }
    std::sort(shown.begin(), shown.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    std::vector<ProgressTask> out;
    out.reserve(shown.size());
    for (auto& [order, task] : shown) out.push_back(std::move(task));
    return out;
}

}  // namespace lr::desktop
