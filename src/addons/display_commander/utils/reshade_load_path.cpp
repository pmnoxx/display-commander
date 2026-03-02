#include "reshade_load_path.hpp"
#include "../config/display_commander_config.hpp"
#include "general_utils.hpp"
#include "logging.hpp"
#include "version_check.hpp"

#include <Windows.h>

#include <ShlObj.h>

#include <algorithm>
#include <atomic>
#include <filesystem>
#include <string>
#include <vector>

namespace display_commander::utils {

namespace {
constexpr const char* RESHADE_SECTION = "DisplayCommander.ReShade";
constexpr const char* KEY_LOAD_SOURCE = "ReshadeLoadSource";  // legacy, for migration only
constexpr const char* KEY_SHARED_PATH = "ReshadeSharedPath";  // legacy, unused
constexpr const char* KEY_SELECTED_VERSION = "ReshadeSelectedVersion";
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
    std::string reshade_me_version;
    if (vc::FetchReShadeLatestFromReshadeMe(&reshade_me_version, nullptr)) {
        if (std::find(s_reshade_versions_combined.begin(), s_reshade_versions_combined.end(), reshade_me_version)
            == s_reshade_versions_combined.end()) {
            s_reshade_versions_combined.push_back(reshade_me_version);
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

// Effective selected version: read KEY_SELECTED_VERSION; if empty, migrate from legacy KEY_LOAD_SOURCE.
// Returns "no" | "local" | "latest" | "global" | "X.Y.Z". Empty config is migrated to "global".
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
        if (selected.empty()) selected = DEFAULT_VERSION;
        return selected;
    }
    return "global";
}

static std::string GetReshadeVersionInDirectoryNormalized(const std::filesystem::path& dir) {
    if (dir.empty()) return "";
    std::filesystem::path dll_path = dir / L"Reshade64.dll";
    if (!std::filesystem::exists(dll_path)) return "";
    std::string raw = ::GetDLLVersionString(dll_path.wstring());
    if (raw.empty()) return "";
    return display_commander::utils::version_check::NormalizeVersionToXyz(raw);
}

std::vector<ReshadeLocation> GetReshadeLocations(const std::filesystem::path& game_directory) {
    std::vector<ReshadeLocation> out;
    namespace vc = display_commander::utils::version_check;
    const std::filesystem::path base = GetLocalReshadeDirectory();
    if (base.empty()) return out;

    // Local: game folder
    if (!game_directory.empty() && DirectoryHasReshadeDlls(game_directory)) {
        ReshadeLocation loc;
        loc.type = ReshadeLocationType::Local;
        loc.version = GetReshadeVersionInDirectoryNormalized(game_directory);
        loc.directory = game_directory;
        out.push_back(std::move(loc));
    }

    // Global: base folder
    if (DirectoryHasReshadeDlls(base)) {
        ReshadeLocation loc;
        loc.type = ReshadeLocationType::Global;
        loc.version = GetReshadeVersionInDirectoryNormalized(base);
        loc.directory = base;
        out.push_back(std::move(loc));
    }

    // SpecificVersion: base/Dll/X.Y.Z
    std::filesystem::path dll_base = base / L"Dll";
    std::error_code ec;
    if (std::filesystem::exists(dll_base, ec) && std::filesystem::is_directory(dll_base, ec)) {
        for (const auto& entry : std::filesystem::directory_iterator(dll_base, ec)) {
            if (ec) continue;
            if (!entry.is_directory(ec)) continue;
            std::string name = entry.path().filename().string();
            if (name.empty() || name == "." || name == "..") continue;
            if (DirectoryHasReshadeDlls(entry.path())) {
                ReshadeLocation loc;
                loc.type = ReshadeLocationType::SpecificVersion;
                loc.version = vc::NormalizeVersionToXyz(name);
                loc.directory = entry.path();
                out.push_back(std::move(loc));
            }
        }
    }
    return out;
}

ChooseReshadeVersionResult ChooseReshadeVersion(const std::vector<ReshadeLocation>& locations,
                                                const std::string& selected_setting) {
    ChooseReshadeVersionResult result;
    const std::string setting = selected_setting.empty() ? "global" : selected_setting;

    if (setting == "no") {
        return result;
    }

    namespace vc = display_commander::utils::version_check;
    const std::filesystem::path base = GetLocalReshadeDirectory();

    auto by_version_desc = [](const ReshadeLocation& a, const ReshadeLocation& b) {
        return vc::CompareVersions(a.version, b.version) > 0;
    };

    if (setting == "local") {
        for (const auto& loc : locations) {
            if (loc.type == ReshadeLocationType::Local) {
                result.directory = loc.directory;
                return result;
            }
        }
        for (const auto& loc : locations) {
            if (loc.type == ReshadeLocationType::Global) {
                result.directory = loc.directory;
                return result;
            }
        }
        std::vector<ReshadeLocation> sorted = locations;
        std::sort(sorted.begin(), sorted.end(), by_version_desc);
        if (!sorted.empty()) {
            result.directory = sorted.front().directory;
        }
        return result;
    }

    if (setting == "latest") {
        std::vector<ReshadeLocation> sorted = locations;
        std::sort(sorted.begin(), sorted.end(), by_version_desc);
        if (!sorted.empty()) {
            result.directory = sorted.front().directory;
        }
        return result;
    }

    if (setting == "global") {
        for (const auto& loc : locations) {
            if (loc.type == ReshadeLocationType::Global) {
                result.directory = loc.directory;
                return result;
            }
        }
        // Fallback: no Global (base has no DLLs), use highest versioned location
        std::vector<ReshadeLocation> sorted = locations;
        std::sort(sorted.begin(), sorted.end(), by_version_desc);
        if (!sorted.empty()) {
            result.fallback_selected = "global";
            result.fallback_loaded = sorted.front().version;
            result.directory = sorted.front().directory;
        } else {
            result.directory = base;
        }
        return result;
    }

    // Specific X.Y.Z
    const std::string normalized = vc::NormalizeVersionToXyz(setting);
    for (const auto& loc : locations) {
        if (loc.type == ReshadeLocationType::SpecificVersion && loc.version == normalized) {
            result.directory = loc.directory;
            return result;
        }
    }
    std::vector<ReshadeLocation> sorted = locations;
    std::sort(sorted.begin(), sorted.end(), by_version_desc);
    if (sorted.empty()) {
        result.directory = base / L"Dll" / std::filesystem::path(setting);
        return result;
    }
    result.fallback_selected = setting;
    result.fallback_loaded = sorted.front().version;
    result.directory = sorted.front().directory;
    return result;
}

static const char* ReshadeLocationTypeToString(ReshadeLocationType t) {
    switch (t) {
        case ReshadeLocationType::Local:           return "Local";
        case ReshadeLocationType::Global:          return "Global";
        case ReshadeLocationType::SpecificVersion: return "SpecificVersion";
        default:                                   return "?";
    }
}

std::filesystem::path GetReshadeDirectoryForLoading(const std::filesystem::path& game_directory) {
    std::string selected = GetReshadeSelectedVersionEffective();
    LogInfo("[reshade] selected = %s", selected.c_str());
    if (selected == "no") {
        return std::filesystem::path();
    }
    std::vector<ReshadeLocation> locations = GetReshadeLocations(game_directory);
    for (size_t i = 0; i < locations.size(); ++i) {
        const ReshadeLocation& loc = locations[i];
        LogInfo("[reshade] location[%zu] type=%s version=%s dir=%s", i, ReshadeLocationTypeToString(loc.type),
                loc.version.c_str(), loc.directory.string().c_str());
    }
    ChooseReshadeVersionResult choose = ChooseReshadeVersion(locations, selected);
    LogInfo("[reshade] chosen dir=%s fallback_selected=%s fallback_loaded=%s", choose.directory.string().c_str(),
            choose.fallback_selected.c_str(), choose.fallback_loaded.c_str());
    s_fallback_selected_version = choose.fallback_selected;
    s_fallback_loaded_version = choose.fallback_loaded;
    return choose.directory;
}

std::filesystem::path GetReshadeDirectoryForLoading() { return GetReshadeDirectoryForLoading(std::filesystem::path()); }

bool IsReshadeLoadDisabledByConfig() { return GetReshadeSelectedVersionEffective() == "no"; }

std::string GetReshadeSelectedVersionFromConfig() { return GetReshadeSelectedVersionEffective(); }

void SetReshadeSelectedVersionInConfig(const std::string& version) {
    using namespace display_commander::config;
    DisplayCommanderConfigManager::GetInstance().SetConfigValue(RESHADE_SECTION, KEY_SELECTED_VERSION, version);
}

const char* const* GetReshadeVersionList(size_t* out_count) {
    EnsureReShadeVersionListFetched();
    *out_count = s_reshade_version_ptrs.size();
    return s_reshade_version_ptrs.data();
}

const char* const* GetReshadeInstalledVersionList(size_t* out_count) {
    static std::vector<std::string> s_installed;
    static std::vector<const char*> s_installed_ptrs;
    s_installed.clear();
    s_installed_ptrs.clear();
    std::filesystem::path dll_base = GetLocalReshadeDirectory() / L"Dll";
    std::error_code ec;
    if (std::filesystem::exists(dll_base, ec) && std::filesystem::is_directory(dll_base, ec)) {
        for (const auto& entry : std::filesystem::directory_iterator(dll_base, ec)) {
            if (ec) continue;
            if (!entry.is_directory(ec)) continue;
            std::string name = entry.path().filename().string();
            if (name.empty() || name == "." || name == "..") continue;
            if (DirectoryHasReshadeDlls(entry.path())) {
                s_installed.push_back(name);
            }
        }
    }
    namespace vc = display_commander::utils::version_check;
    std::sort(s_installed.begin(), s_installed.end(),
              [](const std::string& a, const std::string& b) { return vc::CompareVersions(a, b) > 0; });
    s_installed_ptrs.reserve(s_installed.size());
    for (const auto& s : s_installed) {
        s_installed_ptrs.push_back(s.c_str());
    }
    *out_count = s_installed_ptrs.size();
    return s_installed_ptrs.empty() ? nullptr : s_installed_ptrs.data();
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

std::string GetLocalReshadeVersion() { return GetReshadeVersionInDirectory(GetLocalReshadeDirectory()); }

// Copy currently loaded ReShade to Reshade\Dll\X.Y.Z if not already there. Version from Reshade64.dll in source dir.
bool CopyCurrentReshadeToDll(const std::filesystem::path& loaded_reshade_directory, std::string* out_error) {
    if (loaded_reshade_directory.empty()) {
        if (out_error) *out_error = "Empty path";
        return false;
    }
    std::filesystem::path dll64 = loaded_reshade_directory / L"Reshade64.dll";
    std::error_code ec;
    if (!std::filesystem::exists(dll64, ec)) {
        if (out_error) *out_error = "Reshade64.dll not found in source directory";
        return false;
    }
    std::string version = ::GetDLLVersionString(dll64.wstring());
    if (version.empty()) {
        if (out_error) *out_error = "Could not read version from Reshade64.dll";
        return false;
    }
    std::filesystem::path base = GetLocalReshadeDirectory();
    std::filesystem::path dll_base = base / L"Dll";
    std::filesystem::path target_dir = dll_base / std::filesystem::path(version);
    std::filesystem::path source_canon = std::filesystem::canonical(loaded_reshade_directory, ec);
    if (!ec && std::filesystem::exists(dll_base, ec)) {
        std::filesystem::path dll_canon = std::filesystem::canonical(dll_base, ec);
        if (!ec) {
            auto s_it = source_canon.begin();
            auto d_it = dll_canon.begin();
            while (d_it != dll_canon.end() && s_it != source_canon.end() && *d_it == *s_it) {
                ++d_it;
                ++s_it;
            }
            if (d_it == dll_canon.end()) {
                return true;
            }
        }
    }
    if (DirectoryHasReshadeDlls(target_dir)) {
        return true;
    }
    if (!std::filesystem::exists(target_dir, ec)) {
        std::filesystem::create_directories(target_dir, ec);
        if (ec) {
            if (out_error) *out_error = "Failed to create directory: " + ec.message();
            return false;
        }
    }
    auto copy_one = [&](const wchar_t* name) -> bool {
        std::filesystem::path src = loaded_reshade_directory / name;
        std::filesystem::path dst = target_dir / name;
        if (!std::filesystem::exists(src, ec)) return true;
        if (!TryHardLinkOrCopyFile(src, dst)) {
            if (out_error) *out_error = "Failed to hard link or copy DLL";
            return false;
        }
        return true;
    };
    if (!copy_one(L"Reshade64.dll")) return false;
    if (!copy_one(L"Reshade32.dll")) return false;
    return true;
}

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
