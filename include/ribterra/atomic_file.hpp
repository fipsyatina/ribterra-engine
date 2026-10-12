#pragma once

#include <filesystem>
#include <string>

namespace ribterra {

// Atomically replaces destination with source when the platform supports it.
// Source and destination must be on the same filesystem.
bool atomic_replace_file(const std::filesystem::path& source, const std::filesystem::path& destination, std::string& error);

// Publishes a complete new file, failing if the destination already exists.
bool atomic_create_file(const std::filesystem::path&, const std::filesystem::path&, std::string& error);

} // namespace ribterra
