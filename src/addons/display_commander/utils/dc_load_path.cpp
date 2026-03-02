#include "dc_load_path.hpp"
#include <ShlObj.h>
#include <Windows.h>
#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>
#include "../config/display_commander_config.hpp"
#include "general_utils.hpp"
#include "version_check.hpp"

namespace display_commander::utils {

namespace {
constexpr const char* DC_SECTION = "DisplayCommander.DC";
constexpr const char* KEY_SELECTED_VERSION = "DcSelectedVersion";
constexpr const char* KEY_SELECTOR_MODE = "dc_selector_mode";
constexpr const char* KEY_VERSION_DEBUG = "dc_version_for_debug";
constexpr const char* KEY_VERSION_STABLE = "dc_version_for_stable";

// Canonical DC addon filenames (load zzz_display_commander.addon64 / .addon32, not dc64.dll/dc32.dll).
static const std::wstring DC_ADDON_64 = L"zzz_display_commander.addon64";
static const std::wstring DC_ADDON_32 = L"zzz_display_commander.addon32";

// True if dir contains zzz_display_commander.addon64 or zzz_display_commander.addon32 (case-insensitive).
static bool DirectoryHasDcAddonDlls(const std::filesystem::path& dir) {
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
        if (ec) continue;
        if (!entry.is_regular_file(ec)) continue;
        std::wstring name = entry.path().filename().wstring();
        for (auto& c : name) {
            if (c >= L'A' && c <= L'Z') c += (L'a' - L'A');
        }
        if (name == DC_ADDON_64 || name == DC_ADDON_32) return true;
    }
    return false;
}

static std::filesystem::path GetDcBaseFromLocalAppData() { return GetDisplayCommanderAppDataFolder(); }

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

// Process executable directory (game folder). Empty on failure.
static std::filesystem::path GetProcessDirectory() {
    wchar_t buf[MAX_PATH];
    if (GetModuleFileNameW(nullptr, buf, MAX_PATH) == 0) return {};
    std::filesystem::path p(buf);
    return p.has_filename() ? p.parent_path() : p;
}

// True if the module path filename is the DC addon (zzz_display_commander.addon64 or .addon32).
static bool IsDcAddonModulePath(const std::filesystem::path& module_path) {
    std::wstring name = module_path.filename().wstring();
    for (auto& c : name) {
        if (c >= L'A' && c <= L'Z') c += (L'a' - L'A');
    }
    return name == DC_ADDON_64 || name == DC_ADDON_32;
}

// True if the module path filename is a known DC proxy (dxgi, d3d11, winmm, etc.).
static bool IsKnownDcProxyModulePath(const std::filesystem::path& module_path) {
    std::wstring name = module_path.filename().wstring();
    for (auto& c : name) {
        if (c >= L'A' && c <= L'Z') c += (L'a' - L'A');
    }
    static const std::wstring known[] = {L"dxgi.dll",  L"d3d11.dll", L"d3d12.dll",    L"d3d9.dll",
                                         L"ddraw.dll", L"winmm.dll", L"opengl32.dll", L"version.dll"};
    for (const auto& k : known) {
        if (name == k) return true;
    }
    return false;
}

extern "C" BOOL WINAPI K32EnumProcessModules(HANDLE hProcess, HMODULE* lphModule, DWORD cb, LPDWORD lpcbNeeded);
}  // namespace

std::filesystem::path GetDcDirectoryForLoading(void* current_module) {
    using namespace display_commander::config;
    EnsureDcConfigMigrated();
    auto& config = DisplayCommanderConfigManager::GetInstance();
    std::string mode;
    config.GetConfigValue(DC_SECTION, KEY_SELECTOR_MODE, mode);
    if (mode != "global" && mode != "debug" && mode != "stable") {
        mode = "local";
    }

    std::filesystem::path base = GetDcBaseFromLocalAppData();
    std::filesystem::path stable_base = base / L"stable";
    std::filesystem::path debug_base = base / L"Debug";

    // local: resolution order (1) local zzz_display_commander.addon64/.addon32, (2) global, (3) proxy .dll dir.
    if (mode == "local") {
        std::filesystem::path process_dir = GetProcessDirectory();
        if (DirectoryHasDcAddonDlls(process_dir)) return process_dir;
        if (DirectoryHasDcAddonDlls(base)) return base;
        std::string latest_stable = GetHighestDcVersionInFolder(base, L"stable");
        if (!latest_stable.empty()) return stable_base / std::filesystem::path(latest_stable);
        std::string latest_debug = GetHighestDcVersionInFolder(base, L"Debug");
        if (!latest_debug.empty()) return debug_base / std::filesystem::path(latest_debug);
        if (current_module != nullptr) {
            wchar_t mod_buf[MAX_PATH];
            if (GetModuleFileNameW(reinterpret_cast<HMODULE>(current_module), mod_buf, MAX_PATH) > 0) {
                std::filesystem::path module_path(mod_buf);
                std::filesystem::path module_dir = module_path.has_filename() ? module_path.parent_path() : module_path;
                if (!IsDcAddonModulePath(module_path)) return module_dir;
            }
        }
        return base;
    }

    auto fallback_to_base = [&base]() { return base; };

    if (mode == "global") {
        if (DirectoryHasDcAddonDlls(base)) return base;
        std::string latest_stable = GetHighestDcVersionInFolder(base, L"stable");
        if (!latest_stable.empty()) return stable_base / std::filesystem::path(latest_stable);
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
        std::string highest = GetHighestDcVersionInFolder(base, L"stable");
        if (highest.empty()) return fallback_to_base();
        return stable_base / std::filesystem::path(highest);
    }
    std::filesystem::path dir = stable_base / std::filesystem::path(version);
    if (DirectoryHasDcAddonDlls(dir)) return dir;
    std::string highest = GetHighestDcVersionInFolder(base, L"stable");
    if (!highest.empty()) return stable_base / std::filesystem::path(highest);
    return base;
}

