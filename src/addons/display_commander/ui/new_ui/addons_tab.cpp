// Source Code <Display Commander> // follow this order for includes in all files + add this comment at the top
#include "addons_tab.hpp"
#include "../../config/display_commander_config.hpp"
#include "../forkawesome.h"
#include "../ui_colors.hpp"
#include "../../utils/general_utils.hpp"
#include "../../utils/logging.hpp"
#include "../../utils/reshade_load_path.hpp"
#include "../../utils/string_utils.hpp"
#include "../imgui_wrapper_base.hpp"

// Libraries <ReShade> / <imgui>
#include <imgui.h>
#include <reshade.hpp>

// Libraries <standard C++>
#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <unordered_map>
#include <vector>

// Libraries <Windows.h> — before other Windows headers
#include <Windows.h>

// Libraries <Windows>
#include <psapi.h>
#include <ShlObj.h>
#include <winhttp.h>

namespace ui::new_ui {

namespace {
// Global addon list
std::vector<AddonInfo> g_addon_list;
std::atomic<bool> g_addon_list_dirty(true);  // Set to true to trigger refresh

// Warning dialog state for addon enable/disable
std::atomic<bool> g_show_addon_restart_warning(false);

std::vector<std::string> g_addon_download_urls;
std::atomic<bool> g_addon_download_urls_loaded(false);
std::string g_addon_download_status;
std::unordered_map<std::string, std::string> g_addon_download_etags;

void SetAddonEnabled(const std::string& addon_name, const std::string& addon_file, bool enabled);

// Get the global addons directory path
std::filesystem::path GetGlobalAddonsDirectory() {
    wchar_t localappdata_path[MAX_PATH];
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, localappdata_path))) {
        return std::filesystem::path();
    }
    std::filesystem::path localappdata_dir(localappdata_path);
    return localappdata_dir / L"Programs" / L"Display_Commander" / L"Reshade" / L"Addons";
}

// Get the shaders directory path
std::filesystem::path GetShadersDirectory() {
    wchar_t localappdata_path[MAX_PATH];
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, localappdata_path))) {
        return std::filesystem::path();
    }
    std::filesystem::path localappdata_dir(localappdata_path);
    return localappdata_dir / L"Programs" / L"Display_Commander" / L"Reshade" / L"Shaders";
}

// Get the textures directory path
std::filesystem::path GetTexturesDirectory() {
    wchar_t localappdata_path[MAX_PATH];
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, localappdata_path))) {
        return std::filesystem::path();
    }
    std::filesystem::path localappdata_dir(localappdata_path);
    return localappdata_dir / L"Programs" / L"Display_Commander" / L"Reshade" / L"Textures";
}

// Get the LocalAppData folder path
std::filesystem::path GetDocumentsDirectory() {
    wchar_t localappdata_path[MAX_PATH];
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, localappdata_path))) {
        return std::filesystem::path();
    }
    return std::filesystem::path(localappdata_path);
}

std::filesystem::path GetGlobalShadersMarkerFilePathNoCreate() {
    std::filesystem::path dc_root = GetDisplayCommanderAppDataRootPathNoCreate();
    if (dc_root.empty()) {
        return std::filesystem::path();
    }
    return dc_root / L".GLOBAL_SHADERS";
}

bool IsGlobalShadersEnabled() {
    std::filesystem::path marker_path = GetGlobalShadersMarkerFilePathNoCreate();
    if (marker_path.empty()) {
        return false;
    }

    std::error_code ec;
    return std::filesystem::is_regular_file(marker_path, ec) && !ec;
}

bool SetGlobalShadersEnabled(bool enabled) {
    std::filesystem::path marker_path = GetGlobalShadersMarkerFilePathNoCreate();
    if (enabled) {
        std::filesystem::path dc_root = GetDisplayCommanderAppDataFolder();
        if (dc_root.empty()) {
            LogWarn("Failed to enable global shaders/textures paths: Display Commander app data folder is unavailable");
            return false;
        }
        marker_path = dc_root / L".GLOBAL_SHADERS";

        std::error_code ec;
        std::filesystem::create_directories(dc_root, ec);
        if (ec) {
            LogWarn("Failed to enable global shaders/textures paths: cannot create directory %ls (error: %s)",
                    dc_root.c_str(), ec.message().c_str());
            return false;
        }

        std::ofstream marker_file(marker_path, std::ios::out | std::ios::trunc);
        if (!marker_file.good()) {
            LogWarn("Failed to enable global shaders/textures paths: cannot create marker file %ls", marker_path.c_str());
            return false;
        }
        marker_file.close();
        return true;
    }

    if (marker_path.empty()) {
        return true;
    }

    std::error_code ec;
    std::filesystem::remove(marker_path, ec);
    if (ec) {
        LogWarn("Failed to disable global shaders/textures paths: cannot remove marker file %ls (error: %s)",
                marker_path.c_str(), ec.message().c_str());
        return false;
    }
    return true;
}

std::filesystem::path GetDcConfigGlobalMarkerPathNoCreate() {
    std::filesystem::path dc_root = GetDisplayCommanderAppDataRootPathNoCreate();
    if (dc_root.empty()) return {};
    return dc_root / L".DC_CONFIG_GLOBAL";
}

bool IsPerGameFoldersEnabled() {
    std::filesystem::path marker_path = GetDcConfigGlobalMarkerPathNoCreate();
    if (marker_path.empty()) return false;
    std::error_code ec;
    return std::filesystem::is_regular_file(marker_path, ec) && !ec;
}

bool SetPerGameFoldersEnabled(bool enabled) {
    std::filesystem::path marker_path = GetDcConfigGlobalMarkerPathNoCreate();
    if (enabled) {
        std::filesystem::path dc_root = GetDisplayCommanderAppDataFolder();
        if (dc_root.empty()) return false;
        marker_path = dc_root / L".DC_CONFIG_GLOBAL";
        std::error_code ec;
        std::filesystem::create_directories(dc_root, ec);
        if (ec) return false;
        std::ofstream marker_file(marker_path, std::ios::out | std::ios::trunc);
        return marker_file.good();
    }
    if (marker_path.empty()) return true;
    std::error_code ec;
    std::filesystem::remove(marker_path, ec);
    return !ec;
}

std::filesystem::path GetScreenshotPathMarkerFilePathNoCreate() {
    std::filesystem::path dc_root = GetDisplayCommanderAppDataRootPathNoCreate();
    if (dc_root.empty()) {
        return std::filesystem::path();
    }
    return dc_root / L".SCREENSHOT_PATH";
}

bool IsScreenshotPathEnabled() {
    std::filesystem::path marker_path = GetScreenshotPathMarkerFilePathNoCreate();
    if (marker_path.empty()) {
        return false;
    }

    std::error_code ec;
    return std::filesystem::is_regular_file(marker_path, ec) && !ec;
}

