#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cctype>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <deque>
#include <fcntl.h>
#include <glob.h>
#include <iomanip>
#include <iostream>
#include <memory>
#include <optional>
#include <poll.h>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>
#include <utility>
#include <vector>

#include "gnss_receiver.h"
#include "gps_matcher.h"
#include "ntrip_client.h"
#include "serial_format.h"
#include "udp_gateway.h"

namespace {

std::atomic<bool> running{true};

const std::vector<std::string> LEGACY_FIELDS{
    "ax", "ay", "az", "gx", "gy", "gz", "roll", "pitch", "yaw",
    "timeantwifi", "usciclo1", "usciclo2", "usciclo3", "usciclo4",
    "usciclo5", "si", "accmag", "microsds", "k3"
};

const std::vector<std::string> DEFAULT_FIELDS{
    "timestamp_us", "ax_g", "ay_g", "az_g", "gx_deg_s", "gy_deg_s",
    "gz_deg_s", "roll_deg", "pitch_deg", "yaw_deg", "usciclo1_us",
    "usciclo2_us", "usciclo3_us", "usciclo4_us", "usciclo5_us", "si",
    "accmag_g", "microsds_us"
};

const std::vector<std::string> GPS_FIELDS{
    "doback_timestamp_utc", "gps_timestamp_utc", "gps_delta_ms",
    "latitude", "longitude", "gps_fix", "gps_rtk", "gps_h_acc_m",
    "gps_num_sats", "ntrip_state", "rtcm_age_ms"
};

struct Options {
    std::string espPort{"auto"};
    std::string gnssPort{"auto"};
    std::chrono::milliseconds gpsMaximumDifference{200};
    bool gnssEnabled{true};
    bool ntripEnabled{true};
    std::string ntripHost{"ergnss-tr.ign.es"};
    int ntripPort{2101};
    std::string ntripMountpoint{"VRS3M"};
    bool udpEnabled{true};
    std::string udpHost{"192.168.8.20"};
    std::uint16_t udpTelemetryPort{50100};
    std::uint16_t udpCommandPort{50101};
};

void signalHandler(int) {
    running = false;
}

std::string trim(const std::string& text) {
    const auto first = std::find_if_not(text.begin(), text.end(), [](unsigned char c) {
        return std::isspace(c);
    });
    const auto last = std::find_if_not(text.rbegin(), text.rend(), [](unsigned char c) {
        return std::isspace(c);
    }).base();
    return first < last ? std::string(first, last) : std::string{};
}

std::vector<std::string> splitFields(const std::string& line) {
    std::vector<std::string> fields;
    std::size_t start = 0;
    while (start <= line.size()) {
        const std::size_t separator = line.find(';', start);
        fields.push_back(trim(line.substr(start, separator - start)));
        if (separator == std::string::npos) {
            break;
        }
        start = separator + 1;
    }
    while (!fields.empty() && fields.back().empty()) {
        fields.pop_back();
    }
    return fields;
}

std::string lowercase(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return text;
}

bool isNumber(const std::string& value) {
    if (value.empty()) {
        return false;
    }
    char* end = nullptr;
    std::strtod(value.c_str(), &end);
    return end != value.c_str() && *end == '\0';
}

bool isMeasurement(const std::vector<std::string>& values,
                   const std::vector<std::string>& fields) {
    return values.size() == fields.size() &&
           std::all_of(values.begin(), values.end(), isNumber);
}

const std::vector<std::string>* knownSchemaFor(
    const std::vector<std::string>& values) {
    if (!std::all_of(values.begin(), values.end(), isNumber)) {
        return nullptr;
    }
    if (values.size() == DEFAULT_FIELDS.size()) {
        return &DEFAULT_FIELDS;
    }
    if (values.size() == LEGACY_FIELDS.size()) {
        return &LEGACY_FIELDS;
    }
    return nullptr;
}

std::string friendlyFieldName(const std::string& field) {
    const std::string key = lowercase(trim(field));
    if (key == "ax") return "Aceleración X [ax]";
    if (key == "ay") return "Aceleración Y [ay]";
    if (key == "az") return "Aceleración Z [az]";
    if (key == "gx") return "Giro X [gx]";
    if (key == "gy") return "Giro Y [gy]";
    if (key == "gz") return "Giro Z [gz]";
    if (key == "roll") return "Inclinación lateral [roll]";
    if (key == "pitch") return "Inclinación frontal [pitch]";
    if (key == "yaw") return "Orientación [yaw]";
    if (key == "timeantwifi") return "Tiempo desde WiFi";
    if (key == "usciclo1") return "Tiempo de ciclo 1";
    if (key == "usciclo2") return "Tiempo de ciclo 2";
    if (key == "usciclo3") return "Tiempo de ciclo 3";
    if (key == "usciclo4") return "Tiempo de ciclo 4";
    if (key == "usciclo5") return "Tiempo de ciclo 5";
    if (key == "si") return "Índice de estabilidad [si]";
    if (key == "accmag") return "Magnitud de aceleración";
    if (key == "microsds") return "Tiempo de escritura SD";
    if (key == "k3") return "Factor K3";
    if (key == "timestamp_us") return "Timestamp ESP32 [µs]";
    if (key == "ax_g") return "Aceleración X [g]";
    if (key == "ay_g") return "Aceleración Y [g]";
    if (key == "az_g") return "Aceleración Z [g]";
    if (key == "gx_deg_s") return "Giro X [°/s]";
    if (key == "gy_deg_s") return "Giro Y [°/s]";
    if (key == "gz_deg_s") return "Giro Z [°/s]";
    if (key == "roll_deg") return "Inclinación lateral [°]";
    if (key == "pitch_deg") return "Inclinación frontal [°]";
    if (key == "yaw_deg") return "Orientación [°]";
    if (key == "accmag_g") return "Magnitud de aceleración [g]";
    if (key == "microsds_us") return "Tiempo de escritura SD [µs]";
    if (key == "doback_timestamp_utc") return "Timestamp DOBACK [UTC]";
    if (key == "gps_timestamp_utc") return "Timestamp GPS [UTC]";
    if (key == "gps_delta_ms") return "Diferencia DOBACK-GPS [ms]";
    if (key == "latitude") return "Latitud GPS";
    if (key == "longitude") return "Longitud GPS";
    if (key == "gps_fix") return "Solución GNSS";
    if (key == "gps_rtk") return "Estado RTK";
    if (key == "gps_h_acc_m") return "Precisión horizontal [m]";
    if (key == "gps_num_sats") return "Satélites GPS";
    if (key == "ntrip_state") return "Estado NTRIP";
    if (key == "rtcm_age_ms") return "Edad RTCM [ms]";
    return field;
}

std::string currentTime() {
    const std::time_t now = std::time(nullptr);
    std::tm localTime{};
    localtime_r(&now, &localTime);
    char formatted[16];
    std::strftime(formatted, sizeof(formatted), "%H:%M:%S", &localTime);
    return formatted;
}

void printUsage(const char* program) {
    std::cout
        << "Uso: " << program << " [auto|PUERTO_ESP32] [opciones]\n\n"
        << "Puertos:\n"
        << "  --esp-port RUTA         Puerto ESP32 o 'auto'\n"
        << "  --gnss-port RUTA        Puerto simpleRTK2B o 'auto'\n"
        << "  --no-gnss               Ejecuta sin receptor GNSS\n"
        << "NTRIP (credenciales en NTRIP_USERNAME/NTRIP_PASSWORD):\n"
        << "  --ntrip-host HOST       Caster (ergnss-tr.ign.es)\n"
        << "  --ntrip-port N          Puerto (2101)\n"
        << "  --ntrip-mountpoint ID   Punto de montaje (VRS3M)\n"
        << "  --no-ntrip              No solicitar correcciones\n"
        << "UDP:\n"
        << "  --udp-host IP           IP del dashboard Windows (192.168.8.20)\n"
        << "  --udp-port N            Puerto de telemetría (50100)\n"
        << "  --udp-command-port N    Puerto de comandos (50101)\n"
        << "  --no-udp                Solo interfaz de terminal\n"
        << "  -h, --help              Muestra esta ayuda\n";
}

std::optional<std::uint16_t> parsePort(const std::string& text) {
    try {
        const long value = std::stol(text);
        if (value < 1 || value > 65535) return std::nullopt;
        return static_cast<std::uint16_t>(value);
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

std::optional<Options> parseOptions(int argc, char* argv[]) {
    Options options;
    bool serialPortSeen = false;
    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        if (argument == "-h" || argument == "--help") {
            printUsage(argv[0]);
            return std::nullopt;
        }
        if (argument == "--no-gnss") {
            options.gnssEnabled = false;
            continue;
        }
        if (argument == "--no-ntrip") {
            options.ntripEnabled = false;
            continue;
        }
        if (argument == "--no-udp") {
            options.udpEnabled = false;
            continue;
        }
        if (argument == "--esp-port" || argument == "--gnss-port" ||
            argument == "--ntrip-host" || argument == "--ntrip-port" ||
            argument == "--ntrip-mountpoint" || argument == "--udp-host" ||
            argument == "--udp-port" || argument == "--udp-command-port") {
            if (++index >= argc) {
                std::cerr << "Falta el valor de " << argument << ".\n";
                return std::nullopt;
            }
            const std::string value = argv[index];
            if (argument == "--esp-port") {
                options.espPort = value;
            } else if (argument == "--gnss-port") {
                options.gnssPort = value;
            } else if (argument == "--ntrip-host") {
                options.ntripHost = value;
            } else if (argument == "--ntrip-mountpoint") {
                options.ntripMountpoint = value;
            } else if (argument == "--udp-host") {
                options.udpHost = value;
            } else {
                const auto port = parsePort(value);
                if (!port) {
                    std::cerr << argument << " debe estar entre 1 y 65535.\n";
                    return std::nullopt;
                }
                if (argument == "--ntrip-port") options.ntripPort = *port;
                if (argument == "--udp-port") options.udpTelemetryPort = *port;
                if (argument == "--udp-command-port") options.udpCommandPort = *port;
            }
            continue;
        }
        if (!argument.empty() && argument[0] == '-') {
            std::cerr << "Opción desconocida: " << argument << "\n";
            return std::nullopt;
        }
        if (serialPortSeen) {
            std::cerr << "Solo se puede indicar un puerto serial.\n";
            return std::nullopt;
        }
        options.espPort = argument;
        serialPortSeen = true;
    }
    return options;
}

std::optional<double> measurementValue(
    const std::vector<std::string>& fields,
    const std::vector<std::string>& values,
    const std::vector<std::string>& candidates) {
    for (std::size_t index = 0; index < fields.size() && index < values.size(); ++index) {
        const std::string field = lowercase(trim(fields[index]));
        if (std::find(candidates.begin(), candidates.end(), field) == candidates.end()) {
            continue;
        }
        char* end = nullptr;
        const double value = std::strtod(values[index].c_str(), &end);
        if (end != values[index].c_str() && *end == '\0') return value;
    }
    return std::nullopt;
}

std::optional<udp_gateway::Orientation> measurementOrientation(
    const std::vector<std::string>& fields,
    const std::vector<std::string>& values) {
    const auto roll = measurementValue(fields, values, {"roll", "roll_deg"});
    const auto pitch = measurementValue(fields, values, {"pitch", "pitch_deg"});
    const auto yaw = measurementValue(fields, values, {"yaw", "yaw_deg"});
    if (!roll || !pitch || !yaw) return std::nullopt;
    return udp_gateway::Orientation{*roll, *pitch, *yaw};
}

std::vector<std::string> displayFields(const std::vector<std::string>& fields,
                                       bool gpsEnabled) {
    std::vector<std::string> result = fields;
    if (gpsEnabled) {
        result.insert(result.end(), GPS_FIELDS.begin(), GPS_FIELDS.end());
    }
    return result;
}

std::string decimal(double value, int precision) {
    std::ostringstream output;
    output << std::fixed << std::setprecision(precision) << value;
    return output.str();
}

std::vector<std::string> displayValues(
    const std::vector<std::string>& values,
    const std::optional<std::chrono::system_clock::time_point>& dobackTimestamp,
    const GpsMatcher& gpsMatcher,
    std::chrono::milliseconds maximumDifference,
    bool gpsEnabled,
    const NtripStatus& ntripStatus) {
    if (values.empty() || !gpsEnabled || !dobackTimestamp) {
        return values;
    }

    std::vector<std::string> result = values;
    result.push_back(formatUtcTimestamp(*dobackTimestamp));
    const auto match = gpsMatcher.nearest(*dobackTimestamp, maximumDifference);
    if (!match) {
        result.insert(result.end(), {"—", "—", "—", "—", "NO_FIX",
                                     "NONE", "—", "—"});
    } else {
        result.push_back(match->sample.timestampUtc);
        result.push_back(std::to_string(match->deltaMilliseconds));
        result.push_back(match->sample.latitudeDeg
                             ? decimal(*match->sample.latitudeDeg, 7)
                             : "—");
        result.push_back(match->sample.longitudeDeg
                             ? decimal(*match->sample.longitudeDeg, 7)
                             : "—");
        result.push_back(match->sample.fix);
        result.push_back(match->sample.rtk);
        result.push_back(match->sample.hAccM ? decimal(*match->sample.hAccM, 3) : "—");
        result.push_back(std::to_string(match->sample.numSats));
    }
    result.push_back(ntripStateToString(ntripStatus.state));
    if (ntripStatus.lastRtcmSteady) {
        const auto age = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - *ntripStatus.lastRtcmSteady);
        result.push_back(std::to_string(age.count()));
    } else {
        result.push_back("—");
    }
    return result;
}

std::string shorten(const std::string& text, std::size_t width) {
    if (text.size() <= width) {
        return text;
    }
    if (width <= 3) {
        return text.substr(0, width);
    }
    return text.substr(0, width - 3) + "...";
}

class Dashboard {
public:
    explicit Dashboard(std::string port)
        : port_(std::move(port)), interactive_(isatty(STDOUT_FILENO)) {
        if (interactive_) {
            std::cout << "\033[?25l";
        }
    }

