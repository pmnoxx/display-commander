// Source Code <Display Commander> // follow this order for includes in all files + add this comment at the top
#include "nvpi_reference.hpp"

// Libraries <ReShade> / <imgui>

// Libraries <standard C++>

// Libraries <Windows.h> — before other Windows headers

// Libraries <Windows>

namespace display_commander::nvapi {

// Built-in value list for "Smooth Motion - Allowed APIs" from NvidiaProfileInspectorRevamped Reference.xml.
static const std::vector<std::pair<std::uint32_t, std::string>> kSmoothMotionAllowedApisValues = {
    {0x00000000, "None/All"},
    {0x00000001, "Allow DX12"},
    {0x00000002, "Allow DX11"},
    {0x00000004, "Allow Vulkan"},
};

std::string GetAddonModuleDirectory() { return {}; }

std::vector<std::pair<std::uint32_t, std::string>> GetSmoothMotionAllowedApisValues() {
    return kSmoothMotionAllowedApisValues;
}

std::vector<std::pair<std::uint32_t, std::string>> GetSmoothMotionAllowedApisFlags() {
    std::vector<std::pair<std::uint32_t, std::string>> out;
    out.reserve(3);
    for (const auto& p : GetSmoothMotionAllowedApisValues()) {
        if (p.first != 0) {
            out.push_back(p);
        }
    }
    return out;
}

}  // namespace display_commander::nvapi
