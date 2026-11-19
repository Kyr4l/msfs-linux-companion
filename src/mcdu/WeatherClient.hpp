// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kyr4l
#pragma once
// Fetches the METAR of one airport from aviationweather.gov, on a short-lived thread of its own. Only the four letters of the airport are sent, and only while the
// Weather page is on screen. Uses the system's libcurl when there is one; without it the page says so.
#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

#include "mcdu/Metar.hpp"

namespace lr::mcdu {

class WeatherClient {
public:
    explicit WeatherClient(std::function<void()> onDone) : onDone_(std::move(onDone)) {}
    ~WeatherClient() { cancel(); }
    WeatherClient(const WeatherClient&) = delete;
    WeatherClient& operator=(const WeatherClient&) = delete;

    void fetch(const std::string& airport);  // does nothing while a request is running
    bool take(WeatherResult& out);           // true once per finished request
    void cancel();                           // ends a running request within a moment

private:
    void run(std::string airport);

    std::function<void()> onDone_;
    std::thread thread_;
    std::atomic<bool> busy_{false}, cancelled_{false};
    std::mutex m_;
    WeatherResult result_;
    bool hasResult_ = false;
};

}  // namespace lr::mcdu