bool SetScreenshotPathEnabled(bool enabled) {
    std::filesystem::path marker_path = GetScreenshotPathMarkerFilePathNoCreate();
    if (enabled) {
        std::filesystem::path dc_root = GetDisplayCommanderAppDataFolder();
        if (dc_root.empty()) {
            return false;
        }
        marker_path = dc_root / L".SCREENSHOT_PATH";
        std::error_code ec;
        std::filesystem::create_directories(dc_root, ec);
        if (ec) {
            return false;
        }
        std::ofstream marker_file(marker_path, std::ios::out | std::ios::trunc);
        return marker_file.good();
    }

    if (marker_path.empty()) {
        return true;
    }

    std::error_code ec;
    std::filesystem::remove(marker_path, ec);
    return !ec;
}

std::filesystem::path GetCurrentGameFolderGlobal() {
    std::filesystem::path dc_root = GetDisplayCommanderAppDataFolder();
    if (dc_root.empty()) return {};
    std::string game_name = GetGameNameFromProcess();
    if (game_name.empty()) game_name = "Game";
    return dc_root / L"Games" / std::filesystem::path(game_name);
}

std::filesystem::path GetCurrentGameFolderLocal() {
    std::error_code ec;
    return std::filesystem::current_path(ec);
}

void OpenOrCreateFolder(const std::filesystem::path& folder, const char* log_name) {
    if (folder.empty()) return;
    std::error_code ec;
    std::filesystem::create_directories(folder, ec);
    std::string folder_str = folder.string();
    HINSTANCE result = ShellExecuteA(nullptr, "explore", folder_str.c_str(), nullptr, nullptr, SW_SHOW);
    if (reinterpret_cast<intptr_t>(result) <= 32) {
        LogError("Failed to open %s folder: %s (Error: %ld)", log_name, folder_str.c_str(),
                 reinterpret_cast<intptr_t>(result));
    } else {
        LogInfo("Opened %s folder: %s", log_name, folder_str.c_str());
    }
}

// Get the Reshade directory path (where reshade64.dll/reshade32.dll are located).
// Search order: DC config dir first, then exe dir; also Global/SpecificVersion per settings.
std::filesystem::path GetReshadeDirectory() { return display_commander::utils::GetReshadeDirectoryForLoading(); }

// Convert full path to path relative to LocalAppData (masks username)
// Example: "C:\Users\Piotr\AppData\Local\Programs\Display_Commander\Reshade" -> "%localappdata%\\Programs\\Display
// Commander\\Reshade"
std::string GetPathRelativeToDocuments(const std::filesystem::path& full_path) {
    std::filesystem::path localappdata_dir = GetDocumentsDirectory();
    if (localappdata_dir.empty()) {
        return full_path.string();
    }

    // Convert to strings and normalize path separators to backslashes for Windows
    std::string full_str = full_path.string();
    std::string localappdata_str = localappdata_dir.string();
    std::replace(full_str.begin(), full_str.end(), '/', '\\');
    std::replace(localappdata_str.begin(), localappdata_str.end(), '/', '\\');

    // Check if full_path is exactly LocalAppData directory
    if (full_str == localappdata_str) {
        return "%localappdata%";
    }

    // Check if full_path is within LocalAppData directory
    if (full_str.length() > localappdata_str.length()) {
        // Check if it starts with localappdata_str followed by a path separator
        if (full_str.substr(0, localappdata_str.length()) == localappdata_str
            && (full_str[localappdata_str.length()] == '\\')) {
            // Remove the localappdata_dir part and the leading path separator
            std::string relative = full_str.substr(localappdata_str.length() + 1);
            // Prepend "%localappdata%\\" to maintain clarity
            return "%localappdata%\\" + relative;
        }
    }

    // Path is not under LocalAppData, return original
    return full_path.string();
}

// Check if Reshade64.dll exists in the resolved ReShade directory (from load source setting).
bool Reshade64DllExists() {
    std::filesystem::path reshade_dir = GetReshadeDirectory();
    if (reshade_dir.empty()) {
        return false;
    }
    return std::filesystem::exists(reshade_dir / L"Reshade64.dll");
}

// Check if Reshade32.dll exists in the resolved ReShade directory.
bool Reshade32DllExists() {
    std::filesystem::path reshade_dir = GetReshadeDirectory();
    if (reshade_dir.empty()) {
        return false;
    }
    return std::filesystem::exists(reshade_dir / L"Reshade32.dll");
}

// Get Reshade64.dll version from the resolved ReShade directory.
std::string GetReshade64Version() {
    std::filesystem::path reshade_dir = GetReshadeDirectory();
    if (reshade_dir.empty()) {
        return "";
    }
    std::filesystem::path reshade64_path = reshade_dir / L"Reshade64.dll";
    if (!std::filesystem::exists(reshade64_path)) {
        return "";
    }
    return GetDLLVersionString(reshade64_path.wstring());
}

// Get Reshade32.dll version from the resolved ReShade directory.
std::string GetReshade32Version() {
    std::filesystem::path reshade_dir = GetReshadeDirectory();
    if (reshade_dir.empty()) {
        return "";
    }
    std::filesystem::path reshade32_path = reshade_dir / L"Reshade32.dll";
    if (!std::filesystem::exists(reshade32_path)) {
        return "";
    }
    return GetDLLVersionString(reshade32_path.wstring());
}

// Find all currently loaded ReShade modules by checking for ReShadeRegisterAddon export
std::vector<std::pair<std::string, std::string>> GetLoadedReShadeVersions() {
    std::vector<std::pair<std::string, std::string>> results;  // {module_path, version}

    HMODULE modules[1024];
    DWORD num_modules = 0;
    HANDLE process = GetCurrentProcess();

    if (K32EnumProcessModules(process, modules, sizeof(modules), &num_modules) == 0) {
        return results;
    }

    DWORD module_count = num_modules / sizeof(HMODULE);
    for (DWORD i = 0; i < module_count; i++) {
        // Check if this module has the ReShadeRegisterAddon export (used by addons)
        FARPROC register_func = GetProcAddress(modules[i], "ReShadeRegisterAddon");
        if (register_func == nullptr) {
            continue;
        }

        // This is a ReShade module, get its path and version
        wchar_t module_path[MAX_PATH];
        if (GetModuleFileNameW(modules[i], module_path, MAX_PATH) == 0) {
            continue;
        }

        std::string version = GetDLLVersionString(module_path);
        if (version.empty() || version == "Unknown") {
            version = "Unknown";
        }

        std::string module_path_str;
        int size_needed = WideCharToMultiByte(CP_UTF8, 0, module_path, -1, nullptr, 0, nullptr, nullptr);
        if (size_needed > 0) {
            module_path_str.resize(size_needed - 1);
            WideCharToMultiByte(CP_UTF8, 0, module_path, -1, module_path_str.data(), size_needed, nullptr, nullptr);
        } else {
            module_path_str = "(path unavailable)";
        }

        results.push_back({module_path_str, version});
    }

    return results;
}

