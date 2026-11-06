#include "AppUpdate.hpp"
#include "TestSupport.hpp"
#include <chrono>
#include <fstream>
#include <thread>

void finish(AppUpdate& updater) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (updater.busy() && std::chrono::steady_clock::now() < deadline) { updater.poll(); std::this_thread::yield(); }
    require(!updater.busy());
}
int main() {
    TemporaryDirectory temporary;
    const auto script = temporary.path / "fake updater.py";
    writeTextAtomically(script,
        "print('UNBALL_UPDATE {\"state\":\"working\",\"progress\":50,\"message\":\"Building\"}', flush=True)\n"
        "print('UNBALL_UPDATE {\"state\":\"installed\",\"progress\":100,\"message\":\"Ready\"}', flush=True)\n");
    AppUpdate updater(temporary.path / "app", script, UNBALL_TEST_PYTHON);
    updater.start(); updater.start();
    finish(updater);
    require(updater.progress() == 100 && updater.installed() && updater.message() == "Ready");
    writeTextAtomically(script, "print('UNBALL_UPDATE invalid')\nraise SystemExit(1)\n");
    updater.start(); finish(updater);
    require(!updater.installed());
    writeTextAtomically(script, "import signal\nsignal.pause()\n");
    updater.start(); updater.cancel(); finish(updater);
    require(!updater.installed());

    const auto data = temporary.path / "resolver";
    const auto revision = std::string(40, 'a'), base = std::string(40, 'b');
    const auto release = data / "updates/versions" / revision;
    executable(release / "UnBall-Manager2D", "#!/bin/sh\nexit 0\n");
    writeTextAtomically(release / "installed.json", "{\"validated\":true,\"revision\":\"" + revision + "\"}");
    writeTextAtomically(release / "extractor/unball_extract/__main__.py", "");
    writeTextAtomically(release / "updater/unball_update.py", "");
    writeTextAtomically(data / "updates/current.json", "{\"revision\":\"" + revision + "\",\"base_revision\":\"" + base + "\"}");
    require(installedUpdate(data) == release / "UnBall-Manager2D");
    const auto original = temporary.path / "original";
    executable(original, "#!/bin/sh\nexit 0\n");
    require(startupUpdate(data, original, base) == release / "UnBall-Manager2D");
    require(startupUpdate(data, original, std::string(40, 'c')).empty());
    require(startupUpdate(data, release / "UnBall-Manager2D", revision).empty());
    const auto previous = data / "updates/versions" / base / "UnBall-Manager2D";
    executable(previous, "#!/bin/sh\nexit 0\n");
    require(startupUpdate(data, previous, base) == release / "UnBall-Manager2D");
    writeTextAtomically(data / "updates/current.json", "{\"revision\":\"../outside\"}");
    require(installedUpdate(data).empty());
}
