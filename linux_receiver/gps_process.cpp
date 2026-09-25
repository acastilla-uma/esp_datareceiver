#include "gps_process.h"

#include <cerrno>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

GpsProcess::~GpsProcess() {
    stop();
}

bool GpsProcess::start(const std::string& scriptPath,
                       const std::string& deviceId,
                       std::string* error) {
    int outputPipe[2];
    if (pipe(outputPipe) != 0) {
        if (error) *error = std::strerror(errno);
        return false;
    }

    const pid_t child = fork();
    if (child < 0) {
        if (error) *error = std::strerror(errno);
        close(outputPipe[0]);
        close(outputPipe[1]);
        return false;
    }
    if (child == 0) {
        close(outputPipe[0]);
        if (dup2(outputPipe[1], STDOUT_FILENO) < 0) {
            _exit(126);
        }
        close(outputPipe[1]);
        if (deviceId.empty()) {
            execlp("python3", "python3", scriptPath.c_str(), "--initial", "50",
                   "--interval", "1", static_cast<char*>(nullptr));
        } else {
            execlp("python3", "python3", scriptPath.c_str(), "--initial", "50",
                   "--interval", "1", "--device-id", deviceId.c_str(),
                   static_cast<char*>(nullptr));
        }
        _exit(127);
    }

    close(outputPipe[1]);
    const int flags = fcntl(outputPipe[0], F_GETFL, 0);
    if (flags < 0 || fcntl(outputPipe[0], F_SETFL, flags | O_NONBLOCK) != 0) {
        if (error) *error = std::strerror(errno);
        close(outputPipe[0]);
        kill(child, SIGTERM);
        waitpid(child, nullptr, 0);
        return false;
    }

    fileDescriptor_ = outputPipe[0];
    processId_ = static_cast<int>(child);
    return true;
}

std::vector<std::string> GpsProcess::readLines() {
    std::vector<std::string> lines;
    if (fileDescriptor_ < 0) {
        return lines;
    }

    char buffer[4096];
    while (true) {
        const ssize_t count = read(fileDescriptor_, buffer, sizeof(buffer));
        if (count > 0) {
            pending_.append(buffer, static_cast<std::size_t>(count));
        } else if (count < 0 && errno == EINTR) {
            continue;
        } else {
            break;
        }
    }

    std::size_t newline = 0;
    while ((newline = pending_.find('\n')) != std::string::npos) {
        std::string line = pending_.substr(0, newline);
        pending_.erase(0, newline + 1);
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (!line.empty()) {
            lines.push_back(std::move(line));
        }
    }
    if (pending_.size() > 1024 * 1024) {
        pending_.clear();
    }
    return lines;
}

int GpsProcess::fileDescriptor() const {
    return fileDescriptor_;
}

bool GpsProcess::running() {
    if (processId_ < 0) {
        return false;
    }
    int status = 0;
    const pid_t result = waitpid(static_cast<pid_t>(processId_), &status, WNOHANG);
    if (result == 0) {
        return true;
    }
    if (fileDescriptor_ >= 0) {
        close(fileDescriptor_);
        fileDescriptor_ = -1;
    }
    processId_ = -1;
    return false;
}

void GpsProcess::stop() {
    if (fileDescriptor_ >= 0) {
        close(fileDescriptor_);
        fileDescriptor_ = -1;
    }
    if (processId_ < 0) {
        return;
    }

    const pid_t process = static_cast<pid_t>(processId_);
    kill(process, SIGTERM);
    for (int attempt = 0; attempt < 10; ++attempt) {
        if (waitpid(process, nullptr, WNOHANG) == process) {
            processId_ = -1;
            return;
        }
        usleep(50000);
    }
    kill(process, SIGKILL);
    waitpid(process, nullptr, 0);
    processId_ = -1;
}
