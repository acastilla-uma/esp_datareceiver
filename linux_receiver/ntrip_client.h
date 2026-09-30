#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

enum class NtripState {
    Disabled,
    Connecting,
    Authenticating,
    Streaming,
    Backoff,
    Error,
};

struct NtripConfig {
    std::string host{"ergnss-tr.ign.es"};
    int port{2101};
    std::string mountpoint{"VRS3M"};
    std::string username;
    std::string password;
    std::chrono::seconds ggaInterval{5};
    std::chrono::milliseconds reconnectMin{500};
    std::chrono::milliseconds reconnectMax{10000};
    std::size_t maxQueuedBytes{65536};
    std::chrono::milliseconds socketTimeout{1000};
};

struct NtripStatus {
    NtripState state{NtripState::Disabled};
    std::string host;
    int port{0};
    std::string mountpoint;
    std::uint64_t rtcmBytes{0};
    std::uint64_t reconnects{0};
    std::string lastError;
    std::optional<std::chrono::steady_clock::time_point> lastRtcmSteady;
};

std::string ntripStateToString(NtripState state);
std::string redactNtripSecret(const std::string& text);

class NtripClient {
public:
    explicit NtripClient(NtripConfig config);
    ~NtripClient();

    NtripClient(const NtripClient&) = delete;
    NtripClient& operator=(const NtripClient&) = delete;

    bool start(std::string* error = nullptr);
    void stop();
    bool running() const;

    void setGgaSentence(std::string ggaSentence);
    std::vector<std::uint8_t> popRtcm(std::size_t maxBytes);
    NtripStatus status() const;

private:
    void workerLoop();
    bool streamOnce();
    int connectSocket(std::string* error);
    bool sendAll(int fd, const std::string& text, std::string* error);
    bool readResponseHeader(int fd, std::string* header, std::string* error);
    void pushRtcm(const std::uint8_t* data, std::size_t size);
    void setState(NtripState state);
    void setError(std::string error);
    std::string buildRequest() const;
    std::string latestGga() const;

    NtripConfig config_;
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<std::uint8_t> rtcmQueue_;
    NtripStatus status_;
    std::string latestGgaSentence_;
    std::thread worker_;
    std::atomic<bool> stopRequested_{false};
    std::atomic<bool> running_{false};
};
