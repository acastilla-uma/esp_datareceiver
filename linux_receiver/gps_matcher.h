#pragma once

#include <chrono>
#include <cstddef>
#include <deque>
#include <optional>
#include <string>

struct GpsSample {
    std::chrono::system_clock::time_point timestamp;
    std::string timestampUtc;

    std::optional<double> latitudeDeg;
    std::optional<double> longitudeDeg;
    std::optional<double> heightEllipsoidM;
    std::optional<double> heightMslM;
    std::optional<double> hAccM;
    std::optional<double> vAccM;
    std::optional<double> speedMS;
    std::optional<double> headingDeg;
    std::string fix{"NO_FIX"};
    int fixType{0};
    std::string rtk{"NONE"};
    int numSats{0};
    std::optional<double> pdop;
    std::optional<double> hdop;
    std::optional<double> vdop;
    std::optional<double> correctionAgeS;
    std::optional<int> baseStationId;
};

struct GpsMatch {
    GpsSample sample;
    long long deltaMilliseconds{0};
};

std::optional<std::chrono::system_clock::time_point> parseUtcTimestamp(
    const std::string& timestamp);
std::string formatUtcTimestamp(std::chrono::system_clock::time_point timestamp);

class GpsMatcher {
public:
    explicit GpsMatcher(std::size_t capacity = 256);

    void addSample(GpsSample sample);
    std::optional<GpsMatch> nearest(
        std::chrono::system_clock::time_point timestamp,
        std::chrono::milliseconds maximumDifference) const;
    std::size_t size() const;

private:
    std::size_t capacity_;
    std::deque<GpsSample> samples_;
};
