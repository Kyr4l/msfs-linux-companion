// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kyr4l
#pragma once
// A METAR, the weather report of an airport, read into what the Weather page of the MCDU shows.
#include <string>

namespace lr::mcdu {

struct MetarReport {
    std::string raw, station, time, wind, visibility, temperature, dewpoint, pressure;
};

enum class WeatherStatus { None, Loading, Ok, NoReport, Offline, Unavailable };

struct WeatherResult {
    WeatherStatus status = WeatherStatus::None;
    MetarReport report;
};

MetarReport parseMetar(const std::string& raw);
// Four letters or digits, in capitals: the only thing that ever goes into the address of the request.
bool validAirportCode(const std::string& code);

}  // namespace lr::mcdu
