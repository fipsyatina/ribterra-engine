#include "ribterra/atomic_file.hpp"

#include <cstdio>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace ribterra {

bool atomic_replace_file(const std::filesystem::path& source, const std::filesystem::path& destination, std::string& error) {
#ifdef _WIN32
    if (MoveFileExW(source.c_str(), destination.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) == 0) {
        error = "Atomic file replacement failed (Windows error " + std::to_string(GetLastError()) + ").";
        return false;
    }
#else
    if (std::rename(source.c_str(), destination.c_str()) != 0) {
        error = "Atomic file replacement failed.";
        return false;
    }
#endif
    return true;
}

bool atomic_create_file(const std::filesystem::path& source, const std::filesystem::path& destination, std::string& error) {
#ifdef _WIN32
    if (!MoveFileExW(source.c_str(), destination.c_str(), MOVEFILE_WRITE_THROUGH)) {
#else
    if (::link(source.c_str(), destination.c_str()) != 0) {
#endif
        error = "save.copy_failed";
        return false;
    }
#ifndef _WIN32
    ::unlink(source.c_str());
#endif
    return true;
}
} // namespace ribterra
