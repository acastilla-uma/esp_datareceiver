#include "udp_gateway.h"

#include <arpa/inet.h>
#include <cerrno>
#include <cmath>
#include <cstring>
#include <fcntl.h>
#include <iomanip>
#include <map>
#include <netinet/in.h>
#include <sstream>
#include <stdexcept>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <limits>
#include <utility>

namespace udp_gateway {
namespace {

constexpr double kGravity = 9.81;
constexpr double kPi = 3.14159265358979323846;
constexpr std::size_t kMaxDatagramBytes = 65536;

std::string trim(const std::string& text) {
    const auto first = std::find_if_not(text.begin(), text.end(), [](unsigned char c) {
        return std::isspace(c);
    });
    const auto last = std::find_if_not(text.rbegin(), text.rend(), [](unsigned char c) {
        return std::isspace(c);
    }).base();
    return first < last ? std::string(first, last) : std::string{};
}

std::string lowercase(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return text;
}

std::string uppercase(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) {
        return static_cast<char>(std::toupper(c));
    });
    return text;
}

bool parseDoubleStrict(const std::string& text, double* value) {
    const std::string cleaned = trim(text);
    if (cleaned.empty()) {
        return false;
    }
    char* end = nullptr;
    errno = 0;
    const double parsed = std::strtod(cleaned.c_str(), &end);
    if (end == cleaned.c_str() || *end != '\0' || errno == ERANGE ||
        !std::isfinite(parsed)) {
        return false;
    }
    *value = parsed;
    return true;
}

bool isPositiveFinite(double value) {
    return std::isfinite(value) && value > 0.0;
}

double wrappedDegrees(double value) {
    value = std::fmod(value + 180.0, 360.0);
    if (value < 0.0) {
        value += 360.0;
    }
    return value - 180.0;
}

std::string jsonEscape(const std::string& text) {
    std::ostringstream out;
    for (unsigned char c : text) {
        switch (c) {
            case '"': out << "\\\""; break;
            case '\\': out << "\\\\"; break;
            case '\b': out << "\\b"; break;
            case '\f': out << "\\f"; break;
            case '\n': out << "\\n"; break;
            case '\r': out << "\\r"; break;
            case '\t': out << "\\t"; break;
            default:
                if (c < 0x20) {
                    out << "\\u" << std::hex << std::setw(4) << std::setfill('0')
                        << static_cast<int>(c) << std::dec << std::setfill(' ');
                } else {
                    out << static_cast<char>(c);
                }
        }
    }
    return out.str();
}

std::string jsonNumber(double value) {
    if (!std::isfinite(value)) {
        return "null";
    }
    std::ostringstream out;
    out << std::setprecision(15) << value;
    return out.str();
}

void appendJsonValue(std::ostringstream& out, const std::string& value) {
    double parsed = 0.0;
    if (parseDoubleStrict(value, &parsed)) {
        out << jsonNumber(parsed);
    } else {
        out << '"' << jsonEscape(value) << '"';
    }
}

class JsonObjectParser {
public:
    explicit JsonObjectParser(std::string text) : text_(std::move(text)) {}

    bool parse(std::map<std::string, std::string>* values, std::string* error) {
        skipSpace();
        if (!consume('{')) {
            setError(error, "JSON debe empezar por '{'");
            return false;
        }
        skipSpace();
        if (consume('}')) {
            return true;
        }
        while (pos_ < text_.size()) {
            std::string key;
            if (!parseString(&key, error)) {
                return false;
            }
            skipSpace();
            if (!consume(':')) {
                setError(error, "Falta ':' en JSON");
                return false;
            }
            skipSpace();
            std::string value;
            if (!parseValue(&value, error)) {
                return false;
            }
            (*values)[key] = value;
            skipSpace();
            if (consume('}')) {
                skipSpace();
                if (pos_ != text_.size()) {
                    setError(error, "Contenido extra tras JSON");
                    return false;
                }
                return true;
            }
            if (!consume(',')) {
                setError(error, "Falta ',' en JSON");
                return false;
            }
            skipSpace();
        }
        setError(error, "JSON incompleto");
        return false;
    }

private:
    void skipSpace() {
        while (pos_ < text_.size() &&
               std::isspace(static_cast<unsigned char>(text_[pos_]))) {
            ++pos_;
        }
    }

