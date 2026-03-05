#pragma once

#include <cstdint>

namespace display_commander {

/** ReShade create_device api_version values for D3D9. Matches device_api::d3d9 (0x9000); 0x9100 denotes D3D9Ex (FLIPEX). */
enum class D3D9ApiVersion : std::uint32_t {
    D3D9   = 0x9000,
    D3D9Ex = 0x9100
};

} // namespace display_commander