// Get enabled addons from DisplayCommander config (whitelist approach)
std::vector<std::string> GetEnabledAddons() {
    std::vector<std::string> enabled_addons;

    // Read EnabledAddons from DisplayCommander config
    display_commander::config::get_config_value("ADDONS", "EnabledAddons", enabled_addons);

    return enabled_addons;
}

// Set enabled addons in DisplayCommander config
void SetEnabledAddons(const std::vector<std::string>& enabled_addons) {
    display_commander::config::set_config_value("ADDONS", "EnabledAddons", enabled_addons);
    display_commander::config::save_config("Addon enabled state changed");
}

void SetAddonDownloadUrls(const std::vector<std::string>& urls) {
    display_commander::config::set_config_value("ADDONS", "AddonDownloadUrls", urls);
    display_commander::config::save_config("Addon download URLs changed");
}

std::vector<std::string> SerializeAddonDownloadEtags() {
    std::vector<std::string> entries;
    entries.reserve(g_addon_download_etags.size());
    for (const auto& [url, etag] : g_addon_download_etags) {
        if (url.empty() || etag.empty()) {
            continue;
        }
        entries.push_back(url + "\t" + etag);
    }
    return entries;
}

void SaveAddonDownloadEtags() {
    display_commander::config::set_config_value("ADDONS", "AddonDownloadEtags", SerializeAddonDownloadEtags());
    display_commander::config::save_config("Addon download ETags changed");
}

void EnsureAddonDownloadUrlsLoaded() {
    if (g_addon_download_urls_loaded.load()) {
        return;
    }

    std::vector<std::string> urls;
    if (!display_commander::config::get_config_value("ADDONS", "AddonDownloadUrls", urls)) {
        display_commander::config::set_config_value("ADDONS", "AddonDownloadUrls", std::vector<std::string>());
        display_commander::config::save_config("Initialize addon download URLs");
        urls.clear();
    }

    std::vector<std::string> etag_entries;
    if (display_commander::config::get_config_value("ADDONS", "AddonDownloadEtags", etag_entries)) {
        g_addon_download_etags.clear();
        for (const auto& entry : etag_entries) {
            const size_t tab_pos = entry.find('\t');
            if (tab_pos == std::string::npos) {
                continue;
            }
            const std::string entry_url = entry.substr(0, tab_pos);
            const std::string entry_etag = entry.substr(tab_pos + 1);
            if (!entry_url.empty() && !entry_etag.empty()) {
                g_addon_download_etags[entry_url] = entry_etag;
            }
        }
    }

    g_addon_download_urls = urls;
    g_addon_download_urls_loaded.store(true);
}

bool ParseDownloadUrl(const std::string& url, std::wstring& host, std::wstring& path, INTERNET_PORT& port, bool& is_https,
                      bool include_extra_info = true) {
    if (url.empty()) {
        return false;
    }

    const std::wstring wide_url = display_commander::utils::Utf8ToWide(url);
    if (wide_url.empty()) {
        return false;
    }

    URL_COMPONENTS components = {};
    components.dwStructSize = sizeof(components);
    components.dwHostNameLength = static_cast<DWORD>(-1);
    components.dwUrlPathLength = static_cast<DWORD>(-1);
    components.dwExtraInfoLength = static_cast<DWORD>(-1);
    components.dwSchemeLength = static_cast<DWORD>(-1);

    if (!WinHttpCrackUrl(wide_url.c_str(), 0, 0, &components)) {
        return false;
    }

    if (components.nScheme != INTERNET_SCHEME_HTTP && components.nScheme != INTERNET_SCHEME_HTTPS) {
        return false;
    }

    host.assign(components.lpszHostName, components.dwHostNameLength);
    path.assign(components.lpszUrlPath, components.dwUrlPathLength);
    if (include_extra_info && components.dwExtraInfoLength > 0 && components.lpszExtraInfo != nullptr) {
        path.append(components.lpszExtraInfo, components.dwExtraInfoLength);
    }
    if (path.empty()) {
        path = L"/";
    }

    port = components.nPort;
    is_https = (components.nScheme == INTERNET_SCHEME_HTTPS);
    return !host.empty();
}

std::string QueryHeaderString(HINTERNET request, DWORD query_flag) {
    DWORD size_bytes = 0;
    if (!WinHttpQueryHeaders(request, query_flag, WINHTTP_HEADER_NAME_BY_INDEX, WINHTTP_NO_OUTPUT_BUFFER, &size_bytes,
                             WINHTTP_NO_HEADER_INDEX)) {
        if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || size_bytes == 0) {
            return std::string();
        }
    }

    std::wstring header_buffer(size_bytes / sizeof(wchar_t), L'\0');
    if (!WinHttpQueryHeaders(request, query_flag, WINHTTP_HEADER_NAME_BY_INDEX, header_buffer.data(), &size_bytes,
                             WINHTTP_NO_HEADER_INDEX)) {
        return std::string();
    }

    if (!header_buffer.empty() && header_buffer.back() == L'\0') {
        header_buffer.pop_back();
    }
    return display_commander::utils::WideToUtf8(header_buffer);
}

bool DownloadAddonFromUrl(const std::string& url, const std::filesystem::path& destination_path, std::string& error_out,
                          std::string* downloaded_etag_out = nullptr) {
    std::wstring host;
    std::wstring path;
    INTERNET_PORT port = INTERNET_DEFAULT_HTTPS_PORT;
    bool is_https = true;
    if (!ParseDownloadUrl(url, host, path, port, is_https)) {
        error_out = "Invalid URL. Use full http:// or https:// URL.";
        return false;
    }

    HINTERNET session =
        WinHttpOpen(L"DisplayCommander/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME,
                    WINHTTP_NO_PROXY_BYPASS, 0);
    if (session == nullptr) {
        error_out = "Failed to open WinHTTP session.";
        return false;
    }

    HINTERNET connection = WinHttpConnect(session, host.c_str(), port, 0);
    if (connection == nullptr) {
        WinHttpCloseHandle(session);
        error_out = "Failed to connect to host.";
        return false;
    }

    const DWORD request_flags = is_https ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET request = WinHttpOpenRequest(connection, L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER,
                                           WINHTTP_DEFAULT_ACCEPT_TYPES, request_flags);
    if (request == nullptr) {
        WinHttpCloseHandle(connection);
        WinHttpCloseHandle(session);
        error_out = "Failed to create HTTP request.";
        return false;
    }

    bool ok = WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0)
              && WinHttpReceiveResponse(request, nullptr);
    if (!ok) {
        WinHttpCloseHandle(request);
        WinHttpCloseHandle(connection);
        WinHttpCloseHandle(session);
        error_out = "HTTP request failed.";
        return false;
    }

    DWORD status_code = 0;
    DWORD status_code_size = sizeof(status_code);
    if (!WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                             &status_code, &status_code_size, WINHTTP_NO_HEADER_INDEX)) {
        status_code = 0;
    }
    if (status_code < 200 || status_code >= 300) {
        WinHttpCloseHandle(request);
        WinHttpCloseHandle(connection);
        WinHttpCloseHandle(session);
        error_out = "HTTP server returned status " + std::to_string(status_code) + ".";
        return false;
    }

    if (downloaded_etag_out != nullptr) {
        *downloaded_etag_out = QueryHeaderString(request, WINHTTP_QUERY_ETAG);
    }

    std::vector<uint8_t> response_data;
    while (true) {
        DWORD bytes_available = 0;
        if (!WinHttpQueryDataAvailable(request, &bytes_available)) {
            error_out = "Failed while reading HTTP response.";
            WinHttpCloseHandle(request);
            WinHttpCloseHandle(connection);
            WinHttpCloseHandle(session);
            return false;
        }
        if (bytes_available == 0) {
            break;
        }

        std::vector<uint8_t> chunk(bytes_available);
        DWORD bytes_read = 0;
        if (!WinHttpReadData(request, chunk.data(), bytes_available, &bytes_read)) {
            error_out = "Failed while downloading response body.";
            WinHttpCloseHandle(request);
            WinHttpCloseHandle(connection);
            WinHttpCloseHandle(session);
            return false;
        }
        response_data.insert(response_data.end(), chunk.begin(), chunk.begin() + bytes_read);
    }

    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connection);
    WinHttpCloseHandle(session);

    if (response_data.empty()) {
        error_out = "Download completed with empty file.";
        return false;
    }

    std::error_code ec;
    std::filesystem::create_directories(destination_path.parent_path(), ec);
    if (ec) {
        error_out = "Failed to create destination directory: " + ec.message();
        return false;
    }

    std::ofstream out_file(destination_path, std::ios::binary | std::ios::trunc);
    if (!out_file.good()) {
        error_out = "Failed to create destination file.";
        return false;
    }
    out_file.write(reinterpret_cast<const char*>(response_data.data()), static_cast<std::streamsize>(response_data.size()));
    if (!out_file.good()) {
        error_out = "Failed to write destination file.";
        return false;
    }

    return true;
}

