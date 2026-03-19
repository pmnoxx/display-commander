#pragma once

// List of libraries linked into the Display Commander addon DLL (for UI display).
// Update this when adding or removing target_link_libraries in CMakeLists.txt.
// System import libs (e.g. setupapi.lib) resolve to system DLLs at load time.
// MinHook is a static library; its code is merged into the addon.

namespace display_commander::res {

struct LinkLibraryEntry {
    const char* name;  // e.g. "setupapi"
    const char* note;  // optional short note, or nullptr
};

// Libraries linked to zzz_display_commander (addon DLL). Order matches CMake.
inline constexpr LinkLibraryEntry kAddonLinkLibraries[] = {
    {"setupapi", "Device enumeration, setup API"},
    {"tdh", "Event Trace (PresentMon-style tracing)"},
    {"advapi32", "Registry, ETW"},
    {"wininet", "HTTP (ReShade version list, downloads)"},
    {"bcrypt", "SHA256 (ReShade installer verification)"},
    {"minhook", "Static lib – hooking (in-process)"},
};

inline constexpr int kAddonLinkLibrariesCount =
    static_cast<int>(sizeof(kAddonLinkLibraries) / sizeof(kAddonLinkLibraries[0]));

}  // namespace display_commander::res
