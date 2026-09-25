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
    expectNear(uncalibrated.roll_deg, 11.0, 1e-12, "inactive calibration roll");

    calibration.capture({10.0, -5.0, 90.0});
    udp_gateway::Orientation calibrated = calibration.apply(current);
    expectNear(calibrated.roll_deg, 1.0, 1e-12, "calibrated roll offset");
    expectNear(calibrated.pitch_deg, 1.0, 1e-12, "calibrated pitch offset");
    expectNear(calibrated.yaw_deg, 1.0, 1e-12, "calibrated yaw offset");

    calibration.capture({0.0, 0.0, 359.0});
    calibrated = calibration.apply({0.0, 0.0, 1.0});
    expectNear(calibrated.yaw_deg, 2.0, 1e-12, "yaw wraps through 360 degrees");
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

void testTelemetryJson() {
    udp_gateway::PhysicalConfig config{1000.0, 1.6, 0.8, 120.0};
    udp_gateway::PhysicsDerived physics;
    expect(udp_gateway::calculatePhysics(config, &physics),
           "physics for telemetry");

    udp_gateway::TelemetryPacket packet;
    packet.sequence = 42;
    packet.doback_timestamp_utc = "2026-09-25T10:00:00Z";
    packet.measurement = {{"roll_deg", "1.25"}, {"status", "ok"}};
    packet.raw_orientation = {11.0, -4.0, 91.0};
    packet.orientation = {1.0, 1.0, 1.0};
    packet.gps = {true, "2026-09-25T10:00:00Z", 8.0, 40.1, -3.2, 3, 12};
    packet.config = config;
    packet.physics = physics;

    const std::string json = udp_gateway::serializeTelemetryJson(packet);
    expect(json.find("\"type\":\"telemetry\"") != std::string::npos,
           "telemetry type present");
    expect(json.find("\"sequence\":42") != std::string::npos,
           "telemetry sequence present");
    expect(json.find("\"measurement\":{\"roll_deg\":1.25,\"status\":\"ok\"}") !=
               std::string::npos,
           "measurement object serializes numeric and string values");
    expect(json.find("\"orientation\"") != std::string::npos,
           "orientation object present");
    expect(json.find("\"gps\"") != std::string::npos, "gps object present");
    expect(json.find("\"physics\"") != std::string::npos,
           "physics object present");
}

}  // namespace

int main() {
    testPhysics();
    testCalibration();
    testCommandParsing();
    testTelemetryJson();

    if (failures != 0) {
        std::cerr << failures << " udp_gateway tests failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "udp_gateway tests passed\n";
    return EXIT_SUCCESS;
}
