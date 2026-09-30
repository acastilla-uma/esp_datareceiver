#include "ntrip_client.h"

#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <sstream>
#include <utility>

namespace {

std::string base64Encode(const std::string& input) {
    static const char alphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string output;
    std::uint32_t buffer = 0;
    int bits = 0;
    for (unsigned char character : input) {
        buffer = (buffer << 8) | character;
        bits += 8;
        while (bits >= 6) {
            bits -= 6;
            output.push_back(alphabet[(buffer >> bits) & 0x3F]);
        }
    }
    if (bits > 0) {
        output.push_back(alphabet[(buffer << (6 - bits)) & 0x3F]);
    }
    while (output.size() % 4 != 0) {
        output.push_back('=');
    }
    return output;
}

std::string socketError() {
    return std::strerror(errno);
}

void closeIfValid(int fd) {
    if (fd >= 0) {
        close(fd);
    }
}

bool headerAccepted(const std::string& header) {
    return header.rfind("ICY 200", 0) == 0 ||
           header.rfind("HTTP/1.0 200", 0) == 0 ||
           header.rfind("HTTP/1.1 200", 0) == 0;
}

}  // namespace

std::string ntripStateToString(NtripState state) {
    switch (state) {
        case NtripState::Connecting:
            return "CONNECTING";
        case NtripState::Authenticating:
            return "AUTHENTICATING";
        case NtripState::Streaming:
            return "STREAMING";
        case NtripState::Backoff:
            return "BACKOFF";
        case NtripState::Error:
            return "ERROR";
        case NtripState::Disabled:
        default:
            return "DISABLED";
    }
}

std::string redactNtripSecret(const std::string& text) {
    std::string result = text;
    const std::array<std::string, 3> markers{"Authorization:", "password=", "Password:"};
    for (const auto& marker : markers) {
        std::size_t position = 0;
        while ((position = result.find(marker, position)) != std::string::npos) {
            std::size_t valueStart = position + marker.size();
            while (valueStart < result.size() && result[valueStart] == ' ') {
                ++valueStart;
            }
            const std::size_t lineEnd = result.find_first_of("\r\n&", valueStart);
            result.replace(valueStart, lineEnd == std::string::npos ? std::string::npos
                                                                    : lineEnd - valueStart,
                           "<redacted>");
            position += marker.size();
        }
    }
    return result;
}

NtripClient::NtripClient(NtripConfig config) : config_(std::move(config)) {
    status_.host = config_.host;
    status_.port = config_.port;
    status_.mountpoint = config_.mountpoint;
}

NtripClient::~NtripClient() {
    stop();
}

bool NtripClient::start(std::string* error) {
    if (running_) {
        return true;
    }
    if (config_.host.empty() || config_.mountpoint.empty() || config_.port <= 0) {
        if (error) {
            *error = "configuración NTRIP incompleta";
        }
        return false;
    }
    stopRequested_ = false;
    running_ = true;
    worker_ = std::thread(&NtripClient::workerLoop, this);
    return true;
}

void NtripClient::stop() {
    stopRequested_ = true;
    cv_.notify_all();
    if (worker_.joinable()) {
        worker_.join();
    }
    running_ = false;
    setState(NtripState::Disabled);
}

bool NtripClient::running() const {
    return running_;
}

void NtripClient::setGgaSentence(std::string ggaSentence) {
    if (!ggaSentence.empty() && ggaSentence.back() != '\n') {
        ggaSentence += "\r\n";
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        latestGgaSentence_ = std::move(ggaSentence);
    }
    cv_.notify_all();
}

std::vector<std::uint8_t> NtripClient::popRtcm(std::size_t maxBytes) {
    std::lock_guard<std::mutex> lock(mutex_);
    const std::size_t count = std::min(maxBytes, rtcmQueue_.size());
    std::vector<std::uint8_t> result;
    result.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        result.push_back(rtcmQueue_.front());
        rtcmQueue_.pop_front();
    }
    return result;
}

NtripStatus NtripClient::status() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return status_;
}

