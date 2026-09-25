#pragma once

#include <chrono>
#include <cstddef>
#include <deque>
#include <optional>
#include <string>

struct GpsSample {
    std::chrono::system_clock::time_point timestamp;
    std::string timestampUtc;
    std::string deviceId;
    double latitude{0.0};
    double longitude{0.0};
    int fixType{0};
    int satellites{0};
};

struct GpsMatch {
    GpsSample sample;
    long long deltaMilliseconds{0};
};

std::optional<std::chrono::system_clock::time_point> parseUtcTimestamp(
    const std::string& timestamp);
std::string formatUtcTimestamp(std::chrono::system_clock::time_point timestamp);
std::optional<GpsSample> parseGpsJsonLine(const std::string& line,
                                          std::string* error = nullptr);

class GpsMatcher {
public:
    explicit GpsMatcher(std::size_t capacity = 256);

    bool addJsonLine(const std::string& line, std::string* error = nullptr);
    void addSample(GpsSample sample);
    std::optional<GpsMatch> nearest(
        std::chrono::system_clock::time_point timestamp,
        std::chrono::milliseconds maximumDifference) const;
    std::size_t size() const;

private:
    std::size_t capacity_;
    std::deque<GpsSample> samples_;
};
