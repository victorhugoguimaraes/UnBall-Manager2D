#include "ChildProcess.hpp"
#include "TestSupport.hpp"
#include <chrono>
#include <csignal>
#include <thread>
#include <unistd.h>

namespace {
std::string finish(ChildProcess& process, int seconds = 8) {
    std::string output;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
    while (!process.finished() && std::chrono::steady_clock::now() < deadline) {
        output += process.poll();
        std::this_thread::yield();
    }
    require(process.finished());
    return output;
}
}

int main(int argc, char** argv) {
    if (argc > 1 && std::string(argv[1]) == "--fixture") {
        ::signal(SIGINT, SIG_IGN);
        ::signal(SIGTERM, SIG_IGN);
        const pid_t child = ::fork();
        if (child == -1) return 2;
        if (child == 0) { for (;;) ::pause(); }
        const auto message = std::to_string(child) + "\n";
        ::write(STDOUT_FILENO, message.data(), message.size());
        for (;;) ::pause();
    }
    TemporaryDirectory temporary;
    const std::string literal = "caminho com 'aspas' $(touch SHOULD_NOT_EXIST)";
    ChildProcess echo({"/usr/bin/printf", "%s", literal}, temporary.path);
    require(finish(echo) == literal);
    require(echo.exitCode() == 0);
    require(!std::filesystem::exists(temporary.path / "SHOULD_NOT_EXIST"));
    ChildProcess environment({"/bin/sh", "-c", "printf '%s' \"$UNBALL_PROCESS_TEST\""}, temporary.path,
                             {{"UNBALL_PROCESS_TEST", "UTF-8: ação"}});
    require(finish(environment) == "UTF-8: ação");
    bool missingRejected = false;
    try { ChildProcess missing({"/unball/missing/executable"}); }
    catch (const std::system_error&) { missingRejected = true; }
    require(missingRejected);

    ChildProcess unrelated({"/bin/sleep", "20"});
    ChildProcess stubborn({std::filesystem::absolute(argv[0]).string(), "--fixture"});
    std::string ready;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (ready.find('\n') == std::string::npos && std::chrono::steady_clock::now() < deadline) {
        ready += stubborn.poll();
        std::this_thread::yield();
    }
    require(ready.find('\n') != std::string::npos);
    stubborn.stop();
    finish(stubborn);
    require(stubborn.exitCode() == 128 + SIGKILL);
    require(::kill(unrelated.pid(), 0) == 0); // Never signal processes outside the owned group.
    unrelated.stop();
    finish(unrelated);
}
