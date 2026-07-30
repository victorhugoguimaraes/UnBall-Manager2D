#include "TeamStore.hpp"

#include <algorithm>
#include <fstream>
#include <nlohmann/json.hpp>
#include <stdexcept>

using nlohmann::json;

TeamStore::TeamStore(std::filesystem::path file) : file_(std::move(file)) {}

void TeamStore::load() {
    teams_.clear();
    if (!std::filesystem::exists(file_)) return;

    std::ifstream input(file_);
    if (!input) throw std::runtime_error("Nao foi possivel abrir o cadastro de times.");

    json document = json::parse(input);
    for (const auto& item : document.value("teams", json::array())) {
        teams_.push_back({
            item.value("displayName", ""), item.value("teamName", ""),
            item.value("root", ""), item.value("protectedMode", true)
        });
    }
}

void TeamStore::save() const {
    std::error_code error;
    std::filesystem::create_directories(file_.parent_path(), error);
    if (error) throw std::runtime_error("Nao foi possivel criar a pasta de dados.");

    json document;
    document["teams"] = json::array();
    for (const Team& team : teams_) {
        document["teams"].push_back({
            {"displayName", team.displayName}, {"teamName", team.teamName},
            {"root", team.root.string()}, {"protectedMode", team.protectedMode}
        });
    }
    // Grava primeiro em arquivo temporario para preservar o cadastro anterior em caso de falha.
    auto temporaryFile = file_;
    temporaryFile += ".tmp";
    std::ofstream output(temporaryFile, std::ios::trunc);
    if (!output) throw std::runtime_error("Nao foi possivel gravar o cadastro de times.");
    output << document.dump(2) << '\n';
    output.close();
    if (!output) throw std::runtime_error("Falha ao finalizar o cadastro de times.");

    std::filesystem::rename(temporaryFile, file_, error);
    if (!error) return;

    // Alguns sistemas nao substituem um arquivo existente durante o rename.
    std::filesystem::remove(file_, error);
    error.clear();
    std::filesystem::rename(temporaryFile, file_, error);
    if (error) throw std::runtime_error("Nao foi possivel finalizar o cadastro de times.");
}

void TeamStore::add(Team team) {
    std::string reason;
    if (!isValidTeam(team, reason)) throw std::invalid_argument(reason);

    const auto root = team.root.lexically_normal();
    const auto alreadyRegistered = std::any_of(teams_.begin(), teams_.end(), [&root](const Team& registered) {
        return registered.root.lexically_normal() == root;
    });
    if (alreadyRegistered) throw std::invalid_argument("Esta pasta ja esta cadastrada.");

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
    if (!std::filesystem::is_directory(team.root)) {
        reason = "A pasta do time nao existe.";
        return false;
    }
    reason = "Pasta do time encontrada.";
    return true;
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
