#include "gnss_receiver.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <iomanip>
#include <sstream>

namespace {

constexpr std::size_t kMaxUbxPayload = 4096;

void checksumAdd(std::uint8_t byte, std::uint8_t& ckA, std::uint8_t& ckB) {
    ckA = static_cast<std::uint8_t>(ckA + byte);
    ckB = static_cast<std::uint8_t>(ckB + ckA);
}

std::uint16_t getU2(const std::vector<std::uint8_t>& data, std::size_t offset) {
    return static_cast<std::uint16_t>(data[offset] |
                                     (static_cast<std::uint16_t>(data[offset + 1]) << 8));
}

std::uint32_t getU4(const std::vector<std::uint8_t>& data, std::size_t offset) {
    return static_cast<std::uint32_t>(data[offset]) |
           (static_cast<std::uint32_t>(data[offset + 1]) << 8) |
           (static_cast<std::uint32_t>(data[offset + 2]) << 16) |
           (static_cast<std::uint32_t>(data[offset + 3]) << 24);
}

std::int32_t getI4(const std::vector<std::uint8_t>& data, std::size_t offset) {
    return static_cast<std::int32_t>(getU4(data, offset));
}

std::optional<double> parseDouble(const std::string& value) {
    if (value.empty()) {
        return std::nullopt;
    }
    char* end = nullptr;
    const double parsed = std::strtod(value.c_str(), &end);
    if (end == value.c_str() || *end != '\0' || !std::isfinite(parsed)) {
        return std::nullopt;
    }
    return parsed;
}

std::optional<int> parseInt(const std::string& value) {
    if (value.empty()) {
        return std::nullopt;
    }
    char* end = nullptr;
    const long parsed = std::strtol(value.c_str(), &end, 10);
    if (end == value.c_str() || *end != '\0') {
        return std::nullopt;
    }
    return static_cast<int>(parsed);
}

std::vector<std::string> splitCsv(const std::string& text) {
    std::vector<std::string> fields;
    std::string current;
    for (char character : text) {
        if (character == ',') {
            fields.push_back(current);
            current.clear();
        } else {
            current.push_back(character);
        }
    }
    fields.push_back(current);
    return fields;
}

std::optional<double> parseNmeaCoordinate(const std::string& value,
                                          const std::string& hemisphere) {
    const auto raw = parseDouble(value);
    if (!raw || hemisphere.empty()) {
        return std::nullopt;
    }
    const double degrees = std::floor(*raw / 100.0);
    const double minutes = *raw - degrees * 100.0;
    double coordinate = degrees + minutes / 60.0;
    if (hemisphere == "S" || hemisphere == "W") {
        coordinate = -coordinate;
    } else if (hemisphere != "N" && hemisphere != "E") {
        return std::nullopt;
    }
    return coordinate;
}

std::chrono::system_clock::time_point makeUtc(int year, int month, int day,
                                              int hour, int minute, int second,
                                              int nano) {
    std::tm utc{};
    utc.tm_year = year - 1900;
    utc.tm_mon = month - 1;
    utc.tm_mday = day;
    utc.tm_hour = hour;
    utc.tm_min = minute;
    utc.tm_sec = second;
    utc.tm_isdst = 0;
    const std::time_t time = timegm(&utc);
    auto result = std::chrono::system_clock::from_time_t(time);
    result += std::chrono::nanoseconds(nano);
    return result;
}

std::vector<std::uint8_t> ubxFrame(std::uint8_t cls, std::uint8_t id,
                                   const std::vector<std::uint8_t>& payload) {
    std::vector<std::uint8_t> frame{0xB5, 0x62, cls, id,
                                   static_cast<std::uint8_t>(payload.size() & 0xFF),
                                   static_cast<std::uint8_t>((payload.size() >> 8) & 0xFF)};
    frame.insert(frame.end(), payload.begin(), payload.end());
    std::uint8_t ckA = 0;
    std::uint8_t ckB = 0;
    for (std::size_t i = 2; i < frame.size(); ++i) {
        checksumAdd(frame[i], ckA, ckB);
    }
    frame.push_back(ckA);
    frame.push_back(ckB);
    return frame;
}

bool hasValidNmeaChecksum(const std::string& line) {
    if (line.empty() || line.front() != '$') {
        return false;
    }
    const std::size_t star = line.find('*');
    if (star == std::string::npos || star + 2 >= line.size()) {
        return true;
    }
    std::uint8_t checksum = 0;
    for (std::size_t i = 1; i < star; ++i) {
        checksum ^= static_cast<std::uint8_t>(line[i]);
    }
    unsigned int expected = 0;
    std::istringstream input(line.substr(star + 1, 2));
    input >> std::hex >> expected;
    return input && checksum == expected;
}

}  // namespace