bool CheckAddonUrlForUpdate(const std::string& url, std::string& status_message) {
    const std::string trimmed_url = display_commander::utils::TrimAsciiWhitespace(url);
    if (trimmed_url.empty()) {
        status_message = "URL is empty.";
        return false;
    }

    std::wstring host;
    std::wstring path;
    INTERNET_PORT port = INTERNET_DEFAULT_HTTPS_PORT;
    bool is_https = true;
    if (!ParseDownloadUrl(trimmed_url, host, path, port, is_https)) {
        status_message = "Invalid URL.";
        return false;
    }

    HINTERNET session =
        WinHttpOpen(L"DisplayCommander/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME,
                    WINHTTP_NO_PROXY_BYPASS, 0);
    if (session == nullptr) {
        status_message = "Check failed: cannot open WinHTTP session.";
        return false;
    }
    HINTERNET connection = WinHttpConnect(session, host.c_str(), port, 0);
    if (connection == nullptr) {
        WinHttpCloseHandle(session);
        status_message = "Check failed: cannot connect to host.";
        return false;
    }
    const DWORD request_flags = is_https ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET request = WinHttpOpenRequest(connection, L"HEAD", path.c_str(), nullptr, WINHTTP_NO_REFERER,
                                           WINHTTP_DEFAULT_ACCEPT_TYPES, request_flags);
    if (request == nullptr) {
        WinHttpCloseHandle(connection);
        WinHttpCloseHandle(session);
        status_message = "Check failed: cannot create request.";
        return false;
    }

    const bool ok = WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0)
                    && WinHttpReceiveResponse(request, nullptr);
    if (!ok) {
        WinHttpCloseHandle(request);
        WinHttpCloseHandle(connection);
        WinHttpCloseHandle(session);
        status_message = "Check failed: request error.";
        return false;
    }

    DWORD status_code = 0;
    DWORD status_code_size = sizeof(status_code);
    WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                        &status_code, &status_code_size, WINHTTP_NO_HEADER_INDEX);
    if (status_code < 200 || status_code >= 300) {
        WinHttpCloseHandle(request);
        WinHttpCloseHandle(connection);
        WinHttpCloseHandle(session);
        status_message = "Check failed: HTTP status " + std::to_string(status_code) + ".";
        return false;
    }

    const std::string remote_etag = QueryHeaderString(request, WINHTTP_QUERY_ETAG);

    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connection);
    WinHttpCloseHandle(session);

    if (remote_etag.empty()) {
        status_message = "No ETag exposed by server. Cannot compare versions.";
        return true;
    }

    const auto it = g_addon_download_etags.find(trimmed_url);
    if (it == g_addon_download_etags.end() || it->second.empty()) {
        status_message = "Remote ETag found, but no local ETag saved yet. Download once to start tracking updates.";
        return true;
    }

    if (it->second == remote_etag) {
        status_message = "Up to date (ETag unchanged).";
        return true;
    }

    status_message = "Update available (ETag changed).";
    return true;
}

bool IsExpectedAddonExtension(const std::filesystem::path& addon_path) {
#ifdef _WIN64
    return addon_path.extension() == L".addon64";
#else
    return addon_path.extension() == L".addon32";
#endif
}

bool DownloadAndInstallAddon(const std::string& url, std::string& status_message) {
    const std::string trimmed_url = display_commander::utils::TrimAsciiWhitespace(url);
    if (trimmed_url.empty()) {
        status_message = "URL is empty.";
        return false;
    }

    std::wstring host;
    std::wstring path;
    INTERNET_PORT port = INTERNET_DEFAULT_HTTPS_PORT;
    bool is_https = true;
    if (!ParseDownloadUrl(trimmed_url, host, path, port, is_https, false)) {
        status_message = "Invalid URL. Use full http:// or https:// URL.";
        return false;
    }

    std::filesystem::path file_name = std::filesystem::path(path).filename();
    if (file_name.empty()) {
        status_message = "Could not derive file name from URL.";
        return false;
    }
    if (!IsExpectedAddonExtension(file_name)) {
#ifdef _WIN64
        status_message = "Downloaded file must end with .addon64 on this build.";
#else
        status_message = "Downloaded file must end with .addon32 on this build.";
#endif
        return false;
    }

    std::filesystem::path addons_dir = GetDisplayCommanderAddonsFolder();
    if (addons_dir.empty()) {
        status_message = "Addons directory is unavailable.";
        return false;
    }
    const std::filesystem::path destination_path = addons_dir / file_name;

    std::string error_message;
    std::string downloaded_etag;
    if (!DownloadAddonFromUrl(trimmed_url, destination_path, error_message, &downloaded_etag)) {
        status_message = "Download failed: " + error_message;
        return false;
    }

    const std::string downloaded_file_name = destination_path.filename().string();
    const std::string downloaded_name = destination_path.stem().string();
    SetAddonEnabled(downloaded_name, downloaded_file_name, true);
    g_show_addon_restart_warning.store(true);
    g_addon_list_dirty.store(true);
    if (!downloaded_etag.empty()) {
        g_addon_download_etags[trimmed_url] = downloaded_etag;
        SaveAddonDownloadEtags();
    }

    status_message = "Downloaded and enabled: " + downloaded_file_name;
    return true;
}

