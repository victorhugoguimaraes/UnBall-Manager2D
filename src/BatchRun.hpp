#pragma once
#include "TeamStore.hpp"
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

// Sequential run state. A game is settled only after its export finishes.
class BatchRun final {
public:
    BatchRun(const std::filesystem::path& base, Team left, Team right, int total,
             int timeoutSeconds, bool coach, bool monitor);
    void startGame(const std::filesystem::path& session);
    void endGame(const std::string& outcome, int exitCode, double seconds);
    void endExport(const std::string& status, std::optional<bool> normalEnd = std::nullopt);
    void cancel();
    void abort(const std::string& reason);
    bool finished() const noexcept;
    bool gameRunning() const noexcept { return phase_ == Phase::Game; }
    bool waitingForExport() const noexcept { return phase_ == Phase::Export; }
    bool ready() const noexcept { return phase_ == Phase::Idle && !finished(); }
    bool cancelled() const noexcept { return cancelled_; }
    bool timedOut(double seconds) const noexcept { return phase_ == Phase::Game && seconds >= timeoutSeconds_; }
    int total() const noexcept { return total_; }
    int started() const noexcept { return static_cast<int>(games_.size()); }
    int settled() const noexcept;
    int failures() const noexcept;
    int exportFailures() const noexcept;
    std::string status() const;
    const std::filesystem::path& directory() const noexcept { return directory_; }
    const Team& left() const noexcept { return left_; }
    const Team& right() const noexcept { return right_; }
private:
    enum class Phase { Idle, Game, Export };
    struct Game {
        std::filesystem::path session;
        std::string outcome = "running", exportStatus;
        std::optional<int> exitCode;
        double seconds = 0;
    };
    void save() const;
    std::filesystem::path base_, directory_;
    Team left_, right_;
    int total_, timeoutSeconds_;
    bool coach_, monitor_, cancelled_ = false;
    Phase phase_ = Phase::Idle;
    std::string error_;
    std::vector<Game> games_;
};