    ~Dashboard() {
        finish();
    }

    void render(const std::vector<std::string>& fields,
                const std::vector<std::string>& values,
                unsigned long sampleCount,
                unsigned int idleSeconds,
                const std::string& updatedAt) const {
        if (!interactive_) {
            if (!values.empty()) {
                std::cout << "[Muestra " << sampleCount << "] ";
                for (std::size_t i = 0; i < values.size(); ++i) {
                    if (i != 0) std::cout << " | ";
                    std::cout << fields[i] << '=' << values[i];
                }
                std::cout << '\n';
            }
            return;
        }

        const int width = terminalWidth();
        const int columns = width >= 108 ? 3 : (width >= 72 ? 2 : 1);
        const int columnWidth = std::max(30, (width - (columns - 1) * 3) / columns);
        const int separatorWidth = std::max(38, std::min(width, 140));

        std::cout << "\033[H\033[J"
                  << "\033[1;36m  ESP32 · MONITOR DE ESTABILIDAD\033[0m\n"
                  << "\033[2m" << std::string(separatorWidth, '-') << "\033[0m\n";

        if (values.empty()) {
            std::cout << "  Estado: \033[1;33m● ESPERANDO LA PRIMERA MEDICIÓN\033[0m\n";
        } else if (idleSeconds == 0) {
            std::cout << "  Estado: \033[1;32m● RECIBIENDO DATOS\033[0m\n";
        } else {
            std::cout << "  Estado: \033[1;33m● ÚLTIMO DATO HACE " << idleSeconds
                      << " s\033[0m\n";
        }
        std::cout << "  Puerto: \033[1m" << port_ << "\033[0m  ·  115200 baud\n"
                  << "  Muestra: \033[1m" << sampleCount << "\033[0m  ·  Actualizada: \033[1m"
                  << updatedAt << "\033[0m\n\n"
                  << "  \033[1;37mMEDICIÓN ACTUAL\033[0m\n"
                  << "\033[2m" << std::string(separatorWidth, '-') << "\033[0m\n";

        for (std::size_t row = 0; row * columns < fields.size(); ++row) {
            for (int column = 0; column < columns; ++column) {
                const std::size_t index = row * columns + column;
                if (index >= fields.size()) break;
                if (column != 0) std::cout << " │ ";
                renderCell(friendlyFieldName(fields[index]),
                           index < values.size() ? values[index] : "—",
                           columnWidth);
            }
            std::cout << '\n';
        }

        std::cout << "\033[2m" << std::string(separatorWidth, '-') << "\033[0m\n"
                  << "  \033[2mCtrl+C para salir · Solo se muestra la medición más reciente\033[0m"
                  << std::flush;
    }

