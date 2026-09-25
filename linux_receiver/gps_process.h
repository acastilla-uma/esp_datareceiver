#pragma once

#include <string>
#include <vector>

class GpsProcess {
public:
    GpsProcess() = default;
    ~GpsProcess();

    GpsProcess(const GpsProcess&) = delete;
    GpsProcess& operator=(const GpsProcess&) = delete;

    bool start(const std::string& scriptPath,
               const std::string& deviceId,
               std::string* error = nullptr);
    std::vector<std::string> readLines();
    int fileDescriptor() const;
    bool running();
    void stop();

private:
    int fileDescriptor_{-1};
    int processId_{-1};
    std::string pending_;
};
