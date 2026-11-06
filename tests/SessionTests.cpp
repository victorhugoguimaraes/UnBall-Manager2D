#include "Session.hpp"
#include "ChildProcess.hpp"
#include "TestSupport.hpp"
#include <chrono>
#include <fstream>
#include <iterator>
#include <thread>

int main() {
    TemporaryDirectory temporary;
    const auto base = temporary.path / "espaço d'água $(touch INJECTION)";
    const auto leftRoot = temporary.path / "time ' esquerdo";
    const auto rightRoot = temporary.path / "time direito";
    executable(leftRoot / "bin/start.sh", "#!/bin/sh\nprintf '%s' \"$PWD\"\n");
    executable(rightRoot / "startAll", "#!/bin/sh\nexit 0\n");
    Team left{"Esquerdo", "left", leftRoot, true}, right{"Direito", "right", rightRoot, true};
    require(findTeamLauncher(left.root) == leftRoot / "bin/start.sh");
    const auto first = prepareSession(base, true, &left, &right);
    const auto second = prepareSession(base, false);
    require(first.directory != second.directory);
    require(first.directory.parent_path().filename().string().size() == 10);
    require(first.directory.filename().string().find("left-vs-right") != std::string::npos);
    require(sessionRuntimeDirectory(first.directory) == first.runtime);
    require(!std::filesystem::exists(first.directory / "left.sh"));
    require(first.configuration == first.runtime / "config");
    require(sessionRuntimeDirectory(temporary.path) == temporary.path);
    require(std::filesystem::exists(first.configuration / "session.conf"));
    std::ifstream file(first.configuration / "session.conf");
    const std::string config(std::istreambuf_iterator<char>(file), {});
    require(config.find("server::coach = true") != std::string::npos);
    require(config.find("server::team_l_start") != std::string::npos);
    require(!std::filesystem::exists(first.directory / "INJECTION"));
    ChildProcess launcher({"/bin/sh", (first.runtime / "launchers/left.sh").string()});
    std::string output;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!launcher.finished() && std::chrono::steady_clock::now() < deadline) {
        output += launcher.poll();
        std::this_thread::yield();
    }
    require(launcher.finished());
    require(launcher.exitCode() == 0);
    require(output == (leftRoot / "bin").string());
    bool rejected = false;
    right.teamName = left.teamName;
    try { prepareSession(base, false, &left, &right); }
    catch (const std::invalid_argument&) { rejected = true; }
    require(rejected);
    writeTextAtomically(first.logs / "game.rcg", "ULG6\n");
    writeTextAtomically(base / "log_analysis/log/2026/old.rcg.gz", "test");
    writeTextAtomically(base / "log/random.rcl.gz", "not a replay");
    require(findReplays(base).size() == 2);
    const auto discovery = temporary.path / "teams";
    executable(discovery / "rc2025/teamA/bin/start.sh", "#!/bin/sh\nexit 0\n");
    executable(discovery / "teamB/startAll", "#!/bin/sh\nexit 0\n");
    std::filesystem::create_directories(discovery / "empty/not-a-team");
    require(discoverTeams(discovery).size() == 2);
    // Failed replacement leaves the existing destination intact and no temporary files.
    const auto destination = temporary.path / "destination";
    std::filesystem::create_directory(destination);
    writeTextAtomically(destination / "original", "preserved");
    bool failed = false;
    try { writeTextAtomically(destination, "cannot replace a directory"); }
    catch (const std::filesystem::filesystem_error&) { failed = true; }
    require(failed && std::filesystem::exists(destination / "original"));
    for (const auto& entry : std::filesystem::directory_iterator(temporary.path))
        require(!entry.path().filename().string().starts_with("destination.tmp-"));
}