    void finish() {
        if (interactive_ && !finished_) {
            std::cout << "\033[0m\033[?25h\n" << std::flush;
            finished_ = true;
        }
    }

private:
    int terminalWidth() const {
        winsize size{};
        if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &size) == 0 && size.ws_col > 0) {
            return std::max(40, static_cast<int>(size.ws_col));
        }
        return 100;
    }

    static void renderCell(const std::string& label,
                           const std::string& value,
                           int width) {
        const int valueWidth = std::min(14, std::max(8, width / 3));
        const int labelWidth = std::max(8, width - valueWidth - 3);
        std::cout << "  \033[37m" << std::left << std::setw(labelWidth)
                  << shorten(label, labelWidth) << "\033[0m "
                  << "\033[1;36m" << std::right << std::setw(valueWidth)
                  << shorten(value, valueWidth) << "\033[0m";
    }

    std::string port_;
    bool interactive_;
    bool finished_{false};
};

std::vector<std::string> globPaths(const char* pattern) {
    glob_t matches{};
    std::vector<std::string> paths;
    if (glob(pattern, 0, nullptr, &matches) == 0) {
        for (std::size_t i = 0; i < matches.gl_pathc; ++i) {
            paths.emplace_back(matches.gl_pathv[i]);
        }
    }
    globfree(&matches);
    return paths;
}

