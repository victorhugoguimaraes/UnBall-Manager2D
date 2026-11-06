#pragma once

#include <filesystem>
#include <string_view>

// Replaces the destination only after the entire temporary file was written.
void writeTextAtomically(const std::filesystem::path& destination, std::string_view text);
