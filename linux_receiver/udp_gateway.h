#pragma once

#include <cstdint>
#include <optional>
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
    bool available{false};
    std::optional<std::string> timestamp_utc;
    std::optional<double> delta_ms;
    std::optional<double> latitude_deg;
    std::optional<double> longitude_deg;
    std::optional<double> height_ellipsoid_m;
    std::optional<double> height_msl_m;
    std::optional<double> h_acc_m;
    std::optional<double> v_acc_m;
    std::optional<double> speed_m_s;
    std::optional<double> heading_deg;
    std::string fix{"NO_FIX"};
    int fix_type{0};
    std::string rtk{"NONE"};
    std::optional<int> num_sats;
    std::optional<double> pdop;
    std::optional<double> hdop;
    std::optional<double> vdop;
    std::optional<double> correction_age_s;
    std::optional<int> base_station_id;
};

struct GnssStatusTelemetry {
    bool device_connected{false};
    std::string port;
    std::optional<double> solution_age_ms;
    std::string ntrip_state{"DISABLED"};
    std::string ntrip_host;
    std::optional<int> ntrip_port;
    std::string ntrip_mountpoint;
    std::optional<double> rtcm_age_ms;
    std::optional<std::uint64_t> rtcm_bytes;
    std::optional<std::uint64_t> rtcm_messages;
    std::optional<int> rtcm_message_type;
    std::optional<bool> rtcm_used;
    std::optional<bool> rtcm_crc_failed;
    std::optional<std::uint64_t> reconnects;
    std::string last_error;
    std::optional<std::string> interference_state;
    std::optional<int> jam_ind;
};

struct TelemetryPacket {
    std::uint64_t sequence{0};
    std::string doback_timestamp_utc;
    std::vector<std::pair<std::string, std::string>> measurement;
    Orientation raw_orientation{};
    Orientation orientation{};
    bool calibration_active{false};
    GpsTelemetry gps{};
    GnssStatusTelemetry gnss_status{};
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
