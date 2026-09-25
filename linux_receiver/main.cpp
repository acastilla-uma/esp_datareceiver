#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cctype>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <glob.h>
#include <iomanip>
#include <iostream>
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

#include "gps_matcher.h"
#include "gps_process.h"
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
    "latitude", "longitude", "gps_fix_type", "gps_num_sats"
};

struct Options {
    std::string serialPort{"auto"};
    std::string gpsScript{"/home/agilex/Documents/PhDAlex/GPS_CSG/gps_realtime.py"};
    std::string gpsDevice;
    std::chrono::milliseconds gpsMaximumDifference{10000};
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
    if (key == "gps_fix_type") return "Tipo de fix GPS";
    if (key == "gps_num_sats") return "Satélites GPS";
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
        << "Uso: " << program << " [auto|PUERTO] [opciones]\n\n"
        << "Opciones GPS:\n"
        << "  --gps-script RUTA       Ruta a GPS_CSG/gps_realtime.py\n"
        << "  --gps-device ID         Filtra device_id en gps_points\n"
        << "  --gps-max-delta-ms N    Diferencia temporal máxima (10000 ms)\n"
        << "  --no-gps                Desactiva GPS aunque el script de arranque lo configure\n"
        << "Opciones UDP:\n"
        << "  --udp-host IP           IP del PC Windows (192.168.8.20)\n"
        << "  --udp-port N            Puerto de telemetría del PC (50100)\n"
        << "  --udp-command-port N    Puerto de comandos en Jetson (50101)\n"
        << "  --no-udp                Desactiva telemetría y comandos UDP\n"
        << "  -h, --help              Muestra esta ayuda\n";
}

