#include "ChildProcess.hpp"

#include <array>
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <spawn.h>
#include <stdexcept>
#include <system_error>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;

namespace {
void check(int code) {
    if (code != 0) throw std::system_error(code, std::generic_category(), "Iniciar processo");
}
}

ChildProcess::ChildProcess(const std::vector<std::string>& arguments,
                           const std::filesystem::path& directory,
                           const std::map<std::string, std::string>& environment) {
    if (arguments.empty() || arguments.front().empty()) throw std::invalid_argument("Executável não informado.");
    std::array<int, 2> pipes{};
    if (::pipe2(pipes.data(), O_CLOEXEC) == -1) throw std::system_error(errno, std::generic_category(), "Criar saída do processo");
    posix_spawn_file_actions_t actions;
    posix_spawnattr_t attributes;
    const int actionStatus = ::posix_spawn_file_actions_init(&actions);
    const int attributeStatus = ::posix_spawnattr_init(&attributes);
    try {
        check(actionStatus);
        check(attributeStatus);
        check(::posix_spawn_file_actions_adddup2(&actions, pipes[1], STDOUT_FILENO));
        check(::posix_spawn_file_actions_adddup2(&actions, pipes[1], STDERR_FILENO));
        check(::posix_spawn_file_actions_addclose(&actions, pipes[0]));
        check(::posix_spawn_file_actions_addclose(&actions, pipes[1]));
        if (!directory.empty()) check(::posix_spawn_file_actions_addchdir_np(&actions, directory.c_str()));
        check(::posix_spawnattr_setflags(&attributes, POSIX_SPAWN_SETPGROUP));
        check(::posix_spawnattr_setpgroup(&attributes, 0));

        std::vector<char*> argv;
        for (const auto& argument : arguments) argv.push_back(const_cast<char*>(argument.c_str()));
        argv.push_back(nullptr);
        std::map<std::string, std::string> merged;
        for (auto entry = environ; *entry; ++entry) {
            const std::string value(*entry);
            const auto equal = value.find('=');
            if (equal != std::string::npos) merged[value.substr(0, equal)] = value.substr(equal + 1);
        }
        for (const auto& [key, value] : environment) merged[key] = value;
        std::vector<std::string> values;
        for (const auto& [key, value] : merged) values.push_back(key + "=" + value);
        std::vector<char*> env;
        for (auto& value : values) env.push_back(value.data());
        env.push_back(nullptr);
        pid_t child{};
        check(::posix_spawnp(&child, argv.front(), &actions, &attributes, argv.data(), env.data()));
        pid_ = child;
        output_ = pipes[0];
        const int flags = ::fcntl(output_, F_GETFL);
        if (flags == -1 || ::fcntl(output_, F_SETFL, flags | O_NONBLOCK) == -1) {
            const int error = errno;
            signalGroup(SIGKILL);
            while (::waitpid(pid_, nullptr, 0) == -1 && errno == EINTR) {}
            throw std::system_error(error, std::generic_category(), "Configurar saída do processo");
        }
    } catch (...) {
        if (actionStatus == 0) ::posix_spawn_file_actions_destroy(&actions);
        if (attributeStatus == 0) ::posix_spawnattr_destroy(&attributes);
        ::close(pipes[0]);
        ::close(pipes[1]);
        throw;
    }
    ::posix_spawn_file_actions_destroy(&actions);
    ::posix_spawnattr_destroy(&attributes);
    ::close(pipes[1]);
}

ChildProcess::~ChildProcess() {
    // The normal UI close path drains and stops asynchronously before destruction.
    // This fallback only handles forced shutdown or an exception during setup.
    if (!finished_) signalGroup(SIGKILL);
    if (!exitCode_ && pid_ > 0) {
        while (::waitpid(pid_, nullptr, 0) == -1 && errno == EINTR) {}
    }
    if (output_ != -1) ::close(output_);
}

void ChildProcess::signalGroup(int signal) const noexcept {
    if (pid_ > 0) ::kill(-pid_, signal);
}

void ChildProcess::stop() {
    if (finished_ || stoppingSince_) return;
    stoppingSince_ = std::chrono::steady_clock::now();
    signalGroup(SIGINT);
}

std::string ChildProcess::poll() {
    std::string output;
    if (finished_) return output;
    std::array<char, 4096> buffer{};
    constexpr std::size_t maxBytesPerPoll = 64 * 1024;
    while (output_ != -1 && output.size() < maxBytesPerPoll) {
        const auto count = ::read(output_, buffer.data(), buffer.size());
        if (count > 0) output.append(buffer.data(), static_cast<std::size_t>(count));
        else if (count < 0 && errno == EINTR) continue;
        else {
            if (count == 0 || (errno != EAGAIN && errno != EWOULDBLOCK)) {
                ::close(output_);
                output_ = -1;
            }
            break;
        }
    }
    if (!exitCode_) {
        int status{};
        const auto result = ::waitpid(pid_, &status, WNOHANG);
        if (result == pid_) {
            exitCode_ = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
            // A launcher can finish before its descendants. Keep their group owned.
            if (!stoppingSince_) stop();
        }
    }
    if (stoppingSince_) {
        const auto elapsed = std::chrono::steady_clock::now() - *stoppingSince_;
        if (elapsed >= std::chrono::seconds(3) && stopStage_ == 0) {
            signalGroup(SIGTERM);
            stopStage_ = 1;
        }
        if (elapsed >= std::chrono::seconds(4) && stopStage_ == 1) {
            signalGroup(SIGKILL);
            stopStage_ = 2;
        }
        const bool groupGone = ::kill(-pid_, 0) == -1 && errno == ESRCH;
        if (exitCode_ && output_ == -1 && (groupGone || elapsed >= std::chrono::seconds(5))) finished_ = true;
    }
    return output;
}
