#pragma once

#include "FileIO.hpp"
#include <cstdlib>
#include <filesystem>
#include <source_location>
#include <stdexcept>
#include <string>

inline void require(bool condition, const std::source_location location = std::source_location::current()) {
    if (!condition) throw std::runtime_error(std::string(location.file_name()) + ':' +
        std::to_string(location.line()) + ": condição não atendida");
}

class TemporaryDirectory final {
public:
    TemporaryDirectory() {
        auto pattern = (std::filesystem::temp_directory_path() / "unball-manager-test-XXXXXX").string();
        if (!::mkdtemp(pattern.data())) throw std::runtime_error("Criar pasta de teste");
        path = pattern;
    }
    ~TemporaryDirectory() { std::error_code error; std::filesystem::remove_all(path, error); }
    TemporaryDirectory(const TemporaryDirectory&) = delete;
    TemporaryDirectory& operator=(const TemporaryDirectory&) = delete;
    std::filesystem::path path;
};

inline void executable(const std::filesystem::path& path, const std::string& contents) {
    writeTextAtomically(path, contents);
    std::filesystem::permissions(path, std::filesystem::perms::owner_exec, std::filesystem::perm_options::add);
}