    bool consume(char expected) {
        if (pos_ < text_.size() && text_[pos_] == expected) {
            ++pos_;
            return true;
        }
        return false;
    }

    bool parseString(std::string* value, std::string* error) {
        if (!consume('"')) {
            setError(error, "Se esperaba string JSON");
            return false;
        }
        std::ostringstream out;
        while (pos_ < text_.size()) {
            const char c = text_[pos_++];
            if (c == '"') {
                *value = out.str();
                return true;
            }
            if (c != '\\') {
                if (static_cast<unsigned char>(c) < 0x20) {
                    setError(error, "Caracter de control en string JSON");
                    return false;
                }
                out << c;
                continue;
            }
            if (pos_ >= text_.size()) {
                setError(error, "Escape JSON incompleto");
                return false;
            }
            const char escaped = text_[pos_++];
            switch (escaped) {
                case '"': out << '"'; break;
                case '\\': out << '\\'; break;
                case '/': out << '/'; break;
                case 'b': out << '\b'; break;
                case 'f': out << '\f'; break;
                case 'n': out << '\n'; break;
                case 'r': out << '\r'; break;
                case 't': out << '\t'; break;
                default:
                    setError(error, "Escape JSON no soportado");
                    return false;
            }
        }
        setError(error, "String JSON incompleto");
        return false;
    }

    bool parseValue(std::string* value, std::string* error) {
        if (pos_ >= text_.size()) {
            setError(error, "Valor JSON incompleto");
            return false;
        }
        if (text_[pos_] == '"') {
            return parseString(value, error);
        }
        const std::size_t start = pos_;
        while (pos_ < text_.size() && text_[pos_] != ',' && text_[pos_] != '}') {
            ++pos_;
        }
        const std::string literal = trim(text_.substr(start, pos_ - start));
        if (literal.empty()) {
            setError(error, "Valor JSON vacio");
            return false;
        }
        *value = literal;
        return true;
    }

    static void setError(std::string* error, const std::string& message) {
        if (error) {
            *error = message;
        }
    }

    std::string text_;
    std::size_t pos_{0};
};

bool fillConfigFromMap(const std::map<std::string, std::string>& values,
                       PhysicalConfig* config, std::string* error) {
    const char* keys[] = {
        "mass_kg",
        "track_width_m",
        "cg_height_m",
        "roll_inertia_kg_m2",
    };
    double* destinations[] = {
        &config->mass_kg,
        &config->track_width_m,
        &config->cg_height_m,
        &config->roll_inertia_kg_m2,
    };
    for (std::size_t i = 0; i < 4; ++i) {
        const auto found = values.find(keys[i]);
        if (found == values.end() || !parseDoubleStrict(found->second, destinations[i])) {
            if (error) {
                *error = std::string("Campo config invalido o ausente: ") + keys[i];
            }
            return false;
        }
    }
    return isValidPhysicalConfig(*config, error);
}

UdpCommand parseJsonCommand(const std::string& datagram) {
    UdpCommand command;
    std::map<std::string, std::string> values;
    std::string error;
    if (!JsonObjectParser(datagram).parse(&values, &error)) {
        command.error = error;
        return command;
    }
    const auto typeIt = values.find("type");
    if (typeIt == values.end()) {
        command.error = "Falta campo type";
        return command;
    }
    const std::string type = lowercase(trim(typeIt->second));
    if (type == "calibrate") {
        command.type = CommandType::Calibrate;
        return command;
    }
    if (type != "config") {
        command.error = "Tipo de comando no soportado";
        return command;
    }
    PhysicalConfig config;
    if (!fillConfigFromMap(values, &config, &command.error)) {
        return command;
    }
    command.type = CommandType::Config;
    command.config = config;
    return command;
}

