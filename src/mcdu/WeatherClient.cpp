// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kyr4l
#include "mcdu/WeatherClient.hpp"

#include <dlfcn.h>

#include "util/Isolated.hpp"
#include "util/Log.hpp"

namespace lr::mcdu {

namespace {
constexpr size_t kMaxBody = 4096;   // one METAR is about a hundred characters
constexpr long kTimeoutSeconds = 10, kConnectSeconds = 6;

// The few parts of libcurl that are used, with the numbers its headers give them: the program does not need its development files to build.
constexpr int kOptWriteData = 10001, kOptUrl = 10002, kOptUserAgent = 10018, kOptWriteFunction = 20011, kOptTimeout = 13, kOptNoProgress = 43, kOptFollow = 52, kOptConnectTimeout = 78,
              kOptNoSignal = 99, kOptXferInfoData = 10057, kOptXferInfoFunction = 20219, kOptProtocols = 10319;
constexpr int kInfoResponseCode = 0x200002;

struct Curl {
    void* (*init)() = nullptr;
    int (*setopt)(void*, int, ...) = nullptr;
    int (*perform)(void*) = nullptr;
    int (*getinfo)(void*, int, ...) = nullptr;
    void (*cleanup)(void*) = nullptr;
};

// Stays loaded once it has been needed: unloading a TLS library from a running program is not safe.
const Curl* curlLibrary() {
    static Curl c;
    static const bool ok = [] {
        void* lib = dlopen("libcurl.so.4", RTLD_NOW | RTLD_LOCAL);
        if (!lib) return false;
        c.init = reinterpret_cast<void* (*)()>(dlsym(lib, "curl_easy_init"));
        c.setopt = reinterpret_cast<int (*)(void*, int, ...)>(dlsym(lib, "curl_easy_setopt"));
        c.perform = reinterpret_cast<int (*)(void*)>(dlsym(lib, "curl_easy_perform"));
        c.getinfo = reinterpret_cast<int (*)(void*, int, ...)>(dlsym(lib, "curl_easy_getinfo"));
        c.cleanup = reinterpret_cast<void (*)(void*)>(dlsym(lib, "curl_easy_cleanup"));
        return c.init && c.setopt && c.perform && c.getinfo && c.cleanup;
    }();
    return ok ? &c : nullptr;
}

size_t collect(char* data, size_t size, size_t count, void* user) {
    auto* body = static_cast<std::string*>(user);
    const size_t n = size * count;
    if (body->size() + n > kMaxBody) return 0;  // ends the transfer
    body->append(data, n);
    return n;
}

int progress(void* user, long long, long long, long long, long long) { return static_cast<std::atomic<bool>*>(user)->load() ? 1 : 0; }
}  // namespace

void WeatherClient::fetch(const std::string& airport) {
    if (!validAirportCode(airport) || busy_.exchange(true)) return;
    if (thread_.joinable()) thread_.join();
    cancelled_ = false;
    thread_ = std::thread([this, airport] { run(airport); });
}

bool WeatherClient::take(WeatherResult& out) {
    std::lock_guard<std::mutex> lock(m_);
    if (!hasResult_) return false;
    hasResult_ = false;
    out = result_;
    return true;
}

void WeatherClient::cancel() {
    cancelled_ = true;
    if (thread_.joinable()) thread_.join();
    busy_ = false;
}

void WeatherClient::run(std::string airport) {
    WeatherResult r;
    const Curl* curl = isolatedRun() ? nullptr : curlLibrary();
    void* handle = curl ? curl->init() : nullptr;
    if (!handle) {
        r.status = isolatedRun() ? WeatherStatus::Offline : WeatherStatus::Unavailable;
    } else {
        std::string body;
        const std::string url = "https://aviationweather.gov/api/data/metar?ids=" + airport + "&format=raw";
        curl->setopt(handle, kOptUrl, url.c_str());
        curl->setopt(handle, kOptUserAgent, "msfs-linux-companion");
        curl->setopt(handle, kOptProtocols, "https");
        curl->setopt(handle, kOptWriteFunction, &collect);
        curl->setopt(handle, kOptWriteData, &body);
        curl->setopt(handle, kOptTimeout, kTimeoutSeconds);
        curl->setopt(handle, kOptConnectTimeout, kConnectSeconds);
        curl->setopt(handle, kOptNoSignal, 1L);
        curl->setopt(handle, kOptFollow, 0L);
        curl->setopt(handle, kOptNoProgress, 0L);
        curl->setopt(handle, kOptXferInfoFunction, &progress);
        curl->setopt(handle, kOptXferInfoData, &cancelled_);
        const int rc = curl->perform(handle);
        long code = 0;
        curl->getinfo(handle, kInfoResponseCode, &code);
        curl->cleanup(handle);
        if (rc != 0) {
            r.status = WeatherStatus::Offline;
            LOG_INFO("MCDU: no weather for %s (curl error %d)", airport.c_str(), rc);
        } else if (code == 200 && body.find_first_not_of(" \r\n\t") != std::string::npos) {
            r.status = WeatherStatus::Ok;
            r.report = parseMetar(body);
        } else {
            r.status = WeatherStatus::NoReport;
        }
    }
    {
        std::lock_guard<std::mutex> lock(m_);
        result_ = std::move(r);
        hasResult_ = true;
    }
    busy_ = false;
    if (onDone_) onDone_();
}

}  // namespace lr::mcdu
