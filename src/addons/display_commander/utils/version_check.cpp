#include "version_check.hpp"
#include "general_utils.hpp"
#include "safe_remove.hpp"

#include <ShlObj.h>
#include <Windows.h>
#include <WinInet.h>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <thread>
#include <vector>
#include "../version.hpp"

namespace display_commander::utils::version_check {

namespace {
// Return first three version components (X.Y.Z) from "X.Y.Z" or "X.Y.Z.Build" for use as stable folder name.
static std::string VersionStringToXyzFolder(const std::string& version_str) {
    std::string s = version_str;
    size_t dot_count = 0;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '.') {
            ++dot_count;
            if (dot_count == 3) {
                return s.substr(0, i);
            }
        }
    }
    return s;
}
// RAII wrapper for WinInet handles
struct ScopedInternetHandle {
    HINTERNET handle;
    ScopedInternetHandle(HINTERNET h) : handle(h) {}
    ~ScopedInternetHandle() {
        if (handle != nullptr) {
            InternetCloseHandle(handle);
        }
    }
    ScopedInternetHandle(const ScopedInternetHandle&) = delete;
    ScopedInternetHandle& operator=(const ScopedInternetHandle&) = delete;
    ScopedInternetHandle(ScopedInternetHandle&& other) noexcept : handle(other.handle) { other.handle = nullptr; }
    ScopedInternetHandle& operator=(ScopedInternetHandle&& other) noexcept {
        if (this != &other) {
            if (handle != nullptr) {
                InternetCloseHandle(handle);
            }
            handle = other.handle;
            other.handle = nullptr;
        }
        return *this;
    }
    operator HINTERNET() const { return handle; }
};

// Global version check state
VersionCheckState g_version_check_state;

// Set *out_error to a short description including Win32 error code when provided.
static void SetWinInetError(std::string* out_error, const char* step) {
    if (!out_error) return;
    DWORD err = GetLastError();
    char buf[64];
    snprintf(buf, sizeof(buf), "%s (error %lu)", step, static_cast<unsigned long>(err));
    *out_error = buf;
}

// Download text content from URL using WinInet. On failure, if out_error is non-null, sets a detailed message.
bool DownloadTextFromUrl(const std::string& url, std::string& content, std::string* out_error = nullptr) {
    content.clear();
    int url_len = MultiByteToWideChar(CP_UTF8, 0, url.c_str(), -1, nullptr, 0);
    if (url_len <= 0) {
        if (out_error) *out_error = "Invalid URL encoding";
        return false;
    }
    std::vector<wchar_t> url_wide(url_len);
    MultiByteToWideChar(CP_UTF8, 0, url.c_str(), -1, url_wide.data(), url_len);
    ScopedInternetHandle session =
        InternetOpenW(L"DisplayCommander", INTERNET_OPEN_TYPE_PRECONFIG, nullptr, nullptr, 0);
    if (session.handle == nullptr) {
        SetWinInetError(out_error, "InternetOpen failed");
        return false;
    }
    ScopedInternetHandle request =
        InternetOpenUrlW(session, url_wide.data(), nullptr, 0,
                         INTERNET_FLAG_RELOAD | INTERNET_FLAG_PRAGMA_NOCACHE | INTERNET_FLAG_NO_CACHE_WRITE, 0);
    if (request.handle == nullptr) {
        SetWinInetError(out_error, "Connection failed");
        return false;
    }
    DWORD timeout = 10000;
    InternetSetOption(request, INTERNET_OPTION_CONNECT_TIMEOUT, &timeout, sizeof(timeout));
    InternetSetOption(request, INTERNET_OPTION_RECEIVE_TIMEOUT, &timeout, sizeof(timeout));
    char buffer[4096];
    DWORD bytes_read = 0;
    while (InternetReadFile(request, buffer, sizeof(buffer) - 1, &bytes_read) && bytes_read > 0) {
        buffer[bytes_read] = '\0';
        content += buffer;
    }
    if (content.empty()) {
        SetWinInetError(out_error, "Empty response");
        return false;
    }
    return true;
}

