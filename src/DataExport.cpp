#include "DataExport.hpp"
#include "FileIO.hpp"
#include "Session.hpp"

#include <algorithm>
#include <fstream>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>

namespace {
nlohmann::json readJson(const std::filesystem::path& path) {
    if (!std::filesystem::exists(path)) return nlohmann::json::object();
    std::ifstream stream(path);
    return nlohmann::json::parse(stream);
}
}

DataExport::DataExport(std::string interpreter, std::filesystem::path moduleDirectory)
    : interpreter_(std::move(interpreter)), moduleDirectory_(std::move(moduleDirectory)) {}

bool DataExport::busy() const noexcept { return process_ || !queue_.empty(); }

bool DataExport::contains(const std::filesystem::path& session) const {
    return (process_ && current_ == session) || std::find(queue_.begin(), queue_.end(), session) != queue_.end();
}

void DataExport::state(const std::filesystem::path& session, const std::string& value, const std::string& detail) {
    writeTextAtomically(sessionRuntimeDirectory(session) / "extraction.json", nlohmann::json{{"status", value}, {"detail", detail}}.dump(2));
}

bool DataExport::enqueue(const std::filesystem::path& session) {
    const auto directory = std::filesystem::canonical(session);
    if (contains(directory)) return false;
    state(directory, "queued");
    queue_.push_back(directory);
    return true;
}

bool DataExport::poll() {
    bool changed = false;
    if (process_) {
        output_ += process_->poll();
        constexpr std::size_t maximumOutput = 32768;
        if (output_.size() > maximumOutput) output_.erase(0, output_.size() - maximumOutput);
        if (process_->finished()) {
            try {
                writeTextAtomically(sessionRuntimeDirectory(current_) / "extraction-console.log", output_);
                if (process_->exitCode() == 0) {
                    const auto latest = readJson(current_ / "exports/latest.json");
                    state(current_, latest.at("status").get<std::string>());
                    message_ = "Dados exportados: " + current_.filename().string();
                } else {
                    state(current_, "failed", output_);
                    message_ = "Falha ao extrair dados: " + output_;
                }
            } catch (const std::exception& error) {
                message_ = "Não foi possível registrar o resultado da extração: " + std::string(error.what());
            }
            process_.reset();
            current_.clear();
            output_.clear();
            changed = true;
        }
    }
    if (!process_ && !queue_.empty()) {
        current_ = queue_.front();
        queue_.pop_front();
        try {
            if (!std::filesystem::is_directory(moduleDirectory_ / "unball_extract"))
                throw std::runtime_error("Módulo de extração ausente. Recompile/reinstale o Manager.");
            process_ = std::make_unique<ChildProcess>(
                std::vector<std::string>{interpreter_, "-m", "unball_extract", "--session", current_.string()},
                moduleDirectory_, std::map<std::string, std::string>{{"PYTHONPATH", moduleDirectory_.string()}, {"PYTHONDONTWRITEBYTECODE", "1"}});
            state(current_, "extracting");
            message_ = "Extraindo dados: " + current_.filename().string();
        } catch (const std::exception& error) {
            process_.reset();
            message_ = "Extração não iniciada: " + std::string(error.what());
            try { state(current_, "failed", message_); } catch (const std::exception&) { /* message_ remains visible to the caller. */ }
            current_.clear();
        }
        changed = true;
    }
    return changed;
}

std::vector<DataSession> findDataSessions(const std::filesystem::path& base) {
    std::vector<DataSession> sessions;
    const auto parent = base / "runs/UnBall-Manager2D";
    if (!std::filesystem::is_directory(parent)) return sessions;
    std::filesystem::recursive_directory_iterator iterator(parent, std::filesystem::directory_options::skip_permission_denied);
    for (const auto& entry : iterator) {
        if (!entry.is_directory()) continue;
        const bool candidate = std::filesystem::is_regular_file(entry.path() / "match.json") || std::filesystem::is_directory(entry.path() / "logs");
        if (candidate || iterator.depth() >= 1) iterator.disable_recursion_pending();
        if (!candidate) continue;
        DataSession session{};
        session.directory = std::filesystem::canonical(entry.path());
        session.label = entry.path().filename().string();
        session.status = "pending";
        const auto fileTime = entry.last_write_time();
        const auto clock = std::chrono::file_clock::to_sys(fileTime);
        const auto seconds = std::chrono::system_clock::to_time_t(clock);
        std::tm local{};
        localtime_r(&seconds, &local);
        std::ostringstream created;
        created << std::put_time(&local, "%Y-%m-%dT%H:%M:%S%z");
        session.createdAt = created.str();
        try {
            const auto metadata = readJson(entry.path() / "match.json");
            if (metadata.contains("leftTeam")) session.label = metadata.value("leftTeam", "") + " × " + metadata.value("rightTeam", "");
            else if (metadata.contains("coach")) session.label = "Somente servidor";
            session.createdAt = metadata.value("created_at", session.createdAt);
            session.leftRoot = metadata.value("leftDirectory", "");
            session.rightRoot = metadata.value("rightDirectory", "");
            const auto logDirectory = entry.path() / "logs";
            if (std::filesystem::is_directory(logDirectory)) {
                for (const auto& log : std::filesystem::directory_iterator(logDirectory)) {
                    const auto name = log.path().filename().string();
                    if (log.is_regular_file() && (name.ends_with(".rcg") || name.ends_with(".rcg.gz"))) {
                        if (session.replay.empty()) session.replay = log.path();
                        else { session.replay.clear(); break; } // Ambiguous replay: use the legacy file picker.
                    }
                }
            }
            const auto state = readJson(sessionRuntimeDirectory(entry.path()) / "extraction.json");
            session.status = state.value("status", "pending");
            session.detail = state.value("detail", "");
            const auto latest = readJson(entry.path() / "exports/latest.json");
            if (latest.contains("export_directory")) {
                // Accept a legacy bundle name or versions/<bundle>, never an arbitrary path.
                const std::filesystem::path name = latest.at("export_directory").get<std::string>();
                if (name.empty() || name.is_absolute() || (name.parent_path() != std::filesystem::path{} && name.parent_path() != "versions") || name.filename() == "." || name.filename() == "..") throw std::runtime_error("Destino de exportação inválido.");
                const auto directory = entry.path() / "exports" / name;
                if (std::filesystem::is_regular_file(directory / "manifest.json")) {
                    session.exportDirectory = directory;
                    session.coverage = latest.value("coverage", "");
                    if (session.status == "pending") session.status = latest.value("status", "completed");
                }
            }
        } catch (const std::exception& error) {
            session.status = "failed";
            session.detail = error.what();
        }
        sessions.push_back(std::move(session));
    }
    std::sort(sessions.begin(), sessions.end(), [](const DataSession& left, const DataSession& right) { return left.createdAt != right.createdAt ? left.createdAt > right.createdAt : left.directory > right.directory; });
    return sessions;
}

std::filesystem::path csvDirectory(const std::filesystem::path& bundle) {
    return std::filesystem::is_directory(bundle / "csv") ? bundle / "csv" : bundle;
}
