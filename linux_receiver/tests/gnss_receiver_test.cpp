#include "gnss_receiver.h"

#include <cassert>
#include <cmath>
#include <iostream>

namespace {

void appendU2(std::vector<std::uint8_t>& payload, std::size_t offset, std::uint16_t value) {
    payload[offset] = static_cast<std::uint8_t>(value & 0xFF);
    payload[offset + 1] = static_cast<std::uint8_t>((value >> 8) & 0xFF);
}

void appendU4(std::vector<std::uint8_t>& payload, std::size_t offset, std::uint32_t value) {
    payload[offset] = static_cast<std::uint8_t>(value & 0xFF);
    payload[offset + 1] = static_cast<std::uint8_t>((value >> 8) & 0xFF);
    payload[offset + 2] = static_cast<std::uint8_t>((value >> 16) & 0xFF);
    payload[offset + 3] = static_cast<std::uint8_t>((value >> 24) & 0xFF);
}

std::vector<std::uint8_t> ubx(std::uint8_t cls, std::uint8_t id,
                              const std::vector<std::uint8_t>& payload) {
    std::vector<std::uint8_t> frame{0xB5, 0x62, cls, id,
                                   static_cast<std::uint8_t>(payload.size() & 0xFF),
                                   static_cast<std::uint8_t>((payload.size() >> 8) & 0xFF)};
    frame.insert(frame.end(), payload.begin(), payload.end());
    std::uint8_t ckA = 0;
    std::uint8_t ckB = 0;
    for (std::size_t i = 2; i < frame.size(); ++i) {
        ckA = static_cast<std::uint8_t>(ckA + frame[i]);
        ckB = static_cast<std::uint8_t>(ckB + ckA);
    }
    frame.push_back(ckA);
    frame.push_back(ckB);
    return frame;
}

void parsesNavPvtRtkFixed() {
    std::vector<std::uint8_t> payload(92, 0);
    appendU2(payload, 4, 2026);
    payload[6] = 9;
    payload[7] = 30;
    payload[8] = 12;
    payload[9] = 34;
    payload[10] = 56;
    payload[11] = 0x03;
    payload[20] = 3;
    payload[21] = 0x80;
    payload[23] = 22;
    appendU4(payload, 24, static_cast<std::uint32_t>(-40429863));
    appendU4(payload, 28, static_cast<std::uint32_t>(380468168));
    appendU4(payload, 32, 286430);
    appendU4(payload, 36, 236430);
    appendU4(payload, 40, 12);
    appendU4(payload, 44, 25);
    appendU4(payload, 60, 1234);
    appendU4(payload, 64, 9000000);
    appendU2(payload, 76, 145);

    GnssReceiver receiver;
    const auto frame = ubx(0x01, 0x07, payload);
    receiver.ingest(frame);
    const auto sample = receiver.latestSample();
    assert(sample);
    assert(sample->available);
    assert(sample->timestampUtc == "2026-09-30T12:34:56.000000Z");
    assert(std::fabs(sample->latitudeDeg - 38.0468168) < 1e-9);
    assert(std::fabs(sample->longitudeDeg + 4.0429863) < 1e-9);
    assert(sample->rtk == RtkState::Fixed);
    assert(sample->fix == GnssFix::RtkFixed);
    assert(sample->numSats == 22);
    assert(sample->hAccM && std::fabs(*sample->hAccM - 0.012) < 1e-9);
    assert(sample->pdop && std::fabs(*sample->pdop - 1.45) < 1e-9);
}

void rejectsBadChecksum() {
    std::vector<std::uint8_t> payload(92, 0);
    appendU2(payload, 4, 2026);
    payload[6] = 9;
    payload[7] = 30;
    payload[11] = 0x03;
    auto frame = ubx(0x01, 0x07, payload);
    frame.back() ^= 0xFF;
    GnssReceiver receiver;
    receiver.ingest(frame);
    assert(!receiver.latestSample());
    assert(receiver.snapshot().lastError == "checksum UBX inválido");
}

void parsesDopRtcmRfAndNmeaGga() {
    GnssReceiver receiver;

    std::vector<std::uint8_t> dop(18, 0);
    appendU2(dop, 6, 123);
    appendU2(dop, 10, 234);
    appendU2(dop, 12, 145);
    receiver.ingest(ubx(0x01, 0x04, dop));
    auto sample = receiver.latestSample();
    assert(sample);
    assert(sample->pdop && *sample->pdop == 1.23);
    assert(sample->vdop && *sample->vdop == 2.34);
    assert(sample->hdop && *sample->hdop == 1.45);

    std::vector<std::uint8_t> rtcm(8, 0);
    rtcm[1] = 0x04;  // msgUsed=2 (used), crcFailed=0.
    appendU2(rtcm, 4, 321);
    appendU2(rtcm, 6, 1077);
    receiver.ingest(ubx(0x02, 0x32, rtcm));
    sample = receiver.latestSample();
    assert(sample->baseStationId && *sample->baseStationId == 321);
    assert(receiver.snapshot().rtcmMessages == 1);
    assert(receiver.snapshot().lastRtcmMessageType == 1077);
    assert(receiver.snapshot().lastRtcmUsed == true);
    assert(receiver.snapshot().lastRtcmCrcFailed == false);

    std::vector<std::uint8_t> rf(28, 0);
    rf[1] = 1;
    rf[16] = 83;
    receiver.ingest(ubx(0x0A, 0x38, rf));
    assert(receiver.snapshot().jamInd && *receiver.snapshot().jamInd == 83);
    assert(receiver.snapshot().interferenceState == "CRITICAL");

    receiver.ingestText("$GNGGA,123519,3802.809008,N,00402.579178,W,5,18,0.7,236.4,M,50.0,M,1.2,42*67\r\n");
    sample = receiver.latestSample();
    assert(sample);
    assert(sample->rtk == RtkState::Float);
    assert(sample->fix == GnssFix::RtkFloat);
    assert(sample->correctionAgeS && std::fabs(*sample->correctionAgeS - 1.2) < 1e-9);
    assert(sample->baseStationId && *sample->baseStationId == 42);
    assert(receiver.latestGgaSentence().find("GNGGA") != std::string::npos);
}

void buildsRateCommand() {
    GnssReceiver receiver;
    const auto command = receiver.buildRate5HzCommand();
    assert(command.size() == 14);
    assert(command[0] == 0xB5 && command[1] == 0x62);
    assert(command[2] == 0x06 && command[3] == 0x08);
    assert(command[6] == 0xC8 && command[7] == 0x00);

    const auto startup = receiver.buildStartupCommands();
    assert(startup.size() > command.size());
    assert(std::equal(command.begin(), command.end(), startup.begin()));
    // NAV-PVT CFG-MSG follows the CFG-RATE frame and enables USB output.
    assert(startup[14] == 0xB5 && startup[15] == 0x62);
    assert(startup[16] == 0x06 && startup[17] == 0x01);
    assert(startup[20] == 0x01 && startup[21] == 0x07);
    assert(startup[25] == 0x01);
    // The final CFG-MSG enables NMEA-GGA on USB for VRS/NTRIP.
    assert(startup[startup.size() - 10] == 0xF0);
    assert(startup[startup.size() - 9] == 0x00);
    assert(startup[startup.size() - 5] == 0x01);
}

}  // namespace

int main() {
    parsesNavPvtRtkFixed();
    rejectsBadChecksum();
    parsesDopRtcmRfAndNmeaGga();
    buildsRateCommand();
    std::cout << "gnss_receiver_test: OK\n";
}