// Check if an addon is enabled (whitelist approach - default is disabled)
bool IsAddonEnabled(const std::string& addon_name, const std::string& addon_file) {
    std::vector<std::string> enabled = GetEnabledAddons();

    // Create identifier in format "name@file"
    std::string identifier = addon_name + "@" + addon_file;

    // Check if this addon is in the enabled list
    for (const auto& enabled_entry : enabled) {
        if (enabled_entry == identifier) {
            return true;
        }
    }

    return false;
}

// Enable or disable an addon
void SetAddonEnabled(const std::string& addon_name, const std::string& addon_file, bool enabled) {
    std::vector<std::string> enabled_list = GetEnabledAddons();

    // Create identifier in format "name@file"
    std::string identifier = addon_name + "@" + addon_file;

    // Remove existing entry if present
    enabled_list.erase(std::remove_if(enabled_list.begin(), enabled_list.end(),
                                      [&](const std::string& entry) { return entry == identifier; }),
                       enabled_list.end());

    // Add to enabled list if enabling
    if (enabled) {
        enabled_list.push_back(identifier);
    }

    SetEnabledAddons(enabled_list);
    g_addon_list_dirty.store(true);
}

// Scan for addon files in the global directory
void ScanGlobalAddonsDirectory(std::vector<AddonInfo>& addons) {
    std::filesystem::path addons_dir = GetGlobalAddonsDirectory();

    if (!std::filesystem::exists(addons_dir)) {
        return;
    }

    try {
        std::error_code ec;
        for (const auto& entry : std::filesystem::directory_iterator(
                 addons_dir, std::filesystem::directory_options::skip_permission_denied, ec)) {
            if (ec) {
                continue;
            }

            if (!entry.is_regular_file()) {
                continue;
            }

            const auto& path = entry.path();
            const auto extension = path.extension();

            // Check for .addon64 (64-bit) or .addon32 (32-bit) extensions
            if (extension != L".addon64" && extension != L".addon32") {
                continue;
            }

            // Only include architecture-appropriate addons
#ifdef _WIN64
            if (extension != L".addon64") {
                continue;
            }
#else
            if (extension != L".addon32") {
                continue;
            }
#endif

            AddonInfo info;
            info.file_path = path.string();
            info.file_name = path.filename().string();
            info.name = path.stem().string();  // Name without extension
            info.description = "External addon";
            info.author = "Unknown";
            info.is_external = true;
            info.is_loaded = false;  // We'll check this against ReShade's list
            info.is_enabled = IsAddonEnabled(info.name, info.file_name);

            addons.push_back(info);
        }
    } catch (const std::exception& e) {
        LogWarn("Exception while scanning addons directory: %s", e.what());
    }
}

// Merge with ReShade's loaded addon info
void MergeReShadeAddonInfo(std::vector<AddonInfo>& addons) {
    // Try to access ReShade's addon_loaded_info
    // Note: This requires accessing ReShade's internal state, which may not be directly accessible
    // For now, we'll mark addons as loaded if they exist in the directory and are not disabled

    // We can't directly access reshade::addon_loaded_info from here as it's in ReShade's namespace
    // So we'll infer loaded status from whether the file exists and is not disabled
    for (auto& addon : addons) {
        if (std::filesystem::exists(addon.file_path) && addon.is_enabled) {
            // Check if the module is loaded by trying to get its handle
            // This is a heuristic - the addon might be loaded by ReShade
            addon.is_loaded = true;  // Optimistic assumption
        }
    }
}

// Refresh the addon list
void RefreshAddonListInternal() {
    g_addon_list.clear();

    // Scan global addons directory
    ScanGlobalAddonsDirectory(g_addon_list);

    // Merge with ReShade's info
    MergeReShadeAddonInfo(g_addon_list);

    // Sort by name
    std::sort(g_addon_list.begin(), g_addon_list.end(),
              [](const AddonInfo& a, const AddonInfo& b) { return a.name < b.name; });
}

}  // namespace

void InitAddonsTab() {
    // Initial refresh
    RefreshAddonListInternal();
    // Reset warning flag on initialization (game restart)
    g_show_addon_restart_warning.store(false);
}

void RefreshAddonList() { g_addon_list_dirty.store(true); }

