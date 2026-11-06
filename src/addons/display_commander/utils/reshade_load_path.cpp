#include "reshade_load_path.hpp"
#include "../config/display_commander_config.hpp"
#include "general_utils.hpp"
#include "logging.hpp"
#include "string_utils.hpp"

#include <Windows.h>

#include <ShlObj.h>

#include <filesystem>
#include <string>

namespace display_commander::utils {

namespace {
constexpr const char* RESHADE_SECTION = "DisplayCommander.ReShade";
constexpr const char* KEY_LOAD_SOURCE = "ReshadeLoadSource";  // legacy, for migration only
constexpr const char* KEY_SELECTED_VERSION = "ReshadeSelectedVersion";

// True if dir contains the ReShade DLL for current process bitness (Reshade64.dll / Reshade32.dll).
static bool DirectoryHasReshadeDll(const std::filesystem::path& dir) {
    if (dir.empty()) {
        return false;
    }
    std::error_code ec;
#ifdef _WIN64
    return std::filesystem::exists(dir / L"Reshade64.dll", ec);
#else
    return std::filesystem::exists(dir / L"Reshade32.dll", ec);
#endif
}

}  // namespace

// Effective selected version: read KEY_SELECTED_VERSION; if empty, migrate from legacy KEY_LOAD_SOURCE.
// Returns "no" or a non-"no" value (legacy values are kept for UI consistency).
static std::string GetReshadeSelectedVersionEffective() {
    using namespace display_commander::config;
    auto& config = DisplayCommanderConfigManager::GetInstance();
    std::string selected;
    config.GetConfigValue(RESHADE_SECTION, KEY_SELECTED_VERSION, selected);
    if (!selected.empty()) {
        return selected;
    }
    int load_source = 0;
    config.GetConfigValue(RESHADE_SECTION, KEY_LOAD_SOURCE, load_source);
    if (load_source == 3) return "no";
    if (load_source == 0 || load_source == 1) return "global";  // was "" (base folder)
    if (load_source == 2) {
        config.GetConfigValue(RESHADE_SECTION, KEY_SELECTED_VERSION, selected);
        return selected.empty() ? "global" : selected;
    }
    return "global";
}

std::filesystem::path GetReshadeDirectoryForLoading(const std::filesystem::path& game_directory) {
    const std::string selected = GetReshadeSelectedVersionEffective();
    LogInfo("[reshade] selected = %s", selected.c_str());
    if (selected == "no") return std::filesystem::path();

    // New simplified chain:
    // 1) if Reshade64/32.dll exists next to the game exe (local), use it
    // 2) else try the global base folder: %LocalAppData%\\Programs\\Display_Commander\\Reshade\\Reshade64/32.dll
    if (!game_directory.empty() && DirectoryHasReshadeDll(game_directory)) {
        LogInfo("[reshade] loading from local dir: %s",
                FilesystemPathToUtf8(std::filesystem::absolute(game_directory)).c_str());
        return game_directory;
    }

    const std::filesystem::path base = GetGlobalReshadeDirectory();
    if (!base.empty() && DirectoryHasReshadeDll(base)) {
        LogInfo("[reshade] loading from global dir: %s", FilesystemPathToUtf8(std::filesystem::absolute(base)).c_str());
        return base;
    }

    LogInfo("[reshade] no local/global ReShade DLL found");
    return std::filesystem::path();
}

std::filesystem::path GetReshadeDirectoryForLoading() { return GetReshadeDirectoryForLoading(std::filesystem::path()); }

std::string GetReshadeSelectedVersionFromConfig() { return GetReshadeSelectedVersionEffective(); }

void SetReshadeSelectedVersionInConfig(const std::string& version) {
    using namespace display_commander::config;
    DisplayCommanderConfigManager::GetInstance().SetConfigValue(RESHADE_SECTION, KEY_SELECTED_VERSION, version);
}

std::filesystem::path GetGlobalReshadeDirectory() {
    wchar_t localappdata_path[MAX_PATH];
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, localappdata_path))) {
        return std::filesystem::path();
    }
    return std::filesystem::path(localappdata_path) / L"Programs" / L"Display_Commander" / L"Reshade";
}

std::string GetReshadeVersionInDirectory(const std::filesystem::path& dir) {
    if (dir.empty()) {
        return "";
    }
    std::filesystem::path dll_path = dir / L"Reshade64.dll";
    if (!std::filesystem::exists(dll_path)) {
        return "";
    }
    return ::GetDLLVersionString(dll_path.wstring());
}

std::string GetGlobalReshadeVersion() { return GetReshadeVersionInDirectory(GetGlobalReshadeDirectory()); }

bool DeleteLocalReshadeFromDirectory(const std::filesystem::path& dir, std::string* out_error) {
    if (dir.empty()) {
        if (out_error) *out_error = "Directory is empty";
        return false;
    }
    std::error_code ec;
    auto remove_if_exists = [&dir, &ec, out_error](const wchar_t* name) -> bool {
        std::filesystem::path p = dir / name;
        if (!std::filesystem::exists(p, ec)) return true;
        std::filesystem::remove(p, ec);
        if (ec && out_error) *out_error = "Failed to remove " + p.string() + ": " + ec.message();
        return !ec;
    };
    if (!remove_if_exists(L"Reshade64.dll")) return false;
    if (!remove_if_exists(L"Reshade32.dll")) return false;
    return true;
}

}  // namespace display_commander::utils
