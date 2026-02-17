#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace display_commander::nvapi {

// NVPI Reference.xml setting ID for "Smooth Motion - Allowed APIs" (NvidiaProfileInspectorRevamped Reference.xml).
// MinRequiredDriverVersion 571.86.
constexpr std::uint32_t NVPI_SMOOTH_MOTION_ALLOWED_APIS_ID = 0xB0CC0875;

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
