#pragma once

#include <filesystem>
#include <string>

namespace display_commander::utils {

// Compute SHA256 hash of a file. Returns 64-character lowercase hex string, or empty on failure.
// Uses Windows BCrypt (no std::mutex).
std::string ComputeFileSha256(const std::filesystem::path& file_path);

}  // namespace display_commander::utils
