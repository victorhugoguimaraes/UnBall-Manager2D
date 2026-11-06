#include "TeamStore.hpp"

#include "TestSupport.hpp"
#include <chrono>
#include <stdexcept>
#include <fstream>

int main() {
    const auto uniqueId = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto temporaryRoot = std::filesystem::temp_directory_path() /
        ("unball-manager-tests-" + std::to_string(uniqueId));
    const auto teamRoot = temporaryRoot / "team";
    std::filesystem::create_directories(teamRoot);

    Team team{"Teste", "test_team", teamRoot, true};
    std::string reason;
    require(isValidTeam(team, reason));

    TeamStore store(temporaryRoot / "teams.json");
    store.add(team);

    TeamStore loadedStore(temporaryRoot / "teams.json");
    loadedStore.load();
    require(loadedStore.teams().size() == 1);
    require(loadedStore.teams().front().teamName == "test_team");

    bool duplicateWasRejected = false;
    try {
        store.add(team);
    } catch (const std::invalid_argument&) {
        duplicateWasRejected = true;
    }
    require(duplicateWasRejected);

    // A symlink to the same team must not create a second catalogue entry.
    const auto alias = temporaryRoot / "alias";
    std::filesystem::create_directory_symlink(teamRoot, alias);
    bool aliasWasRejected = false;
    try { store.add(Team{"Alias", "alias", alias, true}); }
    catch (const std::invalid_argument&) { aliasWasRejected = true; }
    require(aliasWasRejected);

    // A malformed load must not destroy a catalogue already held in memory.
    std::ofstream(temporaryRoot / "teams.json") << R"({"teams":[{"displayName":"partial"},42]})";
    bool malformedWasRejected = false;
    try { loadedStore.load(); }
    catch (const std::exception&) { malformedWasRejected = true; }
    require(malformedWasRejected);
    require(loadedStore.teams().size() == 1);
    require(loadedStore.teams().front().teamName == "test_team");

    std::filesystem::remove_all(temporaryRoot);
    return 0;
}
