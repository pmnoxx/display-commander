#include "reshade_load_path.hpp"
#include "../config/display_commander_config.hpp"
#include "general_utils.hpp"
#include "version_check.hpp"
#include <ShlObj.h>
#include <Windows.h>
#include <algorithm>
#include <atomic>
#include <filesystem>
#include <string>
#include <vector>

namespace display_commander::utils {

namespace {
constexpr const char* RESHADE_SECTION = "DisplayCommander.ReShade";
constexpr const char* KEY_LOAD_SOURCE = "ReshadeLoadSource";
constexpr const char* KEY_SHARED_PATH = "ReshadeSharedPath";
constexpr const char* KEY_SELECTED_VERSION = "ReshadeSelectedVersion";

constexpr int DEFAULT_LOAD_SOURCE = 0;  // Local
constexpr const char* DEFAULT_VERSION = "6.7.3";

// Fallback when GitHub fetch fails or has not run yet.
static const char* const RESHADE_VERSIONS_FALLBACK[] = {"6.6.2", "6.7.3"};
static const size_t RESHADE_VERSIONS_FALLBACK_COUNT =
    sizeof(RESHADE_VERSIONS_FALLBACK) / sizeof(RESHADE_VERSIONS_FALLBACK[0]);

// When SpecificVersion is selected but that version is not installed, we load the highest available
// version instead. These are set so the UI can show a warning (loaded != selected).
static std::string s_fallback_selected_version;
static std::string s_fallback_loaded_version;

// Combined list: hardcoded "6.6.2", "6.7.3" plus GitHub tags (>= 6.6.2), sorted descending. Built once per app start.
static std::vector<std::string> s_reshade_versions_combined;
static std::vector<const char*> s_reshade_version_ptrs;
static std::atomic<bool> s_version_list_built{false};

static bool DirectoryHasReshadeDlls(const std::filesystem::path& dir) {
    if (dir.empty()) {
        return false;
    }
    std::error_code ec;
    return std::filesystem::exists(dir / L"Reshade64.dll", ec) && std::filesystem::exists(dir / L"Reshade32.dll", ec);
}

// Returns the version string (subdir name) of the highest available ReShade under base_dll (e.g. base/Dll),
// or empty if none found. "Available" = subdir has both Reshade64.dll and Reshade32.dll.
static std::string GetHighestAvailableVersionInDllDir(const std::filesystem::path& base_dll) {
    std::vector<std::string> available;
    std::error_code ec;
    if (!std::filesystem::exists(base_dll, ec) || !std::filesystem::is_directory(base_dll, ec)) {
        return "";
    }
    for (const auto& entry : std::filesystem::directory_iterator(base_dll, ec)) {
        if (ec) {
            continue;
        }
        if (!entry.is_directory(ec)) {
            continue;
        }
        std::string name = entry.path().filename().string();
        if (name.empty() || name == "." || name == "..") {
            continue;
        }
        if (DirectoryHasReshadeDlls(entry.path())) {
            available.push_back(name);
        }
    }
    if (available.empty()) {
        return "";
    }
    namespace vc = display_commander::utils::version_check;
    std::sort(available.begin(), available.end(),
              [](const std::string& a, const std::string& b) { return vc::CompareVersions(a, b) > 0; });
    return available.front();
}

static void EnsureReShadeVersionListFetched() {
    if (s_version_list_built.exchange(true)) {
        return;
    }
    namespace vc = display_commander::utils::version_check;
    // Start with hardcoded versions
    s_reshade_versions_combined.assign(RESHADE_VERSIONS_FALLBACK,
                                       RESHADE_VERSIONS_FALLBACK + RESHADE_VERSIONS_FALLBACK_COUNT);
    std::vector<std::string> from_github;
    std::string error;
    if (vc::FetchReShadeVersionsFromGitHub(from_github, &error)) {
        for (const std::string& v : from_github) {
            if (std::find(s_reshade_versions_combined.begin(), s_reshade_versions_combined.end(), v)
                == s_reshade_versions_combined.end()) {
                s_reshade_versions_combined.push_back(v);
            }
        }
    }
    std::sort(s_reshade_versions_combined.begin(), s_reshade_versions_combined.end(),
              [](const std::string& a, const std::string& b) { return vc::CompareVersions(a, b) > 0; });
    s_reshade_version_ptrs.resize(s_reshade_versions_combined.size());
    for (size_t i = 0; i < s_reshade_versions_combined.size(); ++i) {
        s_reshade_version_ptrs[i] = s_reshade_versions_combined[i].c_str();
    }
}
}  // namespace

std::filesystem::path GetReshadeDirectoryForLoading() {
    using namespace display_commander::config;
    auto& config = DisplayCommanderConfigManager::GetInstance();

    int load_source = DEFAULT_LOAD_SOURCE;
    config.GetConfigValue(RESHADE_SECTION, KEY_LOAD_SOURCE, load_source);

    std::string shared_path;
    config.GetConfigValue(RESHADE_SECTION, KEY_SHARED_PATH, shared_path);

    std::string selected_version;
    config.GetConfigValue(RESHADE_SECTION, KEY_SELECTED_VERSION, selected_version);
    if (selected_version.empty()) {
        selected_version = DEFAULT_VERSION;
    }

    wchar_t localappdata_path[MAX_PATH];
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, localappdata_path))) {
        return std::filesystem::path();
    }
    std::filesystem::path base = std::filesystem::path(localappdata_path) / L"Programs" / L"Display_Commander"
                                / L"Reshade";

    switch (static_cast<ReshadeLoadSource>(load_source)) {
        case ReshadeLoadSource::Local:
            return base;
        case ReshadeLoadSource::SharedPath: {
            if (shared_path.empty()) {
                return base;
            }
            std::filesystem::path p(shared_path);
            std::error_code ec;
            std::filesystem::path abs = std::filesystem::absolute(p, ec);
            if (ec) {
                return base;
            }
            return abs;
        }
        case ReshadeLoadSource::SpecificVersion: {
            std::filesystem::path dll_base = base / L"Dll";
            std::filesystem::path selected_dir = dll_base / std::filesystem::path(selected_version);
            s_fallback_selected_version.clear();
            s_fallback_loaded_version.clear();
            if (DirectoryHasReshadeDlls(selected_dir)) {
                return selected_dir;
            }
            std::string fallback_version = GetHighestAvailableVersionInDllDir(dll_base);
            if (fallback_version.empty()) {
                return selected_dir;
            }
            s_fallback_selected_version = selected_version;
            s_fallback_loaded_version = fallback_version;
            return dll_base / std::filesystem::path(fallback_version);
        }
        default:
            return base;
    }
}

