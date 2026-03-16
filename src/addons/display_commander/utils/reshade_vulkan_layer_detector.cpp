// Source Code <Display Commander>
#include "reshade_vulkan_layer_detector.hpp"
#include "logging.hpp"

// Libraries <standard C++>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

// Libraries <Windows.h>
#include <Windows.h>

namespace {

constexpr const char* RESHADE_APPS_INI_PATH = "C:\\ProgramData\\ReShade\\ReShadeApps.ini";
constexpr const char APPS_KEY[] = "Apps";

std::string Trim(const std::string& s) {
    auto start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return {};
    auto end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

// Remove UTF-8 BOM (EF BB BF) from start of s so "Apps" key matches when file is saved with BOM.
std::string StripUtf8Bom(const std::string& s) {
    if (s.size() >= 3 && static_cast<unsigned char>(s[0]) == 0xEF
        && static_cast<unsigned char>(s[1]) == 0xBB && static_cast<unsigned char>(s[2]) == 0xBF) {
        return s.substr(3);
    }
    return s;
}

// Split "Apps=path1,path2,path3" into ["path1","path2","path3"] after trimming.
std::vector<std::string> ParseAppsValue(const std::string& value) {
    std::vector<std::string> paths;
    std::istringstream ss(value);
    std::string part;
    while (std::getline(ss, part, ',')) {
        std::string p = Trim(part);
        if (!p.empty()) paths.push_back(p);
    }
    return paths;
}

bool ParseReShadeAppsIni(std::vector<std::string>& out_app_paths) {
    std::ifstream f(RESHADE_APPS_INI_PATH);
    if (!f) return false;
    std::string line;
    while (std::getline(f, line)) {
        std::string trimmed = Trim(line);
        if (trimmed.empty() || trimmed[0] == ';' || trimmed[0] == '#') continue;
        size_t eq = trimmed.find('=');
        if (eq == std::string::npos) continue;
        std::string key = StripUtf8Bom(Trim(trimmed.substr(0, eq)));
        std::string val = Trim(trimmed.substr(eq + 1));
        if (key != APPS_KEY) continue;
        out_app_paths = ParseAppsValue(val);
        return true;
    }
    return false;
}

// Log file size and first 1000 bytes for debugging parse failures. Safe for any file content.
void LogReShadeAppsIniFileDebugInfo() {
    std::filesystem::path path(RESHADE_APPS_INI_PATH);
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
        LogDebug("[Vulkan layer] ReShadeApps.ini does not exist");
        return;
    }
    auto size = std::filesystem::file_size(path, ec);
    if (ec) {
        LogDebug("[Vulkan layer] ReShadeApps.ini file_size failed: %s", ec.message().c_str());
        return;
    }
    LogDebug("[Vulkan layer] ReShadeApps.ini file size: %lld", static_cast<int64_t>(size));
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        LogDebug("[Vulkan layer] ReShadeApps.ini could not reopen for preview");
        return;
    }
    constexpr size_t k_preview_max = 1000;
    size_t to_read = (size < k_preview_max) ? size : k_preview_max;
    std::string buf(to_read, '\0');
    if (!f.read(buf.data(), static_cast<std::streamsize>(to_read))) {
        LogDebug("[Vulkan layer] ReShadeApps.ini read failed");
        return;
    }
    std::string preview;
    preview.reserve(to_read);
    for (unsigned char u : buf) {
        preview.push_back((u >= 32 && u < 127) ? static_cast<char>(u) : '.');
    }
    LogDebug("[Vulkan layer] ReShadeApps.ini first %zu bytes: %s", to_read, preview.c_str());
}

}  // namespace

bool IsReShadeRegisteredAsVulkanLayerForCurrentExe() {
    wchar_t exe_buf[MAX_PATH];
    if (GetModuleFileNameW(nullptr, exe_buf, MAX_PATH) == 0) {
        LogDebug("[Vulkan layer] GetModuleFileNameW failed");
        return false;
    }
    std::filesystem::path current_exe(exe_buf);
    std::error_code ec;
    current_exe = std::filesystem::canonical(current_exe, ec);
    if (ec) current_exe = std::filesystem::path(exe_buf);
    LogDebug("[Vulkan layer] current exe: %s", current_exe.string().c_str());

    std::vector<std::string> app_paths;
    if (!ParseReShadeAppsIni(app_paths)) {
        LogDebug("[Vulkan layer] ParseReShadeAppsIni failed (missing or no Apps=): %s", RESHADE_APPS_INI_PATH);
        LogReShadeAppsIniFileDebugInfo();
        return false;
    }
    LogDebug("[Vulkan layer] ReShadeApps.ini Apps count: %zu", app_paths.size());

    for (const std::string& ap : app_paths) {
        std::filesystem::path listed(ap);
        if (listed.empty()) continue;
        if (!std::filesystem::exists(listed, ec)) {
            LogDebug("[Vulkan layer] listed path does not exist: %s", ap.c_str());
            continue;
        }
        if (std::filesystem::equivalent(current_exe, listed, ec)) {
            LogDebug("[Vulkan layer] match: current exe is in Apps list (listed: %s)", ap.c_str());
            return true;
        }
    }
    LogDebug("[Vulkan layer] no match among %zu Apps entries", app_paths.size());
    return false;
}
