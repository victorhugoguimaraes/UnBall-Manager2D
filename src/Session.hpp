#pragma once

#include "TeamStore.hpp"

#include <filesystem>
#include <vector>

struct SessionFiles {
    std::filesystem::path directory;
    std::filesystem::path configuration;
    std::filesystem::path logs;
    std::filesystem::path runtime;
};

SessionFiles prepareSession(const std::filesystem::path& base, bool coach,
                            const Team* left = nullptr, const Team* right = nullptr);
std::vector<std::filesystem::path> findReplays(const std::filesystem::path& base);

// Legacy sessions keep operational files at the root. New sessions use runtime/.
std::filesystem::path sessionRuntimeDirectory(const std::filesystem::path& session);
