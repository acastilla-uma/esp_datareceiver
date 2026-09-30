#include "udp_gateway.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>

namespace {

int failures = 0;

void expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

void expectNear(double actual, double expected, double tolerance,
                const std::string& message) {
    if (std::fabs(actual - expected) > tolerance) {
        std::cerr << "FAIL: " << message << " expected=" << expected
                  << " actual=" << actual << '\n';
        ++failures;
    }
}

void testPhysics() {
    udp_gateway::PhysicalConfig config{1000.0, 1.6, 0.8, 120.0};
    udp_gateway::PhysicsDerived derived;
    std::string error;
    expect(udp_gateway::calculatePhysics(config, &derived, &error),
           "valid physical config calculates");

    const double d1 = std::sqrt(0.8 * 0.8 + 0.8 * 0.8);
    const double ixx = 1000.0 * d1 * d1 + 120.0;
    const double fic = std::atan(1.6 / (2.0 * 0.8)) * 180.0 /
                       3.14159265358979323846;
    expectNear(derived.d1_m, d1, 1e-12, "D1 formula");
    expectNear(derived.ixx_kg_m2, ixx, 1e-9, "Ixx formula");
    expectNear(derived.fic_deg, fic, 1e-12, "FIc formula");
    expectNear(derived.coeff_si, 2.0 * 1000.0 * 9.81 / ixx, 1e-12,
               "Coeff_SI formula");
    expectNear(derived.alfa_deg, 90.0 - fic, 1e-12, "Alfa formula");

    expect(!udp_gateway::calculatePhysics({0.0, 1.6, 0.8, 120.0}, &derived),
           "zero mass is rejected");
    expect(!udp_gateway::calculatePhysics({1000.0, 1.6, 0.0, 120.0}, &derived),
           "zero cg height is rejected");
    expect(!udp_gateway::calculatePhysics({1000.0, 1.6, 0.8, -1.0}, &derived),
           "negative roll inertia is rejected");
}

void testCalibration() {
    udp_gateway::CalibrationState calibration;
    udp_gateway::Orientation current{11.0, -4.0, 91.0};
    udp_gateway::Orientation uncalibrated = calibration.apply(current);
    expectNear(uncalibrated.roll_deg, 11.0, 1e-12,
               "inactive calibration roll");

    calibration.capture({10.0, -5.0, 90.0});
    udp_gateway::Orientation calibrated = calibration.apply(current);
    expectNear(calibrated.roll_deg, 1.0, 1e-12, "calibrated roll offset");
    expectNear(calibrated.pitch_deg, 1.0, 1e-12, "calibrated pitch offset");
    expectNear(calibrated.yaw_deg, 1.0, 1e-12, "calibrated yaw offset");

    calibration.capture({0.0, 0.0, 359.0});
    calibrated = calibration.apply({0.0, 0.0, 1.0});
    expectNear(calibrated.yaw_deg, 2.0, 1e-12,
               "yaw wraps through 360 degrees");
}

void testCommandParsing() {
    const auto calibrate = udp_gateway::parseCommandDatagram(
        "{\"type\":\"calibrate\"}");
    expect(calibrate.type == udp_gateway::CommandType::Calibrate,
           "JSON calibrate command");

    const auto config = udp_gateway::parseCommandDatagram(
        "{\"type\":\"config\",\"mass_kg\":1500,"
        "\"track_width_m\":1.42,\"cg_height_m\":0.74,"
        "\"roll_inertia_kg_m2\":230}");
    expect(config.type == udp_gateway::CommandType::Config,
           "JSON config command");
    expectNear(config.config.mass_kg, 1500.0, 1e-12, "config mass");
    expectNear(config.config.track_width_m, 1.42, 1e-12, "config track width");
    expectNear(config.config.cg_height_m, 0.74, 1e-12, "config cg height");
    expectNear(config.config.roll_inertia_kg_m2, 230.0, 1e-12,
               "config roll inertia");

    const auto textConfig = udp_gateway::parseCommandDatagram(
        "CONFIG mass_kg=1000 track_width_m=1.5 cg_height_m=0.7 "
        "roll_inertia_kg_m2=88");
    expect(textConfig.type == udp_gateway::CommandType::Config,
           "text config command");

    const auto invalidType = udp_gateway::parseCommandDatagram(
        "{\"type\":\"start\"}");
    expect(invalidType.type == udp_gateway::CommandType::Invalid,
           "invalid type rejected");

    const auto invalidValue = udp_gateway::parseCommandDatagram(
        "{\"type\":\"config\",\"mass_kg\":-1,"
        "\"track_width_m\":1,\"cg_height_m\":1,"
        "\"roll_inertia_kg_m2\":1}");
    expect(invalidValue.type == udp_gateway::CommandType::Invalid,
           "invalid config rejected");
}

void testTelemetryJsonWithRtk() {
    udp_gateway::PhysicalConfig config{1000.0, 1.6, 0.8, 120.0};
    udp_gateway::PhysicsDerived physics;
    expect(udp_gateway::calculatePhysics(config, &physics),
           "physics for telemetry");

    udp_gateway::TelemetryPacket packet;
    packet.sequence = 42;
    packet.doback_timestamp_utc = "2026-09-25T10:00:00.200000Z";
    packet.measurement = {{"roll_deg", "1.25"}, {"status", "ok"}};
    packet.raw_orientation = {11.0, -4.0, 91.0};
    packet.orientation = {1.0, 1.0, 1.0};
    packet.gps.available = true;
    packet.gps.timestamp_utc = "2026-09-25T10:00:00.000000Z";
    packet.gps.delta_ms = 200.0;
    packet.gps.latitude_deg = 40.1;
    packet.gps.longitude_deg = -3.2;
    packet.gps.height_ellipsoid_m = 701.2;
    packet.gps.height_msl_m = 652.1;
    packet.gps.h_acc_m = 0.014;
    packet.gps.v_acc_m = 0.022;
    packet.gps.speed_m_s = 0.5;
    packet.gps.heading_deg = 271.0;
    packet.gps.fix = "RTK_FIXED";
    packet.gps.fix_type = 6;
    packet.gps.rtk = "FIXED";
    packet.gps.num_sats = 24;
    packet.gps.pdop = 1.4;
    packet.gps.hdop = 0.8;
    packet.gps.vdop = 1.1;
    packet.gps.correction_age_s = 0.4;
    packet.gps.base_station_id = 1234;
    packet.gnss_status.device_connected = true;
    packet.gnss_status.port = "/dev/serial/by-id/usb-u-blox";
    packet.gnss_status.solution_age_ms = 40.0;
    packet.gnss_status.ntrip_state = "STREAMING";
    packet.gnss_status.ntrip_host = "ergnss-tr.ign.es";
    packet.gnss_status.ntrip_port = 2101;
    packet.gnss_status.ntrip_mountpoint = "VRS3M";
    packet.gnss_status.rtcm_age_ms = 80.0;
    packet.gnss_status.rtcm_bytes = 4096;
    packet.gnss_status.rtcm_messages = 12;
    packet.gnss_status.rtcm_message_type = 1077;
    packet.gnss_status.rtcm_used = true;
    packet.gnss_status.rtcm_crc_failed = false;
    packet.gnss_status.reconnects = 2;
    packet.gnss_status.interference_state = "OK";
    packet.gnss_status.jam_ind = 0;
    packet.config = config;
    packet.physics = physics;
    packet.physics_configured = true;

    const std::string json = udp_gateway::serializeTelemetryJson(packet);
    expect(json.find("\"type\":\"telemetry\"") != std::string::npos,
           "telemetry type present");
    expect(json.find("\"measurement\":{\"roll_deg\":1.25,\"status\":\"ok\"}") !=
               std::string::npos,
           "measurement object serializes numeric and string values");
    expect(json.find("\"gps\":{\"available\":true") != std::string::npos,
           "gps available contract present");
    expect(json.find("\"valid\"") == std::string::npos,
           "legacy gps.valid is not emitted");
    expect(json.find("\"timestamp_utc\":\"2026-09-25T10:00:00.000000Z\"") !=
               std::string::npos,
           "gps timestamp emitted");
    expect(json.find("\"delta_ms\":200") != std::string::npos,
           "signed gps delta emitted");
    expect(json.find("\"latitude_deg\":40.1") != std::string::npos,
           "latitude_deg emitted");
    expect(json.find("\"fix\":\"RTK_FIXED\"") != std::string::npos,
           "RTK fix emitted");
    expect(json.find("\"gnss_status\"") != std::string::npos,
           "gnss_status object present");
    expect(json.find("\"ntrip_host\":\"ergnss-tr.ign.es\"") != std::string::npos,
           "NTRIP host emitted");
    expect(json.find("\"ntrip_mountpoint\":\"VRS3M\"") != std::string::npos,
           "NTRIP mountpoint emitted");
    expect(json.find("\"rtcm_message_type\":1077") != std::string::npos,
           "RTCM message type emitted");
    expect(json.find("\"rtcm_used\":true") != std::string::npos,
           "RTCM used state emitted");
    expect(json.find("\"rtcm_crc_failed\":false") != std::string::npos,
           "RTCM CRC state emitted");
}

void testTelemetryJsonWithoutGpsKeepsMeasurement() {
    udp_gateway::TelemetryPacket packet;
    packet.sequence = 43;
    packet.doback_timestamp_utc = "2026-09-25T10:00:01.000000Z";
    packet.measurement = {{"roll_deg", "2.5"}};
    packet.gps.available = false;
    packet.gnss_status.device_connected = false;
    packet.gnss_status.ntrip_state = "DISABLED";

    const std::string json = udp_gateway::serializeTelemetryJson(packet);
    expect(json.find("\"measurement\":{\"roll_deg\":2.5}") != std::string::npos,
           "measurement is retained without GPS");
    expect(json.find("\"available\":false") != std::string::npos,
           "gps unavailable state emitted");
    expect(json.find("\"timestamp_utc\":null") != std::string::npos,
           "missing gps timestamp is null");
    expect(json.find("\"delta_ms\":null") != std::string::npos,
           "missing gps delta is null");
    expect(json.find("\"device_connected\":false") != std::string::npos,
           "disconnected GNSS state emitted");
}

}  // namespace

int main() {
    testPhysics();
    testCalibration();
    testCommandParsing();
    testTelemetryJsonWithRtk();
    testTelemetryJsonWithoutGpsKeepsMeasurement();

    if (failures != 0) {
        std::cerr << failures << " udp_gateway tests failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "udp_gateway tests passed\n";
    return EXIT_SUCCESS;
}
