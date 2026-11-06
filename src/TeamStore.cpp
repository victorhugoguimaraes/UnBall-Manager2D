#include "TeamStore.hpp"
#include "FileIO.hpp"

#include <algorithm>
#include <fstream>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <array>
#include <set>
#include <unistd.h>

using nlohmann::json;

TeamStore::TeamStore(std::filesystem::path file) : file_(std::move(file)) {}

void TeamStore::load() {
    if (!std::filesystem::exists(file_)) return;

    std::ifstream input(file_);
    if (!input) throw std::runtime_error("Nao foi possivel abrir o cadastro de times.");

    json document = json::parse(input);
    const auto& items = document.at("teams");
    if (!items.is_array()) throw std::runtime_error("O cadastro de times deve conter uma lista.");
    std::vector<Team> loaded;
    std::set<std::filesystem::path> roots;
    for (const auto& item : items) {
        Team team{
            item.value("displayName", ""), item.value("teamName", ""),
            item.value("root", ""), item.value("protectedMode", true)
        };
        if (team.displayName.empty() || team.teamName.empty() || team.root.empty())
            throw std::runtime_error("Há um time incompleto no cadastro.");
        team.root = std::filesystem::weakly_canonical(team.root);
        if (!roots.insert(team.root).second) throw std::runtime_error("Há pastas duplicadas no cadastro.");
        loaded.push_back(std::move(team));
    }
    teams_.swap(loaded);
}

void TeamStore::save() const {
    json document;
    document["teams"] = json::array();
    for (const Team& team : teams_) {
        document["teams"].push_back({
            {"displayName", team.displayName}, {"teamName", team.teamName},
            {"root", team.root.string()}, {"protectedMode", team.protectedMode}
        });
    }
    writeTextAtomically(file_, document.dump(2) + '\n');
}

void TeamStore::add(Team team) {
    std::string reason;
    if (!isValidTeam(team, reason)) throw std::invalid_argument(reason);

    const auto root = std::filesystem::canonical(team.root);
    const auto alreadyRegistered = std::any_of(teams_.begin(), teams_.end(), [&root](const Team& registered) {
                return std::filesystem::weakly_canonical(registered.root) == root;
    });
    if (alreadyRegistered) throw std::invalid_argument("Esta pasta ja esta cadastrada.");

    team.root = root;
    teams_.push_back(std::move(team));
    try {
        save();
    } catch (...) {
        teams_.pop_back();
        throw;
    }
}

bool isValidTeam(const Team& team, std::string& reason) {
    if (team.displayName.empty() || team.teamName.empty()) {
        reason = "Nome exibido e nome de servidor sao obrigatorios.";
        return false;
    }
    std::error_code error;
    if (!std::filesystem::is_directory(team.root, error)) {
        reason = "A pasta do time nao existe.";
        return false;
    }
    reason = "Pasta do time encontrada.";
    return true;
}

std::filesystem::path findTeamLauncher(const std::filesystem::path& root) {
    constexpr std::array names{"start.sh", "start", "start_team", "startAll", "startAll.sh",
                               "startlocal", "startlocal.sh", "start_team.sh", "localStartAll"};
    for (const auto& subdirectory : {std::filesystem::path{}, std::filesystem::path("bin"), std::filesystem::path("bins")}) {
        for (const auto* name : names) {
            const auto candidate = root / subdirectory / name;
            std::error_code error;
            if (std::filesystem::is_regular_file(candidate, error) && ::access(candidate.c_str(), X_OK) == 0)
                return std::filesystem::absolute(candidate);
        }
    }
    return {};
}

std::vector<Team> discoverTeams(const std::filesystem::path& directory) {
    std::vector<Team> teams;
    if (!std::filesystem::is_directory(directory)) return teams;
    std::set<std::filesystem::path> seen;
    std::filesystem::recursive_directory_iterator iterator(directory,
        std::filesystem::directory_options::skip_permission_denied);
    for (const auto& entry : iterator) {
        std::error_code error;
        if (!entry.is_directory(error)) continue;
        if (iterator.depth() >= 4) iterator.disable_recursion_pending();
        const auto launcher = findTeamLauncher(entry.path());
        if (launcher.empty()) continue;
        const auto canonical = std::filesystem::canonical(entry.path());
        if (seen.insert(canonical).second) {
            const auto name = entry.path().filename().string();
            teams.push_back({name, name, canonical, true});
        }
        iterator.disable_recursion_pending();
    }
    std::sort(teams.begin(), teams.end(), [](const Team& left, const Team& right) { return left.root < right.root; });
    return teams;
}

void TeamStore::remove(size_t index) {
    if (index >= teams_.size()) return;
    auto copy = teams_; // Backup caso a exclusao falhe
    teams_.erase(teams_.begin() + index);
    try {
        save();
    } catch (...) {
        teams_ = std::move(copy);
        throw;
    }
}
