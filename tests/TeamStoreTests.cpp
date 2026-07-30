#include "TeamStore.hpp"

#include <cassert>
#include <chrono>
#include <stdexcept>

int main() {
    const auto uniqueId = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto temporaryRoot = std::filesystem::temp_directory_path() /
        ("unball-manager-tests-" + std::to_string(uniqueId));
    const auto teamRoot = temporaryRoot / "team";
    std::filesystem::create_directories(teamRoot);

    Team team{"Teste", "test_team", teamRoot, true};
    std::string reason;
    assert(isValidTeam(team, reason));

    TeamStore store(temporaryRoot / "teams.json");
    store.add(team);

    TeamStore loadedStore(temporaryRoot / "teams.json");
    loadedStore.load();
    assert(loadedStore.teams().size() == 1);
    assert(loadedStore.teams().front().teamName == "test_team");

    bool duplicateWasRejected = false;
    try {
        store.add(team);
    } catch (const std::invalid_argument&) {
        duplicateWasRejected = true;
    }
    assert(duplicateWasRejected);

    std::filesystem::remove_all(temporaryRoot);
    return 0;
}
