// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kyr4l
#pragma once
// Small thread-safe ring-buffer logger shared by all threads; the UI renders a snapshot.
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <ctime>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

namespace lr {

enum class LogLevel { Debug, Info, Warn, Error };

struct LogEntry {
    std::chrono::system_clock::time_point time;
    LogLevel level;
    std::string text;
};

class Log {
public:
    static Log& instance() {
        static Log log;
        return log;
    }

    void add(LogLevel level, std::string text) {
        std::lock_guard<std::mutex> lock(m_);
        const auto now = std::chrono::system_clock::now();
        if (file_) {  // flushed line by line: after a crash of the sim the last lines are what matters
            const std::time_t t = std::chrono::system_clock::to_time_t(now);
            std::tm tm{};
            localtime_r(&t, &tm);
            const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000;
            static const char kLevel[] = {'D', 'I', 'W', 'E'};
            std::fprintf(file_, "%02d:%02d:%02d.%03d %c %s\n", tm.tm_hour, tm.tm_min, tm.tm_sec, static_cast<int>(ms), kLevel[static_cast<int>(level)], text.c_str());
            std::fflush(file_);
        }
        entries_.push_back({now, level, std::move(text)});
        while (entries_.size() > capacity_) entries_.pop_front();
        ++serial_;
    }

    // Also writes every entry (with the time of day) to `path`.
    bool openFile(const std::string& path) {
        std::lock_guard<std::mutex> lock(m_);
        if (file_) { std::fclose(file_); file_ = nullptr; }
        file_ = std::fopen(path.c_str(), "w");
        return file_ != nullptr;
    }

    __attribute__((format(printf, 3, 4)))
    void logf(LogLevel level, const char* fmt, ...) {
        char buf[2048];
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(buf, sizeof buf, fmt, ap);
        va_end(ap);
        add(level, buf);
    }

    std::vector<LogEntry> snapshot() const {
        std::lock_guard<std::mutex> lock(m_);
        return {entries_.begin(), entries_.end()};
    }

    uint64_t serial() const {
        std::lock_guard<std::mutex> lock(m_);
        return serial_;
    }

    void clear() {
        std::lock_guard<std::mutex> lock(m_);
        entries_.clear();
        ++serial_;
    }

    bool echoToStderr = true;

private:
    mutable std::mutex m_;
    std::deque<LogEntry> entries_;
    size_t capacity_ = 3000;
    std::FILE* file_ = nullptr;
    uint64_t serial_ = 0;
};

#define LR_LOG(level, ...)                                                                 \
    do {                                                                                   \
        ::lr::Log::instance().logf(level, __VA_ARGS__);                                    \
        if (::lr::Log::instance().echoToStderr) {                                          \
            fprintf(stderr, "[%s] ", ::lr::logLevelName(level));                           \
            fprintf(stderr, __VA_ARGS__);                                                  \
            fputc('\n', stderr);                                                           \
        }                                                                                  \
    } while (0)

inline const char* logLevelName(LogLevel l) {
    switch (l) {
        case LogLevel::Debug: return "debug";
        case LogLevel::Info: return "info";
        case LogLevel::Warn: return "warn";
        case LogLevel::Error: return "error";
    }
    return "?";
}

#define LOG_DEBUG(...) LR_LOG(::lr::LogLevel::Debug, __VA_ARGS__)
#define LOG_INFO(...) LR_LOG(::lr::LogLevel::Info, __VA_ARGS__)
#define LOG_WARN(...) LR_LOG(::lr::LogLevel::Warn, __VA_ARGS__)
#define LOG_ERROR(...) LR_LOG(::lr::LogLevel::Error, __VA_ARGS__)

}  // namespace lr
