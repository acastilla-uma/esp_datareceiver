#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

enum class GnssFix {
    NoFix,
    Fix2D,
    Fix3D,
    Dgnss,
    RtkFloat,
    RtkFixed,
};

enum class RtkState {
    None,
    Float,
    Fixed,
};

struct GnssSample {
    bool available{false};
    std::chrono::system_clock::time_point timestamp;
    std::string timestampUtc;
    double latitudeDeg{0.0};
    double longitudeDeg{0.0};
    std::optional<double> heightEllipsoidM;
    std::optional<double> heightMslM;
    std::optional<double> hAccM;
    std::optional<double> vAccM;
    std::optional<double> speedMS;
    std::optional<double> headingDeg;
    GnssFix fix{GnssFix::NoFix};
    int fixType{0};
    RtkState rtk{RtkState::None};
    int numSats{0};
    std::optional<double> pdop;
    std::optional<double> hdop;
    std::optional<double> vdop;
    std::optional<double> correctionAgeS;
    std::optional<int> baseStationId;
};

struct GnssQualitySnapshot {
    bool hasSample{false};
    GnssSample sample;
    std::optional<std::chrono::steady_clock::time_point> lastSolutionSteady;
    std::optional<std::chrono::steady_clock::time_point> lastRtcmSteady;
    std::uint64_t rtcmMessages{0};
    std::optional<int> lastRtcmMessageType;
    std::optional<bool> lastRtcmUsed;
    std::optional<bool> lastRtcmCrcFailed;
    std::optional<int> jamInd;
    std::string interferenceState;
    std::string lastError;
    std::string lastGgaSentence;
};

std::string gnssFixToString(GnssFix fix);
std::string rtkStateToString(RtkState state);
std::string formatGnssUtc(std::chrono::system_clock::time_point timestamp);

class GnssReceiver {
public:
    GnssReceiver();

    void ingest(const std::uint8_t* data, std::size_t size);
    void ingest(const std::vector<std::uint8_t>& data);
    void ingestText(const std::string& data);

    std::optional<GnssSample> latestSample() const;
    GnssQualitySnapshot snapshot() const;
    std::string latestGgaSentence() const;
    std::vector<std::uint8_t> buildRate5HzCommand() const;
    std::vector<std::uint8_t> buildStartupCommands() const;

private:
    enum class ParserState {
        Search,
        UbxSync2,
        UbxClass,
        UbxId,
        UbxLen1,
        UbxLen2,
        UbxPayload,
        UbxCkA,
        UbxCkB,
        Nmea,
    };

    void ingestByte(std::uint8_t byte);
    void resetUbx();
    void handleUbx(std::uint8_t cls, std::uint8_t id,
                   const std::vector<std::uint8_t>& payload);
    void handleNmeaLine(const std::string& line);

    ParserState state_{ParserState::Search};
    std::uint8_t ubxClass_{0};
    std::uint8_t ubxId_{0};
    std::uint16_t ubxLength_{0};
    std::vector<std::uint8_t> ubxPayload_;
    std::uint8_t ckA_{0};
    std::uint8_t ckB_{0};
    std::uint8_t receivedCkA_{0};
    std::string nmeaLine_;

    std::optional<GnssSample> latestSample_;
    std::optional<std::chrono::steady_clock::time_point> lastSolutionSteady_;
    std::optional<std::chrono::steady_clock::time_point> lastRtcmSteady_;
    std::uint64_t rtcmMessages_{0};
    std::optional<int> lastRtcmMessageType_;
    std::optional<bool> lastRtcmUsed_;
    std::optional<bool> lastRtcmCrcFailed_;
    std::optional<int> lastBaseStationId_;
    std::optional<int> jamInd_;
    std::string interferenceState_;
    std::string lastError_;
    std::string latestGgaSentence_;
};
