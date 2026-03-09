#pragma once

// Source Code <Display Commander>
// DLSS-fix API state: entries for NGX/Streamline APIs that need proxy→native conversion.
// Used by Advanced tab "DLSS-fix" subsection under Unsupported features.

#include <cstdint>
#include <string>
#include <vector>

namespace display_commander {

struct DLSSFixAPIEntry {
    std::string api_name;
    bool hooked;
    uint32_t call_count;
};

// Fills out with NGX APIs (14 entries) affected by DLSS-fix. Hooked = (detour installed), call_count from hooks.
void GetDLSSFixNGXAPIEntries(std::vector<DLSSFixAPIEntry>& out);

// Fills out with Streamline APIs (6 entries) affected by DLSS-fix. Hooked = (detour installed), call_count from hooks.
void GetDLSSFixStreamlineAPIEntries(std::vector<DLSSFixAPIEntry>& out);

}  // namespace display_commander