UdpCommand parseTextCommand(const std::string& datagram) {
    UdpCommand command;
    std::string text = trim(datagram);
    if (uppercase(text) == "CALIBRATE" || uppercase(text) == "CALIBRAR") {
        command.type = CommandType::Calibrate;
        return command;
    }
    if (uppercase(text).rfind("CONFIG", 0) != 0) {
        command.error = "Comando desconocido";
        return command;
    }
    text = trim(text.substr(6));
    std::replace(text.begin(), text.end(), ',', ' ');
    std::replace(text.begin(), text.end(), ';', ' ');
    std::map<std::string, std::string> values;
    std::istringstream input(text);
    std::string token;
    while (input >> token) {
        const std::size_t separator = token.find('=');
        if (separator == std::string::npos) {
            command.error = "CONFIG texto requiere pares clave=valor";
            return command;
        }
        values[lowercase(trim(token.substr(0, separator)))] = trim(token.substr(separator + 1));
    }
    PhysicalConfig config;
    if (!fillConfigFromMap(values, &config, &command.error)) {
        return command;
    }
    command.type = CommandType::Config;
    command.config = config;
    return command;
}

bool setNonBlocking(int fd, std::string* error) {
    const int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        if (error) {
            *error = std::strerror(errno);
        }
        return false;
    }
    return true;
}

bool parseIpv4(const std::string& ip, in_addr* address) {
    return inet_pton(AF_INET, ip.c_str(), address) == 1;
}

}  // namespace

void CalibrationState::capture(const Orientation& current) {
    zero = current;
    active = true;
}

Orientation CalibrationState::apply(const Orientation& current) const {
    if (!active) {
        return current;
    }
    return {
        wrappedDegrees(current.roll_deg - zero.roll_deg),
        wrappedDegrees(current.pitch_deg - zero.pitch_deg),
        wrappedDegrees(current.yaw_deg - zero.yaw_deg),
    };
}

bool isValidPhysicalConfig(const PhysicalConfig& config, std::string* error) {
    if (!isPositiveFinite(config.mass_kg)) {
        if (error) *error = "mass_kg debe ser finito y positivo";
        return false;
    }
    if (!isPositiveFinite(config.track_width_m)) {
        if (error) *error = "track_width_m debe ser finito y positivo";
        return false;
    }
    if (!isPositiveFinite(config.cg_height_m)) {
        if (error) *error = "cg_height_m debe ser finito y positivo";
        return false;
    }
    if (!std::isfinite(config.roll_inertia_kg_m2) ||
        config.roll_inertia_kg_m2 < 0.0) {
        if (error) *error = "roll_inertia_kg_m2 debe ser finito y no negativo";
        return false;
    }
    return true;
}

bool calculatePhysics(const PhysicalConfig& config, PhysicsDerived* derived,
                      std::string* error) {
    if (!derived) {
        if (error) *error = "derived no puede ser null";
        return false;
    }
    if (!isValidPhysicalConfig(config, error)) {
        return false;
    }
    const double halfTrack = config.track_width_m / 2.0;
    const double d1 = std::sqrt(config.cg_height_m * config.cg_height_m +
                                halfTrack * halfTrack);
    const double ixx = config.mass_kg * d1 * d1 + config.roll_inertia_kg_m2;
    if (!isPositiveFinite(ixx)) {
        if (error) *error = "Ixx calculado no es positivo";
        return false;
    }
    const double fic = std::atan(config.track_width_m /
                                 (2.0 * config.cg_height_m)) * 180.0 / kPi;
    const double coeff = 2.0 * config.mass_kg * kGravity / ixx;
    *derived = {d1, ixx, fic, coeff, 90.0 - fic};
    return std::isfinite(derived->d1_m) && std::isfinite(derived->ixx_kg_m2) &&
           std::isfinite(derived->fic_deg) && std::isfinite(derived->coeff_si) &&
           std::isfinite(derived->alfa_deg);
}

