#include "gps_matcher.h"

#include <cassert>
#include <chrono>
#include <iostream>

namespace {

GpsSample sampleAt(const std::string& timestamp, double latitude) {
    const auto parsed = parseUtcTimestamp(timestamp);
    assert(parsed);
    GpsSample sample;
    sample.timestamp = *parsed;
    sample.timestampUtc = formatUtcTimestamp(*parsed);
    sample.latitudeDeg = latitude;
    sample.longitudeDeg = -4.0;
    sample.fix = "RTK_FIXED";
    sample.rtk = "FIXED";
    sample.numSats = 24;
    return sample;
}

void handlesTimezoneOffsets() {
    const auto utc = parseUtcTimestamp("2026-09-11 06:12:44+00:00");
    const auto local = parseUtcTimestamp("2026-09-11T08:12:44+02:00");
    assert(utc && local && *utc == *local);
}

void choosesNearestWithinTolerance() {
    GpsMatcher matcher;
    matcher.addSample(sampleAt("2026-09-11T06:12:44.000000Z", 38.0));
    matcher.addSample(sampleAt("2026-09-11T06:12:48.000000Z", 39.0));

    const auto doback = parseUtcTimestamp("2026-09-11T06:12:47.250000Z");
    assert(doback);
    const auto match = matcher.nearest(*doback, std::chrono::milliseconds(2000));
    assert(match);
    assert(match->sample.latitudeDeg == 39.0);
    assert(match->deltaMilliseconds == -750);
    assert(!matcher.nearest(*doback, std::chrono::milliseconds(500)));
}

void acceptsInclusiveTwoHundredMillisecondWindow() {
    GpsMatcher matcher;
    matcher.addSample(sampleAt("2026-09-11T06:12:44.000000Z", 38.0));

    const auto before = parseUtcTimestamp("2026-09-11T06:12:43.800000Z");
    const auto same = parseUtcTimestamp("2026-09-11T06:12:44.000000Z");
    const auto after = parseUtcTimestamp("2026-09-11T06:12:44.200000Z");
    const auto tooEarly = parseUtcTimestamp("2026-09-11T06:12:43.799000Z");
    const auto tooLate = parseUtcTimestamp("2026-09-11T06:12:44.201000Z");
    assert(before && same && after && tooEarly && tooLate);

    assert(matcher.nearest(*before, std::chrono::milliseconds(200))->deltaMilliseconds == -200);
    assert(matcher.nearest(*same, std::chrono::milliseconds(200))->deltaMilliseconds == 0);
    assert(matcher.nearest(*after, std::chrono::milliseconds(200))->deltaMilliseconds == 200);
    assert(!matcher.nearest(*tooEarly, std::chrono::milliseconds(200)));
    assert(!matcher.nearest(*tooLate, std::chrono::milliseconds(200)));
}

void reusesOneGnssSampleAndBoundsHistory() {
    GpsMatcher matcher(1);
    matcher.addSample(sampleAt("2026-09-11T06:12:44.000000Z", 38.0));
    const auto first = parseUtcTimestamp("2026-09-11T06:12:44.050000Z");
    const auto second = parseUtcTimestamp("2026-09-11T06:12:44.150000Z");
    assert(first && second);
    assert(matcher.nearest(*first, std::chrono::milliseconds(200)));
    assert(matcher.nearest(*second, std::chrono::milliseconds(200)));

    matcher.addSample(sampleAt("2026-09-11T06:12:45.000000Z", 39.0));
    assert(matcher.size() == 1);
}

}  // namespace

int main() {
    handlesTimezoneOffsets();
    choosesNearestWithinTolerance();
    acceptsInclusiveTwoHundredMillisecondWindow();
    reusesOneGnssSampleAndBoundsHistory();
    std::cout << "gps_matcher_tests: OK\n";
}
