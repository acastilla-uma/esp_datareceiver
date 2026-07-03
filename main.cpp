#include <atomic>
#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <fstream>
#include <glob.h>
#include <iostream>
#include <poll.h>
#include <set>
#include <string>
#include <termios.h>
#include <unistd.h>
#include <vector>

namespace {

std::atomic<bool> running{true};

void signalHandler(int) {
    running = false;
}

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

std::string generateFileName() {
    const std::time_t now = std::time(nullptr);
    const std::tm* local = std::localtime(&now);
    char filename[64];
    std::strftime(filename, sizeof(filename), "datos_%Y%m%d_%H%M%S.csv", local);
    return filename;
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

    const auto ports = findSerialPorts();
    std::string portname;
    int fd = -1;
    if (argc > 1 && std::string(argv[1]) != "auto") {
        portname = argv[1];
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

    std::cout << "=== Receptor de estabilidad ESP32 ===\n"
              << "Puerto: " << portname << "\n"
              << "Configuración: 115200 baud, 8N1\n"
              << "Ctrl+C para detener\n"
              << "=====================================\n";

    const std::string filename = generateFileName();
    std::ofstream csvFile(filename);
    if (!csvFile) {
        std::cerr << "No se pudo crear " << filename << ": " << std::strerror(errno) << '\n';
        close(fd);
        return 1;
    }

    std::cout << "Guardando en: " << filename << "\nEsperando datos...\n";

    pollfd serialPoll{fd, POLLIN, 0};
    char buffer[1024];
    std::string line;
    bool discardingOversizedLine = false;
    unsigned long lineCount = 0;
    unsigned int idleSeconds = 0;
    bool failed = false;

    while (running) {
        const int pollResult = poll(&serialPoll, 1, 1000);
        if (pollResult < 0) {
            if (errno == EINTR) {
                continue;
            }
            std::cerr << "Error esperando datos: " << std::strerror(errno) << '\n';
            failed = true;
            break;
        }
        if (pollResult == 0) {
            ++idleSeconds;
            if (idleSeconds == 5 || idleSeconds % 15 == 0) {
                std::cerr << "Sin datos durante " << idleSeconds
                          << " s. Pulsa RESET/EN y verifica Serial.begin(115200).\n";
            }
            continue;
        }
        if (serialPoll.revents & (POLLERR | POLLHUP | POLLNVAL)) {
            std::cerr << "El puerto serial se desconectó o dejó de estar disponible.\n";
            failed = true;
            break;
        }

        const ssize_t count = read(fd, buffer, sizeof(buffer));
        if (count < 0) {
            if (errno == EAGAIN || errno == EINTR) {
                continue;
            }
            std::cerr << "Error leyendo datos: " << std::strerror(errno) << '\n';
            failed = true;
            break;
        }
        idleSeconds = 0;
        for (ssize_t i = 0; i < count; ++i) {
            const char c = buffer[i];
            if (c == '\n') {
                if (discardingOversizedLine) {
                    discardingOversizedLine = false;
                } else if (!line.empty()) {
                    csvFile << line << '\n';
                    csvFile.flush();
                    if (!csvFile) {
                        std::cerr << "Error escribiendo el archivo CSV.\n";
                        failed = true;
                        running = false;
                        break;
                    }
                    std::cout << '[' << ++lineCount << "] " << line << '\n';
                }
                line.clear();
            } else if (c != '\r') {
                if (line.size() < 65536) {
                    line += c;
                } else if (!discardingOversizedLine) {
                    std::cerr << "Línea serial mayor de 64 KiB; se descarta hasta el próximo salto de línea.\n";
                    discardingOversizedLine = true;
                    line.clear();
                }
            }
        }
    }

    if (!line.empty()) {
        csvFile << line << '\n';
    }
    close(fd);
    std::cout << "\nSesión finalizada. Líneas: " << lineCount << ", archivo: " << filename << '\n';
    return failed ? 1 : 0;
}