void NtripClient::workerLoop() {
    auto backoff = config_.reconnectMin;
    while (!stopRequested_) {
        if (streamOnce()) {
            backoff = config_.reconnectMin;
        } else if (!stopRequested_) {
            setState(NtripState::Backoff);
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait_for(lock, backoff, [this] { return stopRequested_.load(); });
            backoff = std::min(config_.reconnectMax, backoff * 2);
            ++status_.reconnects;
        }
    }
}

bool NtripClient::streamOnce() {
    setState(NtripState::Connecting);
    std::string error;
    const int fd = connectSocket(&error);
    if (fd < 0) {
        setError(error);
        return false;
    }

    setState(NtripState::Authenticating);
    const std::string request = buildRequest();
    if (!sendAll(fd, request, &error)) {
        closeIfValid(fd);
        setError(error);
        return false;
    }

    std::string header;
    if (!readResponseHeader(fd, &header, &error)) {
        closeIfValid(fd);
        setError(error);
        return false;
    }
    if (!headerAccepted(header)) {
        closeIfValid(fd);
        setError("NTRIP rechazado: " + redactNtripSecret(header.substr(0, 120)));
        return false;
    }

    setError("");
    setState(NtripState::Streaming);
    auto lastGgaSent = std::chrono::steady_clock::time_point::min();
    std::array<std::uint8_t, 2048> buffer{};
    while (!stopRequested_) {
        const auto now = std::chrono::steady_clock::now();
        const std::string gga = latestGga();
        if (!gga.empty() && (lastGgaSent == std::chrono::steady_clock::time_point::min() ||
                             now - lastGgaSent >= config_.ggaInterval)) {
            if (!sendAll(fd, gga, &error)) {
                closeIfValid(fd);
                setError(error);
                return false;
            }
            lastGgaSent = now;
        }

        pollfd pfd{fd, POLLIN, 0};
        const int timeoutMs = static_cast<int>(
            std::max<std::chrono::milliseconds>(std::chrono::milliseconds(50),
                                                config_.socketTimeout)
                .count());
        const int ready = poll(&pfd, 1, timeoutMs);
        if (ready < 0) {
            if (errno == EINTR) {
                continue;
            }
            closeIfValid(fd);
            setError("poll NTRIP: " + socketError());
            return false;
        }
        if (ready == 0) {
            continue;
        }
        if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) {
            closeIfValid(fd);
            setError("flujo NTRIP cerrado");
            return false;
        }
        if (pfd.revents & POLLIN) {
            const ssize_t received = recv(fd, buffer.data(), buffer.size(), 0);
            if (received > 0) {
                pushRtcm(buffer.data(), static_cast<std::size_t>(received));
            } else if (received == 0) {
                closeIfValid(fd);
                setError("flujo NTRIP finalizado");
                return false;
            } else if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
                closeIfValid(fd);
                setError("recv NTRIP: " + socketError());
                return false;
            }
        }
    }
    closeIfValid(fd);
    return true;
}

int NtripClient::connectSocket(std::string* error) {
    addrinfo hints{};
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_family = AF_UNSPEC;
    addrinfo* results = nullptr;
    const std::string port = std::to_string(config_.port);
    const int rc = getaddrinfo(config_.host.c_str(), port.c_str(), &hints, &results);
    if (rc != 0) {
        if (error) {
            *error = "DNS NTRIP: " + std::string(gai_strerror(rc));
        }
        return -1;
    }
    int fd = -1;
    for (addrinfo* current = results; current != nullptr; current = current->ai_next) {
        fd = socket(current->ai_family, current->ai_socktype, current->ai_protocol);
        if (fd < 0) {
            continue;
        }
        const int originalFlags = fcntl(fd, F_GETFL, 0);
        if (originalFlags < 0 ||
            fcntl(fd, F_SETFL, originalFlags | O_NONBLOCK) < 0) {
            closeIfValid(fd);
            fd = -1;
            continue;
        }
        timeval timeout{};
        timeout.tv_sec = static_cast<int>(config_.socketTimeout.count() / 1000);
        timeout.tv_usec = static_cast<int>((config_.socketTimeout.count() % 1000) * 1000);
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
        const int connectResult = connect(fd, current->ai_addr, current->ai_addrlen);
        bool connected = connectResult == 0;
        if (!connected && errno == EINPROGRESS) {
            pollfd pfd{fd, POLLOUT, 0};
            int ready = -1;
            do {
                ready = poll(&pfd, 1, static_cast<int>(config_.socketTimeout.count()));
            } while (ready < 0 && errno == EINTR && !stopRequested_);
            if (ready > 0 && (pfd.revents & POLLOUT)) {
                int socketStatus = 0;
                socklen_t statusSize = sizeof(socketStatus);
                if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &socketStatus, &statusSize) == 0 &&
                    socketStatus == 0) {
                    connected = true;
                } else if (socketStatus != 0) {
                    errno = socketStatus;
                }
            } else if (ready == 0) {
                errno = ETIMEDOUT;
            }
        }
        if (connected) {
            if (fcntl(fd, F_SETFL, originalFlags) == 0) {
                break;
            }
        }
        closeIfValid(fd);
        fd = -1;
    }
    freeaddrinfo(results);
    if (fd < 0 && error) {
        *error = "conexión NTRIP: " + socketError();
    }
    return fd;
}

