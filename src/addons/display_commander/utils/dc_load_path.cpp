#include "dc_load_path.hpp"
#include "../config/display_commander_config.hpp"
#include "general_utils.hpp"
#include "version_check.hpp"
#include <ShlObj.h>
#include <Windows.h>
#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

namespace display_commander::utils {

namespace {
constexpr const char* DC_SECTION = "DisplayCommander.DC";
constexpr const char* KEY_SELECTED_VERSION = "DcSelectedVersion";
constexpr const char* KEY_SELECTOR_MODE = "dc_selector_mode";
constexpr const char* KEY_VERSION_DEBUG = "dc_version_for_debug";
constexpr const char* KEY_VERSION_STABLE = "dc_version_for_stable";

// True if dir contains at least one .addon64 or .addon32 file whose name contains "display_commander".
// (One arch is enough so that a single-arch copy from e.g. winmm.dll proxy shows up in the version list.)
static bool DirectoryHasDcAddonDlls(const std::filesystem::path& dir) {
    if (dir.empty()) return false;
    std::error_code ec;
    bool has_64 = false;
    bool has_32 = false;
    for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
        if (ec) continue;
        if (!entry.is_regular_file(ec)) continue;
        std::wstring name = entry.path().filename().wstring();
        for (auto& c : name) {
            if (c >= L'A' && c <= L'Z') c += (L'a' - L'A');
        }
        if (name.find(L"display_commander") == std::wstring::npos) continue;
        if (name.size() >= 8 && name.compare(name.size() - 8, 8, L".addon64") == 0) has_64 = true;
        if (name.size() >= 8 && name.compare(name.size() - 8, 8, L".addon32") == 0) has_32 = true;
    }
    return has_64 || has_32;
}

static std::filesystem::path GetDcBaseFromLocalAppData() {
    return GetDisplayCommanderAppDataFolder();
}

// Highest version (by CompareVersions) in base/subdir/*, or empty if none.
static std::string GetHighestDcVersionInFolder(const std::filesystem::path& base, const wchar_t* subdir) {
    std::filesystem::path folder = base / subdir;
    std::error_code ec;
    if (!std::filesystem::exists(folder, ec) || !std::filesystem::is_directory(folder, ec)) return "";
    std::vector<std::string> versions;
    for (const auto& entry : std::filesystem::directory_iterator(folder, ec)) {
        if (ec) continue;
        if (!entry.is_directory(ec)) continue;
        std::string name = entry.path().filename().string();
        if (name.empty() || name == "." || name == "..") continue;
        if (DirectoryHasDcAddonDlls(entry.path())) versions.push_back(name);
    }
    if (versions.empty()) return "";
    namespace vc = display_commander::utils::version_check;
    std::sort(versions.begin(), versions.end(),
              [](const std::string& a, const std::string& b) { return vc::CompareVersions(a, b) > 0; });
    return versions.front();
}

// One-time migration: if dc_selector_mode is missing, derive from DcSelectedVersion and write new keys.
static void EnsureDcConfigMigrated() {
    using namespace display_commander::config;
    auto& config = DisplayCommanderConfigManager::GetInstance();
    std::string mode;
    if (config.GetConfigValue(DC_SECTION, KEY_SELECTOR_MODE, mode) && !mode.empty()) {
        return;  // Already migrated
    }
    std::string legacy;
    config.GetConfigValue(DC_SECTION, KEY_SELECTED_VERSION, legacy);
    if (legacy.empty()) {
        config.SetConfigValue(DC_SECTION, KEY_SELECTOR_MODE, "local");
        config.SetConfigValue(DC_SECTION, KEY_VERSION_STABLE, "latest");
    } else if (legacy == "latest") {
        config.SetConfigValue(DC_SECTION, KEY_SELECTOR_MODE, "stable");
        config.SetConfigValue(DC_SECTION, KEY_VERSION_STABLE, "latest");
    } else {
        config.SetConfigValue(DC_SECTION, KEY_SELECTOR_MODE, "stable");
        config.SetConfigValue(DC_SECTION, KEY_VERSION_STABLE, legacy);
    }
    config.SetConfigValue(DC_SECTION, KEY_VERSION_DEBUG, "latest");
    config.SaveConfig("DC selector migration");
}

// Fill static vectors and return pointer array for subdir (Dll or Debug). Caller must not keep pointers across calls.
const char* const* GetDcInstalledVersionListIn(const std::filesystem::path& base, const wchar_t* subdir,
                                               size_t* out_count) {
    static std::vector<std::string> s_installed;
    static std::vector<const char*> s_installed_ptrs;
    s_installed.clear();
    s_installed_ptrs.clear();
    std::filesystem::path folder = base / subdir;
    std::error_code ec;
    if (std::filesystem::exists(folder, ec) && std::filesystem::is_directory(folder, ec)) {
        for (const auto& entry : std::filesystem::directory_iterator(folder, ec)) {
            if (ec) continue;
            if (!entry.is_directory(ec)) continue;
            std::string name = entry.path().filename().string();
            if (name.empty() || name == "." || name == "..") continue;
            if (DirectoryHasDcAddonDlls(entry.path())) {
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
}  // namespace

std::filesystem::path GetDcDirectoryForLoading() {
    using namespace display_commander::config;
    EnsureDcConfigMigrated();
    auto& config = DisplayCommanderConfigManager::GetInstance();
    std::string mode;
    config.GetConfigValue(DC_SECTION, KEY_SELECTOR_MODE, mode);
    if (mode != "global" && mode != "debug" && mode != "stable") {
        mode = "local";
    }

    std::filesystem::path base = GetDcBaseFromLocalAppData();
    std::filesystem::path dll_base = base / L"Dll";
    std::filesystem::path debug_base = base / L"Debug";

    // local: return base so loader does not load from central (load_path == base -> no load).
    if (mode == "local") {
        return base;
    }

    auto fallback_to_base = [&base]() { return base; };

    if (mode == "global") {
        if (DirectoryHasDcAddonDlls(base)) return base;
        std::string latest_dll = GetHighestDcVersionInFolder(base, L"Dll");
        if (!latest_dll.empty()) return dll_base / std::filesystem::path(latest_dll);
        std::string latest_debug = GetHighestDcVersionInFolder(base, L"Debug");
        if (!latest_debug.empty()) return debug_base / std::filesystem::path(latest_debug);
        return base;
    }

    if (mode == "debug") {
        std::string version;
        config.GetConfigValue(DC_SECTION, KEY_VERSION_DEBUG, version);
        if (version.empty()) version = "latest";
        if (version == "latest") {
            std::string highest = GetHighestDcVersionInFolder(base, L"Debug");
            if (highest.empty()) return fallback_to_base();
            return debug_base / std::filesystem::path(highest);
        }
        std::filesystem::path dir = debug_base / std::filesystem::path(version);
        if (DirectoryHasDcAddonDlls(dir)) return dir;
        std::string highest = GetHighestDcVersionInFolder(base, L"Debug");
        if (!highest.empty()) return debug_base / std::filesystem::path(highest);
        return base;
    }

    // stable
    std::string version;
    config.GetConfigValue(DC_SECTION, KEY_VERSION_STABLE, version);
    if (version.empty()) version = "latest";
    if (version == "latest") {
        std::string highest = GetHighestDcVersionInFolder(base, L"Dll");
        if (highest.empty()) return fallback_to_base();
        return dll_base / std::filesystem::path(highest);
    }
    std::filesystem::path dir = dll_base / std::filesystem::path(version);
    if (DirectoryHasDcAddonDlls(dir)) return dir;
    std::string highest = GetHighestDcVersionInFolder(base, L"Dll");
    if (!highest.empty()) return dll_base / std::filesystem::path(highest);
    return base;
}

std::filesystem::path GetLocalDcDirectory() {
    return GetDcBaseFromLocalAppData();
}

std::string GetDcVersionInDirectory(const std::filesystem::path& dir) {
    if (dir.empty()) return "";
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
        if (ec) continue;
        if (!entry.is_regular_file(ec)) continue;
        std::wstring name = entry.path().filename().wstring();
        if (name.size() < 8) continue;
        if (name.compare(name.size() - 8, 8, L".addon64") != 0) continue;
        if (name.find(L"display_commander") == std::wstring::npos) continue;
        return GetDLLVersionString(entry.path().wstring());
    }
    return "";
}

std::string GetLocalDcVersion() {
    return GetDcVersionInDirectory(GetLocalDcDirectory());
}

std::string GetDcSelectorModeFromConfig() {
    EnsureDcConfigMigrated();
    using namespace display_commander::config;
    std::string value;
    DisplayCommanderConfigManager::GetInstance().GetConfigValue(DC_SECTION, KEY_SELECTOR_MODE, value);
    if (value != "local" && value != "global" && value != "debug" && value != "stable") value = "local";
    return value;
}

void SetDcSelectorModeInConfig(const std::string& mode) {
    using namespace display_commander::config;
    DisplayCommanderConfigManager::GetInstance().SetConfigValue(DC_SECTION, KEY_SELECTOR_MODE, mode);
}

std::string GetDcVersionForDebugFromConfig() {
    EnsureDcConfigMigrated();
    using namespace display_commander::config;
    std::string value;
    DisplayCommanderConfigManager::GetInstance().GetConfigValue(DC_SECTION, KEY_VERSION_DEBUG, value);
    if (value.empty()) value = "latest";
    return value;
}

void SetDcVersionForDebugInConfig(const std::string& version) {
    using namespace display_commander::config;
    DisplayCommanderConfigManager::GetInstance().SetConfigValue(DC_SECTION, KEY_VERSION_DEBUG, version);
}

std::string GetDcVersionForStableFromConfig() {
    EnsureDcConfigMigrated();
    using namespace display_commander::config;
    std::string value;
    DisplayCommanderConfigManager::GetInstance().GetConfigValue(DC_SECTION, KEY_VERSION_STABLE, value);
    if (value.empty()) value = "latest";
    return value;
}

void SetDcVersionForStableInConfig(const std::string& version) {
    using namespace display_commander::config;
    DisplayCommanderConfigManager::GetInstance().SetConfigValue(DC_SECTION, KEY_VERSION_STABLE, version);
}

std::string GetDcSelectedVersionFromConfig() {
    using namespace display_commander::config;
    std::string value;
    DisplayCommanderConfigManager::GetInstance().GetConfigValue(DC_SECTION, KEY_SELECTED_VERSION, value);
    return value;
}

void SetDcSelectedVersionInConfig(const std::string& version) {
    using namespace display_commander::config;
    DisplayCommanderConfigManager::GetInstance().SetConfigValue(DC_SECTION, KEY_SELECTED_VERSION, version);
}

std::filesystem::path GetDcAddonPathInDirectory(const std::filesystem::path& dir) {
    if (dir.empty()) return {};
    std::error_code ec;
#ifdef _WIN64
    const std::wstring suffix = L".addon64";
#else
    const std::wstring suffix = L".addon32";
#endif
    for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
        if (ec) continue;
        if (!entry.is_regular_file(ec)) continue;
        std::wstring name = entry.path().filename().wstring();
        for (auto& c : name) {
            if (c >= L'A' && c <= L'Z') c += (L'a' - L'A');
        }
        if (name.find(L"display_commander") == std::wstring::npos) continue;
        if (name.size() >= suffix.size() && name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0) {
            return entry.path();
        }
    }
    return {};
}

const char* const* GetDcInstalledVersionListStable(size_t* out_count) {
    return GetDcInstalledVersionListIn(GetLocalDcDirectory(), L"Dll", out_count);
}

const char* const* GetDcInstalledVersionListDebug(size_t* out_count) {
    return GetDcInstalledVersionListIn(GetLocalDcDirectory(), L"Debug", out_count);
}

const char* const* GetDcInstalledVersionList(size_t* out_count) {
    return GetDcInstalledVersionListStable(out_count);
}

}  // namespace display_commander::utils