void DrawAddonsHeader(display_commander::ui::IImGuiWrapper& imgui) {
    // Addons Subsection
    if (imgui.CollapsingHeader("Addons", display_commander::ui::wrapper_flags::TreeNodeFlags_None)) {
        imgui.Spacing();

        // Check if we need to refresh
        if (g_addon_list_dirty.load()) {
            RefreshAddonListInternal();
            g_addon_list_dirty.store(false);
        }

        // Refresh button
        ui::colors::PushIconColor(&imgui, ui::colors::ICON_ACTION);
        if (imgui.Button(ICON_FK_REFRESH " Refresh")) {
            RefreshAddonList();
        }
        ui::colors::PopIconColor(&imgui);
        if (imgui.IsItemHovered()) {
            imgui.SetTooltipEx("Refresh the list of available addons");
        }

        imgui.SameLine();

        // Enable All button
        ui::colors::PushIconColor(&imgui, ui::colors::ICON_ACTION);
        if (imgui.Button(ICON_FK_OK " Enable All")) {
            std::vector<std::string> enabled_list;
            for (const auto& addon : g_addon_list) {
                std::string identifier = addon.name + "@" + addon.file_name;
                enabled_list.push_back(identifier);
            }
            SetEnabledAddons(enabled_list);
            // Update local state
            for (auto& addon : g_addon_list) {
                addon.is_enabled = true;
            }
            g_addon_list_dirty.store(true);
            g_show_addon_restart_warning.store(true);
        }
        ui::colors::PopIconColor(&imgui);
        if (imgui.IsItemHovered()) {
            imgui.SetTooltipEx("Enable all addons");
        }

        imgui.SameLine();

        // Disable All button
        ui::colors::PushIconColor(&imgui, ui::colors::ICON_ACTION);
        if (imgui.Button(ICON_FK_CANCEL " Disable All")) {
            SetEnabledAddons(std::vector<std::string>());
            // Update local state
            for (auto& addon : g_addon_list) {
                addon.is_enabled = false;
            }
            g_addon_list_dirty.store(true);
            g_show_addon_restart_warning.store(true);
        }
        ui::colors::PopIconColor(&imgui);
        if (imgui.IsItemHovered()) {
            imgui.SetTooltipEx("Disable all addons");
        }

        imgui.SameLine();

        // Open Addons Folder button
        ui::colors::PushIconColor(&imgui, ui::colors::ICON_ACTION);
        if (imgui.Button(ICON_FK_FOLDER_OPEN " Open Addons Folder")) {
            std::filesystem::path addons_dir = GetGlobalAddonsDirectory();

            // Create directory if it doesn't exist
            if (!std::filesystem::exists(addons_dir)) {
                try {
                    std::filesystem::create_directories(addons_dir);
                } catch (const std::exception& e) {
                    LogError("Failed to create addons directory: %s", e.what());
                }
            }

            if (std::filesystem::exists(addons_dir)) {
                std::string addons_dir_str = addons_dir.string();
                HINSTANCE result = ShellExecuteA(nullptr, "explore", addons_dir_str.c_str(), nullptr, nullptr, SW_SHOW);

                if (reinterpret_cast<intptr_t>(result) <= 32) {
                    LogError("Failed to open addons folder: %s (Error: %ld)", addons_dir_str.c_str(),
                             reinterpret_cast<intptr_t>(result));
                } else {
                    LogInfo("Opened addons folder: %s", addons_dir_str.c_str());
                }
            }
        }
        ui::colors::PopIconColor(&imgui);
        if (imgui.IsItemHovered()) {
            imgui.SetTooltipEx("Open the global addons directory in Windows Explorer");
        }

        imgui.Spacing();
        imgui.Separator();
        imgui.Spacing();

        EnsureAddonDownloadUrlsLoaded();

        imgui.TextColored(ui::colors::TEXT_DEFAULT, "Addon URL Downloads");
        if (imgui.IsItemHovered()) {
            imgui.SetTooltipEx("Add addon URLs and download them directly into the global Addons folder.");
        }

        if (imgui.Button("+ Add URL")) {
            g_addon_download_urls.emplace_back();
            SetAddonDownloadUrls(g_addon_download_urls);
        }
        if (imgui.IsItemHovered()) {
            imgui.SetTooltipEx("Add a new URL row");
        }
        imgui.SameLine();
        if (imgui.Button("Check All URLs")) {
            size_t up_to_date_count = 0;
            size_t update_available_count = 0;
            size_t other_count = 0;
            for (const auto& url : g_addon_download_urls) {
                std::string status_message;
                if (!CheckAddonUrlForUpdate(url, status_message)) {
                    ++other_count;
                    continue;
                }
                if (status_message.find("Update available") != std::string::npos) {
                    ++update_available_count;
                } else if (status_message.find("Up to date") != std::string::npos) {
                    ++up_to_date_count;
                } else {
                    ++other_count;
                }
            }
            g_addon_download_status = "Check all done. Updates: " + std::to_string(update_available_count)
                                      + ", up-to-date: " + std::to_string(up_to_date_count)
                                      + ", other: " + std::to_string(other_count) + ".";
        }
        if (imgui.IsItemHovered()) {
            imgui.SetTooltipEx("Check all URLs using remote ETag metadata.");
        }

        if (!g_addon_download_urls.empty()) {
            for (size_t i = 0; i < g_addon_download_urls.size(); ++i) {
                std::string label = "URL##AddonDownloadUrl" + std::to_string(i);
                std::vector<char> url_buffer(g_addon_download_urls[i].begin(), g_addon_download_urls[i].end());
                url_buffer.push_back('\0');
                if (url_buffer.size() < 2048) {
                    url_buffer.resize(2048, '\0');
                }

                if (imgui.InputText(label.c_str(), url_buffer.data(), url_buffer.size())) {
                    g_addon_download_urls[i] = url_buffer.data();
                    SetAddonDownloadUrls(g_addon_download_urls);
                }

                imgui.SameLine();
                std::string download_button = "Download##AddonDownload" + std::to_string(i);
                if (imgui.Button(download_button.c_str())) {
                    std::string status_message;
                    const bool success = DownloadAndInstallAddon(g_addon_download_urls[i], status_message);
                    g_addon_download_status = status_message;
                    if (success) {
                        RefreshAddonList();
                    }
                }

                imgui.SameLine();
                std::string check_button = "Check##AddonCheck" + std::to_string(i);
                if (imgui.Button(check_button.c_str())) {
                    std::string status_message;
                    CheckAddonUrlForUpdate(g_addon_download_urls[i], status_message);
                    g_addon_download_status = status_message;
                }

                imgui.SameLine();
                std::string remove_button = "-##AddonDownloadRemove" + std::to_string(i);
                if (imgui.Button(remove_button.c_str())) {
                    g_addon_download_urls.erase(g_addon_download_urls.begin() + static_cast<std::ptrdiff_t>(i));
                    SetAddonDownloadUrls(g_addon_download_urls);
                    break;
                }
                if (imgui.IsItemHovered()) {
                    imgui.SetTooltipEx("Remove this URL row");
                }
            }
        } else {
            imgui.TextColored(ui::colors::TEXT_DIMMED, "No addon URLs added yet.");
        }

        if (!g_addon_download_status.empty()) {
            imgui.TextWrapped("%s", g_addon_download_status.c_str());
        }

        imgui.Spacing();
        imgui.Separator();
        imgui.Spacing();

        // Display addon list
        if (g_addon_list.empty()) {
            imgui.TextColored(ui::colors::TEXT_DIMMED, "No addons found in global directory.");
            imgui.Spacing();
            imgui.TextWrapped("Addons should be placed in: %s",
                              GetPathRelativeToDocuments(GetGlobalAddonsDirectory()).c_str());
        } else {
            // Create table for addon list
            const int table_flags = display_commander::ui::wrapper_flags::TableFlags_Borders
                                    | display_commander::ui::wrapper_flags::TableFlags_RowBg
                                    | display_commander::ui::wrapper_flags::TableFlags_Resizable;
            if (imgui.BeginTable("AddonsTable", 4, table_flags)) {
                imgui.TableSetupColumn("Enabled", display_commander::ui::wrapper_flags::TableColumnFlags_WidthFixed,
                                       160.0f);
                imgui.TableSetupColumn("Name", display_commander::ui::wrapper_flags::TableColumnFlags_WidthStretch);
                imgui.TableSetupColumn("File", display_commander::ui::wrapper_flags::TableColumnFlags_WidthFixed,
                                       500.0f);
                imgui.TableSetupColumn("Actions",
                                       display_commander::ui::wrapper_flags::TableColumnFlags_WidthFixed, 100.0f);
                imgui.TableHeadersRow();

                for (size_t i = 0; i < g_addon_list.size(); ++i) {
                    auto& addon = g_addon_list[i];

                    imgui.TableNextRow();

                    // Enabled checkbox
                    imgui.TableNextColumn();
                    bool enabled = addon.is_enabled;
                    if (imgui.Checkbox(("##Enabled" + std::to_string(i)).c_str(), &enabled)) {
                        SetAddonEnabled(addon.name, addon.file_name, enabled);
                        addon.is_enabled = enabled;
                        g_addon_list_dirty.store(true);
                        g_show_addon_restart_warning.store(true);
                    }
                    if (imgui.IsItemHovered()) {
                        imgui.SetTooltipEx("%s this addon", enabled ? "Disable" : "Enable");
                    }

                    // Name
                    imgui.TableNextColumn();
                    imgui.Text("%s", addon.name.c_str());
                    if (!addon.description.empty() && imgui.IsItemHovered()) {
                        imgui.SetTooltipEx("%s", addon.description.c_str());
                    }

                    // File name
                    imgui.TableNextColumn();
                    imgui.TextColored(ui::colors::TEXT_DIMMED, "%s", addon.file_name.c_str());

                    // Actions (Open Folder button)
                    imgui.TableNextColumn();
                    std::string folder_button_id = "Folder##" + std::to_string(i);
                    if (imgui.Button(folder_button_id.c_str())) {
                        std::filesystem::path addon_path(addon.file_path);
                        std::filesystem::path folder_path = addon_path.parent_path();

                        if (std::filesystem::exists(folder_path)) {
                            std::string folder_str = folder_path.string();
                            HINSTANCE result =
                                ShellExecuteA(nullptr, "explore", folder_str.c_str(), nullptr, nullptr, SW_SHOW);

                            if (reinterpret_cast<intptr_t>(result) <= 32) {
                                LogError("Failed to open folder: %s (Error: %ld)", folder_str.c_str(),
                                         reinterpret_cast<intptr_t>(result));
                            } else {
                                LogInfo("Opened folder: %s", folder_str.c_str());
                            }
                        }
                    }
                    if (imgui.IsItemHovered()) {
                        imgui.SetTooltipEx("Open the folder containing this addon");
                    }
                }

                imgui.EndTable();
            }

            imgui.Spacing();
            imgui.Separator();
            imgui.Spacing();

            // Warning message for addon enable/disable
            if (g_show_addon_restart_warning.load()) {
                imgui.TextColored(ui::colors::TEXT_WARNING,
                                  ICON_FK_WARNING " Warning: Game restart required for addon changes to take effect.");
                imgui.Spacing();
            }

            // Info text
            imgui.TextColored(ui::colors::TEXT_DIMMED,
                              "Note: Addons are disabled by default. Enable addons to load them on next game restart.");
            imgui.TextColored(ui::colors::TEXT_DIMMED,
                              "Changes to addon enabled/disabled state require a game restart to take effect.");
            imgui.TextColored(ui::colors::TEXT_DIMMED, "Addons directory: %s",
                              GetPathRelativeToDocuments(GetGlobalAddonsDirectory()).c_str());
        }
    }

}

