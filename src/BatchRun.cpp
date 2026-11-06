#include "BatchRun.hpp"
#include "FileIO.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <nlohmann/json.hpp>
#include <sstream>
#include <stdexcept>

namespace {
std::string csv(const std::string& value) {
    std::string result = "\"";
    for (char c : value) { if (c == '"') result += '"'; result += c; }
    return result + '"';
}
}
BatchRun::BatchRun(const std::filesystem::path& base, Team left, Team right, int total,
                   int timeoutSeconds, bool coach, bool monitor)
    : base_(std::filesystem::absolute(base)), left_(std::move(left)), right_(std::move(right)),
      total_(total), timeoutSeconds_(timeoutSeconds), coach_(coach), monitor_(monitor) {
    if (total < 2 || total > 1000 || timeoutSeconds <= 0 || left_.teamName == right_.teamName)
        throw std::invalid_argument("Lote inválido: escolha 2 a 1000 jogos, tempo limite positivo e times distintos.");
    const auto now = std::chrono::system_clock::now();
    const auto clock = std::chrono::system_clock::to_time_t(now);
    std::tm local{}; localtime_r(&clock, &local);
    std::ostringstream day, name;
    day << std::put_time(&local, "%Y-%m-%d");
    name << std::put_time(&local, "%H-%M-%S") << '_' << std::hex << now.time_since_epoch().count();
    const auto parent = base_ / "runs/UnBall-Manager2D/lotes" / day.str();
    std::filesystem::create_directories(parent);
    directory_ = parent / name.str();
    for (int suffix = 1; !std::filesystem::create_directory(directory_); ++suffix)
        directory_ = parent / (name.str() + '-' + std::to_string(suffix));
    directory_ = std::filesystem::canonical(directory_);
    save();
}
bool BatchRun::finished() const noexcept { return phase_ == Phase::Idle && (cancelled_ || !error_.empty() || started() >= total_); }
int BatchRun::settled() const noexcept {
    return static_cast<int>(std::count_if(games_.begin(), games_.end(), [](const Game& g) { return !g.exportStatus.empty(); }));
}
int BatchRun::failures() const noexcept {
    return static_cast<int>(std::count_if(games_.begin(), games_.end(), [](const Game& g) { return g.outcome == "failed" || g.outcome == "timeout" || g.outcome == "launch_failed"; }));
}
int BatchRun::exportFailures() const noexcept {
    return static_cast<int>(std::count_if(games_.begin(), games_.end(), [](const Game& g) { return g.exportStatus == "failed" || g.exportStatus == "not_started"; }));
}
std::string BatchRun::status() const {
    if (!finished()) return cancelled_ ? "cancelling" : "running";
    if (!error_.empty()) return "failed";
    if (cancelled_) return "cancelled";
    return failures() || exportFailures() ? "completed_with_errors" : "completed";
}
void BatchRun::startGame(const std::filesystem::path& session) {
    if (!ready()) throw std::logic_error("Lote não está pronto para outro jogo.");
    std::ifstream input(session / "match.json");
    auto metadata = nlohmann::json::parse(input);
    metadata["batch_id"] = directory_.filename().string();
    metadata["batch_directory"] = directory_.string();
    metadata["batch_index"] = started() + 1;
    metadata["batch_total"] = total_;
    writeTextAtomically(session / "match.json", metadata.dump(2));
    games_.push_back(Game{session, "running", "", {}, 0});
    phase_ = Phase::Game;
    save();
}
void BatchRun::endGame(const std::string& outcome, int exitCode, double seconds) {
    if (phase_ != Phase::Game || !std::isfinite(seconds) || seconds < 0 ||
        (outcome != "completed" && outcome != "failed" && outcome != "timeout" && outcome != "cancelled"))
        throw std::logic_error("Resultado de jogo inválido.");
    auto& game = games_.back();
    game.outcome = outcome; game.exitCode = exitCode; game.seconds = seconds;
    phase_ = Phase::Export;
    save();
}
void BatchRun::endExport(const std::string& status, std::optional<bool> normalEnd) {
    if (phase_ != Phase::Export || (status != "completed" && status != "completed_with_warnings" && status != "failed"))
        throw std::logic_error("Resultado da exportação inválido.");
    auto& game = games_.back();
    if (normalEnd && game.exitCode == 0 && game.outcome != "cancelled" && game.outcome != "timeout")
        game.outcome = *normalEnd ? "completed" : "failed";
    game.exportStatus = status;
    phase_ = Phase::Idle;
    save();
}
void BatchRun::cancel() { cancelled_ = true; save(); }
void BatchRun::abort(const std::string& reason) {
    error_ = reason;
    if (phase_ != Phase::Idle && !games_.empty()) {
        if (phase_ == Phase::Game) games_.back().outcome = "launch_failed";
        games_.back().exportStatus = "not_started";
    }
    phase_ = Phase::Idle;
    save();
}
void BatchRun::save() const {
    nlohmann::json document{{"mode", "sequential"}, {"total", total_}, {"started", started()},
        {"settled", settled()}, {"game_failures", failures()}, {"export_failures", exportFailures()},
        {"status", status()}, {"error", error_}, {"timeout_seconds", timeoutSeconds_},
        {"left_team", left_.teamName}, {"right_team", right_.teamName},
        {"left_directory", left_.root.string()}, {"right_directory", right_.root.string()},
        {"coach", coach_}, {"monitor", monitor_}, {"games", nlohmann::json::array()}};
    std::ostringstream summary;
    summary.imbue(std::locale::classic());
    summary << "jogo,sessao,status,codigo_saida,duracao_segundos,extracao\n";
    int index = 0;
    for (const auto& game : games_) {
        const auto relative = game.session.lexically_relative(base_).string();
        document["games"].push_back({{"index", ++index}, {"session", relative}, {"outcome", game.outcome},
            {"exit_code", game.exitCode ? nlohmann::json(*game.exitCode) : nlohmann::json(nullptr)},
            {"duration_seconds", game.seconds}, {"export_status", game.exportStatus}});
        summary << index << ',' << csv(relative) << ',' << game.outcome << ',';
        if (game.exitCode) summary << *game.exitCode;
        summary << ',' << game.seconds << ',' << game.exportStatus << '\n';
    }
    writeTextAtomically(directory_ / "resumo.csv", summary.str());
    writeTextAtomically(directory_ / "lote.json", document.dump(2) + '\n');
}
