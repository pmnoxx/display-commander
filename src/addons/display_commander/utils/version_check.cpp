#include "version_check.hpp"
#include "../version.hpp"
#include <Windows.h>
#include <WinInet.h>
#include <ShlObj.h>
#include <sstream>
#include <thread>
#include <filesystem>
#include <fstream>
#include <vector>
#include <chrono>
#include <cctype>

namespace display_commander::utils::version_check {

namespace {
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
    ScopedInternetHandle(ScopedInternetHandle&& other) noexcept : handle(other.handle) {
        other.handle = nullptr;
    }
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

// Download text content from URL using WinInet
bool DownloadTextFromUrl(const std::string& url, std::string& content) {
    content.clear();

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

    // Set timeouts (10 seconds for version check)
    DWORD timeout = 10000;
    InternetSetOption(request, INTERNET_OPTION_CONNECT_TIMEOUT, &timeout, sizeof(timeout));
    InternetSetOption(request, INTERNET_OPTION_RECEIVE_TIMEOUT, &timeout, sizeof(timeout));

    // Read response
    char buffer[4096];
    DWORD bytes_read = 0;
    while (InternetReadFile(request, buffer, sizeof(buffer) - 1, &bytes_read) && bytes_read > 0) {
        buffer[bytes_read] = '\0';
        content += buffer;
    }

    return !content.empty();
}

// Download binary file from URL
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

// Parse JSON to extract version and download URLs from GitHub API response
bool ParseGitHubReleaseJson(const std::string& json, std::string& version, std::string& url_64, std::string& url_32, std::string& build_number) {
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
                        std::string release_name = json.substr(name_quote_start + 1, name_quote_end - name_quote_start - 1);
                        // Try to find build number in release name (e.g., "v0.10.0.1162" or "Build 1162")
                        build_number = ExtractBuildNumber(release_name);
                        if (build_number.empty()) {
                            // Look for "Build 1162" pattern
                            size_t build_pos = release_name.find("Build ");
                            if (build_pos != std::string::npos) {
                                size_t build_start = build_pos + 6; // Length of "Build "
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

VersionCheckState& GetVersionCheckState() {
    return g_version_check_state;
}

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

std::filesystem::path GetDownloadDirectory() {
    wchar_t documents_path[MAX_PATH];
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_MYDOCUMENTS, nullptr, SHGFP_TYPE_CURRENT, documents_path))) {
        return std::filesystem::path();
    }
    std::filesystem::path documents_dir(documents_path);
    return documents_dir / L"Display Commander";
}

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
            // Get latest release from GitHub API
            std::string api_url = "https://api.github.com/repos/pmnoxx/display-commander/releases/latest";
            std::string json_response;

            if (!DownloadTextFromUrl(api_url, json_response)) {
                auto* error = new std::string("Failed to connect to GitHub API");
                state.error_message.store(error);
                state.status.store(VersionComparison::CheckFailed);
                state.checking.store(false);
                return;
            }

            std::string latest_version;
            std::string url_64;
            std::string url_32;
            std::string build_number;

            // Parse release information - continue even if URLs are missing (they might be in assets)
            if (!ParseGitHubReleaseJson(json_response, latest_version, url_64, url_32, build_number)) {
                // If we at least got the version, store it and continue
                if (!latest_version.empty()) {
                    auto* version_str = new std::string(latest_version);
                    state.latest_version.store(version_str);
                    
                    // Store build number if found
                    if (!build_number.empty()) {
                        auto* build_str = new std::string(build_number);
                        state.build_number.store(build_str);
                    }
                    
                    // If URLs are missing, try to find them in assets array
                    if (url_64.empty() && url_32.empty()) {
                        // Look for assets array and find download URLs there
                        size_t assets_pos = json_response.find("\"assets\"");
                        if (assets_pos != std::string::npos) {
                            // Look for browser_download_url in assets
                            size_t url_search = assets_pos;
                            while ((url_search = json_response.find("\"browser_download_url\"", url_search)) != std::string::npos) {
                                size_t url_colon = json_response.find(':', url_search);
                                if (url_colon == std::string::npos) {
                                    break;
                                }
                                size_t url_quote_start = json_response.find('"', url_colon);
                                if (url_quote_start == std::string::npos) {
                                    break;
                                }
                                size_t url_quote_end = json_response.find('"', url_quote_start + 1);
                                if (url_quote_end == std::string::npos) {
                                    break;
                                }
                                std::string url = json_response.substr(url_quote_start + 1, url_quote_end - url_quote_start - 1);
                                if (url.find(".addon64") != std::string::npos && url_64.empty()) {
                                    url_64 = url;
                                } else if (url.find(".addon32") != std::string::npos && url_32.empty()) {
                                    url_32 = url;
                                }
                                url_search = url_quote_end + 1;
                                if (url_search >= json_response.length()) break;
                                if (!url_64.empty() && !url_32.empty()) break; // Found both, stop searching
                            }
                        }
                    }
                    
                    // Store URLs if found
                    if (!url_64.empty()) {
                        auto* url_str = new std::string(url_64);
                        state.download_url_64.store(url_str);
                    }
                    if (!url_32.empty()) {
                        auto* url_str = new std::string(url_32);
                        state.download_url_32.store(url_str);
                    }
                    
                    // Store URLs if found (only if not already stored)
                    if (!url_64.empty() && state.download_url_64.load() == nullptr) {
                        auto* url_str = new std::string(url_64);
                        state.download_url_64.store(url_str);
                    }
                    if (!url_32.empty() && state.download_url_32.load() == nullptr) {
                        auto* url_str = new std::string(url_32);
                        state.download_url_32.store(url_str);
                    }
                    
                    // If we have version but no URLs, show warning but don't fail completely
                    if (url_64.empty() && url_32.empty() && 
                        state.download_url_64.load() == nullptr && state.download_url_32.load() == nullptr) {
                        auto* error = new std::string("Version found but download URLs not available");
                        state.error_message.store(error);
                    } else {
                        // Clear any previous error if we have URLs now
                        state.error_message.store(nullptr);
                    }
                    
                    // Continue with version comparison even if URLs are missing
                    // (URLs might be available from previous check)
                } else {
                    // Complete failure - couldn't even get version
                    auto* error = new std::string("Failed to parse release information (could not find version)");
                    state.error_message.store(error);
                    state.status.store(VersionComparison::CheckFailed);
                    state.checking.store(false);
                    return;
                }
            }

            // Store results (if not already stored above)
            if (state.latest_version.load() == nullptr && !latest_version.empty()) {
                auto* version_str = new std::string(latest_version);
                state.latest_version.store(version_str);
            }
            
            // Store build number if found, otherwise try to use current build if versions match
            // Always prefer using current build number if versions match (same release)
            if (!latest_version.empty()) {
                std::string current_version_str = ParseVersionString(DISPLAY_COMMANDER_VERSION_STRING_MAJOR_MINOR_PATCH);
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
                std::string current_version_str = ParseVersionString(DISPLAY_COMMANDER_VERSION_STRING_MAJOR_MINOR_PATCH);
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
                timestamp_mod = 1; // Ensure it's never 000000
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
    
    // Get build number - try from parameter, then from stored build number, then from version string, then use current build if versions match
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

}  // namespace display_commander::utils::version_check
