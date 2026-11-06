#pragma once

#include <chrono>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

// Owns one process group, including descendants left behind by its leader.
// poll() is non-blocking and bounds output work per UI tick.
class ChildProcess final {
public:
    ChildProcess(const std::vector<std::string>& arguments,
                 const std::filesystem::path& directory = {},
                 const std::map<std::string, std::string>& environment = {});
    ~ChildProcess();
    ChildProcess(const ChildProcess&) = delete;
    ChildProcess& operator=(const ChildProcess&) = delete;
    ChildProcess(ChildProcess&&) = delete;
    ChildProcess& operator=(ChildProcess&&) = delete;

    std::string poll();
    void stop();
    bool finished() const noexcept { return finished_; }
    int pid() const noexcept { return pid_; }
    std::optional<int> exitCode() const noexcept { return exitCode_; }

private:
    void signalGroup(int signal) const noexcept;
    int pid_{-1};
    int output_{-1};
    std::optional<int> exitCode_;
    std::optional<std::chrono::steady_clock::time_point> stoppingSince_;
    int stopStage_{};
    bool finished_{};
};