// Fetch ReShade tags from GitHub API (crosire/reshade), filter >= 6.6.2, sort descending.
// Used by reshade_load_path for the version list. Runs in same thread (call once per app start).
bool FetchReShadeTagsFromGitHubImpl(std::vector<std::string>& out_versions, std::string* out_error) {
    out_versions.clear();
    std::string content;
    if (!DownloadTextFromUrl("https://api.github.com/repos/crosire/reshade/tags", content, out_error)) {
        if (out_error && !out_error->empty())
            *out_error = std::string("ReShade tags from GitHub: ") + *out_error;
        else if (out_error)
            *out_error = "Failed to fetch ReShade tags from GitHub";
        return false;
    }
    const std::string min_version("6.6.2");
    size_t pos = 0;
    while ((pos = content.find("\"name\"", pos)) != std::string::npos) {
        size_t colon = content.find(':', pos);
        if (colon == std::string::npos) {
            break;
        }
        size_t q1 = content.find('"', colon);
        if (q1 == std::string::npos) {
            break;
        }
        size_t q2 = content.find('"', q1 + 1);
        if (q2 == std::string::npos) {
            break;
        }
        std::string name = content.substr(q1 + 1, q2 - q1 - 1);
        std::string ver = display_commander::utils::version_check::ParseVersionString(name);
        if (ver.empty() || ver.find_first_not_of("0123456789.") != std::string::npos) {
            pos = q2 + 1;
            continue;
        }
        if (display_commander::utils::version_check::CompareVersions(ver, min_version) >= 0) {
            out_versions.push_back(ver);
        }
        pos = q2 + 1;
    }
    std::sort(out_versions.begin(), out_versions.end(), [](const std::string& a, const std::string& b) {
        return display_commander::utils::version_check::CompareVersions(a, b) > 0;
    });
    auto last = std::unique(out_versions.begin(), out_versions.end());
    out_versions.erase(last, out_versions.end());
    return !out_versions.empty();
}

// Parse latest ReShade version from reshade.me HTML (e.g. href="/downloads/ReShade_Setup_6.7.3.exe" or
// "ReShade 6.7.3"). Returns first X.Y.Z found that is >= 6.6.2, or empty.
static std::string ParseReShadeLatestFromReshadeMeHtml(const std::string& html) {
    const std::string prefix = "ReShade_Setup_";
    const std::string min_version("6.6.2");
    size_t pos = 0;
    while ((pos = html.find(prefix, pos)) != std::string::npos) {
        size_t start = pos + prefix.size();
        size_t end = start;
        while (end < html.size() && (std::isdigit(static_cast<unsigned char>(html[end])) || html[end] == '.')) {
            ++end;
        }
        if (end > start) {
            std::string ver = html.substr(start, end - start);
            while (!ver.empty() && ver.back() == '.') {
                ver.pop_back();
            }
            if (!ver.empty() && display_commander::utils::version_check::CompareVersions(ver, min_version) >= 0) {
                return ver;
            }
        }
        pos = start;
    }
    return "";
}

// Fetch once and cache. Called from public FetchReShadeLatestFromReshadeMe.
bool FetchReShadeLatestFromReshadeMeImpl(std::string* out_version, std::string* out_error) {
    static std::string s_cached;
    static bool s_done = false;
    if (s_done) {
        if (out_version) *out_version = s_cached;
        if (out_error && s_cached.empty()) *out_error = "No version from reshade.me";
        return !s_cached.empty();
    }
    std::string content;
    if (!DownloadTextFromUrl("https://reshade.me", content, out_error)) {
        s_done = true;
        if (out_error && !out_error->empty())
            *out_error = std::string("reshade.me: ") + *out_error;
        else if (out_error)
            *out_error = "Failed to fetch reshade.me";
        return false;
    }
    s_cached = ParseReShadeLatestFromReshadeMeHtml(content);
    s_done = true;
    if (out_version) *out_version = s_cached;
    if (out_error && s_cached.empty()) *out_error = "Could not parse version from reshade.me";
    return !s_cached.empty();
}

}  // namespace

