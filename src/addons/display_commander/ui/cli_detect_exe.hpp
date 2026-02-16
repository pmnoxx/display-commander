#pragma once

#include <string>

// DetectExe: PE parsing for bitness and graphics API (ReShade DLL suggestion).
// Used by CLI (DetectExe command) and standalone installer UI.

namespace cli_detect_exe {

struct DetectResult {
    std::string exe_path;
    bool is_64bit = false;
    bool has_d3d9 = false, has_d3d11 = false, has_d3d12 = false, has_dxgi = false;
    bool has_opengl32 = false, has_vulkan = false;
};

// Run detection on exe at path (wide). Returns true and fills result on success.
bool DetectExeForPath(const wchar_t* exe_path_wide, DetectResult* out);

// Suggested ReShade DLL name: "d3d12", "dxgi", "d3d9", "opengl32", "vulkan", or "unknown".
const char* ReShadeDllFromDetect(const DetectResult& r);
}  // namespace cli_detect_exe
