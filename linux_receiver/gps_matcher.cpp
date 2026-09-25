#include "gps_matcher.h"

#include <cmath>
#include <cstdlib>
#include <ctime>
#include <iomanip>
#include <limits>
#include <regex>
#include <sstream>
#include <utility>

namespace {

std::optional<std::string> jsonValue(const std::string& json,
                                     const std::string& key) {
    const std::string marker = "\"" + key + "\":";
    const std::size_t markerPosition = json.find(marker);
    if (markerPosition == std::string::npos) {
        return std::nullopt;
    }

    std::size_t position = markerPosition + marker.size();
    if (position >= json.size()) {
        return std::nullopt;
    }
    if (json[position] == '"') {
        ++position;
        std::string value;
        bool escaped = false;
        for (; position < json.size(); ++position) {
            const char character = json[position];
            if (escaped) {
                value += character;
                escaped = false;
            } else if (character == '\\') {
                escaped = true;
            } else if (character == '"') {
                return value;
            } else {
                value += character;
            }
        }
        return std::nullopt;
    }

    const std::size_t end = json.find_first_of(",}", position);
    if (end == std::string::npos) {
        return std::nullopt;
    }
    return json.substr(position, end - position);
}

std::optional<double> jsonDouble(const std::string& json,
                                 const std::string& key) {
    const auto text = jsonValue(json, key);
    if (!text || *text == "null") {
        return std::nullopt;
    }
    char* end = nullptr;
    const double value = std::strtod(text->c_str(), &end);
    if (end == text->c_str() || *end != '\0' || !std::isfinite(value)) {
        return std::nullopt;
    }
    return value;
}

std::optional<int> jsonInteger(const std::string& json,
                               const std::string& key) {
    const auto number = jsonDouble(json, key);
    if (!number || *number < std::numeric_limits<int>::min() ||
        *number > std::numeric_limits<int>::max()) {
        return std::nullopt;
    }
    return static_cast<int>(*number);
}

void setError(std::string* error, const std::string& message) {
    if (error) {
        *error = message;
    }
}

}  // namespace

std::optional<std::chrono::system_clock::time_point> parseUtcTimestamp(
    const std::string& timestamp) {
    static const std::regex pattern(
        R"(^(\d{4})-(\d{2})-(\d{2})[ T](\d{2}):(\d{2}):(\d{2})(?:\.(\d{1,9}))?(Z|[+-]\d{2}:?\d{2})$)");
    std::smatch match;
    if (!std::regex_match(timestamp, match, pattern)) {
        return std::nullopt;
    }

    std::tm utc{};
    utc.tm_year = std::stoi(match[1].str()) - 1900;
    utc.tm_mon = std::stoi(match[2].str()) - 1;
    utc.tm_mday = std::stoi(match[3].str());
    utc.tm_hour = std::stoi(match[4].str());
    utc.tm_min = std::stoi(match[5].str());
    utc.tm_sec = std::stoi(match[6].str());
    utc.tm_isdst = 0;

    const std::time_t seconds = timegm(&utc);
    if (seconds == static_cast<std::time_t>(-1)) {
        return std::nullopt;
    }

    std::chrono::nanoseconds fraction{0};
    if (match[7].matched) {
        std::string digits = match[7].str();
        digits.append(9 - digits.size(), '0');
        fraction = std::chrono::nanoseconds(std::stoll(digits));
    }

    std::chrono::seconds offset{0};
    const std::string zone = match[8].str();
    if (zone != "Z") {
        const int hours = std::stoi(zone.substr(1, 2));
        const int minutes = std::stoi(zone.substr(zone.size() - 2));
        const int sign = zone[0] == '+' ? 1 : -1;
        offset = std::chrono::seconds(sign * (hours * 3600 + minutes * 60));
    }

    return std::chrono::system_clock::from_time_t(seconds) + fraction - offset;
}

std::string formatUtcTimestamp(
    std::chrono::system_clock::time_point timestamp) {
    const auto microseconds = std::chrono::duration_cast<std::chrono::microseconds>(
        timestamp.time_since_epoch());
    auto seconds = std::chrono::duration_cast<std::chrono::seconds>(microseconds);
    auto remainder = microseconds - seconds;
    if (remainder.count() < 0) {
        seconds -= std::chrono::seconds(1);
        remainder += std::chrono::seconds(1);
    }

    const std::time_t value = static_cast<std::time_t>(seconds.count());
    std::tm utc{};
    gmtime_r(&value, &utc);
    std::ostringstream output;
    output << std::put_time(&utc, "%Y-%m-%dT%H:%M:%S") << '.'
           << std::setfill('0') << std::setw(6) << remainder.count() << 'Z';
    return output.str();
}

std::optional<GpsSample> parseGpsJsonLine(const std::string& line,
                                          std::string* error) {
    const auto timestampText = jsonValue(line, "ts");
    const auto latitude = jsonDouble(line, "lat");
    const auto longitude = jsonDouble(line, "lon");
    if (!timestampText || !latitude || !longitude) {
        setError(error, "faltan ts, lat o lon en el registro GPS");
        return std::nullopt;
    }

    const auto timestamp = parseUtcTimestamp(*timestampText);
    if (!timestamp) {
        setError(error, "el timestamp GPS no tiene un formato UTC válido: " +
                            *timestampText);
        return std::nullopt;
    }
    if (*latitude < -90.0 || *latitude > 90.0 || *longitude < -180.0 ||
        *longitude > 180.0) {
        setError(error, "las coordenadas GPS están fuera de rango");
        return std::nullopt;
    }

    GpsSample sample;
    sample.timestamp = *timestamp;
    sample.timestampUtc = formatUtcTimestamp(*timestamp);
    sample.latitude = *latitude;
    sample.longitude = *longitude;
    sample.deviceId = jsonValue(line, "device_id").value_or("");
    sample.fixType = jsonInteger(line, "fix_type").value_or(0);
    sample.satellites = jsonInteger(line, "num_sats").value_or(0);
    return sample;
}

GpsMatcher::GpsMatcher(std::size_t capacity)
    : capacity_(capacity == 0 ? 1 : capacity) {}

bool GpsMatcher::addJsonLine(const std::string& line, std::string* error) {
    auto sample = parseGpsJsonLine(line, error);
    if (!sample) {
        return false;
    }
    addSample(std::move(*sample));
    return true;
}

void GpsMatcher::addSample(GpsSample sample) {
    samples_.push_back(std::move(sample));
    while (samples_.size() > capacity_) {
        samples_.pop_front();
    }
}

std::optional<GpsMatch> GpsMatcher::nearest(
    std::chrono::system_clock::time_point timestamp,
    std::chrono::milliseconds maximumDifference) const {
    const GpsSample* best = nullptr;
    long long bestAbsoluteDelta = std::numeric_limits<long long>::max();
    long long bestDelta = 0;
    for (const auto& sample : samples_) {
        const long long delta = std::chrono::duration_cast<std::chrono::milliseconds>(
                                    timestamp - sample.timestamp)
                                    .count();
        const long long absoluteDelta = std::llabs(delta);
        if (absoluteDelta < bestAbsoluteDelta) {
            best = &sample;
            bestAbsoluteDelta = absoluteDelta;
            bestDelta = delta;
        }
    }
    if (!best || bestAbsoluteDelta > maximumDifference.count()) {
        return std::nullopt;
    }
    return GpsMatch{*best, bestDelta};
}

std::size_t GpsMatcher::size() const {
    return samples_.size();
}
