#include "AppUpdate.hpp"
#include "AppVersion.hpp"
#include <algorithm>
#include <fstream>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string_view>
#include <unistd.h>

namespace {
nlohmann::json pointer(const std::filesystem::path& appData) {
    std::ifstream input(appData / "updates/current.json");
    return nlohmann::json::parse(input);
}
bool validRevision(const std::string& value) {
    return value.size() == 40 && std::all_of(value.begin(), value.end(), [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
}
}
std::string applicationRevision() { return unballSourceRevision; }

std::filesystem::path installedUpdate(const std::filesystem::path& appData) {
    try {
        const auto data = pointer(appData);
        const auto revision = data.at("revision").get<std::string>();
        if (!validRevision(revision)) return {};
        const auto directory = appData / "updates/versions" / revision;
        std::ifstream input(directory / "installed.json");
        const auto installed = nlohmann::json::parse(input);
        const auto executable = directory / "UnBall-Manager2D";
        if (installed.value("validated", false) && installed.value("revision", "") == revision &&
            std::filesystem::is_regular_file(executable) && ::access(executable.c_str(), X_OK) == 0 &&
            std::filesystem::is_directory(directory / "extractor/unball_extract") &&
            std::filesystem::is_regular_file(directory / "updater/unball_update.py")) return executable;
    } catch (const std::exception&) { /* A broken pointer never prevents the base application from opening. */ }
    return {};
}
std::filesystem::path startupUpdate(const std::filesystem::path& appData,
                                    const std::filesystem::path& executable, const std::string& currentRevision) {
    const auto target = installedUpdate(appData);
    if (target.empty()) return {};
    try {
        const auto current = std::filesystem::canonical(executable);
        if (current == std::filesystem::canonical(target)) return {};
        const bool managed = current.parent_path().parent_path() == std::filesystem::weakly_canonical(appData / "updates/versions");
        if (managed || pointer(appData).value("base_revision", "") == currentRevision) return target;
    } catch (const std::exception&) {}
    return {};
}
AppUpdate::AppUpdate(std::filesystem::path appData, std::filesystem::path script, std::string interpreter)
    : appData_(std::move(appData)), script_(std::move(script)), interpreter_(std::move(interpreter)) {}
void AppUpdate::start() {
    if (busy()) return;
    if (!std::filesystem::is_regular_file(script_)) throw std::runtime_error("Atualizador ausente. Recompile ou reinstale o Manager.");
    progress_ = 0; installed_ = false; cancelled_ = false; terminal_ = false; pending_.clear();
    process_ = std::make_unique<ChildProcess>(std::vector<std::string>{interpreter_, script_.string(),
        "--root", (appData_ / "updates").string(), "--current-sha", applicationRevision()});
    message_ = "Verificando atualizações…";
}
void AppUpdate::cancel() {
    if (process_ && !terminal_) { cancelled_ = true; message_ = "Cancelando atualização…"; process_->stop(); }
}
bool AppUpdate::poll() {
    if (!process_) return false;
    bool changed = false;
    pending_ += process_->poll();
    std::size_t end;
    while ((end = pending_.find('\n')) != std::string::npos) {
        const auto line = pending_.substr(0, end);
        pending_.erase(0, end + 1);
        constexpr std::string_view prefix = "UNBALL_UPDATE ";
        if (!line.starts_with(prefix)) continue;
        try {
            const auto data = nlohmann::json::parse(line.substr(prefix.size()));
            progress_ = std::clamp(data.at("progress").get<int>(), 0, 100);
            message_ = data.at("message").get<std::string>();
            const auto state = data.at("state").get<std::string>();
            installed_ = state == "installed";
            terminal_ = state != "working";
            changed = true;
        } catch (const std::exception&) { /* A malformed line cannot mark an installation as successful. */ }
    }
    if (pending_.size() > 65536) pending_.erase(0, pending_.size() - 65536);
    if (process_->finished()) {
        if (!terminal_) message_ = cancelled_ ? "Atualização cancelada." : "A atualização falhou. Consulte o log.";
        if (process_->exitCode() != 0) installed_ = false;
        process_.reset();
        changed = true;
    }
    return changed;
}
