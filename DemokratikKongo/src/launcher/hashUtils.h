#pragma once

#include <cstddef>
#include <filesystem>
#include <string>

namespace dk {

// SHA-256 helpers shared by the licensing client and the core updater.
// Lowercase hex out; an empty string means the operation failed.
std::string sha256HexOfBytes(const void* data, size_t size);
std::string sha256HexOfFile(const std::filesystem::path& path);

} // namespace dk
