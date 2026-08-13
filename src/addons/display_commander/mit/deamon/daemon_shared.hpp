// Source Code <Display Commander> // follow this order for includes in all files + add this comment at the top
#pragma once

// Libraries <standard C++>
#include <cstddef>
#include <cstdint>

namespace display_commander::mit::deamon {

inline constexpr std::uint32_t kDaemonSharedMagic = 0x44434D4E;  // 'DCMN'
inline constexpr std::uint32_t kDaemonSharedVersion = 1;

#pragma pack(push, 1)
struct DaemonSharedState {
    std::uint32_t magic;
    std::uint32_t version;
    std::uint32_t size;
    char screen_id[128];
    std::int32_t screen_size_width;
    std::int32_t screen_size_height;
    bool restore_resolution;
    bool restore_hdr;
};
#pragma pack(pop)

static_assert(sizeof(DaemonSharedState) == 150, "DaemonSharedState size must stay stable for shared memory");

inline constexpr wchar_t kDaemonMappingNamePrefix[] = L"Local\\DisplayCommander_Daemon_";

}  // namespace display_commander::mit::deamon
