#include "gps_matcher.h"

#include <cassert>
#include <chrono>
#include <iostream>

namespace {

void parsesRealGpsShape() {
    const auto sample = parseGpsJsonLine(
        R"({"id":14125,"device_id":"GPSTEST001","ts":"2026-09-11 06:12:44+00:00","lat":38.0468168,"lon":-4.0429863,"alt":286.43,"hdop":99.99,"fix_type":2,"num_sats":7,"speed_kmh":0.0,"received_at":"2026-09-11 06:12:46.754421+00:00"})");
    assert(sample);
    assert(sample->deviceId == "GPSTEST001");
    assert(sample->latitude == 38.0468168);
    assert(sample->longitude == -4.0429863);
    assert(sample->fixType == 2);
    assert(sample->satellites == 7);
    assert(sample->timestampUtc == "2026-09-11T06:12:44.000000Z");
}

void handlesTimezoneOffsets() {
    const auto utc = parseUtcTimestamp("2026-09-11 06:12:44+00:00");
    const auto local = parseUtcTimestamp("2026-09-11T08:12:44+02:00");
    assert(utc && local && *utc == *local);
}

void choosesNearestWithinTolerance() {
    GpsMatcher matcher;
    assert(matcher.addJsonLine(
        R"({"device_id":"gps","ts":"2026-09-11 06:12:44+00:00","lat":38.0,"lon":-4.0,"fix_type":1,"num_sats":5})"));
    assert(matcher.addJsonLine(
        R"({"device_id":"gps","ts":"2026-09-11 06:12:48+00:00","lat":39.0,"lon":-5.0,"fix_type":1,"num_sats":6})"));

    const auto doback = parseUtcTimestamp("2026-09-11 06:12:47.250+00:00");
    assert(doback);
    const auto match = matcher.nearest(*doback, std::chrono::milliseconds(2000));
    assert(match);
    assert(match->sample.latitude == 39.0);
    assert(match->deltaMilliseconds == -750);
    assert(!matcher.nearest(*doback, std::chrono::milliseconds(500)));
}

void rejectsInvalidInput() {
    std::string error;
    assert(!parseGpsJsonLine(
        R"({"ts":"not-a-date","lat":38.0,"lon":-4.0})", &error));
    assert(!error.empty());
    assert(!parseGpsJsonLine(
        R"({"ts":"2026-09-11 06:12:44+00:00","lat":138.0,"lon":-4.0})"));
}

}  // namespace

int main() {
    parsesRealGpsShape();
    handlesTimezoneOffsets();
    choosesNearestWithinTolerance();
    rejectsInvalidInput();
    std::cout << "gps_matcher_tests: OK\n";
}