void DrawShadersHeader(display_commander::ui::IImGuiWrapper& imgui) {
    // Shaders Subsection
    if (imgui.CollapsingHeader("Shaders", display_commander::ui::wrapper_flags::TreeNodeFlags_None)) {
        bool global_shaders_enabled = IsGlobalShadersEnabled();
        if (imgui.Checkbox("Enable global shaders/textures paths", &global_shaders_enabled)) {
            if (!SetGlobalShadersEnabled(global_shaders_enabled)) {
                // Keep UI strictly in sync with marker-file existence.
                global_shaders_enabled = IsGlobalShadersEnabled();
            }
        }
        if (imgui.IsItemHovered()) {
            std::filesystem::path marker_path = GetGlobalShadersMarkerFilePathNoCreate();
            if (marker_path.empty()) {
                marker_path = std::filesystem::path(L"%localappdata%") / L"Programs" / L"Display_Commander"
                              / L".GLOBAL_SHADERS";
            }
            imgui.SetTooltipEx(
                "When enabled, Display Commander adds global Shaders/Textures paths to ReShade.\n"
                "State is controlled by: %s",
                GetPathRelativeToDocuments(marker_path).c_str());
        }

        imgui.Spacing();

        // Open Shaders Folder button
        ui::colors::PushIconColor(&imgui, ui::colors::ICON_ACTION);
        if (imgui.Button(ICON_FK_FOLDER_OPEN " Open Shaders Folder")) {
            std::filesystem::path shaders_dir = GetShadersDirectory();

            // Create directory if it doesn't exist
            if (!std::filesystem::exists(shaders_dir)) {
                try {
                    std::filesystem::create_directories(shaders_dir);
                } catch (const std::exception& e) {
                    LogError("Failed to create shaders directory: %s", e.what());
                }
            }

            if (std::filesystem::exists(shaders_dir)) {
                std::string shaders_dir_str = shaders_dir.string();
                HINSTANCE result =
                    ShellExecuteA(nullptr, "explore", shaders_dir_str.c_str(), nullptr, nullptr, SW_SHOW);

                if (reinterpret_cast<intptr_t>(result) <= 32) {
                    LogError("Failed to open shaders folder: %s (Error: %ld)", shaders_dir_str.c_str(),
                             reinterpret_cast<intptr_t>(result));
                } else {
                    LogInfo("Opened shaders folder: %s", shaders_dir_str.c_str());
                }
            }
        }
        ui::colors::PopIconColor(&imgui);
        if (imgui.IsItemHovered()) {
            imgui.SetTooltipEx("Open the shaders directory in Windows Explorer");
        }

        imgui.SameLine();

        // Open Textures Folder button
        ui::colors::PushIconColor(&imgui, ui::colors::ICON_ACTION);
        if (imgui.Button(ICON_FK_FOLDER_OPEN " Open Textures Folder")) {
            std::filesystem::path textures_dir = GetTexturesDirectory();

            // Create directory if it doesn't exist
            if (!std::filesystem::exists(textures_dir)) {
                try {
                    std::filesystem::create_directories(textures_dir);
                } catch (const std::exception& e) {
                    LogError("Failed to create textures directory: %s", e.what());
                }
            }

            if (std::filesystem::exists(textures_dir)) {
                std::string textures_dir_str = textures_dir.string();
                HINSTANCE result =
                    ShellExecuteA(nullptr, "explore", textures_dir_str.c_str(), nullptr, nullptr, SW_SHOW);

                if (reinterpret_cast<intptr_t>(result) <= 32) {
                    LogError("Failed to open textures folder: %s (Error: %ld)", textures_dir_str.c_str(),
                             reinterpret_cast<intptr_t>(result));
                } else {
                    LogInfo("Opened textures folder: %s", textures_dir_str.c_str());
                }
            }
        }
        ui::colors::PopIconColor(&imgui);
        if (imgui.IsItemHovered()) {
            imgui.SetTooltipEx("Open the textures directory in Windows Explorer");
        }
    }

}

