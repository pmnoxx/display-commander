// Source Code <Display Commander>
#include "safe_remove.hpp"
#include "logging.hpp"
#include "version_check.hpp"

// Libraries <standard C++>
#include <filesystem>
#include <string>

// Libraries <Windows.h>
#include <Windows.h>

namespace display_commander::utils {

bool IsSafeTempSubdirPath(const std::filesystem::path& dir) {
    if (dir.empty()) return false;
    std::filesystem::path parent = dir.parent_path();
    std::wstring pid_part = dir.filename().wstring();
    if (pid_part.empty()) return false;
    for (wchar_t c : pid_part) {
        if (c < L'0' || c > L'9') return false;
    }
    return parent.filename() == L"tmp";
}

namespace {

bool IsAllowedSystemTempSubdir(const std::filesystem::path& path, const wchar_t* subdir_name) {
    wchar_t temp_path_buf[MAX_PATH];
    if (GetTempPathW(static_cast<DWORD>(std::size(temp_path_buf)), temp_path_buf) == 0) {
        return false;
    }
    std::filesystem::path allowed = std::filesystem::path(temp_path_buf).lexically_normal() / subdir_name;
    std::filesystem::path normalized = path.lexically_normal();
    return normalized == allowed;
}

bool IsAllowedStagingPath(const std::filesystem::path& path) {
    std::filesystem::path base = version_check::GetDownloadDirectory();
    if (base.empty()) return false;
    std::filesystem::path allowed = (base / L"Debug" / L"_staging_latest_debug").lexically_normal();
    return path.lexically_normal() == allowed;
}

}  // namespace

bool IsAllowedForRemoveAll(const std::filesystem::path& path) {
    if (path.empty()) return false;
    if (IsSafeTempSubdirPath(path)) return true;
    if (IsAllowedSystemTempSubdir(path, L"dc_reshade_update")) return true;
    if (IsAllowedSystemTempSubdir(path, L"dc_reshade_download")) return true;
    if (IsAllowedStagingPath(path)) return true;
    return false;
}

bool SafeRemoveAll(const std::filesystem::path& path, std::error_code& ec) {
    ec.clear();
    if (!IsAllowedForRemoveAll(path)) {
        LogError("SafeRemoveAll: path not on whitelist, refused: %s", path.string().c_str());
        return false;
    }
    if (!std::filesystem::exists(path, ec)) {
        return true;
    }
    std::filesystem::remove_all(path, ec);
    return !ec;
}

}  // namespace display_commander::utils
