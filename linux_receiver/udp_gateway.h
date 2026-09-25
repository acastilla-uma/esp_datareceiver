#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace udp_gateway {

struct Orientation {
    double roll_deg{0.0};
    double pitch_deg{0.0};
    double yaw_deg{0.0};
};

struct CalibrationState {
    Orientation zero{};
    bool active{false};

    void capture(const Orientation& current);
    Orientation apply(const Orientation& current) const;
};

struct PhysicalConfig {
    double mass_kg{0.0};
    double track_width_m{0.0};
    double cg_height_m{0.0};
    double roll_inertia_kg_m2{0.0};
};

struct PhysicsDerived {
    double d1_m{0.0};
    double ixx_kg_m2{0.0};
    double fic_deg{0.0};
    double coeff_si{0.0};
    double alfa_deg{0.0};
};

bool isValidPhysicalConfig(const PhysicalConfig& config, std::string* error = nullptr);
bool calculatePhysics(const PhysicalConfig& config, PhysicsDerived* derived,
                      std::string* error = nullptr);

enum class CommandType {
    Calibrate,
    Config,
    Invalid,
};

struct UdpCommand {
    CommandType type{CommandType::Invalid};
    PhysicalConfig config{};
    std::string sender_ip;
    std::string error;
};

UdpCommand parseCommandDatagram(const std::string& datagram);

struct GpsTelemetry {
    bool valid{false};
    std::string gps_timestamp_utc;
    double delta_ms{0.0};
    double latitude{0.0};
    double longitude{0.0};
    int fix_type{0};
    int num_sats{0};
};

struct TelemetryPacket {
    std::uint64_t sequence{0};
    std::string doback_timestamp_utc;
    std::vector<std::pair<std::string, std::string>> measurement;
    Orientation raw_orientation{};
    Orientation orientation{};
    bool calibration_active{false};
    GpsTelemetry gps{};
    PhysicalConfig config{};
    PhysicsDerived physics{};
    bool physics_configured{false};
};

std::string serializeTelemetryJson(const TelemetryPacket& packet);

struct UdpGatewayOptions {
    std::string pc_ip{"192.168.8.20"};
    std::uint16_t telemetry_port{50100};
    std::uint16_t command_port{50101};
    std::string bind_ip{"0.0.0.0"};
};

class UdpGateway {
public:
    explicit UdpGateway(UdpGatewayOptions options = {});
    ~UdpGateway();

    UdpGateway(const UdpGateway&) = delete;
    UdpGateway& operator=(const UdpGateway&) = delete;

    bool open(std::string* error = nullptr);
    void close();
    bool isOpen() const;
    int commandFileDescriptor() const;

    bool sendTelemetry(const TelemetryPacket& packet, std::string* error = nullptr);
    bool sendJson(const std::string& json, std::string* error = nullptr);
    std::vector<UdpCommand> pollCommands();

private:
    UdpGatewayOptions options_;
    int send_fd_{-1};
    int recv_fd_{-1};
};

}  // namespace udp_gateway
