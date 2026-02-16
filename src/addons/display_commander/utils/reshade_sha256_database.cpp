#include "reshade_sha256_database.hpp"

namespace display_commander::utils {

namespace {

struct ReShadeSha256Entry {
    const char* version;   // e.g. "6.7.2"
    const char* sha256_64; // ReShade64.dll SHA256 (64 hex chars) or empty if not yet computed
    const char* sha256_32; // ReShade32.dll SHA256
};

// Populate hashes by running: scripts\download_reshade_hashes.ps1
// Empty string = not in database (run the script and paste output here).
const ReShadeSha256Entry kReshadeSha256Db[] = {
    {"6.7.2", "", ""},
    {"6.7.1", "", ""},
    {"6.6.2", "", ""},
};

const size_t kReshadeSha256DbCount = sizeof(kReshadeSha256Db) / sizeof(kReshadeSha256Db[0]);

}  // namespace

const char* GetReShadeExpectedSha256(const std::string& version, bool is_64bit) {
    if (version.empty()) return nullptr;
    for (size_t i = 0; i < kReshadeSha256DbCount; i++) {
        if (version == kReshadeSha256Db[i].version) {
            const char* h = is_64bit ? kReshadeSha256Db[i].sha256_64 : kReshadeSha256Db[i].sha256_32;
            if (h == nullptr || h[0] == '\0') return nullptr;
            return h;
        }
    }
    return nullptr;
}

std::string NormalizeReShadeVersionForLookup(const std::string& version) {
    if (version.empty()) return {};
    size_t dot_count = 0;
    size_t end = 0;
    for (size_t i = 0; i < version.size(); i++) {
        if (version[i] == '.') {
            dot_count++;
            if (dot_count >= 3) {
                end = i;
                break;
            }
        }
        end = i + 1;
    }
    return version.substr(0, end);
}

}  // namespace display_commander::utils