void DrawReshadeDllHeader(display_commander::ui::IImGuiWrapper& imgui) {
    if (imgui.CollapsingHeader("Reshade DLL", display_commander::ui::wrapper_flags::TreeNodeFlags_None)) {
        // Global ReShade Subsection (only show if ReShade DLL exists in LocalAppData)
        bool reshade64_exists = Reshade64DllExists();
        bool reshade32_exists = Reshade32DllExists();

        if (reshade64_exists || reshade32_exists) {
            imgui.Spacing();
            imgui.Spacing();

            // Show status for each DLL with version
            if (reshade64_exists) {
                std::string version = GetReshade64Version();
                if (!version.empty() && version != "Unknown") {
                    imgui.TextColored(ui::colors::TEXT_SUCCESS, ICON_FK_OK " Reshade64.dll found (v%s)",
                                      version.c_str());
                } else {
                    imgui.TextColored(ui::colors::TEXT_SUCCESS, ICON_FK_OK " Reshade64.dll found");
                }
            }
            if (reshade32_exists) {
                std::string version = GetReshade32Version();
                if (!version.empty() && version != "Unknown") {
                    imgui.TextColored(ui::colors::TEXT_SUCCESS, ICON_FK_OK " Reshade32.dll found (v%s)",
                                      version.c_str());
                } else {
                    imgui.TextColored(ui::colors::TEXT_SUCCESS, ICON_FK_OK " Reshade32.dll found");
                }
            }

            // Show currently loaded ReShade versions (found by checking for ReShadeRegisterAddon export)
            std::vector<std::pair<std::string, std::string>> loaded_reshade = GetLoadedReShadeVersions();
            if (!loaded_reshade.empty()) {
                imgui.Spacing();
                imgui.TextColored(ui::colors::TEXT_DEFAULT, "Currently loaded ReShade modules:");
                imgui.Indent();
                for (const auto& [module_path, version] : loaded_reshade) {
                    std::filesystem::path path_obj(module_path);
                    std::string module_name = path_obj.filename().string();
                    imgui.TextColored(ui::colors::TEXT_DEFAULT, ICON_FK_OK " %s (v%s)", module_name.c_str(),
                                      version.c_str());
                    if (imgui.IsItemHovered()) {
                        std::filesystem::path module_path_obj(module_path);
                        imgui.SetTooltipEx("%s", GetPathRelativeToDocuments(module_path_obj).c_str());
                    }
                }
                imgui.Unindent();
            }

            imgui.Spacing();

            // Open Reshade Folder button
            ui::colors::PushIconColor(&imgui, ui::colors::ICON_ACTION);
            if (imgui.Button(ICON_FK_FOLDER_OPEN " Open Reshade Folder")) {
                std::filesystem::path reshade_dir = GetReshadeDirectory();

                // Create directory if it doesn't exist
                if (!std::filesystem::exists(reshade_dir)) {
                    try {
                        std::filesystem::create_directories(reshade_dir);
                    } catch (const std::exception& e) {
                        LogError("Failed to create Reshade directory: %s", e.what());
                    }
                }

                if (!reshade_dir.empty() && std::filesystem::exists(reshade_dir)) {
                    std::string reshade_dir_str = reshade_dir.string();
                    HINSTANCE result =
                        ShellExecuteA(nullptr, "explore", reshade_dir_str.c_str(), nullptr, nullptr, SW_SHOW);

                    if (reinterpret_cast<intptr_t>(result) <= 32) {
                        LogError("Failed to open Reshade folder: %s (Error: %ld)", reshade_dir_str.c_str(),
                                 reinterpret_cast<intptr_t>(result));
                    } else {
                        LogInfo("Opened Reshade folder: %s", reshade_dir_str.c_str());
                    }
                }
            }
            ui::colors::PopIconColor(&imgui);
            if (imgui.IsItemHovered()) {
                imgui.SetTooltipEx(
                    "Open the Reshade folder (containing reshade64.dll/reshade32.dll) in Windows Explorer");
            }
        }
    }
}

void DrawPerGameFoldersHeader(display_commander::ui::IImGuiWrapper& imgui) {
    if (!imgui.CollapsingHeader("Per game folders", display_commander::ui::wrapper_flags::TreeNodeFlags_None)) return;

    bool enabled = IsPerGameFoldersEnabled();
    if (imgui.Checkbox("Enable per game folders", &enabled)) {
        if (!SetPerGameFoldersEnabled(enabled)) enabled = IsPerGameFoldersEnabled();
    }
    if (imgui.IsItemHovered()) {
        std::filesystem::path marker_path = GetDcConfigGlobalMarkerPathNoCreate();
        if (marker_path.empty()) {
            marker_path = std::filesystem::path(L"%localappdata%") / L"Programs" / L"Display_Commander"
                          / L".DC_CONFIG_GLOBAL";
        }
        imgui.SetTooltipEx("Enabled only when marker exists: %s", GetPathRelativeToDocuments(marker_path).c_str());
    }

    imgui.Spacing();
    ui::colors::PushIconColor(&imgui, ui::colors::ICON_ACTION);
    if (imgui.Button(ICON_FK_FOLDER_OPEN " Open current game folder (global)")) {
        OpenOrCreateFolder(GetCurrentGameFolderGlobal(), "global current game");
    }
    ui::colors::PopIconColor(&imgui);
    if (imgui.IsItemHovered()) {
        imgui.SetTooltipEx("Open %%localappdata%%\\Programs\\Display_Commander\\Games\\<game_name>.");
    }

    ui::colors::PushIconColor(&imgui, ui::colors::ICON_ACTION);
    if (imgui.Button(ICON_FK_FOLDER_OPEN " Open current game folder (local)")) {
        OpenOrCreateFolder(GetCurrentGameFolderLocal(), "local current game");
    }
    ui::colors::PopIconColor(&imgui);
    if (imgui.IsItemHovered()) {
        imgui.SetTooltipEx("Open current working directory for this game process.");
    }
}

void DrawScreenshotsHeader(display_commander::ui::IImGuiWrapper& imgui) {
    if (!imgui.CollapsingHeader("Screenshots", display_commander::ui::wrapper_flags::TreeNodeFlags_None)) return;

    bool enabled = IsScreenshotPathEnabled();
    if (imgui.Checkbox("Add ./Screenshots path to ReShade settings", &enabled)) {
        if (!SetScreenshotPathEnabled(enabled)) {
            enabled = IsScreenshotPathEnabled();
        }
    }
    if (imgui.IsItemHovered()) {
        std::filesystem::path marker_path = GetScreenshotPathMarkerFilePathNoCreate();
        if (marker_path.empty()) {
            marker_path = std::filesystem::path(L"%localappdata%") / L"Programs" / L"Display_Commander"
                          / L".SCREENSHOT_PATH";
        }
        imgui.SetTooltipEx("Enabled only when marker exists: %s", GetPathRelativeToDocuments(marker_path).c_str());
    }

    imgui.Spacing();
    ui::colors::PushIconColor(&imgui, ui::colors::ICON_ACTION);
    if (imgui.Button(ICON_FK_FOLDER_OPEN " Open Screenshots Folder")) {
        OpenOrCreateFolder(GetCurrentGameFolderLocal() / L"Screenshots", "screenshots");
    }
    ui::colors::PopIconColor(&imgui);
    if (imgui.IsItemHovered()) {
        imgui.SetTooltipEx("Open current game screenshots folder: .\\Screenshots");
    }
}

void DrawAddonsTab(display_commander::ui::IImGuiWrapper& imgui) {
    DrawAddonsHeader(imgui);
    DrawShadersHeader(imgui);
    DrawScreenshotsHeader(imgui);
    DrawPerGameFoldersHeader(imgui);
    DrawReshadeDllHeader(imgui);
}

}  // namespace ui::new_ui
