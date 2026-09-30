#include "../linux_receiver/ntrip_client.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cassert>
#include <chrono>
#include <cstring>
#include <functional>
#include <iostream>
#include <string>
#include <thread>

namespace {

void closeIfValid(int fd) {
    if (fd >= 0) {
        close(fd);
    }
}

class FakeNtripServer {
public:
    explicit FakeNtripServer(bool accept = true) : accept_(accept) {
        fd_ = socket(AF_INET, SOCK_STREAM, 0);
        assert(fd_ >= 0);
        int enabled = 1;
        setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, &enabled, sizeof(enabled));
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.sin_port = 0;
        assert(bind(fd_, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0);
        assert(listen(fd_, 1) == 0);
        socklen_t length = sizeof(address);
        assert(getsockname(fd_, reinterpret_cast<sockaddr*>(&address), &length) == 0);
        port_ = ntohs(address.sin_port);
        thread_ = std::thread([this] { run(); });
    }

    ~FakeNtripServer() {
        closeIfValid(fd_);
        if (thread_.joinable()) {
            thread_.join();
        }
    }

    int port() const { return port_; }
    std::string request() const { return request_; }
    std::string gga() const { return gga_; }

private:
    void run() {
        const int client = accept(fd_, nullptr, nullptr);
        if (client < 0) {
            return;
        }
        char buffer[512]{};
        while (request_.find("\r\n\r\n") == std::string::npos) {
            const ssize_t received = recv(client, buffer, sizeof(buffer), 0);
            if (received <= 0) {
                closeIfValid(client);
                return;
            }
            request_.append(buffer, static_cast<std::size_t>(received));
        }
        if (!accept_) {
            const std::string response = "HTTP/1.1 401 Unauthorized\r\n\r\n";
            send(client, response.data(), response.size(), MSG_NOSIGNAL);
            closeIfValid(client);
            return;
        }
        const std::string response = "ICY 200 OK\r\n\r\n";
        send(client, response.data(), response.size(), MSG_NOSIGNAL);
        const unsigned char rtcm[] = {0xD3, 0x00, 0x03, 0x01, 0x02, 0x03};
        send(client, rtcm, sizeof(rtcm), MSG_NOSIGNAL);
        const ssize_t ggaReceived = recv(client, buffer, sizeof(buffer), 0);
        if (ggaReceived > 0) {
            gga_.append(buffer, static_cast<std::size_t>(ggaReceived));
        }
        closeIfValid(client);
    }

    bool accept_;
    int fd_{-1};
    int port_{0};
    std::thread thread_;
    std::string request_;
    std::string gga_;
};

bool waitUntil(const std::function<bool()>& predicate) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (std::chrono::steady_clock::now() < deadline) {
        if (predicate()) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return predicate();
}

void streamsRtcmAndSendsGga() {
    FakeNtripServer server;
    NtripConfig config;
    config.host = "127.0.0.1";
    config.port = server.port();
    config.mountpoint = "VRS3M";
    config.username = "user";
    config.password = "secret";
    config.ggaInterval = std::chrono::seconds(0);
    config.reconnectMin = std::chrono::milliseconds(2000);
    config.socketTimeout = std::chrono::milliseconds(100);
    config.maxQueuedBytes = 4;

    NtripClient client(config);
    client.setGgaSentence("$GNGGA,1*00");
    assert(client.start());
    assert(waitUntil([&] { return client.status().rtcmBytes >= 6; }));
    const auto data = client.popRtcm(16);
    client.stop();

    assert(data.size() == 4);
    assert(data[0] == 0x03 && data[3] == 0x03);
    assert(server.request().find("GET /VRS3M HTTP/1.0") != std::string::npos);
    assert(server.request().find("Authorization: Basic dXNlcjpzZWNyZXQ") != std::string::npos);
    assert(waitUntil([&] { return server.gga().find("$GNGGA") != std::string::npos; }));
}

void redactsErrorsAndRejectsUnauthorized() {
    assert(redactNtripSecret("Authorization: Basic abc\r\nPassword: top") ==
           "Authorization: <redacted>\r\nPassword: <redacted>");
    FakeNtripServer server(false);
    NtripConfig config;
    config.host = "127.0.0.1";
    config.port = server.port();
    config.mountpoint = "VRS3M";
    config.username = "user";
    config.password = "secret";
    config.reconnectMin = std::chrono::milliseconds(2000);
    config.socketTimeout = std::chrono::milliseconds(100);
    NtripClient client(config);
    assert(client.start());
    assert(waitUntil([&] { return client.status().state == NtripState::Backoff; }));
    const auto status = client.status();
    client.stop();
    assert(status.lastError.find("secret") == std::string::npos);
    assert(status.lastError.find("NTRIP rechazado") != std::string::npos);
}

}  // namespace

int main() {
    streamsRtcmAndSendsGga();
    redactsErrorsAndRejectsUnauthorized();
    std::cout << "ntrip_client_test: OK\n";
}
