// 1
#include "addon.hpp"

// 2
#include <windows.h>

// 4
#include <atomic>
#include <reshade.hpp>

// 5
#include "globals.hpp"
#include "ui/new_ui/controls/performance_overlay/reshade_overlay_event.hpp"
#include "utils/detour_call_tracker.hpp"
#include "utils/logging.hpp"
#include "version.hpp"

// Export for multi-proxy coordination: other DC instances (dxgi, winmm, version.dll) scan this to decide HOOKED vs
// PROXY_DLL_ONLY
extern "C" __declspec(dllexport) int GetDisplayCommanderState() {
    return static_cast<int>(g_display_commander_state.load(std::memory_order_acquire));
}

// Export addon information
extern "C" __declspec(dllexport) constexpr const char* NAME = "Display Commander";
extern "C" __declspec(dllexport) constexpr const char* AUTHOR = "pmnoxx";
extern "C" __declspec(dllexport) constexpr const char* DESCRIPTION =
    "RenoDX Display Commander - Advanced display and performance management.";
extern "C" __declspec(dllexport) constexpr const char* WEBSITE = "https://github.com/pmnoxx/display-commander";
extern "C" __declspec(dllexport) constexpr const char* ISSUES = "https://github.com/pmnoxx/display-commander/issues";

// Export version string function
extern "C" __declspec(dllexport) const char* GetDisplayCommanderVersion() { return DISPLAY_COMMANDER_VERSION_STRING; }

// Export function to get the DLL load timestamp in nanoseconds
// Used to resolve conflicts when multiple DLLs are loaded at the same time
extern "C" __declspec(dllexport) LONGLONG LoadedNs() { return g_dll_load_time_ns.load(std::memory_order_acquire); }

bool FinishAddonRegistration(HMODULE addon_module, HMODULE reshade_module, bool do_unregister) {
    g_module.store(addon_module, std::memory_order_release);
    // Store ReShade module handle for unload detection (don't override if already set)
    HMODULE expected = nullptr;
    (void)g_reshade_module.compare_exchange_strong(expected, reshade_module);
    LogInfo("[AddonInit] Stored ReShade module handle: 0x%p", reshade_module);

    bool ok = false;
    if (do_unregister) {
        reshade::unregister_addon(addon_module);
        ok = reshade::register_addon(addon_module, reshade_module);
        reshade::unregister_overlay("DC", OnRegisterOverlayDisplayCommander);
        reshade::register_overlay("DC", OnRegisterOverlayDisplayCommander);
    } else {
        ok = reshade::register_addon(addon_module, reshade_module);
        reshade::register_overlay("DC", OnRegisterOverlayDisplayCommander);
    }
    if (ok) {
        RegisterReShadeEvents(addon_module);
    }
    return ok;
}

// Export addon initialization function
extern "C" __declspec(dllexport) bool AddonInit(HMODULE addon_module, HMODULE reshade_module) {
    CALL_GUARD_NO_TS();;
    if (g_display_commander_state.load(std::memory_order_acquire) != DC_STATE_HOOKED) {
        LogInfo("[AddonInit] Display Commander state is not HOOKED, refusing to load");
        return false;
    }
    FinishAddonRegistration(addon_module, reshade_module, true);
    return true;
}
