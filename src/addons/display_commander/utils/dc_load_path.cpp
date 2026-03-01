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
}  // namespace

// Highest version (by CompareVersions) in Dll\*, or empty if none.
static std::string GetHighestDcVersionInDll() {
    std::filesystem::path dll_base = GetDcBaseFromLocalAppData() / L"Dll";
    std::error_code ec;
    if (!std::filesystem::exists(dll_base, ec) || !std::filesystem::is_directory(dll_base, ec)) return "";
    std::vector<std::string> versions;
    for (const auto& entry : std::filesystem::directory_iterator(dll_base, ec)) {
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

std::filesystem::path GetDcDirectoryForLoading() {
    using namespace display_commander::config;
    auto& config = DisplayCommanderConfigManager::GetInstance();
    std::string override_val;
    config.GetConfigValue(DC_SECTION, KEY_SELECTED_VERSION, override_val);

    std::filesystem::path base = GetDcBaseFromLocalAppData();
    std::filesystem::path dll_base = base / L"Dll";

    if (override_val.empty()) {
        return base;
    }
    if (override_val == "latest") {
        std::string highest = GetHighestDcVersionInDll();
        if (highest.empty()) return base;
        return dll_base / std::filesystem::path(highest);
    }
    // Specific X.Y.Z: use it if present, else fallback to base (current)
    std::filesystem::path selected_dir = dll_base / std::filesystem::path(override_val);
    if (DirectoryHasDcAddonDlls(selected_dir)) return selected_dir;
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

const char* const* GetDcInstalledVersionList(size_t* out_count) {
    static std::vector<std::string> s_installed;
    static std::vector<const char*> s_installed_ptrs;
    s_installed.clear();
    s_installed_ptrs.clear();
    std::filesystem::path dll_base = GetLocalDcDirectory() / L"Dll";
    std::error_code ec;
    if (std::filesystem::exists(dll_base, ec) && std::filesystem::is_directory(dll_base, ec)) {
        for (const auto& entry : std::filesystem::directory_iterator(dll_base, ec)) {
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

}  // namespace display_commander::utils