std::vector<std::string> findSerialPorts() {
    std::vector<std::string> ports;
    std::set<std::string> devices;
    for (const char* pattern : {"/dev/serial/by-id/*", "/dev/ttyACM*", "/dev/ttyUSB*"}) {
        for (const auto& path : globPaths(pattern)) {
            char* resolved = realpath(path.c_str(), nullptr);
            const std::string device = resolved ? resolved : path;
            std::free(resolved);
            if (devices.insert(device).second) {
                ports.push_back(path);
            }
        }
    }
    return ports;
}

std::string resolvedDevice(const std::string& path) {
    char* resolved = realpath(path.c_str(), nullptr);
    if (!resolved) return path;
    const std::string result(resolved);
    std::free(resolved);
    return result;
}

bool sameDevice(const std::string& left, const std::string& right) {
    return !left.empty() && !right.empty() && resolvedDevice(left) == resolvedDevice(right);
}

bool isLikelyGnssPort(const std::string& path) {
    const std::string key = lowercase(path);
    return key.find("u-blox") != std::string::npos ||
           key.find("ublox") != std::string::npos ||
           key.find("zed-f9") != std::string::npos ||
           key.find("ardusimple") != std::string::npos;
}

bool isLikelyEspPort(const std::string& path) {
    const std::string key = lowercase(path);
    return key.find("ch340") != std::string::npos ||
           key.find("ch341") != std::string::npos ||
           key.find("1a86") != std::string::npos ||
           key.find("cp210") != std::string::npos ||
           key.find("esp32") != std::string::npos ||
           key.find("/dev/ttyusb") != std::string::npos;
}

std::string detectGnssPort(const std::vector<std::string>& ports) {
    const auto found = std::find_if(ports.begin(), ports.end(), isLikelyGnssPort);
    return found == ports.end() ? std::string{} : *found;
}

