#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace display_commander::nvapi {

// NVPI Reference.xml setting ID for "Smooth Motion - Allowed APIs" (NvidiaProfileInspectorRevamped Reference.xml).
// MinRequiredDriverVersion 571.86.
constexpr std::uint32_t NVPI_SMOOTH_MOTION_ALLOWED_APIS_ID = 0xB0CC0875;

// NVPI Reference.xml setting ID for "Smooth Motion - Enable" (50 series). MinRequiredDriverVersion 571.86.
constexpr std::uint32_t NVPI_SMOOTH_MOTION_ENABLE_50_ID = 0xB0D384C0;

// NVPI Reference.xml setting ID for "RTX HDR - Enable" (fallback). At runtime we resolve ID from driver via
// NvAPI_DRS_GetSettingIdFromName("RTX HDR - Enable") when resolve_id_from_driver is set (driver may use different ID, e.g. group 5).
constexpr std::uint32_t NVPI_RTX_HDR_ENABLE_ID = 0x00DD48FB;
// RTX HDR - Debanding, Allow, Contrast, Middle Grey, Peak Brightness, Saturation (NPI CustomSettingNames.xml).
constexpr std::uint32_t NVPI_RTX_HDR_DEBANDING_ID = 0x00432F84;
constexpr std::uint32_t NVPI_RTX_HDR_ALLOW_ID = 0x1077A11A;
constexpr std::uint32_t NVPI_RTX_HDR_CONTRAST_ID = 0x00DD48FE;
constexpr std::uint32_t NVPI_RTX_HDR_MIDDLE_GREY_ID = 0x00DD48FD;
constexpr std::uint32_t NVPI_RTX_HDR_PEAK_BRIGHTNESS_ID = 0x00DD48FC;
constexpr std::uint32_t NVPI_RTX_HDR_SATURATION_ID = 0x00DD48FF;

// Returns (value, label) pairs for "Smooth Motion - Allowed APIs" from Reference.xml if available,
// otherwise a built-in list matching NvidiaProfileInspectorRevamped Reference.xml.
// Source: https://github.com/xHybred/NvidiaProfileInspectorRevamped/blob/master/nspector/Reference.xml
std::vector<std::pair<std::uint32_t, std::string>> GetSmoothMotionAllowedApisValues();

// Returns only the flag (bit) entries for "Smooth Motion - Allowed APIs", excluding 0 (None/All).
// Used for bit-field UI: one checkbox per flag; combined value = OR of selected flags.
std::vector<std::pair<std::uint32_t, std::string>> GetSmoothMotionAllowedApisFlags();

// Returns the addon DLL directory path (where Reference.xml is copied at build time). Empty if unresolved.
std::string GetAddonModuleDirectory();

}  // namespace display_commander::nvapi