std::filesystem::path GetLocalDcDirectory() { return GetDcBaseFromLocalAppData(); }

std::filesystem::path GetLocalDcAddonDirectory() {
    std::filesystem::path process_dir = GetProcessDirectory();
    if (process_dir.empty() || !DirectoryHasDcAddonDlls(process_dir)) return {};
    return process_dir;
}

// Shared logic: find first proxy module (non-exe, non-addon). Returns module path; optional out_dir.
static std::filesystem::path GetDcProxyModulePathImpl(std::filesystem::path* out_dir) {
    std::filesystem::path process_dir = GetProcessDirectory();
    HMODULE modules[512];
    DWORD needed = 0;
    if (K32EnumProcessModules(GetCurrentProcess(), modules, sizeof(modules), &needed) == 0) return {};
    wchar_t exe_buf[MAX_PATH];
    if (GetModuleFileNameW(nullptr, exe_buf, MAX_PATH) == 0) return {};
    std::filesystem::path exe_path(exe_buf);
    std::error_code ec;
    exe_path = std::filesystem::canonical(exe_path, ec);
    if (ec) exe_path = std::filesystem::path(exe_buf);
    const size_t n = (needed < sizeof(modules)) ? (needed / sizeof(HMODULE)) : (sizeof(modules) / sizeof(HMODULE));
    std::filesystem::path fallback_path;
    for (size_t i = 0; i < n; ++i) {
        wchar_t mod_buf[MAX_PATH];
        if (GetModuleFileNameW(modules[i], mod_buf, MAX_PATH) == 0) continue;
        std::filesystem::path mod_path(mod_buf);
        std::filesystem::path mod_canon = std::filesystem::canonical(mod_path, ec);
        if (ec) mod_canon = mod_path;
        if (mod_canon == exe_path) continue;
        if (IsDcAddonModulePath(mod_path)) continue;
        if (!IsKnownDcProxyModulePath(mod_path)) continue;  // Only DC proxy DLLs (dxgi, winmm, etc.).
        if (!process_dir.empty()) {
            std::filesystem::path dir = mod_path.has_filename() ? mod_path.parent_path() : mod_path;
            std::filesystem::path dir_canon = std::filesystem::canonical(dir, ec);
            std::filesystem::path proc_canon = std::filesystem::canonical(process_dir, ec);
            if (!ec && dir_canon == proc_canon) {
                if (out_dir) *out_dir = dir;
                return mod_path;  // Prefer proxy in game folder.
            }
        }
        if (fallback_path.empty()) {
            fallback_path = mod_path;
            if (out_dir) *out_dir = mod_path.has_filename() ? mod_path.parent_path() : mod_path;
        }
    }
    if (out_dir && !fallback_path.empty())
        *out_dir = fallback_path.has_filename() ? fallback_path.parent_path() : fallback_path;
    return fallback_path;
}

std::filesystem::path GetDcProxyDirectory() {
    std::filesystem::path dir;
    GetDcProxyModulePathImpl(&dir);
    return dir;
}

std::filesystem::path GetDcProxyModulePath() { return GetDcProxyModulePathImpl(nullptr); }

std::string GetDcVersionInDirectory(const std::filesystem::path& dir) {
    if (dir.empty()) return "";
    std::error_code ec;
    std::filesystem::path path64 = dir / std::filesystem::path(DC_ADDON_64);
    std::filesystem::path path32 = dir / std::filesystem::path(DC_ADDON_32);
    if (std::filesystem::exists(path64, ec)) return GetDLLVersionString(path64.wstring());
    if (std::filesystem::exists(path32, ec)) return GetDLLVersionString(path32.wstring());
    return "";
}

std::string GetLocalDcVersion() { return GetDcVersionInDirectory(GetLocalDcDirectory()); }

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
    std::filesystem::path p = dir / std::filesystem::path(DC_ADDON_64);
#else
    std::filesystem::path p = dir / std::filesystem::path(DC_ADDON_32);
#endif
    if (std::filesystem::exists(p, ec)) return p;
    return {};
}

const char* const* GetDcInstalledVersionListStable(size_t* out_count) {
    return GetDcInstalledVersionListIn(GetLocalDcDirectory(), L"stable", out_count);
}

const char* const* GetDcInstalledVersionListDebug(size_t* out_count) {
    return GetDcInstalledVersionListIn(GetLocalDcDirectory(), L"Debug", out_count);
}

const char* const* GetDcInstalledVersionList(size_t* out_count) { return GetDcInstalledVersionListStable(out_count); }

bool DeleteLocalDcAddonFromDirectory(const std::filesystem::path& dir, std::string* out_error) {
    if (dir.empty()) {
        if (out_error) *out_error = "Directory is empty";
        return false;
    }
    std::error_code ec;
    auto remove_if_exists = [&dir, &ec, out_error](const std::wstring& name) -> bool {
        std::filesystem::path p = dir / std::filesystem::path(name);
        if (!std::filesystem::exists(p, ec)) return true;
        std::filesystem::remove(p, ec);
        if (ec && out_error) *out_error = "Failed to remove " + p.string() + ": " + ec.message();
        return !ec;
    };
    if (!remove_if_exists(DC_ADDON_64)) return false;
    if (!remove_if_exists(DC_ADDON_32)) return false;
    return true;
}

}  // namespace display_commander::utils