// Download binary file from URL (public for ReShade update, etc.)
bool DownloadBinaryFromUrl(const std::string& url, const std::filesystem::path& file_path) {
    // Convert URL to wide string
    int url_len = MultiByteToWideChar(CP_UTF8, 0, url.c_str(), -1, nullptr, 0);
    if (url_len <= 0) {
        return false;
    }
    std::vector<wchar_t> url_wide(url_len);
    MultiByteToWideChar(CP_UTF8, 0, url.c_str(), -1, url_wide.data(), url_len);

    // Open internet session
    ScopedInternetHandle session =
        InternetOpenW(L"DisplayCommander", INTERNET_OPEN_TYPE_PRECONFIG, nullptr, nullptr, 0);
    if (session.handle == nullptr) {
        return false;
    }

    // Open URL
    ScopedInternetHandle request =
        InternetOpenUrlW(session, url_wide.data(), nullptr, 0,
                         INTERNET_FLAG_RELOAD | INTERNET_FLAG_PRAGMA_NOCACHE | INTERNET_FLAG_NO_CACHE_WRITE, 0);
    if (request.handle == nullptr) {
        return false;
    }

    // Set timeouts (30 seconds for download)
    DWORD timeout = 30000;
    InternetSetOption(request, INTERNET_OPTION_CONNECT_TIMEOUT, &timeout, sizeof(timeout));
    InternetSetOption(request, INTERNET_OPTION_RECEIVE_TIMEOUT, &timeout, sizeof(timeout));

    // Create output file
    std::filesystem::path parent_dir = file_path.parent_path();
    if (!parent_dir.empty() && !std::filesystem::exists(parent_dir)) {
        std::filesystem::create_directories(parent_dir);
    }

    std::ofstream out_file(file_path, std::ios::binary);
    if (!out_file) {
        return false;
    }

    // Read and write data
    char buffer[8192];
    DWORD bytes_read = 0;
    while (InternetReadFile(request, buffer, sizeof(buffer), &bytes_read) && bytes_read > 0) {
        out_file.write(buffer, bytes_read);
        if (!out_file) {
            return false;
        }
    }

    out_file.close();
    return std::filesystem::exists(file_path) && std::filesystem::file_size(file_path) > 0;
}

// Fetch ReShade versions from GitHub (crosire/reshade tags API). Returns all tags >= 6.6.2, sorted descending.
// Call at most once per app start; on failure returns false and out_versions is empty.
bool FetchReShadeVersionsFromGitHub(std::vector<std::string>& out_versions, std::string* out_error) {
    return FetchReShadeTagsFromGitHubImpl(out_versions, out_error);
}

// Fetch latest from reshade.me (once per process, then cached).
bool FetchReShadeLatestFromReshadeMe(std::string* out_version, std::string* out_error) {
    return FetchReShadeLatestFromReshadeMeImpl(out_version, out_error);
}