std::string gnssFixToString(GnssFix fix) {
    switch (fix) {
        case GnssFix::Fix2D:
            return "2D";
        case GnssFix::Fix3D:
            return "3D";
        case GnssFix::Dgnss:
            return "DGNSS";
        case GnssFix::RtkFloat:
            return "RTK_FLOAT";
        case GnssFix::RtkFixed:
            return "RTK_FIXED";
        case GnssFix::NoFix:
        default:
            return "NO_FIX";
    }
}

std::string rtkStateToString(RtkState state) {
    switch (state) {
        case RtkState::Float:
            return "FLOAT";
        case RtkState::Fixed:
            return "FIXED";
        case RtkState::None:
        default:
            return "NONE";
    }
}

std::string formatGnssUtc(std::chrono::system_clock::time_point timestamp) {
    const auto microseconds =
        std::chrono::duration_cast<std::chrono::microseconds>(timestamp.time_since_epoch());
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

GnssReceiver::GnssReceiver() = default;

void GnssReceiver::ingest(const std::uint8_t* data, std::size_t size) {
    for (std::size_t i = 0; i < size; ++i) {
        ingestByte(data[i]);
    }
}

void GnssReceiver::ingest(const std::vector<std::uint8_t>& data) {
    ingest(data.data(), data.size());
}

void GnssReceiver::ingestText(const std::string& data) {
    ingest(reinterpret_cast<const std::uint8_t*>(data.data()), data.size());
}

std::optional<GnssSample> GnssReceiver::latestSample() const {
    return latestSample_;
}

GnssQualitySnapshot GnssReceiver::snapshot() const {
    GnssQualitySnapshot result;
    result.hasSample = latestSample_.has_value();
    if (latestSample_) {
        result.sample = *latestSample_;
    }
    result.lastSolutionSteady = lastSolutionSteady_;
    result.lastRtcmSteady = lastRtcmSteady_;
    result.rtcmMessages = rtcmMessages_;
    result.lastRtcmMessageType = lastRtcmMessageType_;
    result.lastRtcmUsed = lastRtcmUsed_;
    result.lastRtcmCrcFailed = lastRtcmCrcFailed_;
    result.jamInd = jamInd_;
    result.interferenceState = interferenceState_;
    result.lastError = lastError_;
    result.lastGgaSentence = latestGgaSentence_;
    return result;
}

std::string GnssReceiver::latestGgaSentence() const {
    return latestGgaSentence_;
}

std::vector<std::uint8_t> GnssReceiver::buildRate5HzCommand() const {
    // UBX-CFG-RATE: 200 ms measurement rate, one navigation cycle, UTC alignment.
    const std::vector<std::uint8_t> payload{0xC8, 0x00, 0x01, 0x00, 0x00, 0x00};
    return ubxFrame(0x06, 0x08, payload);
}

std::vector<std::uint8_t> GnssReceiver::buildStartupCommands() const {
    std::vector<std::uint8_t> commands = buildRate5HzCommand();
    const auto enableOnUsb = [&](std::uint8_t messageClass, std::uint8_t messageId,
                                 std::uint8_t rate) {
        // UBX-CFG-MSG, RAM-only: I2C, UART1, UART2, USB, SPI, reserved.
        const auto frame = ubxFrame(
            0x06, 0x01,
            {messageClass, messageId, 0x00, 0x00, 0x00, rate, 0x00, 0x00});
        commands.insert(commands.end(), frame.begin(), frame.end());
    };
    enableOnUsb(0x01, 0x07, 1);  // NAV-PVT: solution and accuracy.
    enableOnUsb(0x01, 0x04, 1);  // NAV-DOP.
    enableOnUsb(0x01, 0x35, 5);  // NAV-SAT: once per second at 5 Hz.
    enableOnUsb(0x02, 0x32, 1);  // RXM-RTCM: correction diagnostics.
    enableOnUsb(0x0A, 0x38, 5);  // MON-RF: once per second at 5 Hz.
    enableOnUsb(0xF0, 0x00, 1);  // NMEA-GGA: position sent to the VRS caster.
    return commands;
}

void GnssReceiver::ingestByte(std::uint8_t byte) {
    switch (state_) {
        case ParserState::Search:
            if (byte == 0xB5) {
                resetUbx();
                state_ = ParserState::UbxSync2;
            } else if (byte == '$') {
                nmeaLine_.clear();
                nmeaLine_.push_back('$');
                state_ = ParserState::Nmea;
            }
            break;
        case ParserState::UbxSync2:
            state_ = (byte == 0x62) ? ParserState::UbxClass : ParserState::Search;
            break;
        case ParserState::UbxClass:
            ubxClass_ = byte;
            checksumAdd(byte, ckA_, ckB_);
            state_ = ParserState::UbxId;
            break;
        case ParserState::UbxId:
            ubxId_ = byte;
            checksumAdd(byte, ckA_, ckB_);
            state_ = ParserState::UbxLen1;
            break;
        case ParserState::UbxLen1:
            ubxLength_ = byte;
            checksumAdd(byte, ckA_, ckB_);
            state_ = ParserState::UbxLen2;
            break;
        case ParserState::UbxLen2:
            ubxLength_ |= static_cast<std::uint16_t>(byte) << 8;
            checksumAdd(byte, ckA_, ckB_);
            if (ubxLength_ > kMaxUbxPayload) {
                lastError_ = "UBX payload demasiado grande";
                state_ = ParserState::Search;
            } else if (ubxLength_ == 0) {
                state_ = ParserState::UbxCkA;
            } else {
                ubxPayload_.clear();
                ubxPayload_.reserve(ubxLength_);
                state_ = ParserState::UbxPayload;
            }
            break;
        case ParserState::UbxPayload:
            ubxPayload_.push_back(byte);
            checksumAdd(byte, ckA_, ckB_);
            if (ubxPayload_.size() == ubxLength_) {
                state_ = ParserState::UbxCkA;
            }
            break;
        case ParserState::UbxCkA:
            receivedCkA_ = byte;
            state_ = ParserState::UbxCkB;
            break;
        case ParserState::UbxCkB:
            if (receivedCkA_ == ckA_ && byte == ckB_) {
                handleUbx(ubxClass_, ubxId_, ubxPayload_);
            } else {
                lastError_ = "checksum UBX inválido";
            }
            state_ = ParserState::Search;
            break;
        case ParserState::Nmea:
            if (byte == '\n' || byte == '\r') {
                if (!nmeaLine_.empty()) {
                    handleNmeaLine(nmeaLine_);
                }
                state_ = ParserState::Search;
            } else if (nmeaLine_.size() < 128) {
                nmeaLine_.push_back(static_cast<char>(byte));
            } else {
                lastError_ = "línea NMEA demasiado larga";
                state_ = ParserState::Search;
            }
            break;
    }
}

void GnssReceiver::resetUbx() {
    ubxPayload_.clear();
    ubxLength_ = 0;
    ckA_ = 0;
    ckB_ = 0;
    receivedCkA_ = 0;
}

void GnssReceiver::handleUbx(std::uint8_t cls, std::uint8_t id,
                             const std::vector<std::uint8_t>& payload) {
    if (cls == 0x01 && id == 0x07) {
        if (payload.size() < 92) {
            lastError_ = "NAV-PVT corto";
            return;
        }
        GnssSample sample = latestSample_.value_or(GnssSample{});
        const int year = getU2(payload, 4);
        const int month = payload[6];
        const int day = payload[7];
        const int hour = payload[8];
        const int minute = payload[9];
        const int second = payload[10];
        const std::uint8_t valid = payload[11];
        const std::int32_t nano = getI4(payload, 16);
        const bool validTime = (valid & 0x03) == 0x03 && year >= 2000 && month >= 1 &&
                               month <= 12 && day >= 1 && day <= 31;
        sample.timestamp = validTime ? makeUtc(year, month, day, hour, minute, second, nano)
                                     : std::chrono::system_clock::now();
        sample.timestampUtc = formatGnssUtc(sample.timestamp);
        sample.available = true;
        sample.fixType = payload[20];
        sample.numSats = payload[23];
        sample.longitudeDeg = static_cast<double>(getI4(payload, 24)) * 1e-7;
        sample.latitudeDeg = static_cast<double>(getI4(payload, 28)) * 1e-7;
        sample.heightEllipsoidM = static_cast<double>(getI4(payload, 32)) / 1000.0;
        sample.heightMslM = static_cast<double>(getI4(payload, 36)) / 1000.0;
        sample.hAccM = static_cast<double>(getU4(payload, 40)) / 1000.0;
        sample.vAccM = static_cast<double>(getU4(payload, 44)) / 1000.0;
        sample.speedMS = static_cast<double>(getI4(payload, 60)) / 1000.0;
        sample.headingDeg = static_cast<double>(getI4(payload, 64)) * 1e-5;
        sample.pdop = static_cast<double>(getU2(payload, 76)) / 100.0;

        const std::uint8_t carrSoln = static_cast<std::uint8_t>((payload[21] >> 6) & 0x03);
        if (carrSoln == 2) {
            sample.rtk = RtkState::Fixed;
            sample.fix = GnssFix::RtkFixed;
        } else if (carrSoln == 1) {
            sample.rtk = RtkState::Float;
            sample.fix = GnssFix::RtkFloat;
        } else {
            sample.rtk = RtkState::None;
            if (sample.fixType >= 4) {
                sample.fix = GnssFix::Dgnss;
            } else if (sample.fixType == 3) {
                sample.fix = GnssFix::Fix3D;
            } else if (sample.fixType == 2) {
                sample.fix = GnssFix::Fix2D;
            } else {
                sample.fix = GnssFix::NoFix;
            }
        }
        if (lastRtcmSteady_) {
            const auto age = std::chrono::duration<double>(
                std::chrono::steady_clock::now() - *lastRtcmSteady_);
            sample.correctionAgeS = age.count();
        }
        if (lastBaseStationId_) {
            sample.baseStationId = *lastBaseStationId_;
        }
        latestSample_ = sample;
        lastSolutionSteady_ = std::chrono::steady_clock::now();
        lastError_.clear();
        return;
    }

    if (cls == 0x01 && id == 0x04) {
        if (payload.size() < 18) {
            lastError_ = "NAV-DOP corto";
            return;
        }
        if (!latestSample_) {
            latestSample_ = GnssSample{};
        }
        latestSample_->pdop = static_cast<double>(getU2(payload, 6)) / 100.0;
        latestSample_->vdop = static_cast<double>(getU2(payload, 10)) / 100.0;
        latestSample_->hdop = static_cast<double>(getU2(payload, 12)) / 100.0;
        return;
    }

    if (cls == 0x01 && id == 0x35) {
        if (payload.size() < 8) {
            lastError_ = "NAV-SAT corto";
            return;
        }
        if (!latestSample_) {
            latestSample_ = GnssSample{};
        }
        latestSample_->numSats = payload[5];
        return;
    }

    if (cls == 0x02 && id == 0x32) {
        if (payload.size() < 8) {
            lastError_ = "RXM-RTCM corto";
            return;
        }
        lastRtcmSteady_ = std::chrono::steady_clock::now();
        ++rtcmMessages_;
        const std::uint8_t flags = payload[1];
        const std::uint8_t used = static_cast<std::uint8_t>((flags >> 1) & 0x03);
        lastRtcmCrcFailed_ = (flags & 0x01) != 0;
        lastRtcmUsed_ = used == 2 ? std::optional<bool>(true)
                                  : used == 1 ? std::optional<bool>(false)
                                              : std::nullopt;
        lastBaseStationId_ = getU2(payload, 4);
        lastRtcmMessageType_ = getU2(payload, 6);
        if (latestSample_) {
            latestSample_->baseStationId = *lastBaseStationId_;
            latestSample_->correctionAgeS = 0.0;
        }
        return;
    }

    if (cls == 0x0A && id == 0x38) {
        if (payload.size() < 24) {
            lastError_ = "MON-RF corto";
            return;
        }
        const int blocks = std::max<int>(1, payload[1]);
        int worstJam = 0;
        for (int block = 0; block < blocks; ++block) {
            const std::size_t offset = 4 + static_cast<std::size_t>(block) * 24;
            if (offset + 13 >= payload.size()) {
                break;
            }
            worstJam = std::max(worstJam, static_cast<int>(payload[offset + 12]));
        }
        jamInd_ = worstJam;
        if (worstJam >= 80) {
            interferenceState_ = "CRITICAL";
        } else if (worstJam >= 40) {
            interferenceState_ = "WARNING";
        } else {
            interferenceState_ = "OK";
        }
    }
}

void GnssReceiver::handleNmeaLine(const std::string& line) {
    if (!hasValidNmeaChecksum(line)) {
        lastError_ = "checksum NMEA inválido";
        return;
    }
    const std::size_t star = line.find('*');
    const std::string body = line.substr(1, star == std::string::npos ? std::string::npos
                                                                      : star - 1);
    const auto fields = splitCsv(body);
    if (fields.empty() || (fields[0].size() < 5 || fields[0].substr(fields[0].size() - 3) != "GGA")) {
        return;
    }
    latestGgaSentence_ = line;
    if (fields.size() < 10) {
        lastError_ = "GGA corto";
        return;
    }
    const auto latitude = parseNmeaCoordinate(fields[2], fields[3]);
    const auto longitude = parseNmeaCoordinate(fields[4], fields[5]);
    const auto quality = parseInt(fields[6]).value_or(0);
    if (!latitude || !longitude || quality <= 0) {
        return;
    }
    GnssSample sample = latestSample_.value_or(GnssSample{});
    sample.available = true;
    sample.timestamp = std::chrono::system_clock::now();
    sample.timestampUtc = formatGnssUtc(sample.timestamp);
    sample.latitudeDeg = *latitude;
    sample.longitudeDeg = *longitude;
    sample.fixType = quality;
    sample.numSats = parseInt(fields[7]).value_or(sample.numSats);
    sample.hdop = parseDouble(fields[8]);
    if (auto altitude = parseDouble(fields[9])) {
        sample.heightMslM = *altitude;
    }
    if (fields.size() > 13) {
        sample.correctionAgeS = parseDouble(fields[13]);
    }
    if (fields.size() > 14) {
        sample.baseStationId = parseInt(fields[14]);
        lastBaseStationId_ = sample.baseStationId;
    }
    if (quality == 4) {
        sample.rtk = RtkState::Fixed;
        sample.fix = GnssFix::RtkFixed;
    } else if (quality == 5) {
        sample.rtk = RtkState::Float;
        sample.fix = GnssFix::RtkFloat;
    } else if (quality == 2) {
        sample.rtk = RtkState::None;
        sample.fix = GnssFix::Dgnss;
    } else {
        sample.rtk = RtkState::None;
        sample.fix = GnssFix::Fix3D;
    }
    latestSample_ = sample;
    lastSolutionSteady_ = std::chrono::steady_clock::now();
    lastError_.clear();
}