std::optional<std::uint16_t> parsePort(const std::string& text) {
    try {
        const long value = std::stol(text);
        if (value < 1 || value > 65535) {
            return std::nullopt;
        }
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
        if (argument == "--no-gps") {
            options.gpsScript.clear();
            continue;
        }
        if (argument == "--no-udp") {
            options.udpEnabled = false;
            continue;
        }
        if (argument == "--gps-script" || argument == "--gps-device" ||
            argument == "--gps-max-delta-ms" || argument == "--udp-host" ||
            argument == "--udp-port" || argument == "--udp-command-port") {
            if (++index >= argc) {
                std::cerr << "Falta el valor de " << argument << ".\n";
                return std::nullopt;
            }
            const std::string value = argv[index];
            if (argument == "--gps-script") {
                options.gpsScript = value;
            } else if (argument == "--gps-device") {
                options.gpsDevice = value;
            } else if (argument == "--udp-host") {
                options.udpHost = value;
            } else if (argument == "--udp-port" ||
                       argument == "--udp-command-port") {
                const auto port = parsePort(value);
                if (!port) {
                    std::cerr << argument << " debe estar entre 1 y 65535.\n";
                    return std::nullopt;
                }
                if (argument == "--udp-port") {
                    options.udpTelemetryPort = *port;
                } else {
                    options.udpCommandPort = *port;
                }
            } else {
                try {
                    const long long milliseconds = std::stoll(value);
                    if (milliseconds < 0) {
                        throw std::out_of_range("negative");
                    }
                    options.gpsMaximumDifference =
                        std::chrono::milliseconds(milliseconds);
                } catch (const std::exception&) {
                    std::cerr << "--gps-max-delta-ms debe ser un entero no negativo.\n";
                    return std::nullopt;
                }
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
        options.serialPort = argument;
        serialPortSeen = true;
    }
    return options;
}

std::optional<double> measurementValue(
    const std::vector<std::string>& fields,
    const std::vector<std::string>& values,
    const std::vector<std::string>& candidates) {
    for (std::size_t index = 0; index < fields.size() && index < values.size();
         ++index) {
        const std::string field = lowercase(trim(fields[index]));
        if (std::find(candidates.begin(), candidates.end(), field) ==
            candidates.end()) {
            continue;
        }
        char* end = nullptr;
        const double value = std::strtod(values[index].c_str(), &end);
        if (end != values[index].c_str() && *end == '\0') {
            return value;
        }
    }
    return std::nullopt;
}

std::optional<udp_gateway::Orientation> measurementOrientation(
    const std::vector<std::string>& fields,
    const std::vector<std::string>& values) {
    const auto roll = measurementValue(fields, values, {"roll", "roll_deg"});
    const auto pitch = measurementValue(fields, values, {"pitch", "pitch_deg"});
    const auto yaw = measurementValue(fields, values, {"yaw", "yaw_deg"});
    if (!roll || !pitch || !yaw) {
        return std::nullopt;
    }
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
    bool gpsEnabled) {
    if (values.empty() || !gpsEnabled || !dobackTimestamp) {
        return values;
    }

    std::vector<std::string> result = values;
    result.push_back(formatUtcTimestamp(*dobackTimestamp));
    const auto match = gpsMatcher.nearest(*dobackTimestamp, maximumDifference);
    if (!match) {
        result.insert(result.end(), {"—", "—", "—", "—", "—", "—"});
        return result;
    }
    result.push_back(match->sample.timestampUtc);
    result.push_back(std::to_string(match->deltaMilliseconds));
    result.push_back(decimal(match->sample.latitude, 7));
    result.push_back(decimal(match->sample.longitude, 7));
    result.push_back(std::to_string(match->sample.fixType));
    result.push_back(std::to_string(match->sample.satellites));
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
    std::string portname;
    int fd = -1;
    if (options->serialPort != "auto") {
        portname = options->serialPort;
        fd = setupSerial(portname);
    } else {
        for (const auto& candidate : ports) {
            fd = setupSerial(candidate);
            if (fd >= 0) {
                portname = candidate;
                break;
            }
        }
    }

    if (fd < 0) {
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

    udp_gateway::CalibrationState calibration;
    udp_gateway::PhysicalConfig physicalConfig;
    udp_gateway::PhysicsDerived physics;
    bool physicsConfigured = false;
    std::optional<udp_gateway::Orientation> latestRawOrientation;

    GpsProcess gpsProcess;
    GpsMatcher gpsMatcher;
    bool gpsEnabled = !options->gpsScript.empty();
    if (gpsEnabled) {
        std::string gpsError;
        if (!gpsProcess.start(options->gpsScript, options->gpsDevice, &gpsError)) {
            std::cerr << "No se pudo iniciar GPS_CSG: " << gpsError
                      << ". Continuando solo con DOBACK.\n";
            gpsEnabled = false;
        } else {
            std::cerr << "GPS activo: " << options->gpsScript;
            if (!options->gpsDevice.empty()) {
                std::cerr << " --device-id " << options->gpsDevice;
            }
            std::cerr << '\n';
        }
    }

    char buffer[1024];
    std::string line;
    // El puerto puede abrirse en mitad de una trama. Descartar hasta el primer
    // salto de línea evita mostrar un registro inicial parcial o residual.
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
    Dashboard dashboard(portname);
    dashboard.render(displayFields(fieldNames, gpsEnabled), currentDisplayValues,
                     lineCount, idleSeconds, updatedAt);

    auto processUdpCommands = [&]() {
        if (!udpEnabled) {
            return;
        }
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
                if (!udp_gateway::calculatePhysics(command.config, &candidate,
                                                   &error)) {
                    std::cerr << "Configuración física ignorada: " << error << '\n';
                    continue;
                }
                physicalConfig = command.config;
                physics = candidate;
                physicsConfigured = true;
                std::cerr << "Configuración física actualizada desde "
                          << command.sender_ip << ".\n";
            }
        }
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
            dashboard.render(displayFields(fieldNames, gpsEnabled),
                             currentDisplayValues, lineCount, idleSeconds,
                             updatedAt);
            return;
        }

        if (!isMeasurement(fields, fieldNames)) {
            const auto* detectedSchema = knownSchemaFor(fields);
            if (!detectedSchema) {
                return;
            }
            fieldNames = *detectedSchema;
        }

        currentValues = fields;
        currentDobackTimestamp = std::chrono::system_clock::now();
        latestRawOrientation = measurementOrientation(fieldNames, currentValues);
        currentDisplayValues = displayValues(
            currentValues, currentDobackTimestamp, gpsMatcher,
            options->gpsMaximumDifference, gpsEnabled);
        updatedAt = currentTime();
        ++lineCount;
        dashboard.render(displayFields(fieldNames, gpsEnabled),
                         currentDisplayValues, lineCount, idleSeconds,
                         updatedAt);

        if (udpEnabled && latestRawOrientation) {
            udp_gateway::TelemetryPacket packet;
            packet.sequence = lineCount;
            packet.doback_timestamp_utc =
                formatUtcTimestamp(*currentDobackTimestamp);
            for (std::size_t index = 0;
                 index < fieldNames.size() && index < currentValues.size();
                 ++index) {
                packet.measurement.emplace_back(fieldNames[index],
                                                currentValues[index]);
            }
            packet.raw_orientation = *latestRawOrientation;
            packet.orientation = calibration.apply(*latestRawOrientation);
            packet.calibration_active = calibration.active;
            const auto gpsMatch = gpsMatcher.nearest(
                *currentDobackTimestamp, options->gpsMaximumDifference);
            if (gpsMatch) {
                packet.gps.valid = true;
                packet.gps.gps_timestamp_utc = gpsMatch->sample.timestampUtc;
                packet.gps.delta_ms = gpsMatch->deltaMilliseconds;
                packet.gps.latitude = gpsMatch->sample.latitude;
                packet.gps.longitude = gpsMatch->sample.longitude;
                packet.gps.fix_type = gpsMatch->sample.fixType;
                packet.gps.num_sats = gpsMatch->sample.satellites;
            }
            packet.config = physicalConfig;
            packet.physics = physics;
            packet.physics_configured = physicsConfigured;
            std::string udpError;
            if (!udpGateway.sendTelemetry(packet, &udpError) && lineCount == 1) {
                std::cerr << "No se pudo enviar telemetría UDP: " << udpError
                          << '\n';
            }
        }
    };

    while (running) {
        pollfd polls[3]{{fd, POLLIN, 0}, {-1, POLLIN, 0}, {-1, POLLIN, 0}};
        nfds_t pollCount = 1;
        const int gpsFd = gpsEnabled ? gpsProcess.fileDescriptor() : -1;
        if (gpsFd >= 0) {
            polls[pollCount++] = {gpsFd, POLLIN, 0};
        }
        const int udpCommandFd =
            udpEnabled ? udpGateway.commandFileDescriptor() : -1;
        if (udpCommandFd >= 0) {
            polls[pollCount++] = {udpCommandFd, POLLIN, 0};
        }

        const int pollResult = poll(polls, pollCount, 1000);
        if (pollResult < 0) {
            if (errno == EINTR) {
                continue;
            }
            dashboard.finish();
            std::cerr << "Error esperando datos: " << std::strerror(errno) << '\n';
            failed = true;
            break;
        }
        if (pollResult == 0) {
            ++idleSeconds;
            currentDisplayValues = displayValues(
                currentValues, currentDobackTimestamp, gpsMatcher,
                options->gpsMaximumDifference, gpsEnabled);
            dashboard.render(displayFields(fieldNames, gpsEnabled),
                             currentDisplayValues, lineCount, idleSeconds,
                             updatedAt);
            continue;
        }

        for (nfds_t index = 1; index < pollCount; ++index) {
            if (polls[index].fd == gpsFd && polls[index].revents) {
                for (const auto& gpsLine : gpsProcess.readLines()) {
                    std::string parseError;
                    gpsMatcher.addJsonLine(gpsLine, &parseError);
                }
                if (!gpsProcess.running()) {
                    std::cerr << "El proceso GPS_CSG terminó. Continuando solo con DOBACK.\n";
                    gpsEnabled = false;
                }
            }
            if (polls[index].fd == udpCommandFd && polls[index].revents) {
                processUdpCommands();
            }
        }

        if (polls[0].revents & (POLLERR | POLLHUP | POLLNVAL)) {
            dashboard.finish();
            std::cerr << "El puerto serial se desconectó o dejó de estar disponible.\n";
            failed = true;
            break;
        }
        if (!(polls[0].revents & POLLIN)) {
            continue;
        }

        const ssize_t count = read(fd, buffer, sizeof(buffer));
        if (count < 0) {
            if (errno == EAGAIN || errno == EINTR) {
                continue;
            }
            dashboard.finish();
            std::cerr << "Error leyendo datos: " << std::strerror(errno) << '\n';
            failed = true;
            break;
        }
        idleSeconds = 0;
        for (ssize_t i = 0; i < count; ++i) {
            const char c = buffer[i];
            if (c == '\n') {
                if (discardingPartialLine) {
                    discardingPartialLine = false;
                    if (!line.empty() &&
                        isMeasurementHeader(splitFields(line))) {
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

    close(fd);
    dashboard.finish();
    std::cout << "Sesión finalizada. Muestras recibidas: " << lineCount << '\n';
    return failed ? 1 : 0;
}