namespace {

// Parse JSON to extract version and download URLs from GitHub API response
bool ParseGitHubReleaseJson(const std::string& json, std::string& version, std::string& url_64, std::string& url_32,
                            std::string& build_number) {
    version.clear();
    url_64.clear();
    url_32.clear();
    build_number.clear();

    // Simple JSON parsing for GitHub API response
    // Look for "tag_name": "v0.10.0"
    size_t tag_pos = json.find("\"tag_name\"");
    if (tag_pos == std::string::npos) {
        return false;
    }

    size_t colon_pos = json.find(':', tag_pos);
    if (colon_pos == std::string::npos) {
        return false;
    }

    size_t quote_start = json.find('"', colon_pos);
    if (quote_start == std::string::npos) {
        return false;
    }

    size_t quote_end = json.find('"', quote_start + 1);
    if (quote_end == std::string::npos) {
        return false;
    }

    version = json.substr(quote_start + 1, quote_end - quote_start - 1);
    version = ParseVersionString(version);  // Remove "v" prefix if present

    // Try to extract build number from tag_name if it has 4 components (e.g., "v0.10.0.1162")
    build_number = ExtractBuildNumber(version);

    // If not found in tag, try to extract from "name" field (release name)
    if (build_number.empty()) {
        size_t name_pos = json.find("\"name\"");
        if (name_pos != std::string::npos) {
            size_t name_colon = json.find(':', name_pos);
            if (name_colon != std::string::npos) {
                size_t name_quote_start = json.find('"', name_colon);
                if (name_quote_start != std::string::npos) {
                    size_t name_quote_end = json.find('"', name_quote_start + 1);
                    if (name_quote_end != std::string::npos) {
                        std::string release_name =
                            json.substr(name_quote_start + 1, name_quote_end - name_quote_start - 1);
                        // Try to find build number in release name (e.g., "v0.10.0.1162" or "Build 1162")
                        build_number = ExtractBuildNumber(release_name);
                        if (build_number.empty()) {
                            // Look for "Build 1162" pattern
                            size_t build_pos = release_name.find("Build ");
                            if (build_pos != std::string::npos) {
                                size_t build_start = build_pos + 6;  // Length of "Build "
                                size_t build_end = release_name.find_first_not_of("0123456789", build_start);
                                if (build_end == std::string::npos) {
                                    build_end = release_name.length();
                                }
                                if (build_end > build_start) {
                                    build_number = release_name.substr(build_start, build_end - build_start);
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // Look for "browser_download_url" entries
    size_t search_pos = 0;
    while ((search_pos = json.find("\"browser_download_url\"", search_pos)) != std::string::npos) {
        size_t url_colon = json.find(':', search_pos);
        if (url_colon == std::string::npos) {
            break;
        }

        size_t url_quote_start = json.find('"', url_colon);
        if (url_quote_start == std::string::npos) {
            break;
        }

        size_t url_quote_end = json.find('"', url_quote_start + 1);
        if (url_quote_end == std::string::npos) {
            break;
        }

        std::string url = json.substr(url_quote_start + 1, url_quote_end - url_quote_start - 1);

        // Check if it's a .addon64 or .addon32 file
        if (url.find(".addon64") != std::string::npos && url_64.empty()) {
            url_64 = url;
        } else if (url.find(".addon32") != std::string::npos && url_32.empty()) {
            url_32 = url;
        }

        search_pos = url_quote_end + 1;
    }

    // Return true if we have at least the version (URLs might be in assets array with different structure)
    // URLs are optional - they might be stored from a previous check
    return !version.empty();
}

}  // anonymous namespace

VersionCheckState& GetVersionCheckState() { return g_version_check_state; }

int CompareVersions(const std::string& v1, const std::string& v2) {
    std::string clean_v1 = ParseVersionString(v1);
    std::string clean_v2 = ParseVersionString(v2);

    std::istringstream iss1(clean_v1);
    std::istringstream iss2(clean_v2);

    int major1, minor1, patch1;
    int major2, minor2, patch2;
    char dot1, dot2;

    if (!(iss1 >> major1 >> dot1 >> minor1 >> dot2 >> patch1)) {
        return 0;  // Invalid version format
    }
    if (!(iss2 >> major2 >> dot1 >> minor2 >> dot2 >> patch2)) {
        return 0;  // Invalid version format
    }

    if (major1 != major2) {
        return (major1 < major2) ? -1 : 1;
    }
    if (minor1 != minor2) {
        return (minor1 < minor2) ? -1 : 1;
    }
    if (patch1 != patch2) {
        return (patch1 < patch2) ? -1 : 1;
    }

    return 0;  // Versions are equal
}

std::string ParseVersionString(const std::string& version_str) {
    std::string result = version_str;
    // Remove "v" prefix if present
    if (!result.empty() && (result[0] == 'v' || result[0] == 'V')) {
        result = result.substr(1);
    }
    return result;
}

std::string NormalizeVersionToXyz(const std::string& version_str) {
    std::string s = ParseVersionString(version_str);
    size_t dot_count = 0;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '.') {
            ++dot_count;
            if (dot_count == 3) {
                return s.substr(0, i);
            }
        }
    }
    return s;
}

std::filesystem::path GetDownloadDirectory() {
    wchar_t localappdata_path[MAX_PATH];
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, localappdata_path))) {
        return std::filesystem::path();
    }
    std::filesystem::path base(localappdata_path);
    return base / L"Programs" / L"Display_Commander";
}

static std::string ParseVersionFromReleaseBody(const std::string& json);

void CheckForUpdates() {
    auto& state = GetVersionCheckState();

    // Prevent multiple simultaneous checks
    bool expected = false;
    if (!state.checking.compare_exchange_strong(expected, true)) {
        return;  // Already checking
    }

    state.status.store(VersionComparison::Checking);

    // Run check in background thread
    std::thread check_thread([]() {
        auto& state = GetVersionCheckState();

        try {
            // Get version from latest_debug HTML page only (no API, no asset download)
            std::string page_url = "https://github.com/pmnoxx/display-commander/releases/tag/latest_debug";
            std::string html;

            std::string dl_err;
            if (!DownloadTextFromUrl(page_url, html, &dl_err)) {
                auto* error = new std::string(dl_err.empty() ? "Failed to fetch latest_debug page" : dl_err);
                state.error_message.store(error);
                state.status.store(VersionComparison::CheckFailed);
                state.checking.store(false);
                return;
            }

            std::string latest_version = ParseVersionFromReleaseBody(html);
            if (latest_version.empty()) {
                auto* error = new std::string("Failed to parse latest_debug page (no 'Version in binaries')");
                state.error_message.store(error);
                state.status.store(VersionComparison::CheckFailed);
                state.checking.store(false);
                return;
            }

            std::string build_number = ExtractBuildNumber(latest_version);
            state.error_message.store(nullptr);
            // Download URLs are not fetched here; they are resolved when user clicks Download (FetchReleaseByTag).

            // Store results (if not already stored above)
            if (state.latest_version.load() == nullptr && !latest_version.empty()) {
                auto* version_str = new std::string(latest_version);
                state.latest_version.store(version_str);
            }

            // Store build number if found, otherwise try to use current build if versions match
            // Always prefer using current build number if versions match (same release)
            if (!latest_version.empty()) {
                std::string current_version_str =
                    ParseVersionString(DISPLAY_COMMANDER_VERSION_STRING_MAJOR_MINOR_PATCH);
                if (current_version_str == latest_version) {
                    // Same release - use current build number
                    std::string current_build = DISPLAY_COMMANDER_VERSION_BUILD_STRING;
                    if (!current_build.empty() && state.build_number.load() == nullptr) {
                        auto* build_str = new std::string(current_build);
                        state.build_number.store(build_str);
                    }
                } else if (!build_number.empty() && state.build_number.load() == nullptr) {
                    // Different release with build number found in release
                    auto* build_str = new std::string(build_number);
                    state.build_number.store(build_str);
                } else if (state.build_number.load() == nullptr) {
                    // No build number found - try to extract from current full version as fallback
                    std::string current_full_version = DISPLAY_COMMANDER_VERSION_STRING;
                    std::string extracted = ExtractBuildNumber(current_full_version);
                    if (!extracted.empty() && extracted != "0") {
                        auto* build_str = new std::string(extracted);
                        state.build_number.store(build_str);
                    }
                }
            }

            // Compare with current version
            if (!latest_version.empty()) {
                std::string current_version_str =
                    ParseVersionString(DISPLAY_COMMANDER_VERSION_STRING_MAJOR_MINOR_PATCH);
                int comparison = CompareVersions(current_version_str, latest_version);

                if (comparison < 0) {
                    state.status.store(VersionComparison::UpdateAvailable);
                } else {
                    state.status.store(VersionComparison::UpToDate);
                }
            } else {
                // If we don't have version info, keep status as CheckFailed
                state.status.store(VersionComparison::CheckFailed);
            }

        } catch (...) {
            auto* error = new std::string("Exception during version check");
            state.error_message.store(error);
            state.status.store(VersionComparison::CheckFailed);
        }

        state.checking.store(false);
    });

    check_thread.detach();
}

// Extract build number from version string (e.g., "0.10.0.1234" -> "1234")
// If not found, returns empty string
std::string ExtractBuildNumber(const std::string& version_str) {
    size_t last_dot = version_str.find_last_of('.');
    if (last_dot == std::string::npos) {
        return "";
    }

    std::string potential_build = version_str.substr(last_dot + 1);
    // Check if it's a number
    bool is_number = !potential_build.empty();
    for (char c : potential_build) {
        if (!std::isdigit(static_cast<unsigned char>(c))) {
            is_number = false;
            break;
        }
    }

    if (is_number) {
        return potential_build;
    }
    return "";
}

// Format build number as 6 digits with leading zeros
std::string FormatBuildNumber(const std::string& build_str) {
    if (build_str.empty()) {
        // Use timestamp as fallback (last 6 digits of Unix timestamp, ensure it's not 000000)
        auto now = std::chrono::system_clock::now();
        auto timestamp = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
        long long timestamp_mod = timestamp % 1000000;
        // If it's 0, use milliseconds instead to get a non-zero value
        if (timestamp_mod == 0) {
            auto ms_timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
            timestamp_mod = ms_timestamp % 1000000;
            if (timestamp_mod == 0) {
                timestamp_mod = 1;  // Ensure it's never 000000
            }
        }
        char buffer[16];
        snprintf(buffer, sizeof(buffer), "%06lld", timestamp_mod);
        return std::string(buffer);
    }

    // Try to parse as number and format as 6 digits
    try {
        int build_num = std::stoi(build_str);
        char buffer[16];
        snprintf(buffer, sizeof(buffer), "%06d", build_num);
        return std::string(buffer);
    } catch (...) {
        // If parsing fails, use the string as-is (padded to 6 digits)
        std::string result = build_str;
        while (result.length() < 6) {
            result = "0" + result;
        }
        if (result.length() > 6) {
            result = result.substr(result.length() - 6);
        }
        return result;
    }
}

bool DownloadUpdate(bool is_64bit, const std::string& build_number) {
    auto& state = GetVersionCheckState();

    std::string* url_ptr = is_64bit ? state.download_url_64.load() : state.download_url_32.load();
    if (url_ptr == nullptr || url_ptr->empty()) {
        return false;
    }

    std::string url = *url_ptr;

    // Get build number - try from parameter, then from stored build number, then from version string, then use current
    // build if versions match
    std::string build_num = build_number;
    if (build_num.empty()) {
        std::string* stored_build_ptr = state.build_number.load();
        if (stored_build_ptr != nullptr && !stored_build_ptr->empty()) {
            build_num = *stored_build_ptr;
        } else {
            // Try to extract from version string
            std::string* latest_version_ptr = state.latest_version.load();
            if (latest_version_ptr != nullptr) {
                build_num = ExtractBuildNumber(*latest_version_ptr);
            }

            // If still empty and versions match, use current build number
            if (build_num.empty() && latest_version_ptr != nullptr) {
                std::string current_version = ParseVersionString(DISPLAY_COMMANDER_VERSION_STRING_MAJOR_MINOR_PATCH);
                if (current_version == *latest_version_ptr) {
                    build_num = DISPLAY_COMMANDER_VERSION_BUILD_STRING;
                }
            }
        }
    }

    // Format as 6 digits
    std::string formatted_build = FormatBuildNumber(build_num);

    // Create filename: zzz_display_commander_BUILD.addon64/32
    auto download_dir = GetDownloadDirectory();
    if (download_dir.empty()) {
        return false;
    }

    std::string filename = "zzz_display_commander_" + formatted_build + (is_64bit ? ".addon64" : ".addon32");
    std::filesystem::path download_path = download_dir / filename;

    return DownloadBinaryFromUrl(url, download_path);
}

// Fetch latest stable release version from GitHub (releases/latest). Returns version string e.g. "0.12.201".
bool FetchLatestStableReleaseVersion(std::string* out_version, std::string* out_error) {
    if (out_version) out_version->clear();
    std::string json;
    if (!DownloadTextFromUrl("https://api.github.com/repos/pmnoxx/display-commander/releases/latest", json,
                             out_error)) {
        if (out_error && !out_error->empty())
            *out_error = std::string("Latest stable from GitHub: ") + *out_error;
        else if (out_error)
            *out_error = "Failed to fetch latest stable from GitHub";
        return false;
    }
    std::string ver_parsed, url_64, url_32, build_num;
    if (!ParseGitHubReleaseJson(json, ver_parsed, url_64, url_32, build_num)) {
        if (out_error) *out_error = "Invalid latest release JSON";
        return false;
    }
    std::string ver = ParseVersionString(ver_parsed);
    if (ver.empty() || ver.find_first_not_of("0123456789.") != std::string::npos) {
        if (out_error) *out_error = "No valid version in latest release";
        return false;
    }
    if (out_version) *out_version = ver;
    return true;
}

// Fetch a release by exact tag (e.g. "v0.12.189" or "latest_debug"). Fills url_64 and url_32 from assets.
static bool FetchReleaseByTag(const std::string& tag, std::string* out_url_64, std::string* out_url_32,
                              std::string* out_error) {
    std::string url = "https://api.github.com/repos/pmnoxx/display-commander/releases/tags/" + tag;
    std::string json;
    if (!DownloadTextFromUrl(url, json, out_error)) {
        if (out_error && !out_error->empty())
            *out_error = std::string("Release info: ") + *out_error;
        else if (out_error)
            *out_error = "Failed to fetch release info";
        return false;
    }
    std::string ver_parsed, url_64, url_32, build_num;
    if (!ParseGitHubReleaseJson(json, ver_parsed, url_64, url_32, build_num)) {
        if (out_error) *out_error = "Invalid release JSON";
        return false;
    }
    if (url_64.empty() || url_32.empty()) {
        if (out_error) *out_error = "Release has no addon64/addon32 assets";
        return false;
    }
    if (out_url_64) *out_url_64 = url_64;
    if (out_url_32) *out_url_32 = url_32;
    return true;
}

static bool DownloadDcReleaseToDll(const std::string& tag, const std::string& folder_name, std::string* out_error) {
    std::string url_64, url_32;
    if (!FetchReleaseByTag(tag, &url_64, &url_32, out_error)) {
        return false;
    }
    std::filesystem::path base = GetDownloadDirectory() / L"stable" / std::filesystem::path(folder_name);
    std::error_code ec;
    std::filesystem::create_directories(base, ec);
    if (ec) {
        if (out_error) *out_error = "Could not create stable version folder";
        return false;
    }
    auto filenameFromUrl = [](const std::string& u) {
        size_t last = u.rfind('/');
        if (last != std::string::npos && last + 1 < u.size()) {
            return u.substr(last + 1);
        }
        return u;
    };
    std::string name64 = filenameFromUrl(url_64);
    std::string name32 = filenameFromUrl(url_32);
    std::filesystem::path path64 = base / name64;
    std::filesystem::path path32 = base / name32;
    if (!DownloadBinaryFromUrl(url_64, path64)) {
        if (out_error) *out_error = "Failed to download 64-bit addon";
        return false;
    }
    if (!DownloadBinaryFromUrl(url_32, path32)) {
        if (out_error) *out_error = "Failed to download 32-bit addon";
        return false;
    }
    return true;
}

// Download a specific DC version to stable\<version>\ (both addon64 and addon32).
bool DownloadDcVersionToDll(const std::string& version, std::string* out_error) {
    std::string tag = version;
    if (tag.empty() || (tag[0] != 'v' && tag[0] != 'V')) {
        tag = "v" + tag;
    }
    return DownloadDcReleaseToDll(tag, version, out_error);
}

// Parse "Version in binaries" then X.Y.Z.W from HTML or Markdown (e.g. "</strong>: 0.12.395.2637" or "**: 0.12.395.2637").
static std::string ParseVersionFromReleaseBody(const std::string& text) {
    const char prefix[] = "Version in binaries";
    size_t pos = text.find(prefix);
    if (pos == std::string::npos) return {};
    pos += sizeof(prefix) - 1;
    // Skip until first digit (handles HTML "</strong>: " and Markdown "**: ")
    while (pos < text.size() && !std::isdigit(static_cast<unsigned char>(text[pos]))) ++pos;
    if (pos >= text.size()) return {};
    size_t start = pos;
    while (pos < text.size() && (std::isdigit(static_cast<unsigned char>(text[pos])) || text[pos] == '.')) ++pos;
    return text.substr(start, pos - start);
}

// Fetch latest_debug page (HTML) and return the version from the page (no API, no asset download).
bool FetchLatestDebugReleaseVersion(std::string* out_version, std::string* out_error) {
    if (out_version) out_version->clear();
    std::string url = "https://github.com/pmnoxx/display-commander/releases/tag/latest_debug";
    std::string html;
    if (!DownloadTextFromUrl(url, html, out_error)) {
        if (out_error && !out_error->empty())
            *out_error = std::string("latest_debug page: ") + *out_error;
        else if (out_error)
            *out_error = "Failed to fetch latest_debug page";
        return false;
    }
    std::string ver = ParseVersionFromReleaseBody(html);
    if (ver.empty()) {
        if (out_error) *out_error = "Page has no 'Version in binaries'";
        return false;
    }
    if (out_version) *out_version = ver;
    return true;
}

// Download latest_debug to Display_Commander\Debug\X.Y.Z (local cache for override choice: latest from
// GitHub or pick a cached version).
bool DownloadDcLatestDebugToDebugFolder(std::string* out_error) {
    std::string url_64, url_32;
    if (!FetchReleaseByTag("latest_debug", &url_64, &url_32, out_error)) {
        return false;
    }
    std::filesystem::path base = GetDownloadDirectory() / L"Debug";
    const std::string staging_name = "_staging_latest_debug";
    std::filesystem::path staging = base / std::filesystem::path(staging_name);
    std::error_code ec;
    std::filesystem::create_directories(staging, ec);
    if (ec) {
        if (out_error) *out_error = "Could not create staging folder";
        return false;
    }
    auto filenameFromUrl = [](const std::string& u) {
        size_t last = u.rfind('/');
        if (last != std::string::npos && last + 1 < u.size()) return u.substr(last + 1);
        return u;
    };
    std::string name64 = filenameFromUrl(url_64);
    std::string name32 = filenameFromUrl(url_32);
    std::filesystem::path path64 = staging / name64;
    std::filesystem::path path32 = staging / name32;
    if (!DownloadBinaryFromUrl(url_64, path64)) {
        display_commander::utils::SafeRemoveAll(staging, ec);
        if (out_error) *out_error = "Failed to download 64-bit addon";
        return false;
    }
    if (!DownloadBinaryFromUrl(url_32, path32)) {
        display_commander::utils::SafeRemoveAll(staging, ec);
        if (out_error) *out_error = "Failed to download 32-bit addon";
        return false;
    }
    std::string version_from_dll = GetDLLVersionString(path64.wstring());
    if (version_from_dll.empty() || version_from_dll == "Unknown") {
        display_commander::utils::SafeRemoveAll(staging, ec);
        if (out_error) *out_error = "Could not read version from downloaded addon";
        return false;
    }
    std::string folder_xyz = VersionStringToXyzFolder(version_from_dll);
    if (folder_xyz.empty()) {
        display_commander::utils::SafeRemoveAll(staging, ec);
        if (out_error) *out_error = "Invalid version from addon";
        return false;
    }
    std::filesystem::path target_dir = base / std::filesystem::path(folder_xyz);
    std::filesystem::create_directories(target_dir, ec);
    if (ec) {
        display_commander::utils::SafeRemoveAll(staging, ec);
        if (out_error) *out_error = "Could not create Debug version folder";
        return false;
    }
    std::filesystem::path dest64 = target_dir / name64;
    std::filesystem::path dest32 = target_dir / name32;
    std::filesystem::rename(path64, dest64, ec);
    if (ec) {
        std::filesystem::copy(path64, dest64, std::filesystem::copy_options::overwrite_existing, ec);
        if (ec) {
            display_commander::utils::SafeRemoveAll(staging, ec);
            if (out_error) *out_error = "Could not move 64-bit addon to version folder";
            return false;
        }
        std::filesystem::remove(path64, ec);
    }
    std::filesystem::rename(path32, dest32, ec);
    if (ec) {
        std::filesystem::copy(path32, dest32, std::filesystem::copy_options::overwrite_existing, ec);
        if (ec) {
            std::filesystem::remove(dest64, ec);
            display_commander::utils::SafeRemoveAll(staging, ec);
            if (out_error) *out_error = "Could not move 32-bit addon to version folder";
            return false;
        }
        std::filesystem::remove(path32, ec);
    }
    display_commander::utils::SafeRemoveAll(staging, ec);
    return true;
}

}  // namespace display_commander::utils::version_check
