#pragma once

#include <windows.h>

namespace utils {

// All global/static SRWLOCKs are declared here and defined in srwlock_registry.cpp.
// Stuck-detection can call LogAllSrwlockStatus() to report every lock (HELD/free)
// to make it easier to see which lock is hanging when diagnosing deadlocks.

// --- Registry locks (15 file-level globals) ---
extern SRWLOCK g_reshade_runtimes_lock;
extern SRWLOCK g_dlss_override_handles_srwlock;
extern SRWLOCK g_module_srwlock;
extern SRWLOCK g_blocked_dlls_srwlock;
extern SRWLOCK g_context_lock;
extern SRWLOCK g_seen_exception_addresses_lock;
extern SRWLOCK g_hid_suppression_mutex;
extern SRWLOCK g_nvapi_lock;
extern SRWLOCK g_ngx_handle_mutex;
extern SRWLOCK g_qpc_modules_srwlock;
extern SRWLOCK g_nvll_sleep_mode_params_lock;
extern SRWLOCK g_vulkan_extensions_lock;
extern SRWLOCK g_game_reflex_sleep_mode_params_lock;
extern SRWLOCK g_dinput_devices_mutex;
extern SRWLOCK g_dinput_device_hooks_mutex;

// Logs status of all 17 locks (15 above + logger queue_lock + swapchain_tracking)
// to the addon log. HELD = lock is in use; free = not held. Call from stuck-detection.
void LogAllSrwlockStatus();

}  // namespace utils