UdpCommand parseCommandDatagram(const std::string& datagram) {
    if (datagram.size() > kMaxDatagramBytes) {
        UdpCommand command;
        command.error = "Datagrama demasiado grande";
        return command;
    }
    const std::string text = trim(datagram);
    if (text.empty()) {
        UdpCommand command;
        command.error = "Datagrama vacio";
        return command;
    }
    if (text.front() == '{') {
        return parseJsonCommand(text);
    }
    return parseTextCommand(text);
}

std::string serializeTelemetryJson(const TelemetryPacket& packet) {
    std::ostringstream out;
    out << "{";
    out << "\"type\":\"telemetry\",";
    out << "\"sequence\":" << packet.sequence << ",";
    out << "\"doback_timestamp_utc\":\"" << jsonEscape(packet.doback_timestamp_utc)
        << "\",";

    out << "\"measurement\":{";
    for (std::size_t i = 0; i < packet.measurement.size(); ++i) {
        if (i != 0) out << ",";
        out << "\"" << jsonEscape(packet.measurement[i].first) << "\":";
        appendJsonValue(out, packet.measurement[i].second);
    }
    out << "},";

    out << "\"orientation\":{";
    out << "\"calibrated\":" << (packet.calibration_active ? "true" : "false")
        << ",";
    out << "\"roll_deg\":" << jsonNumber(packet.orientation.roll_deg) << ",";
    out << "\"pitch_deg\":" << jsonNumber(packet.orientation.pitch_deg) << ",";
    out << "\"yaw_deg\":" << jsonNumber(packet.orientation.yaw_deg) << ",";
    out << "\"raw_roll_deg\":" << jsonNumber(packet.raw_orientation.roll_deg) << ",";
    out << "\"raw_pitch_deg\":" << jsonNumber(packet.raw_orientation.pitch_deg) << ",";
    out << "\"raw_yaw_deg\":" << jsonNumber(packet.raw_orientation.yaw_deg);
    out << "},";

    out << "\"gps\":{";
    out << "\"valid\":" << (packet.gps.valid ? "true" : "false") << ",";
    out << "\"gps_timestamp_utc\":\"" << jsonEscape(packet.gps.gps_timestamp_utc)
        << "\",";
    out << "\"delta_ms\":" << jsonNumber(packet.gps.delta_ms) << ",";
    out << "\"latitude\":" << jsonNumber(packet.gps.latitude) << ",";
    out << "\"longitude\":" << jsonNumber(packet.gps.longitude) << ",";
    out << "\"fix_type\":" << packet.gps.fix_type << ",";
    out << "\"num_sats\":" << packet.gps.num_sats;
    out << "},";

    out << "\"physics\":{";
    out << "\"configured\":" << (packet.physics_configured ? "true" : "false")
        << ",";
    out << "\"mass_kg\":" << jsonNumber(packet.config.mass_kg) << ",";
    out << "\"track_width_m\":" << jsonNumber(packet.config.track_width_m) << ",";
    out << "\"cg_height_m\":" << jsonNumber(packet.config.cg_height_m) << ",";
    out << "\"roll_inertia_kg_m2\":"
        << jsonNumber(packet.config.roll_inertia_kg_m2) << ",";
    out << "\"d1_m\":" << jsonNumber(packet.physics.d1_m) << ",";
    out << "\"ixx_kg_m2\":" << jsonNumber(packet.physics.ixx_kg_m2) << ",";
    out << "\"fic_deg\":" << jsonNumber(packet.physics.fic_deg) << ",";
    out << "\"coeff_si\":" << jsonNumber(packet.physics.coeff_si) << ",";
    out << "\"alfa_deg\":" << jsonNumber(packet.physics.alfa_deg);
    out << "}";
    out << "}";
    return out.str();
}

UdpGateway::UdpGateway(UdpGatewayOptions options) : options_(std::move(options)) {}

UdpGateway::~UdpGateway() {
    close();
}