int setupSerial(const std::string& portname) {
    const int fd = open(portname.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) {
        std::cerr << "Error abriendo " << portname << ": " << std::strerror(errno) << '\n';
        if (errno == EACCES) {
            std::cerr << "Comprueba que tu sesión pertenece al grupo dialout.\n";
        }
        return -1;
    }

    termios tty{};
    if (tcgetattr(fd, &tty) != 0) {
        std::cerr << "Error leyendo la configuración serial: " << std::strerror(errno) << '\n';
        close(fd);
        return -1;
    }

    cfmakeraw(&tty);
    cfsetospeed(&tty, B115200);
    cfsetispeed(&tty, B115200);
    tty.c_cflag = (tty.c_cflag & ~CSIZE) | CS8;
    tty.c_cflag |= CLOCAL | CREAD;
    tty.c_cflag &= ~(PARENB | CSTOPB | CRTSCTS);
    tty.c_cc[VMIN] = 0;
    tty.c_cc[VTIME] = 0;

    if (tcsetattr(fd, TCSANOW, &tty) != 0) {
        std::cerr << "Error configurando el puerto: " << std::strerror(errno) << '\n';
        close(fd);
        return -1;
    }
    tcflush(fd, TCIFLUSH);
    return fd;
}

void printDetectedPorts(const std::vector<std::string>& ports) {
    if (ports.empty()) {
        std::cerr << "No se encontró ningún /dev/ttyACM*, /dev/ttyUSB* ni /dev/serial/by-id/*.\n"
                  << "Comprueba el cable USB-C (debe transmitir datos), alimentación y el driver USB.\n";
        return;
    }
    std::cerr << "Puertos detectados:\n";
    for (const auto& port : ports) {
        std::cerr << "  " << port << '\n';
    }
}

GpsSample matcherSample(const GnssSample& source) {
    GpsSample sample;
    sample.timestamp = source.timestamp;
    sample.timestampUtc = source.timestampUtc;
    sample.latitudeDeg = source.latitudeDeg;
    sample.longitudeDeg = source.longitudeDeg;
    sample.heightEllipsoidM = source.heightEllipsoidM;
    sample.heightMslM = source.heightMslM;
    sample.hAccM = source.hAccM;
    sample.vAccM = source.vAccM;
    sample.speedMS = source.speedMS;
    sample.headingDeg = source.headingDeg;
    sample.fix = gnssFixToString(source.fix);
    sample.fixType = source.fixType;
    sample.rtk = rtkStateToString(source.rtk);
    sample.numSats = source.numSats;
    sample.pdop = source.pdop;
    sample.hdop = source.hdop;
    sample.vdop = source.vdop;
    sample.correctionAgeS = source.correctionAgeS;
    sample.baseStationId = source.baseStationId;
    return sample;
}

std::optional<double> steadyAgeMilliseconds(
    const std::optional<std::chrono::steady_clock::time_point>& timestamp) {
    if (!timestamp) return std::nullopt;
    return std::chrono::duration<double, std::milli>(
               std::chrono::steady_clock::now() - *timestamp)
        .count();
}

}  // namespace

