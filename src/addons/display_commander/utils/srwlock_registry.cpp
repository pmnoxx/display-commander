#include "srwlock_registry.hpp"
#include "display_commander_logger.hpp"
#include "globals.hpp"
#include "logging.hpp"
#include "srwlock_wrapper.hpp"

namespace utils {

// --- Definitions (all 16 global/static SRWLOCKs) ---
SRWLOCK g_reshade_runtimes_lock = SRWLOCK_INIT;
SRWLOCK g_dlss_override_handles_srwlock = SRWLOCK_INIT;
SRWLOCK g_dlss_tracked_srwlock = SRWLOCK_INIT;
SRWLOCK g_module_srwlock = SRWLOCK_INIT;
SRWLOCK g_blocked_dlls_srwlock = SRWLOCK_INIT;
SRWLOCK g_host_loaded_apis_srwlock = SRWLOCK_INIT;
SRWLOCK g_context_lock = SRWLOCK_INIT;
SRWLOCK g_seen_exception_addresses_lock = SRWLOCK_INIT;
SRWLOCK g_nvapi_lock = SRWLOCK_INIT;
SRWLOCK g_ngx_handle_mutex = SRWLOCK_INIT;
SRWLOCK g_qpc_modules_srwlock = SRWLOCK_INIT;
SRWLOCK g_nvll_sleep_mode_params_lock = SRWLOCK_INIT;
SRWLOCK g_vulkan_extensions_lock = SRWLOCK_INIT;
SRWLOCK g_game_reflex_sleep_mode_params_lock = SRWLOCK_INIT;
SRWLOCK g_dinput_devices_mutex = SRWLOCK_INIT;
SRWLOCK g_dinput_device_hooks_mutex = SRWLOCK_INIT;
SRWLOCK g_wndproc_map_lock = SRWLOCK_INIT;
SRWLOCK g_continuous_monitoring_loop_lock = SRWLOCK_INIT;
SRWLOCK g_hdr_upgrade_back_buffers_lock = SRWLOCK_INIT;
SRWLOCK g_proxy_getproc_logged_srwlock = SRWLOCK_INIT;
SRWLOCK g_getproc_all_logged_srwlock = SRWLOCK_INIT;

namespace {

static void LogOne(const char* name, bool held) { LogInfo("SRWLOCK %s: %s", name, held ? "HELD" : "free"); }

}  // namespace

void LogAllSrwlockStatus() {
    LogOne("logger queue_lock", display_commander::logger::IsWriteLockHeld());
    LogOne("reshade_runtimes", TryIsSRWLockHeld(g_reshade_runtimes_lock));
    LogOne("swapchain_tracking", IsSwapchainTrackingLockHeld());
    LogOne("loadlibrary module", TryIsSRWLockHeld(g_module_srwlock));
    LogOne("loadlibrary blocked_dlls", TryIsSRWLockHeld(g_blocked_dlls_srwlock));
    LogOne("host_loaded_apis", TryIsSRWLockHeld(g_host_loaded_apis_srwlock));
    LogOne("dlss_override_handles", TryIsSRWLockHeld(g_dlss_override_handles_srwlock));
    LogOne("dlss_tracked", TryIsSRWLockHeld(g_dlss_tracked_srwlock));
    LogOne("detour context_lock", TryIsSRWLockHeld(g_context_lock));
    LogOne("seen_exception_addresses", TryIsSRWLockHeld(g_seen_exception_addresses_lock));
    LogOne("nvapi", TryIsSRWLockHeld(g_nvapi_lock));
    LogOne("ngx_handle", TryIsSRWLockHeld(g_ngx_handle_mutex));
    LogOne("qpc_modules", TryIsSRWLockHeld(g_qpc_modules_srwlock));
    LogOne("nvll_sleep_mode_params", TryIsSRWLockHeld(g_nvll_sleep_mode_params_lock));
    LogOne("vulkan_extensions", TryIsSRWLockHeld(g_vulkan_extensions_lock));
    LogOne("game_reflex_sleep_mode_params", TryIsSRWLockHeld(g_game_reflex_sleep_mode_params_lock));
    LogOne("dinput_devices", TryIsSRWLockHeld(g_dinput_devices_mutex));
    LogOne("dinput_device_hooks", TryIsSRWLockHeld(g_dinput_device_hooks_mutex));
    LogOne("wndproc_map", TryIsSRWLockHeld(g_wndproc_map_lock));
    LogOne("continuous_monitoring_loop", TryIsSRWLockHeld(g_continuous_monitoring_loop_lock));
    LogOne("hdr_upgrade_back_buffers", TryIsSRWLockHeld(g_hdr_upgrade_back_buffers_lock));
    LogOne("proxy_getproc_logged", TryIsSRWLockHeld(g_proxy_getproc_logged_srwlock));
    LogOne("getproc_all_logged", TryIsSRWLockHeld(g_getproc_all_logged_srwlock));
}

}  // namespace utils
