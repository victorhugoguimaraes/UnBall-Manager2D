#include "Session.hpp"
#include "ChildProcess.hpp"
#include "DataExport.hpp"
#include "TestSupport.hpp"
#include <chrono>
#include <fstream>
#include <iostream>
#include <thread>

namespace {
std::string quote(const std::string& value) {
    std::string result = "'";
    for (char character : value) result += character == '\'' ? "'\"'\"'" : std::string(1, character);
    return result + "'";
}
}

// Opt-in integration check: server executable, strategy bin directory, output base.
int main(int argc, char** argv) {
    if (argc != 4) { std::cerr << "Uso: SessionSmokeTest RCSSSERVER STRATEGY_BIN OUTPUT_BASE\n"; return 2; }
    const auto strategy = std::filesystem::absolute(argv[2]);
    const auto base = std::filesystem::absolute(argv[3]);
    const auto teams = base / "fixtures";
    for (const auto* name : {"ManagerLeft", "ManagerRight"})
        executable(teams / name / "start.sh", "#!/bin/sh\ncd " + quote(strategy.string()) +
            "\nexec ./start.sh -p 16000 -P 16002 -t " + name + "\n");
    const Team left{"Left", "ManagerLeft", teams / "ManagerLeft", true};
    const Team right{"Right", "ManagerRight", teams / "ManagerRight", true};
    const auto session = prepareSession(base, false, &left, &right);
    std::ofstream configuration(session.configuration / "session.conf", std::ios::app);
    configuration << "server::port = 16000\nserver::coach_port = 16001\nserver::olcoach_port = 16002\n"
        "server::half_time = 5\nserver::nr_normal_halfs = 2\nserver::nr_extra_halfs = 0\n"
        "server::penalty_shoot_outs = false\nserver::game_over_wait = 1\nserver::connect_wait = 2\nserver::kick_off_wait = 5\n";
    configuration.close();
    ChildProcess server({std::filesystem::absolute(argv[1]).string(), "include=config/session.conf"}, session.runtime,
                         {{"RCSS_CONF_DIR", session.configuration.string()}});
    std::string output;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(55);
    while (!server.finished() && std::chrono::steady_clock::now() < deadline) {
        output += server.poll();
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    writeTextAtomically(session.runtime / "smoke-console.log", output);
    require(server.finished());
    require(server.exitCode() == 0);
    unsigned players = 0;
    for (std::size_t position = 0; (position = output.find(" player (Manager", position)) != std::string::npos; ++position) ++players;
    require(players == 22);
    require(findReplays(base).size() >= 1);
    require(output.find("Game Over") != std::string::npos);
    DataExport exporter("python3", std::filesystem::canonical(argv[0]).parent_path() / "extractor");
    require(exporter.enqueue(session.directory));
    const auto exportDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    while (exporter.busy() && std::chrono::steady_clock::now() < exportDeadline) {
        exporter.poll();
        std::this_thread::yield();
    }
    require(!exporter.busy());
    const auto data = findDataSessions(base);
    require(!data.empty() && !data[0].exportDirectory.empty());
    require(std::filesystem::is_regular_file(csvDirectory(data[0].exportDirectory) / "jogadores.csv"));
    std::cout << "CSV exportados para " << data[0].exportDirectory << '\n';
    std::cout << "Partida concluída com 22 jogadores; replay em " << session.logs << '\n';
}