int main(int argc, char* argv[]) {
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    const auto options = parseOptions(argc, argv);
    if (!options) {
        return argc > 1 && (std::string(argv[1]) == "-h" ||
                            std::string(argv[1]) == "--help")
                   ? 0
                   : 1;
    }

    const auto ports = findSerialPorts();
    std::string gnssPortname;
    if (options->gnssEnabled) {
        gnssPortname = options->gnssPort == "auto"
                           ? detectGnssPort(ports)
                           : options->gnssPort;
    }

    std::string portname;
    int fd = -1;
    if (options->espPort != "auto") {
        portname = options->espPort;
        if (!sameDevice(portname, gnssPortname)) fd = setupSerial(portname);
    } else {
        for (int preferred = 1; preferred >= 0 && fd < 0; --preferred) {
            for (const auto& candidate : ports) {
                if (sameDevice(candidate, gnssPortname) || isLikelyGnssPort(candidate)) {
                    continue;
                }
                if (static_cast<int>(isLikelyEspPort(candidate)) != preferred) continue;
                fd = setupSerial(candidate);
                if (fd >= 0) {
                    portname = candidate;
                    break;
                }
            }
        }
    }
    if (fd < 0) {
        std::cerr << "No se pudo abrir un puerto ESP32 distinto del receptor GNSS.\n";
        printDetectedPorts(ports);
        return 1;
    }

    udp_gateway::UdpGateway udpGateway({
        options->udpHost,
        options->udpTelemetryPort,
        options->udpCommandPort,
        "0.0.0.0",
    });
    bool udpEnabled = options->udpEnabled;
    if (udpEnabled) {
        std::string udpError;
        if (!udpGateway.open(&udpError)) {
            std::cerr << "No se pudo iniciar UDP: " << udpError
                      << ". Continuando solo con la terminal.\n";
            udpEnabled = false;
        } else {
            std::cerr << "UDP activo: telemetría a " << options->udpHost << ':'
                      << options->udpTelemetryPort << ", comandos en :"
                      << options->udpCommandPort << ".\n";
        }
    }

    GnssReceiver gnssReceiver;
    GpsMatcher gpsMatcher;
    std::string lastMatchedGnssTimestamp;
    int gnssFd = -1;
    std::vector<std::uint8_t> gnssWriteBuffer;
    auto nextGnssReconnect = std::chrono::steady_clock::now();
    auto connectGnss = [&]() {
        if (!options->gnssEnabled || gnssFd >= 0 ||
            std::chrono::steady_clock::now() < nextGnssReconnect) return;
        if (options->gnssPort == "auto") {
            gnssPortname = detectGnssPort(findSerialPorts());
        }
        if (gnssPortname.empty() || sameDevice(gnssPortname, portname)) {
            nextGnssReconnect = std::chrono::steady_clock::now() + std::chrono::seconds(2);
            return;
        }
        gnssFd = setupSerial(gnssPortname);
        if (gnssFd < 0) {
            nextGnssReconnect = std::chrono::steady_clock::now() + std::chrono::seconds(2);
            return;
        }
        gnssWriteBuffer = gnssReceiver.buildStartupCommands();
        std::cerr << "GNSS activo en " << gnssPortname << " (objetivo 5 Hz).\n";
    };
    connectGnss();

    const char* ntripUserEnv = std::getenv("NTRIP_USERNAME");
    const char* ntripPasswordEnv = std::getenv("NTRIP_PASSWORD");
    NtripConfig ntripConfig;
    ntripConfig.host = options->ntripHost;
    ntripConfig.port = options->ntripPort;
    ntripConfig.mountpoint = options->ntripMountpoint;
    ntripConfig.username = ntripUserEnv ? ntripUserEnv : "";
    ntripConfig.password = ntripPasswordEnv ? ntripPasswordEnv : "";
    std::unique_ptr<NtripClient> ntripClient;
    if (options->ntripEnabled) {
        ntripClient = std::make_unique<NtripClient>(ntripConfig);
        if (ntripConfig.username.empty() || ntripConfig.password.empty()) {
            std::cerr << "NTRIP desactivado: define NTRIP_USERNAME y NTRIP_PASSWORD.\n";
        } else {
            std::string ntripError;
            if (!ntripClient->start(&ntripError)) {
                std::cerr << "No se pudo iniciar NTRIP: " << ntripError << '\n';
            } else {
                std::cerr << "NTRIP configurado: " << ntripConfig.host << ':'
                          << ntripConfig.port << '/' << ntripConfig.mountpoint << ".\n";
            }
        }
    }
    auto ntripStatus = [&]() {
        if (ntripClient) return ntripClient->status();
        NtripStatus status;
        status.host = options->ntripHost;
        status.port = options->ntripPort;
        status.mountpoint = options->ntripMountpoint;
        return status;
    };

    udp_gateway::CalibrationState calibration;
    udp_gateway::PhysicalConfig physicalConfig;
    udp_gateway::PhysicsDerived physics;
    bool physicsConfigured = false;
    std::optional<udp_gateway::Orientation> latestRawOrientation;

    char buffer[4096];
    std::string line;
    bool discardingPartialLine = true;
    bool discardingOversizedLine = false;
    unsigned long lineCount = 0;
    unsigned int idleSeconds = 0;
    bool failed = false;
    std::vector<std::string> fieldNames = DEFAULT_FIELDS;
    std::vector<std::string> currentValues;
    std::vector<std::string> currentDisplayValues;
    std::optional<std::chrono::system_clock::time_point> currentDobackTimestamp;
    std::string updatedAt = "--:--:--";
    auto lastEspData = std::chrono::steady_clock::now();
    Dashboard dashboard(portname);
    dashboard.render(displayFields(fieldNames, options->gnssEnabled),
                     currentDisplayValues, lineCount, idleSeconds, updatedAt);

    auto processUdpCommands = [&]() {
        if (!udpEnabled) return;
        for (const auto& command : udpGateway.pollCommands()) {
            if (command.type == udp_gateway::CommandType::Calibrate) {
                if (!latestRawOrientation) {
                    std::cerr << "Calibración ignorada: todavía no hay orientación DOBACK.\n";
                    continue;
                }
                calibration.capture(*latestRawOrientation);
                std::cerr << "Calibración aplicada a roll, pitch y yaw.\n";
            } else if (command.type == udp_gateway::CommandType::Config) {
                udp_gateway::PhysicsDerived candidate;
                std::string error;
                if (!udp_gateway::calculatePhysics(command.config, &candidate, &error)) {
                    std::cerr << "Configuración física ignorada: " << error << '\n';
                    continue;
                }
                physicalConfig = command.config;
                physics = candidate;
                physicsConfigured = true;
            }
        }
    };

    auto fillGnssTelemetry = [&](udp_gateway::TelemetryPacket& packet) {
        const auto match = currentDobackTimestamp
                               ? gpsMatcher.nearest(*currentDobackTimestamp,
                                                    options->gpsMaximumDifference)
                               : std::nullopt;
        if (match) {
            const auto& sample = match->sample;
            packet.gps.available = true;
            packet.gps.timestamp_utc = sample.timestampUtc;
            packet.gps.delta_ms = static_cast<double>(match->deltaMilliseconds);
            packet.gps.latitude_deg = sample.latitudeDeg;
            packet.gps.longitude_deg = sample.longitudeDeg;
            packet.gps.height_ellipsoid_m = sample.heightEllipsoidM;
            packet.gps.height_msl_m = sample.heightMslM;
            packet.gps.h_acc_m = sample.hAccM;
            packet.gps.v_acc_m = sample.vAccM;
            packet.gps.speed_m_s = sample.speedMS;
            packet.gps.heading_deg = sample.headingDeg;
            packet.gps.fix = sample.fix;
            packet.gps.fix_type = sample.fixType;
            packet.gps.rtk = sample.rtk;
            packet.gps.num_sats = sample.numSats;
            packet.gps.pdop = sample.pdop;
            packet.gps.hdop = sample.hdop;
            packet.gps.vdop = sample.vdop;
            packet.gps.correction_age_s = sample.correctionAgeS;
            packet.gps.base_station_id = sample.baseStationId;
        }
        const auto quality = gnssReceiver.snapshot();
        const auto network = ntripStatus();
        packet.gnss_status.device_connected = gnssFd >= 0;
        packet.gnss_status.port = gnssPortname;
        packet.gnss_status.solution_age_ms =
            steadyAgeMilliseconds(quality.lastSolutionSteady);
        packet.gnss_status.ntrip_state = ntripStateToString(network.state);
        packet.gnss_status.ntrip_host = network.host;
        packet.gnss_status.ntrip_port = network.port;
        packet.gnss_status.ntrip_mountpoint = network.mountpoint;
        packet.gnss_status.rtcm_age_ms = steadyAgeMilliseconds(network.lastRtcmSteady);
        packet.gnss_status.rtcm_bytes = network.rtcmBytes;
        packet.gnss_status.rtcm_messages = quality.rtcmMessages;
        packet.gnss_status.rtcm_message_type = quality.lastRtcmMessageType;
        packet.gnss_status.rtcm_used = quality.lastRtcmUsed;
        packet.gnss_status.rtcm_crc_failed = quality.lastRtcmCrcFailed;
        packet.gnss_status.reconnects = network.reconnects;
        packet.gnss_status.last_error = !network.lastError.empty()
                                                ? network.lastError
                                                : quality.lastError;
        if (!quality.interferenceState.empty()) {
            packet.gnss_status.interference_state = quality.interferenceState;
        }
        packet.gnss_status.jam_ind = quality.jamInd;
    };

    auto processSerialLine = [&](const std::string& completedLine) {
        const auto fields = splitFields(completedLine);
        if (isMeasurementHeader(fields)) {
            fieldNames = fields;
            if (currentValues.size() != fieldNames.size()) {
                currentValues.clear();
                currentDisplayValues.clear();
                currentDobackTimestamp.reset();
            }
            return;
        }
        if (!isMeasurement(fields, fieldNames)) {
            const auto* detectedSchema = knownSchemaFor(fields);
            if (!detectedSchema) return;
            fieldNames = *detectedSchema;
        }

        currentValues = fields;
        currentDobackTimestamp = std::chrono::system_clock::now();
        latestRawOrientation = measurementOrientation(fieldNames, currentValues);
        idleSeconds = 0;
        lastEspData = std::chrono::steady_clock::now();
        updatedAt = currentTime();
        ++lineCount;
        const auto network = ntripStatus();
        currentDisplayValues = displayValues(
            currentValues, currentDobackTimestamp, gpsMatcher,
            options->gpsMaximumDifference, options->gnssEnabled, network);
        dashboard.render(displayFields(fieldNames, options->gnssEnabled),
                         currentDisplayValues, lineCount, idleSeconds, updatedAt);

        if (udpEnabled) {
            udp_gateway::TelemetryPacket packet;
            packet.sequence = lineCount;
            packet.doback_timestamp_utc = formatUtcTimestamp(*currentDobackTimestamp);
            for (std::size_t index = 0;
                 index < fieldNames.size() && index < currentValues.size(); ++index) {
                packet.measurement.emplace_back(fieldNames[index], currentValues[index]);
            }
            if (latestRawOrientation) {
                packet.raw_orientation = *latestRawOrientation;
                packet.orientation = calibration.apply(*latestRawOrientation);
            }
            packet.calibration_active = calibration.active;
            packet.config = physicalConfig;
            packet.physics = physics;
            packet.physics_configured = physicsConfigured;
            fillGnssTelemetry(packet);
            std::string udpError;
            if (!udpGateway.sendTelemetry(packet, &udpError) && lineCount == 1) {
                std::cerr << "No se pudo enviar telemetría UDP: " << udpError << '\n';
            }
        }
    };

    while (running) {
        connectGnss();
        if (gnssFd >= 0 && ntripClient) {
            const auto rtcm = ntripClient->popRtcm(4096);
            if (!rtcm.empty()) {
                if (gnssWriteBuffer.size() + rtcm.size() > 65536) {
                    gnssWriteBuffer.clear();
                }
                gnssWriteBuffer.insert(gnssWriteBuffer.end(), rtcm.begin(), rtcm.end());
            }
        }
        if (gnssFd >= 0 && !gnssWriteBuffer.empty()) {
            const ssize_t written = write(gnssFd, gnssWriteBuffer.data(),
                                          gnssWriteBuffer.size());
            if (written > 0) {
                gnssWriteBuffer.erase(gnssWriteBuffer.begin(),
                                      gnssWriteBuffer.begin() + written);
            } else if (written < 0 && errno != EAGAIN && errno != EINTR) {
                close(gnssFd);
                gnssFd = -1;
                gnssWriteBuffer.clear();
                nextGnssReconnect = std::chrono::steady_clock::now() +
                                    std::chrono::seconds(2);
            }
        }

        pollfd polls[3]{{fd, POLLIN, 0}, {-1, POLLIN, 0}, {-1, POLLIN, 0}};
        nfds_t pollCount = 1;
        const nfds_t gnssIndex = pollCount;
        if (gnssFd >= 0) polls[pollCount++] = {gnssFd, POLLIN, 0};
        const int udpCommandFd = udpEnabled ? udpGateway.commandFileDescriptor() : -1;
        const nfds_t udpIndex = pollCount;
        if (udpCommandFd >= 0) polls[pollCount++] = {udpCommandFd, POLLIN, 0};

        const int pollResult = poll(polls, pollCount, 200);
        if (pollResult < 0) {
            if (errno == EINTR) continue;
            dashboard.finish();
            std::cerr << "Error esperando datos: " << std::strerror(errno) << '\n';
            failed = true;
            break;
        }
        idleSeconds = static_cast<unsigned int>(
            std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::steady_clock::now() - lastEspData)
                .count());
        if (pollResult == 0) {
            currentDisplayValues = displayValues(
                currentValues, currentDobackTimestamp, gpsMatcher,
                options->gpsMaximumDifference, options->gnssEnabled, ntripStatus());
            dashboard.render(displayFields(fieldNames, options->gnssEnabled),
                             currentDisplayValues, lineCount, idleSeconds, updatedAt);
            continue;
        }

        if (gnssFd >= 0 && gnssIndex < pollCount && polls[gnssIndex].fd == gnssFd &&
            (polls[gnssIndex].revents & (POLLERR | POLLHUP | POLLNVAL))) {
            close(gnssFd);
            gnssFd = -1;
            gnssWriteBuffer.clear();
            nextGnssReconnect = std::chrono::steady_clock::now() +
                                std::chrono::seconds(2);
        } else if (gnssFd >= 0 && gnssIndex < pollCount &&
                   polls[gnssIndex].fd == gnssFd &&
                   (polls[gnssIndex].revents & POLLIN)) {
            const ssize_t count = read(gnssFd, buffer, sizeof(buffer));
            if (count > 0) {
                gnssReceiver.ingest(reinterpret_cast<std::uint8_t*>(buffer),
                                    static_cast<std::size_t>(count));
                if (const auto sample = gnssReceiver.latestSample()) {
                    if (sample->timestampUtc != lastMatchedGnssTimestamp) {
                        gpsMatcher.addSample(matcherSample(*sample));
                        lastMatchedGnssTimestamp = sample->timestampUtc;
                    }
                }
                if (ntripClient) {
                    ntripClient->setGgaSentence(gnssReceiver.latestGgaSentence());
                }
            }
        }
        if (udpCommandFd >= 0 && udpIndex < pollCount &&
            polls[udpIndex].fd == udpCommandFd && polls[udpIndex].revents) {
            processUdpCommands();
        }

        if (polls[0].revents & (POLLERR | POLLHUP | POLLNVAL)) {
            dashboard.finish();
            std::cerr << "El puerto serial ESP32 se desconectó.\n";
            failed = true;
            break;
        }
        if (!(polls[0].revents & POLLIN)) continue;

        const ssize_t count = read(fd, buffer, sizeof(buffer));
        if (count < 0) {
            if (errno == EAGAIN || errno == EINTR) continue;
            dashboard.finish();
            std::cerr << "Error leyendo datos ESP32: " << std::strerror(errno) << '\n';
            failed = true;
            break;
        }
        for (ssize_t i = 0; i < count; ++i) {
            const char c = buffer[i];
            if (c == '\n') {
                if (discardingPartialLine) {
                    discardingPartialLine = false;
                    if (!line.empty() && isMeasurementHeader(splitFields(line))) {
                        processSerialLine(line);
                    }
                } else if (discardingOversizedLine) {
                    discardingOversizedLine = false;
                } else if (!line.empty()) {
                    processSerialLine(line);
                }
                line.clear();
            } else if (c != '\r') {
                if (line.size() < 65536) {
                    line += c;
                } else if (!discardingOversizedLine) {
                    discardingOversizedLine = true;
                    line.clear();
                }
            }
        }
    }

    if (ntripClient) ntripClient->stop();
    if (gnssFd >= 0) close(gnssFd);
    close(fd);
    dashboard.finish();
    std::cout << "Sesión finalizada. Muestras recibidas: " << lineCount << '\n';
    return failed ? 1 : 0;
}