bool NtripClient::sendAll(int fd, const std::string& text, std::string* error) {
    const char* data = text.data();
    std::size_t remaining = text.size();
    while (remaining > 0 && !stopRequested_) {
        const ssize_t sent = send(fd, data, remaining, MSG_NOSIGNAL);
        if (sent > 0) {
            data += sent;
            remaining -= static_cast<std::size_t>(sent);
            continue;
        }
        if (sent < 0 && errno == EINTR) {
            continue;
        }
        if (error) {
            *error = "send NTRIP: " + socketError();
        }
        return false;
    }
    return remaining == 0;
}

bool NtripClient::readResponseHeader(int fd, std::string* header, std::string* error) {
    header->clear();
    char character = '\0';
    while (header->size() < 4096 && !stopRequested_) {
        const ssize_t received = recv(fd, &character, 1, 0);
        if (received == 1) {
            header->push_back(character);
            if (header->find("\r\n\r\n") != std::string::npos ||
                header->find("\n\n") != std::string::npos) {
                return true;
            }
        } else if (received == 0) {
            if (error) {
                *error = "NTRIP cerró antes de enviar cabecera";
            }
            return false;
        } else if (errno != EINTR) {
            if (error) {
                *error = "recv cabecera NTRIP: " + socketError();
            }
            return false;
        }
    }
    if (error) {
        *error = "cabecera NTRIP demasiado larga";
    }
    return false;
}

void NtripClient::pushRtcm(const std::uint8_t* data, std::size_t size) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (std::size_t i = 0; i < size; ++i) {
        rtcmQueue_.push_back(data[i]);
    }
    while (rtcmQueue_.size() > config_.maxQueuedBytes) {
        rtcmQueue_.pop_front();
    }
    status_.rtcmBytes += size;
    status_.lastRtcmSteady = std::chrono::steady_clock::now();
}

void NtripClient::setState(NtripState state) {
    std::lock_guard<std::mutex> lock(mutex_);
    status_.state = state;
}

void NtripClient::setError(std::string error) {
    std::lock_guard<std::mutex> lock(mutex_);
    status_.lastError = redactNtripSecret(error);
    if (!status_.lastError.empty()) {
        status_.state = NtripState::Error;
    }
}

std::string NtripClient::buildRequest() const {
    const std::string mountpoint =
        config_.mountpoint.empty() || config_.mountpoint.front() == '/'
            ? config_.mountpoint
            : "/" + config_.mountpoint;
    std::ostringstream request;
    request << "GET " << mountpoint << " HTTP/1.0\r\n"
            << "Host: " << config_.host << "\r\n"
            << "User-Agent: esp_datareceiver/1.0\r\n"
            << "Accept: */*\r\n"
            << "Ntrip-Version: Ntrip/2.0\r\n"
            << "Connection: close\r\n";
    if (!config_.username.empty() || !config_.password.empty()) {
        request << "Authorization: Basic "
                << base64Encode(config_.username + ":" + config_.password) << "\r\n";
    }
    request << "\r\n";
    return request.str();
}

std::string NtripClient::latestGga() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return latestGgaSentence_;
}