ReshadeLoadSource GetReshadeLoadSourceFromConfig() {
    using namespace display_commander::config;
    int value = DEFAULT_LOAD_SOURCE;
    DisplayCommanderConfigManager::GetInstance().GetConfigValue(RESHADE_SECTION, KEY_LOAD_SOURCE, value);
    if (value < 0 || value > 2) {
        value = DEFAULT_LOAD_SOURCE;
    }
    return static_cast<ReshadeLoadSource>(value);
}

void SetReshadeLoadSourceInConfig(ReshadeLoadSource value) {
    using namespace display_commander::config;
    DisplayCommanderConfigManager::GetInstance().SetConfigValue(RESHADE_SECTION, KEY_LOAD_SOURCE,
                                                                static_cast<int>(value));
}

std::string GetReshadeSharedPathFromConfig() {
    using namespace display_commander::config;
    std::string value;
    DisplayCommanderConfigManager::GetInstance().GetConfigValue(RESHADE_SECTION, KEY_SHARED_PATH, value);
    return value;
}

void SetReshadeSharedPathInConfig(const std::string& path) {
    using namespace display_commander::config;
    DisplayCommanderConfigManager::GetInstance().SetConfigValue(RESHADE_SECTION, KEY_SHARED_PATH, path);
}

std::string GetReshadeSelectedVersionFromConfig() {
    using namespace display_commander::config;
    std::string value;
    DisplayCommanderConfigManager::GetInstance().GetConfigValue(RESHADE_SECTION, KEY_SELECTED_VERSION, value);
    if (value.empty()) {
        value = DEFAULT_VERSION;
    }
    return value;
}

void SetReshadeSelectedVersionInConfig(const std::string& version) {
    using namespace display_commander::config;
    DisplayCommanderConfigManager::GetInstance().SetConfigValue(RESHADE_SECTION, KEY_SELECTED_VERSION, version);
}

const char* const* GetReshadeVersionList(size_t* out_count) {
    EnsureReShadeVersionListFetched();
    *out_count = s_reshade_version_ptrs.size();
    return s_reshade_version_ptrs.data();
}

bool GetReshadeLoadFallbackVersionInfo(std::string* out_selected_version, std::string* out_loaded_version) {
    if (s_fallback_selected_version.empty()) {
        return false;
    }
    if (out_selected_version != nullptr) {
        *out_selected_version = s_fallback_selected_version;
    }
    if (out_loaded_version != nullptr) {
        *out_loaded_version = s_fallback_loaded_version;
    }
    return true;
}

std::filesystem::path GetLocalReshadeDirectory() {
    wchar_t localappdata_path[MAX_PATH];
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, localappdata_path))) {
        return std::filesystem::path();
    }
    return std::filesystem::path(localappdata_path) / L"Programs" / L"Display_Commander" / L"Reshade";
}

static std::string GetReshadeVersionInDirectory(const std::filesystem::path& dir) {
    if (dir.empty()) {
        return "";
    }
    std::filesystem::path dll_path = dir / L"Reshade64.dll";
    if (!std::filesystem::exists(dll_path)) {
        return "";
    }
    return ::GetDLLVersionString(dll_path.wstring());
}

std::string GetLocalReshadeVersion() { return GetReshadeVersionInDirectory(GetLocalReshadeDirectory()); }

std::string GetSharedReshadeVersion() {
    std::string shared_path = GetReshadeSharedPathFromConfig();
    if (shared_path.empty()) {
        return "";
    }
    std::filesystem::path p(shared_path);
    std::error_code ec;
    std::filesystem::path abs = std::filesystem::absolute(p, ec);
    if (ec) {
        return "";
    }
    return GetReshadeVersionInDirectory(abs);
}

}  // namespace display_commander::utils
