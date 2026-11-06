#include "Session.hpp"
#include "FileIO.hpp"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <cctype>
#include <iomanip>
#include <nlohmann/json.hpp>
#include <set>
#include <sstream>
#include <stdexcept>

namespace {
std::string shellQuote(const std::string& value) {
    std::string quoted = "'";
    for (const char character : value) quoted += character == '\'' ? "'\"'\"'" : std::string(1, character);
    return quoted + "'";
}

// RCSS parses strings itself, independently of the shell.
std::string configQuote(const std::string& value) {
    if (value.find_first_of("\r\n\0", 0, 3) != std::string::npos)
        throw std::invalid_argument("O caminho contém um caractere de controle.");
    std::string result = "\"";
    for (const char character : value) {
        if (character == '\\' || character == '"') result += '\\';
        result += character;
    }
    return result + '"';
}

std::string directoryLabel(const std::string& name) {
    std::string result;
    for (const unsigned char character : name) {
        if (result.size() >= 32) break;
        const char next = character < 128 && (std::isalnum(character) || character == '-') ? static_cast<char>(character) : '_';
        if (next != '_' || result.empty() || result.back() != '_') result += next;
    }
    return result.empty() ? "time" : result;
}

std::string launcherScript(const Team& team) {
    const auto launcher = findTeamLauncher(team.root);
    if (launcher.empty()) throw std::invalid_argument("Time sem inicializador executável: " + team.displayName);
    return "#!/bin/sh\nset -eu\ncd " + shellQuote(launcher.parent_path().string()) +
           "\nexec " + shellQuote(launcher.string()) + "\n";
}
}

SessionFiles prepareSession(const std::filesystem::path& base, bool coach, const Team* left, const Team* right) {
    if ((left == nullptr) != (right == nullptr)) throw std::invalid_argument("Selecione os dois times.");
    std::string leftScript, rightScript;
    if (left) {
        if (left->teamName == right->teamName) throw std::invalid_argument("Os times precisam de nomes distintos no servidor.");
        leftScript = launcherScript(*left);
        rightScript = launcherScript(*right);
    }
    const auto now = std::chrono::system_clock::now();
    const auto clock = std::chrono::system_clock::to_time_t(now);
    std::tm local{};
    localtime_r(&clock, &local);
    std::ostringstream day, label, created;
    day << std::put_time(&local, "%Y-%m-%d");
    label << std::put_time(&local, "%H-%M-%S") << '_'
          << (left ? directoryLabel(left->teamName) + "-vs-" + directoryLabel(right->teamName) : "servidor")
          << '_' << std::hex << now.time_since_epoch().count();
    created << std::put_time(&local, "%Y-%m-%dT%H:%M:%S%z");
    const auto parent = std::filesystem::absolute(base) / "runs" / "UnBall-Manager2D" / day.str();
    std::filesystem::create_directories(parent);
    auto directory = parent / label.str();
    for (unsigned suffix = 1; !std::filesystem::create_directory(directory); ++suffix)
        directory = parent / (label.str() + "-" + std::to_string(suffix));
    directory = std::filesystem::canonical(directory);
    SessionFiles session{directory, directory / "runtime/config", directory / "logs", directory / "runtime"};
    std::filesystem::create_directories(session.configuration);
    std::filesystem::create_directory(session.logs);
    std::filesystem::create_directory(session.runtime / "launchers");
    std::string config = "server::coach = " + std::string(coach ? "true" : "false") +
        "\nserver::coach_w_referee = " + (coach ? "true" : "false") +
        "\nserver::game_logging = true\nserver::text_logging = true\nserver::game_log_dir = " +
        configQuote(session.logs.string()) + "\nserver::text_log_dir = " + configQuote(session.logs.string()) + "\n";
    if (left) {
        for (const auto& [name, script] : {std::pair{"left.sh", leftScript}, std::pair{"right.sh", rightScript}}) {
            const auto path = session.runtime / "launchers" / name;
            writeTextAtomically(path, script);
            std::filesystem::permissions(path, std::filesystem::perms::owner_exec, std::filesystem::perm_options::add);
        }
        config += "server::auto_mode = true\nserver::kick_off_wait = 100\nserver::connect_wait = 10\n";
        config += "server::team_l_start = " + configQuote(shellQuote((session.runtime / "launchers/left.sh").string())) + '\n';
        config += "server::team_r_start = " + configQuote(shellQuote((session.runtime / "launchers/right.sh").string())) + '\n';
    }
    writeTextAtomically(session.configuration / "session.conf", config);
    nlohmann::json summary{{"layout_version", 2}, {"created_at", created.str()}, {"coach", coach}, {"logs", session.logs.string()}};
    if (left) {
        summary["leftTeam"] = left->teamName;
        summary["rightTeam"] = right->teamName;
        summary["leftDirectory"] = left->root.string();
        summary["rightDirectory"] = right->root.string();
    }
    writeTextAtomically(directory / "match.json", summary.dump(2) + '\n');
    return session;
}

std::vector<std::filesystem::path> findReplays(const std::filesystem::path& base) {
    struct Replay { std::filesystem::path path; std::filesystem::file_time_type time; };
    std::vector<Replay> found;
    std::set<std::filesystem::path> seen;
    for (const auto& directory : {base / "log_analysis" / "log", base / "log", base / "runs" / "UnBall-Manager2D"}) {
        std::error_code error;
        if (!std::filesystem::is_directory(directory, error)) continue;
        std::filesystem::recursive_directory_iterator iterator(directory,
            std::filesystem::directory_options::skip_permission_denied, error), end;
        while (iterator != end) {
            const auto entry = *iterator;
            if (iterator.depth() >= 12) iterator.disable_recursion_pending();
            const auto name = entry.path().filename().string();
            if (entry.is_regular_file(error) && (name.ends_with(".rcg") || name.ends_with(".rcg.gz"))) {
                const auto canonical = std::filesystem::weakly_canonical(entry.path(), error);
                if (!error && seen.insert(canonical).second) {
                    const auto time = entry.last_write_time(error);
                    if (!error) found.push_back({canonical, time});
                }
            }
            iterator.increment(error);
            if (error) error.clear();
        }
    }
    std::sort(found.begin(), found.end(), [](const Replay& left, const Replay& right) {
        return left.time != right.time ? left.time > right.time : left.path < right.path;
    });
    std::vector<std::filesystem::path> result;
    for (auto& replay : found) result.push_back(std::move(replay.path));
    return result;
}

std::filesystem::path sessionRuntimeDirectory(const std::filesystem::path& session) {
    return std::filesystem::is_directory(session / "runtime") ? session / "runtime" : session;
}