bool UdpGateway::open(std::string* error) {
    close();

    in_addr pcAddress{};
    in_addr bindAddress{};
    if (!parseIpv4(options_.pc_ip, &pcAddress)) {
        if (error) *error = "IP del PC no valida: " + options_.pc_ip;
        return false;
    }
    if (!parseIpv4(options_.bind_ip, &bindAddress)) {
        if (error) *error = "IP de bind no valida: " + options_.bind_ip;
        return false;
    }

    send_fd_ = socket(AF_INET, SOCK_DGRAM, 0);
    if (send_fd_ < 0) {
        if (error) *error = std::strerror(errno);
        return false;
    }
    if (!setNonBlocking(send_fd_, error)) {
        close();
        return false;
    }

    recv_fd_ = socket(AF_INET, SOCK_DGRAM, 0);
    if (recv_fd_ < 0) {
        if (error) *error = std::strerror(errno);
        close();
        return false;
    }
    if (!setNonBlocking(recv_fd_, error)) {
        close();
        return false;
    }

    int reuse = 1;
    setsockopt(recv_fd_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    sockaddr_in receiveAddress{};
    receiveAddress.sin_family = AF_INET;
    receiveAddress.sin_addr = bindAddress;
    receiveAddress.sin_port = htons(options_.command_port);
    if (bind(recv_fd_, reinterpret_cast<sockaddr*>(&receiveAddress),
             sizeof(receiveAddress)) < 0) {
        if (error) *error = std::strerror(errno);
        close();
        return false;
    }
    return true;
}

void UdpGateway::close() {
    if (send_fd_ >= 0) {
        ::close(send_fd_);
        send_fd_ = -1;
    }
    if (recv_fd_ >= 0) {
        ::close(recv_fd_);
        recv_fd_ = -1;
    }
}

bool UdpGateway::isOpen() const {
    return send_fd_ >= 0 && recv_fd_ >= 0;
}

int UdpGateway::commandFileDescriptor() const {
    return recv_fd_;
}

bool UdpGateway::sendTelemetry(const TelemetryPacket& packet, std::string* error) {
    return sendJson(serializeTelemetryJson(packet), error);
}

bool UdpGateway::sendJson(const std::string& json, std::string* error) {
    if (send_fd_ < 0) {
        if (error) *error = "Gateway UDP no abierto";
        return false;
    }
    if (json.size() > kMaxDatagramBytes) {
        if (error) *error = "JSON de telemetria demasiado grande";
        return false;
    }
    sockaddr_in destination{};
    destination.sin_family = AF_INET;
    destination.sin_port = htons(options_.telemetry_port);
    if (!parseIpv4(options_.pc_ip, &destination.sin_addr)) {
        if (error) *error = "IP del PC no valida";
        return false;
    }
    const ssize_t sent = sendto(send_fd_, json.data(), json.size(), 0,
                                reinterpret_cast<sockaddr*>(&destination),
                                sizeof(destination));
    if (sent < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            if (error) *error = "Socket UDP no listo para enviar";
        } else if (error) {
            *error = std::strerror(errno);
        }
        return false;
    }
    return static_cast<std::size_t>(sent) == json.size();
}

std::vector<UdpCommand> UdpGateway::pollCommands() {
    std::vector<UdpCommand> commands;
    if (recv_fd_ < 0) {
        return commands;
    }

    in_addr allowedAddress{};
    if (!parseIpv4(options_.pc_ip, &allowedAddress)) {
        return commands;
    }

    while (true) {
        char buffer[kMaxDatagramBytes];
        sockaddr_in sender{};
        socklen_t senderLength = sizeof(sender);
        const ssize_t received = recvfrom(recv_fd_, buffer, sizeof(buffer), 0,
                                          reinterpret_cast<sockaddr*>(&sender),
                                          &senderLength);
        if (received < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
                break;
            }
            break;
        }
        if (sender.sin_addr.s_addr != allowedAddress.s_addr) {
            continue;
        }
        char senderIp[INET_ADDRSTRLEN] = {};
        inet_ntop(AF_INET, &sender.sin_addr, senderIp, sizeof(senderIp));
        UdpCommand command = parseCommandDatagram(
            std::string(buffer, buffer + received));
        command.sender_ip = senderIp;
        if (command.type != CommandType::Invalid) {
            commands.push_back(command);
        }
    }
    return commands;
}

}  // namespace udp_gateway
