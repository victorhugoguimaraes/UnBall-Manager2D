#include "BatchRun.hpp"
#include "Session.hpp"
#include "TestSupport.hpp"
#include <fstream>
#include <nlohmann/json.hpp>

nlohmann::json read(const std::filesystem::path& path) {
    std::ifstream input(path); return nlohmann::json::parse(input);
}
int main() {
    TemporaryDirectory temp;
    executable(temp.path / "left/start.sh", "#!/bin/sh\nexit 0\n");
    executable(temp.path / "right/start.sh", "#!/bin/sh\nexit 0\n");
    const Team left{"Left", "Left", temp.path / "left", true}, right{"Right", "Right", temp.path / "right", true};
    BatchRun batch(temp.path, left, right, 3, 60, false, false);
    require(batch.ready() && !batch.finished() && batch.settled() == 0);
    const auto first = prepareSession(temp.path, false, &left, &right);
    batch.startGame(first.directory);
    require(!batch.ready());
    require(!batch.timedOut(59.9) && batch.timedOut(60.0));
    require(read(first.directory / "match.json")["batch_index"] == 1);
    bool rejected = false;
    try { batch.startGame(first.directory); } catch (const std::logic_error&) { rejected = true; }
    require(rejected);
    batch.endGame("completed", 0, 10);
    require(batch.waitingForExport() && batch.settled() == 0 && !batch.ready());
    batch.endExport("completed_with_warnings");
    require(batch.settled() == 1 && batch.ready());
    const auto second = prepareSession(temp.path, false, &left, &right);
    batch.startGame(second.directory);
    batch.endGame("timeout", 130, 60);
    batch.endExport("failed");
    require(batch.ready() && batch.failures() == 1 && batch.exportFailures() == 1);
    const auto third = prepareSession(temp.path, false, &left, &right);
    batch.startGame(third.directory);
    batch.endGame("completed", 0, 11);
    batch.endExport("completed");
    require(batch.finished() && batch.settled() == 3 && batch.status() == "completed_with_errors");
    require(read(batch.directory() / "lote.json")["games"].size() == 3);
    require(std::filesystem::is_regular_file(batch.directory() / "resumo.csv"));

    BatchRun cancelled(temp.path, left, right, 5, 60, false, false);
    cancelled.startGame(first.directory);
    cancelled.cancel();
    require(!cancelled.finished() && !cancelled.ready());
    cancelled.endGame("cancelled", 130, 2);
    cancelled.endExport("completed_with_warnings");
    require(cancelled.finished() && cancelled.status() == "cancelled" && cancelled.started() == 1);
    BatchRun waiting(temp.path, left, right, 5, 60, false, false);
    waiting.startGame(first.directory);
    waiting.endGame("completed", 0, 1);
    waiting.cancel();
    require(!waiting.finished());
    waiting.endExport("completed");
    require(waiting.finished() && waiting.settled() == 1);
    BatchRun aborted(temp.path, left, right, 2, 60, false, false);
    aborted.abort("missing server");
    require(aborted.finished() && aborted.status() == "failed" && !aborted.ready());
    BatchRun invalidEnd(temp.path, left, right, 2, 60, false, false);
    invalidEnd.startGame(first.directory);
    invalidEnd.endGame("completed", 0, 1);
    invalidEnd.endExport("completed_with_warnings", false);
    require(invalidEnd.failures() == 1 && invalidEnd.ready());
}
