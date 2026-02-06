#include <iostream>
#include <fstream>
#include <string>
#include <cstring>
#include <ctime>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <csignal>
#include <atomic>

std::atomic<bool> running(true);

// Manejador para Ctrl+C
void signalHandler(int signum) {
    std::cout << "\n\nIniando cierre...\n";
    running = false;
}

// Configurar puerto serial
int setupSerial(const char *portname) {
    int fd = open(portname, O_RDWR | O_NOCTTY | O_SYNC);
    if (fd < 0) {
        std::cerr << "Error abriendo puerto " << portname << ": " << strerror(errno) << std::endl;
        return -1;
    }

    struct termios tty;
    if (tcgetattr(fd, &tty) != 0) {
        std::cerr << "Error obteniendo atributos del puerto\n";
        close(fd);
        return -1;
    }

    // Configurar 115200 baud, 8N1
    cfsetospeed(&tty, B115200);
    cfsetispeed(&tty, B115200);

    tty.c_cflag = (tty.c_cflag & ~CSIZE) | CS8;     // 8 bits
    tty.c_iflag &= ~IGNBRK;
    tty.c_lflag = 0;
    tty.c_oflag = 0;
    tty.c_cc[VMIN] = 0;
    tty.c_cc[VTIME] = 5;

    tty.c_iflag &= ~(IXON | IXOFF | IXANY);
    tty.c_cflag |= (CLOCAL | CREAD);
    tty.c_cflag &= ~(PARENB | PARODD);
    tty.c_cflag &= ~CSTOPB;
    tty.c_cflag &= ~CRTSCTS;

    if (tcsetattr(fd, TCSANOW, &tty) != 0) {
        std::cerr << "Error configurando puerto\n";
        close(fd);
        return -1;
    }

    return fd;
}

// Generador de nombre de archivo con timestamp
std::string generateFileName() {
    time_t now = time(nullptr);
    struct tm *tm_info = localtime(&now);
    char filename[256];
    strftime(filename, sizeof(filename), "datos_%Y%m%d_%H%M%S.csv", tm_info);
    return std::string(filename);
}

int main(int argc, char *argv[]) {
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);

    // Argumentos por defecto
    const char *portname = "/dev/ttyACM0";
    if (argc > 1) {
        portname = argv[1];
    }

    std::cout << "=== ESP32 Data Receiver - Linux ===" << std::endl;
    std::cout << "Puerto serial: " << portname << std::endl;
    std::cout << "Velocidad: 115200 baud" << std::endl;
    std::cout << "Presiona Ctrl+C para detener" << std::endl;
    std::cout << "================================\n" << std::endl;

    // Configurar puerto serial
    int fd = setupSerial(portname);
    if (fd < 0) {
        std::cerr << "No se pudo configurar el puerto serial\n";
        return 1;
    }

    // Crear archivo CSV
    std::string filename = generateFileName();
    std::ofstream csvFile(filename);
    if (!csvFile.is_open()) {
        std::cerr << "Error creando archivo: " << filename << std::endl;
        close(fd);
        return 1;
    }

    std::cout << "Archivo creado: " << filename << std::endl;
    std::cout << "\nRecibiendo datos...\n" << std::endl;

    char buffer[1024];
    std::string line;
    unsigned long lineCount = 0;
    bool headerWritten = false;

    while (running) {
        int n = read(fd, buffer, sizeof(buffer));
        
        if (n > 0) {
            for (int i = 0; i < n; i++) {
                char c = buffer[i];
                
                if (c == '\n') {
                    if (!line.empty()) {
                        // Escribir línea en CSV
                        csvFile << line << std::endl;
                        csvFile.flush();
                        lineCount++;

                        // Mostrar progreso cada 10 líneas
                        if (lineCount % 10 == 0) {
                            std::cout << "ACC("
                                << campos[0] << ", "
                                << campos[1] << ", "
                                << campos[2] << ")  GYRO("
                                << campos[3] << ", "
                                << campos[4] << ", "
                                << campos[5] << ")  SI("
                                << campos[6] << ")" << std::endl;

                        }
                    }
                    line.clear();
                } else if (c != '\r' && c >= 32 && c <= 126) {
                    line += c;
                }
            }
        } else {
            // Pequeña pausa para no consumir CPU
            usleep(10000);
        }
    }

    // Cerrar archivos
    csvFile.close();
    close(fd);

    std::cout << "\n=== Cierre de sesión ===" << std::endl;
    std::cout << "Total de líneas recibidas: " << lineCount << std::endl;
    std::cout << "Archivo guardado: " << filename << std::endl;
    std::cout << "Puerto serial cerrado" << std::endl;

    return 0;
}
