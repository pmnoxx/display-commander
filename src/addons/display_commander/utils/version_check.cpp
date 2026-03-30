// Source Code <Display Commander>
#include "version_check.hpp"

// Libraries <Standard C++>
#include <filesystem>
#include <sstream>
#include <string>

// Libraries <Windows.h>
#include <Windows.h>

// Libraries <Windows>
#include <ShlObj.h>

namespace display_commander::utils::version_check {

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

}  // namespace display_commander::utils::version_check
