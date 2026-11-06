#include "DataExport.hpp"
#include "Session.hpp"
#include "TestSupport.hpp"
#include <chrono>
#include <algorithm>
#include <fstream>
#include <nlohmann/json.hpp>
#include <thread>

namespace {
nlohmann::json read(const std::filesystem::path& path) {
    std::ifstream input(path);
    return nlohmann::json::parse(input);
}
void finish(DataExport& exporter) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (exporter.busy() && std::chrono::steady_clock::now() < deadline) {
        exporter.poll();
        std::this_thread::yield();
    }
    require(!exporter.busy());
}
}
int main() {
    TemporaryDirectory temporary;
    const auto session = temporary.path / "runs/UnBall-Manager2D/sessão d'água";
    writeTextAtomically(session / "logs/game.rcg", "ULG6\n(team 0 Left Right 0 0)\n(playmode 0 play_on)\n(show 0 ((b) 0 0 0 0))\n");
    DataExport exporter(UNBALL_TEST_PYTHON, UNBALL_TEST_EXTRACTOR);
    require(exporter.enqueue(session));
    require(!exporter.enqueue(session));
    require(exporter.contains(session));
    finish(exporter);
    auto sessions = findDataSessions(temporary.path);
    require(sessions.size() == 1);
    require(sessions[0].status == "completed_with_warnings");
    require(sessions[0].coverage == "parcial");
    require(std::filesystem::is_regular_file(csvDirectory(sessions[0].exportDirectory) / "bola.csv"));
    const auto previous = read(session / "exports/latest.json");
    writeTextAtomically(session / "logs/game.rcg", "ULG99\n");
    require(exporter.enqueue(session));
    finish(exporter);
    require(read(session / "extraction.json")["status"] == "failed");
    require(read(session / "exports/latest.json") == previous);
    require(findDataSessions(temporary.path)[0].exportDirectory == sessions[0].exportDirectory);
    DataExport missing("/unball/missing/python", UNBALL_TEST_EXTRACTOR);
    require(missing.enqueue(session));
    finish(missing);
    require(read(session / "extraction.json")["status"] == "failed");
    const auto organized = prepareSession(temporary.path, false);
    writeTextAtomically(organized.logs / "game.rcg", "ULG6\n(team 0 Left Right 0 0)\n(show 0 ((b) 0 0 0 0))\n");
    require(exporter.enqueue(organized.directory));
    finish(exporter);
    require(std::filesystem::is_regular_file(organized.runtime / "extraction.json"));
    require(!std::filesystem::exists(organized.directory / "extraction.json"));
    const auto all = findDataSessions(temporary.path);
    require(all.size() == 2);
    require(std::any_of(all.begin(), all.end(), [&](const DataSession& data) { return data.directory == organized.directory && !data.replay.empty() && data.createdAt.size() >= 19; }));
    require(csvDirectory(session) == session); // Flat legacy bundles remain readable.
    const auto legacy = session / "exports/old-flat-bundle";
    writeTextAtomically(legacy / "manifest.json", "{}");
    writeTextAtomically(legacy / "bola.csv", "sessao_id,x\nlegacy,0\n");
    writeTextAtomically(session / "exports/latest.json", R"({"export_directory":"old-flat-bundle","status":"completed","coverage":"completa"})");
    const auto mixed = findDataSessions(temporary.path);
    const auto old = std::find_if(mixed.begin(), mixed.end(), [&](const DataSession& data) { return data.directory == session; });
    require(old != mixed.end() && old->exportDirectory == legacy);
    require(csvDirectory(old->exportDirectory) == legacy);
}
