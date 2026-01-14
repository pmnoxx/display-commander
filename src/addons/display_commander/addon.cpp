#include "addon.hpp"
#include <windows.h>
#include <atomic>
#include <memory>
#include <string>
#include "globals.hpp"
#include "utils/timing.hpp"
#include "version.hpp"

// Export addon information
extern "C" __declspec(dllexport) constexpr const char* NAME = "Display Commander";
extern "C" __declspec(dllexport) constexpr const char* DESCRIPTION =
    "RenoDX Display Commander - Advanced display and performance management.";

// Export version string function
extern "C" __declspec(dllexport) const char* GetDisplayCommanderVersion() { return DISPLAY_COMMANDER_VERSION_STRING; }

// Export function to notify other Display Commander instances about multiple versions
extern "C" __declspec(dllexport) void NotifyDisplayCommanderMultipleVersions(const char* caller_version) {
    if (caller_version == nullptr) {
        return;
    }

    // Store the other version in a global atomic variable
    // This will be displayed as a warning in the main tab UI
    // Create a shared string with the caller's version
    auto version_str = std::make_shared<const std::string>(caller_version);
    g_other_dc_version_detected.store(version_str);

    // Log to debug output
    char msg[256];
    snprintf(msg, sizeof(msg), "[DisplayCommander] Notified of multiple versions by another instance: v%s\n",
             caller_version);
    OutputDebugStringA(msg);
}

// Export function to get the DLL load timestamp in nanoseconds
// Used to resolve conflicts when multiple DLLs are loaded at the same time
extern "C" __declspec(dllexport) LONGLONG LoadedNs() { return g_dll_load_time_ns.load(std::memory_order_acquire); }
