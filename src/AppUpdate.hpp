#pragma once
#include "ChildProcess.hpp"
#include <filesystem>
#include <memory>
#include <string>

std::string applicationRevision();
std::filesystem::path installedUpdate(const std::filesystem::path& appData);
std::filesystem::path startupUpdate(const std::filesystem::path& appData,
                                    const std::filesystem::path& executable, const std::string& currentRevision);

class AppUpdate final {
public:
    AppUpdate(std::filesystem::path appData, std::filesystem::path script, std::string interpreter = "python3");
    void start();
    void cancel();
    bool poll();
    bool busy() const noexcept { return static_cast<bool>(process_); }
    int progress() const noexcept { return progress_; }
    const std::string& message() const noexcept { return message_; }
    bool installed() const noexcept { return installed_; }
    std::filesystem::path log() const { return appData_ / "updates/update.log"; }
private:
    std::filesystem::path appData_, script_;
    std::string interpreter_, pending_, message_ = "Verifique e instale a versão mais recente do GitHub.";
    std::unique_ptr<ChildProcess> process_;
    int progress_ = 0;
    bool installed_ = false, cancelled_ = false, terminal_ = false;
};
