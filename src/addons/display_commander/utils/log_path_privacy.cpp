// Source Code <Display Commander> // follow this order for includes in all files + add this comment at the top
#include "log_path_privacy.hpp"

// Libraries <standard C++>
#include <cstddef>
#include <string>

namespace display_commander::utils {

namespace {

constexpr char kPlaceholder[] = "<user>";
constexpr size_t kPlaceholderLen = sizeof(kPlaceholder) - 1;

bool IsPathSep(char c) { return c == '\\' || c == '/'; }

char ToLowerAscii(char c) {
    if (c >= 'A' && c <= 'Z') {
        return static_cast<char>(c + 32);
    }
    return c;
}

bool MatchUsersAt(const std::string& s, size_t u_index) {
    static const char kUsers[] = "users";
    for (size_t k = 0; k < 5; ++k) {
        if (ToLowerAscii(s[u_index + k]) != kUsers[k]) {
            return false;
        }
    }
    return true;
}

// Pattern: sep + "documents and settings" + sep (22 letters between seps)
bool MatchDocumentsAndSettingsAt(const std::string& s, size_t d_index) {
    static const char kDoc[] = "documents and settings";
    constexpr size_t kDocLen = 22;
    if (d_index + kDocLen > s.size()) {
        return false;
    }
    for (size_t k = 0; k < kDocLen; ++k) {
        if (ToLowerAscii(s[d_index + k]) != kDoc[k]) {
            return false;
        }
    }
    return true;
}

// sep + users (5) + sep => profile starts at off + 7
void ReplaceProfileSegment(std::string& s, size_t profile_start, size_t& scan_from) {
    size_t profile_end = profile_start;
    const size_t n = s.size();
    while (profile_end < n && !IsPathSep(s[profile_end])) {
        ++profile_end;
    }
    if (profile_end > profile_start) {
        s.replace(profile_start, profile_end - profile_start, kPlaceholder, kPlaceholderLen);
        scan_from = profile_start + kPlaceholderLen;
    } else {
        scan_from = profile_start;
    }
}

}  // namespace

std::string SanitizeLogUserPaths(std::string message) {
    size_t i = 0;
    while (i < message.size()) {
        const size_t remaining = message.size() - i;
        // \Users\<profile> or /Users/<profile>
        if (remaining >= 7 && IsPathSep(message[i]) && MatchUsersAt(message, i + 1) && IsPathSep(message[i + 6])) {
            const size_t profile_start = i + 7;
            ReplaceProfileSegment(message, profile_start, i);
            continue;
        }
        // \Documents and Settings\<profile> (24 = 1 + 22 + 1)
        if (remaining >= 24 && IsPathSep(message[i]) && MatchDocumentsAndSettingsAt(message, i + 1)
            && IsPathSep(message[i + 23])) {
            const size_t profile_start = i + 24;
            ReplaceProfileSegment(message, profile_start, i);
            continue;
        }
        ++i;
    }
    return message;
}

}  // namespace display_commander::utils
