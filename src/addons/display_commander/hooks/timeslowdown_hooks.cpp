#include "timeslowdown_hooks.hpp"
#include "hook_suppression_manager.hpp"
#include "../globals.hpp"
#include "../settings/experimental_tab_settings.hpp"
#include "../utils/general_utils.hpp"
#include "../utils/logging.hpp"
#include "../utils/srwlock_wrapper.hpp"
#include "../swapchain_events.hpp"
#include <MinHook.h>
#include <atomic>
#include <memory>
#include <set>
#include <map>
#include <vector>
#include <string>
#include <sstream>
#include <windows.h>

// NTSTATUS constants
#ifndef STATUS_SUCCESS
#define STATUS_SUCCESS ((NTSTATUS)0x00000000L)
#endif
#ifndef STATUS_UNSUCCESSFUL
#define STATUS_UNSUCCESSFUL ((NTSTATUS)0xC0000001L)
#endif
#ifndef NT_SUCCESS
#define NT_SUCCESS(Status) (((NTSTATUS)(Status)) >= 0)
#endif

namespace display_commanderhooks {

// Hook type constants
const char *HOOK_QUERY_PERFORMANCE_COUNTER = "QueryPerformanceCounter";
const char *HOOK_GET_TICK_COUNT = "GetTickCount";
const char *HOOK_GET_TICK_COUNT64 = "GetTickCount64";
const char *HOOK_TIME_GET_TIME = "timeGetTime";
const char *HOOK_GET_SYSTEM_TIME = "GetSystemTime";
const char *HOOK_GET_SYSTEM_TIME_AS_FILE_TIME = "GetSystemTimeAsFileTime";
const char *HOOK_GET_SYSTEM_TIME_PRECISE_AS_FILE_TIME = "GetSystemTimePreciseAsFileTime";
const char *HOOK_GET_LOCAL_TIME = "GetLocalTime";
const char *HOOK_NT_QUERY_SYSTEM_TIME = "NtQuerySystemTime";

// Helper function to apply timeslowdown with baseline tracking
template<typename T>
T ApplyTimeslowdownWithBaseline(T result) {
    static T start_time = 0;

    // Initialize start_time on first call
    if (start_time == 0) {
        start_time = result;
    }

    // Apply timeslowdown if enabled
    if (settings::g_experimentalTabSettings.timeslowdown_enabled.GetValue()) {
        float multiplier = settings::g_experimentalTabSettings.timeslowdown_multiplier.GetValue();
        if (multiplier > 0.0) {
            result = static_cast<T>(start_time + ((result - start_time) * multiplier));
        }
    }

    return result;
}

// Timeslowdown state structure
struct TimeslowdownState {
    LONGLONG original_quad_ts;
    LONGLONG original_quad_value;
    long double multiplier;

    TimeslowdownState() : original_quad_ts(0), original_quad_value(0), multiplier(1.0) {}
};

// Original function pointers
QueryPerformanceCounter_pfn QueryPerformanceCounter_Original = nullptr;
QueryPerformanceFrequency_pfn QueryPerformanceFrequency_Original = nullptr;
GetTickCount_pfn GetTickCount_Original = nullptr;
GetTickCount64_pfn GetTickCount64_Original = nullptr;
timeGetTime_pfn timeGetTime_Original = nullptr;
timeGetTime_pfn timeGetTime_Direct = nullptr;
GetSystemTime_pfn GetSystemTime_Original = nullptr;
GetSystemTimeAsFileTime_pfn GetSystemTimeAsFileTime_Original = nullptr;
GetSystemTimePreciseAsFileTime_pfn GetSystemTimePreciseAsFileTime_Original = nullptr;
GetLocalTime_pfn GetLocalTime_Original = nullptr;
NtQuerySystemTime_pfn NtQuerySystemTime_Original = nullptr;

// Hook state
std::atomic<bool> g_timeslowdown_hooks_installed{false};

// Timeslowdown configuration - now using global instance

// Individual hook type configurations - using atomic variables for DLL safety
std::atomic<TimerHookType> g_query_performance_counter_hook_type{TimerHookType::None};
std::atomic<TimerHookType> g_get_tick_count_hook_type{TimerHookType::None};
std::atomic<TimerHookType> g_get_tick_count64_hook_type{TimerHookType::None};
std::atomic<TimerHookType> g_time_get_time_hook_type{TimerHookType::None};
std::atomic<TimerHookType> g_get_system_time_hook_type{TimerHookType::None};
std::atomic<TimerHookType> g_get_system_time_as_file_time_hook_type{TimerHookType::None};
std::atomic<TimerHookType> g_get_system_time_precise_as_file_time_hook_type{TimerHookType::None};
std::atomic<TimerHookType> g_get_local_time_hook_type{TimerHookType::None};
std::atomic<TimerHookType> g_nt_query_system_time_hook_type{TimerHookType::None};

// Per-hook call counters
std::atomic<uint64_t> g_qpc_call_count{0};
std::atomic<uint64_t> g_qpf_call_count{0};
std::atomic<uint64_t> g_get_tick_count_call_count{0};
std::atomic<uint64_t> g_get_tick_count64_call_count{0};
std::atomic<uint64_t> g_time_get_time_call_count{0};
std::atomic<uint64_t> g_get_system_time_call_count{0};
std::atomic<uint64_t> g_get_system_time_as_file_time_call_count{0};
std::atomic<uint64_t> g_get_system_time_precise_as_file_time_call_count{0};
std::atomic<uint64_t> g_get_local_time_call_count{0};
std::atomic<uint64_t> g_nt_query_system_time_call_count{0};

// Atomic shared_ptr for thread-safe state access
std::atomic<std::shared_ptr<const TimeslowdownState>> g_timeslowdown_state{std::make_shared<TimeslowdownState>()};

// Render thread tracking
std::atomic<DWORD> g_render_thread_id{0}; // 0 means not set yet

// Thread-local storage for cached thread ID (per-thread cache)
thread_local DWORD tls_thread_id = 0;

namespace {
    // QPC calling module tracking
    SRWLOCK g_qpc_modules_srwlock = SRWLOCK_INIT;
    std::set<HMODULE> g_qpc_calling_modules;
    // Map to track which modules have QPC enabled (default: enabled for all)
    std::map<HMODULE, bool> g_qpc_module_enabled_states;
} // anonymous namespace

namespace {
    // Helper function to get module name from HMODULE
    std::wstring GetModuleNameFromHandle(HMODULE hModule) {
        if (hModule == nullptr) {
            return L"<null>";
        }

        wchar_t module_path[MAX_PATH];
        DWORD path_length = GetModuleFileNameW(hModule, module_path, MAX_PATH);
        if (path_length == 0) {
            return L"<unknown>";
        }

        // Extract just the filename from the full path
        std::wstring path(module_path);
        size_t last_slash = path.find_last_of(L"\\/");
        if (last_slash != std::wstring::npos) {
            return path.substr(last_slash + 1);
        }
        return path;
    }
} // anonymous namespace

// Helper function to get hook identifier by name (DLL-safe) - kept for backward compatibility only
TimerHookIdentifier GetHookIdentifierByName(const char *hook_name) {
    if (strcmp(hook_name, HOOK_QUERY_PERFORMANCE_COUNTER) == 0) {
        return TimerHookIdentifier::QueryPerformanceCounter;
    } else if (strcmp(hook_name, HOOK_GET_TICK_COUNT) == 0) {
        return TimerHookIdentifier::GetTickCount;
    } else if (strcmp(hook_name, HOOK_GET_TICK_COUNT64) == 0) {
        return TimerHookIdentifier::GetTickCount64;
    } else if (strcmp(hook_name, HOOK_TIME_GET_TIME) == 0) {
        return TimerHookIdentifier::TimeGetTime;
    } else if (strcmp(hook_name, HOOK_GET_SYSTEM_TIME) == 0) {
        return TimerHookIdentifier::GetSystemTime;
    } else if (strcmp(hook_name, HOOK_GET_SYSTEM_TIME_AS_FILE_TIME) == 0) {
        return TimerHookIdentifier::GetSystemTimeAsFileTime;
    } else if (strcmp(hook_name, HOOK_GET_SYSTEM_TIME_PRECISE_AS_FILE_TIME) == 0) {
        return TimerHookIdentifier::GetSystemTimePreciseAsFileTime;
    } else if (strcmp(hook_name, HOOK_GET_LOCAL_TIME) == 0) {
        return TimerHookIdentifier::GetLocalTime;
    } else if (strcmp(hook_name, HOOK_NT_QUERY_SYSTEM_TIME) == 0) {
        return TimerHookIdentifier::NtQuerySystemTime;
    }
    return TimerHookIdentifier::Count; // Invalid identifier
}

// Helper function to get hook type by identifier (DLL-safe)
TimerHookType GetHookTypeById(TimerHookIdentifier id) {
    switch (id) {
        case TimerHookIdentifier::QueryPerformanceCounter:
            return g_query_performance_counter_hook_type.load();
        case TimerHookIdentifier::GetTickCount:
            return g_get_tick_count_hook_type.load();
        case TimerHookIdentifier::GetTickCount64:
            return g_get_tick_count64_hook_type.load();
        case TimerHookIdentifier::TimeGetTime:
            return g_time_get_time_hook_type.load();
        case TimerHookIdentifier::GetSystemTime:
            return g_get_system_time_hook_type.load();
        case TimerHookIdentifier::GetSystemTimeAsFileTime:
            return g_get_system_time_as_file_time_hook_type.load();
        case TimerHookIdentifier::GetSystemTimePreciseAsFileTime:
            return g_get_system_time_precise_as_file_time_hook_type.load();
        case TimerHookIdentifier::GetLocalTime:
            return g_get_local_time_hook_type.load();
        case TimerHookIdentifier::NtQuerySystemTime:
            return g_nt_query_system_time_hook_type.load();
        default:
            return TimerHookType::None;
    }
}

// Helper function to get hook type by name (DLL-safe) - kept for backward compatibility
TimerHookType GetHookTypeByName(const char *hook_name) {
    TimerHookIdentifier id = GetHookIdentifierByName(hook_name);
    return GetHookTypeById(id);
}

// Render thread management functions
void SetRenderThreadId(DWORD thread_id) {
    if (thread_id != 0) {
        g_render_thread_id.store(thread_id, std::memory_order_release);
        LogInfo("Render thread ID set to: %lu", thread_id);
    }
}

DWORD GetRenderThreadId() {
    return g_render_thread_id.load(std::memory_order_acquire);
}

bool IsCurrentThreadRenderThread() {
    DWORD render_thread_id = GetRenderThreadId();
    if (render_thread_id == 0) {
        return false; // Render thread not set yet
    }

    // Use thread-local storage to cache thread ID for performance
    if (tls_thread_id == 0) {
        tls_thread_id = GetCurrentThreadId();
    }

    return tls_thread_id == render_thread_id;
}

// Helper function to check if a hook should be applied by identifier (DLL-safe)
bool ShouldApplyHookById(TimerHookIdentifier id) {
    TimerHookType type = GetHookTypeById(id);

    if (type == TimerHookType::None) {
        return false;
    }

    if (type == TimerHookType::Enabled) {
        return true;
    }

    if (type == TimerHookType::EnableRenderThread) {
        return IsCurrentThreadRenderThread();
    }

    if (type == TimerHookType::EnableNonRenderThread) {
        return !IsCurrentThreadRenderThread();
    }

    return false;
}

// Helper function to check if a hook should be applied (DLL-safe) - kept for backward compatibility
bool ShouldApplyHook(const char *hook_name) {
    TimerHookIdentifier id = GetHookIdentifierByName(hook_name);
    return ShouldApplyHookById(id);
}

// Hooked QueryPerformanceCounter function
BOOL WINAPI QueryPerformanceCounter_Detour(LARGE_INTEGER *lpPerformanceCount) {
    g_qpc_call_count.fetch_add(1, std::memory_order_relaxed);

    // Track calling module
    HMODULE calling_module = GetCallingDLL();
    if (calling_module != nullptr) {
        bool is_new_module = false;
        {
            utils::SRWLockExclusive lock(g_qpc_modules_srwlock);
            is_new_module = (g_qpc_calling_modules.find(calling_module) == g_qpc_calling_modules.end());
            g_qpc_calling_modules.insert(calling_module);
            // Default to disabled if not in map
            if (g_qpc_module_enabled_states.find(calling_module) == g_qpc_module_enabled_states.end()) {
                // Check settings if this is a new module
                bool should_enable = false;
                if (is_new_module) {
                    std::string enabled_modules_str = settings::g_experimentalTabSettings.qpc_enabled_modules.GetValue();
                    if (!enabled_modules_str.empty()) {
                        std::wstring module_name = GetModuleNameFromHandle(calling_module);
                        // Parse comma-separated module names
                        std::stringstream ss(enabled_modules_str);
                        std::string module_name_narrow;
                        while (std::getline(ss, module_name_narrow, ',')) {
                            // Trim whitespace
                            module_name_narrow.erase(0, module_name_narrow.find_first_not_of(" \t"));
                            module_name_narrow.erase(module_name_narrow.find_last_not_of(" \t") + 1);
                            if (!module_name_narrow.empty()) {
                                std::wstring wmodule_name(module_name_narrow.begin(), module_name_narrow.end());
                                if (wmodule_name == module_name) {
                                    should_enable = true;
                                    break;
                                }
                            }
                        }
                    }
                }
                g_qpc_module_enabled_states[calling_module] = should_enable;
            }
        }

        // Check if this module has QPC enabled
        bool module_enabled = false;
        {
            utils::SRWLockShared lock(g_qpc_modules_srwlock);
            auto it = g_qpc_module_enabled_states.find(calling_module);
            if (it != g_qpc_module_enabled_states.end()) {
                module_enabled = it->second;
            }
        }

        // If module is disabled, return original result without applying timeslowdown
        if (!module_enabled) {
            if (!QueryPerformanceCounter_Original) {
                return QueryPerformanceCounter(lpPerformanceCount);
            }
            return QueryPerformanceCounter_Original(lpPerformanceCount);
        }
    }

    if (!QueryPerformanceCounter_Original) {
        return QueryPerformanceCounter(lpPerformanceCount);
    }

    // Call original function first
    BOOL result = QueryPerformanceCounter_Original(lpPerformanceCount);
    // Check if this hook should be applied (efficient enum-based check)
    if (!ShouldApplyHookById(TimerHookIdentifier::QueryPerformanceCounter)) {
        return result;
    }

    if (result == FALSE || lpPerformanceCount == nullptr || !g_initialized_with_hwnd.load()) {
        return result;
    }
    // Get current state atomically
    auto current_state = g_timeslowdown_state.load();

    if (!settings::g_experimentalTabSettings.timeslowdown_enabled.GetValue()
       && settings::g_experimentalTabSettings.timeslowdown_compatibility_mode.GetValue()) {
        return result;
    }

    // if time slow down is enabled, or was enabled before, then apply the time slow down
    if (current_state->original_quad_ts > 0 || settings::g_experimentalTabSettings.timeslowdown_enabled.GetValue()) {
        LONGLONG now_qpc = lpPerformanceCount->QuadPart;
        float new_multiplier = settings::g_experimentalTabSettings.timeslowdown_enabled.GetValue()
                                   ? settings::g_experimentalTabSettings.timeslowdown_multiplier.GetValue()
                                   : 1.0f;

        // Check if we need to update state
        bool needs_update = (current_state->original_quad_ts == 0) || (current_state->multiplier != new_multiplier);

        if (needs_update) {
            // Create new state with updated values
            auto new_state = std::make_shared<TimeslowdownState>(*current_state);

            if (new_state->original_quad_ts == 0) {
                new_state->original_quad_ts = now_qpc;
                new_state->original_quad_value = now_qpc;
            } else if (current_state->multiplier != new_multiplier) {
                new_state->original_quad_value =
                    current_state->original_quad_value +
                    (now_qpc - current_state->original_quad_ts) * current_state->multiplier;
                new_state->original_quad_ts = now_qpc;
            }

            new_state->multiplier = new_multiplier;

            // Atomically update the state
            g_timeslowdown_state.store(new_state);
            current_state = new_state;
        }

        // Apply timeslowdown calculation
        lpPerformanceCount->QuadPart = current_state->original_quad_value +
                                        (now_qpc - current_state->original_quad_ts) * current_state->multiplier;
    }

    return true;
}

// Hooked QueryPerformanceFrequency function
BOOL WINAPI QueryPerformanceFrequency_Detour(LARGE_INTEGER *lpFrequency) {
    g_qpf_call_count.fetch_add(1, std::memory_order_relaxed);
    if (!QueryPerformanceFrequency_Original) {
        return QueryPerformanceFrequency(lpFrequency);
    }

    // Call original function first
    BOOL result = QueryPerformanceFrequency_Original(lpFrequency);

    return result;
}

// Hooked GetTickCount function
DWORD WINAPI GetTickCount_Detour() {
    g_get_tick_count_call_count.fetch_add(1, std::memory_order_relaxed);
    if (!GetTickCount_Original) {
        return GetTickCount();
    }

    // Call original function first
    DWORD result = GetTickCount_Original();

    // Check if this hook should be applied (efficient enum-based check)
    if (!ShouldApplyHookById(TimerHookIdentifier::GetTickCount)) {
        return result;
    }

    // Apply timeslowdown with baseline tracking
    return ApplyTimeslowdownWithBaseline(result);
}

// Hooked GetTickCount64 function
ULONGLONG WINAPI GetTickCount64_Detour() {
    g_get_tick_count64_call_count.fetch_add(1, std::memory_order_relaxed);
    if (!GetTickCount64_Original) {
        return GetTickCount64();
    }

    // Call original function first
    ULONGLONG result = GetTickCount64_Original();

    // Check if this hook should be applied (efficient enum-based check)
    if (!ShouldApplyHookById(TimerHookIdentifier::GetTickCount64)) {
        return result;
    }

    // Apply timeslowdown with baseline tracking
    return ApplyTimeslowdownWithBaseline(result);
}

// Initialize timeGetTime direct function pointer
static void InitializeTimeGetTimeDirect() {
    if (timeGetTime_Direct) {
        return; // Already initialized
    }

    HMODULE winmm_module = LoadLibraryA("winmm.dll");
    if (winmm_module) {
        timeGetTime_Direct = (timeGetTime_pfn)GetProcAddress(winmm_module, "timeGetTime");
        if (timeGetTime_Direct) {
            LogInfo("timeGetTime direct function initialized successfully");
        } else {
            LogWarn("timeGetTime not found in winmm.dll");
        }
    } else {
        LogInfo("winmm.dll not found - timeGetTime will be unavailable");
    }
}

// Hooked timeGetTime function
DWORD WINAPI timeGetTime_Detour() {
    g_time_get_time_call_count.fetch_add(1, std::memory_order_relaxed);
    if (!timeGetTime_Original) {
        return timeGetTime_Direct ? timeGetTime_Direct() : 0;
    }

    // Call original function first
    DWORD result = timeGetTime_Original();

    // Check if this hook should be applied (efficient enum-based check)
    if (!ShouldApplyHookById(TimerHookIdentifier::TimeGetTime)) {
        return result;
    }

    // Apply timeslowdown if enabled
    if (settings::g_experimentalTabSettings.timeslowdown_enabled.GetValue()) {
        float multiplier = settings::g_experimentalTabSettings.timeslowdown_multiplier.GetValue();
        if (multiplier > 0.0) {
            result = static_cast<DWORD>(result * multiplier);
        }
    }

    return result;
}

// Hooked GetSystemTime function
void WINAPI GetSystemTime_Detour(LPSYSTEMTIME lpSystemTime) {
    g_get_system_time_call_count.fetch_add(1, std::memory_order_relaxed);
    if (!GetSystemTime_Original) {
        GetSystemTime(lpSystemTime);
        return;
    }

    // Call original function first
    GetSystemTime_Original(lpSystemTime);

    // Check if this hook should be applied (efficient enum-based check)
    if (!ShouldApplyHookById(TimerHookIdentifier::GetSystemTime)) {
        return;
    }

    // Apply timeslowdown if enabled
    if (settings::g_experimentalTabSettings.timeslowdown_enabled.GetValue() && lpSystemTime != nullptr) {
        float multiplier = settings::g_experimentalTabSettings.timeslowdown_multiplier.GetValue();
        if (multiplier > 0.0) {
            // Convert SYSTEMTIME to milliseconds since epoch, apply multiplier, then convert back
            FILETIME ft;
            SystemTimeToFileTime(lpSystemTime, &ft);

            ULARGE_INTEGER uli;
            uli.LowPart = ft.dwLowDateTime;
            uli.HighPart = ft.dwHighDateTime;

            // Apply multiplier to milliseconds since epoch
            uli.QuadPart = static_cast<ULONGLONG>(uli.QuadPart * multiplier);

            ft.dwLowDateTime = uli.LowPart;
            ft.dwHighDateTime = uli.HighPart;

            FileTimeToSystemTime(&ft, lpSystemTime);
        }
    }
}

// Hooked GetSystemTimeAsFileTime function
void WINAPI GetSystemTimeAsFileTime_Detour(LPFILETIME lpSystemTimeAsFileTime) {
    g_get_system_time_as_file_time_call_count.fetch_add(1, std::memory_order_relaxed);
    if (!GetSystemTimeAsFileTime_Original) {
        GetSystemTimeAsFileTime(lpSystemTimeAsFileTime);
        return;
    }

    // Call original function first
    GetSystemTimeAsFileTime_Original(lpSystemTimeAsFileTime);

    // Check if this hook should be applied (efficient enum-based check)
    if (!ShouldApplyHookById(TimerHookIdentifier::GetSystemTimeAsFileTime)) {
        return;
    }

    // Apply timeslowdown if enabled
    if (settings::g_experimentalTabSettings.timeslowdown_enabled.GetValue() && lpSystemTimeAsFileTime != nullptr) {
        float multiplier = settings::g_experimentalTabSettings.timeslowdown_multiplier.GetValue();
        if (multiplier > 0.0) {
            ULARGE_INTEGER uli;
            uli.LowPart = lpSystemTimeAsFileTime->dwLowDateTime;
            uli.HighPart = lpSystemTimeAsFileTime->dwHighDateTime;

            // Apply multiplier to file time
            uli.QuadPart = static_cast<ULONGLONG>(uli.QuadPart * multiplier);

            lpSystemTimeAsFileTime->dwLowDateTime = uli.LowPart;
            lpSystemTimeAsFileTime->dwHighDateTime = uli.HighPart;
        }
    }
}

// Hooked GetSystemTimePreciseAsFileTime function
void WINAPI GetSystemTimePreciseAsFileTime_Detour(LPFILETIME lpSystemTimeAsFileTime) {
    g_get_system_time_precise_as_file_time_call_count.fetch_add(1, std::memory_order_relaxed);
    if (!GetSystemTimePreciseAsFileTime_Original) {
        GetSystemTimePreciseAsFileTime(lpSystemTimeAsFileTime);
        return;
    }

    // Call original function first
    GetSystemTimePreciseAsFileTime_Original(lpSystemTimeAsFileTime);

    // Check if this hook should be applied (efficient enum-based check)
    if (!ShouldApplyHookById(TimerHookIdentifier::GetSystemTimePreciseAsFileTime)) {
        return;
    }

    // Apply timeslowdown if enabled
    if (settings::g_experimentalTabSettings.timeslowdown_enabled.GetValue() && lpSystemTimeAsFileTime != nullptr) {
        float multiplier = settings::g_experimentalTabSettings.timeslowdown_multiplier.GetValue();
        if (multiplier > 0.0) {
            ULARGE_INTEGER uli;
            uli.LowPart = lpSystemTimeAsFileTime->dwLowDateTime;
            uli.HighPart = lpSystemTimeAsFileTime->dwHighDateTime;

            // Apply multiplier to file time
            uli.QuadPart = static_cast<ULONGLONG>(uli.QuadPart * multiplier);

            lpSystemTimeAsFileTime->dwLowDateTime = uli.LowPart;
            lpSystemTimeAsFileTime->dwHighDateTime = uli.HighPart;
        }
    }
}

// Hooked GetLocalTime function
void WINAPI GetLocalTime_Detour(LPSYSTEMTIME lpSystemTime) {
    g_get_local_time_call_count.fetch_add(1, std::memory_order_relaxed);
    if (!GetLocalTime_Original) {
        GetLocalTime(lpSystemTime);
        return;
    }

    // Call original function first
    GetLocalTime_Original(lpSystemTime);

    // Check if this hook should be applied (efficient enum-based check)
    if (!ShouldApplyHookById(TimerHookIdentifier::GetLocalTime)) {
        return;
    }

    // Apply timeslowdown if enabled
    if (settings::g_experimentalTabSettings.timeslowdown_enabled.GetValue() && lpSystemTime != nullptr) {
        float multiplier = settings::g_experimentalTabSettings.timeslowdown_multiplier.GetValue();
        if (multiplier > 0.0) {
            // Convert SYSTEMTIME to milliseconds since epoch, apply multiplier, then convert back
            FILETIME ft;
            SystemTimeToFileTime(lpSystemTime, &ft);

            ULARGE_INTEGER uli;
            uli.LowPart = ft.dwLowDateTime;
            uli.HighPart = ft.dwHighDateTime;

            // Apply multiplier to milliseconds since epoch
            uli.QuadPart = static_cast<ULONGLONG>(uli.QuadPart * multiplier);

            ft.dwLowDateTime = uli.LowPart;
            ft.dwHighDateTime = uli.HighPart;

            FileTimeToSystemTime(&ft, lpSystemTime);
        }
    }
}

// Hooked NtQuerySystemTime function
NTSTATUS WINAPI NtQuerySystemTime_Detour(PLARGE_INTEGER SystemTime) {
    g_nt_query_system_time_call_count.fetch_add(1, std::memory_order_relaxed);
    if (!NtQuerySystemTime_Original) {
        // Load ntdll.dll and get the function
        HMODULE ntdll = GetModuleHandleA("ntdll.dll");
        if (ntdll) {
            NtQuerySystemTime_Original = (NtQuerySystemTime_pfn)GetProcAddress(ntdll, "NtQuerySystemTime");
        }
        if (!NtQuerySystemTime_Original) {
            return STATUS_UNSUCCESSFUL;
        }
    }

    // Call original function first
    NTSTATUS result = NtQuerySystemTime_Original(SystemTime);

    // Check if this hook should be applied (efficient enum-based check)
    if (!ShouldApplyHookById(TimerHookIdentifier::NtQuerySystemTime)) {
        return result;
    }

    // Apply timeslowdown if enabled
    if (settings::g_experimentalTabSettings.timeslowdown_enabled.GetValue() && SystemTime != nullptr &&
        NT_SUCCESS(result)) {
        float multiplier = settings::g_experimentalTabSettings.timeslowdown_multiplier.GetValue();
        if (multiplier > 0.0) {
            // Apply multiplier to system time
            SystemTime->QuadPart = static_cast<LONGLONG>(SystemTime->QuadPart * multiplier);
        }
    }

    return result;
}

bool InstallTimeslowdownHooks() {
    if (g_timeslowdown_hooks_installed.load()) {
        LogInfo("Timeslowdown hooks already installed");
        return true;
    }

    // Check if Time Slowdown hooks should be suppressed
    if (display_commanderhooks::HookSuppressionManager::GetInstance().ShouldSuppressHook(display_commanderhooks::HookType::TIMESLOWDOWN)) {
        LogInfo("Time Slowdown hooks installation suppressed by user setting");
        return false;
    }

    // Initialize MinHook (only if not already initialized)
    MH_STATUS init_status = SafeInitializeMinHook(display_commanderhooks::HookType::TIMESLOWDOWN);
    if (init_status != MH_OK && init_status != MH_ERROR_ALREADY_INITIALIZED) {
        LogError("Failed to initialize MinHook for timeslowdown hooks - Status: %d", init_status);
        return false;
    }

    if (init_status == MH_ERROR_ALREADY_INITIALIZED) {
        LogInfo("MinHook already initialized, proceeding with timeslowdown hooks");
    } else {
        LogInfo("MinHook initialized successfully for timeslowdown hooks");
    }

    // Initialize hook types to None by default (atomic variables are already initialized)
    // No mutex needed - atomic variables are thread-safe

    // Hook QueryPerformanceCounter
    if (!CreateAndEnableHook(QueryPerformanceCounter, QueryPerformanceCounter_Detour,
                             (LPVOID *)&QueryPerformanceCounter_Original, HOOK_QUERY_PERFORMANCE_COUNTER)) {
        LogError("Failed to create and enable QueryPerformanceCounter hook");
        return false;
    }

    // Hook QueryPerformanceFrequency
    if (!CreateAndEnableHook(QueryPerformanceFrequency, QueryPerformanceFrequency_Detour,
                             (LPVOID *)&QueryPerformanceFrequency_Original, "QueryPerformanceFrequency")) {
        LogError("Failed to create and enable QueryPerformanceFrequency hook");
        return false;
    }

    // Hook GetTickCount
    if (!CreateAndEnableHook(GetTickCount, GetTickCount_Detour, (LPVOID *)&GetTickCount_Original, HOOK_GET_TICK_COUNT)) {
        LogError("Failed to create and enable GetTickCount hook");
        return false;
    }

    // Hook GetTickCount64
    if (!CreateAndEnableHook(GetTickCount64, GetTickCount64_Detour, (LPVOID *)&GetTickCount64_Original, HOOK_GET_TICK_COUNT64)) {
        LogError("Failed to create and enable GetTickCount64 hook");
        return false;
    }

    // Initialize timeGetTime direct function first
    InitializeTimeGetTimeDirect();

    // Hook timeGetTime (only if we have the direct function)
    if (timeGetTime_Direct) {
        if (!CreateAndEnableHook(timeGetTime_Direct, timeGetTime_Detour, (LPVOID *)&timeGetTime_Original, HOOK_TIME_GET_TIME)) {
            LogError("Failed to create and enable timeGetTime hook");
            return false;
        }
    } else {
        LogInfo("timeGetTime not available - skipping hook installation");
    }

    // Hook GetSystemTime
    if (!CreateAndEnableHook(GetSystemTime, GetSystemTime_Detour, (LPVOID *)&GetSystemTime_Original, HOOK_GET_SYSTEM_TIME)) {
        LogError("Failed to create and enable GetSystemTime hook");
        return false;
    }

    // Hook GetSystemTimeAsFileTime
    if (!CreateAndEnableHook(GetSystemTimeAsFileTime, GetSystemTimeAsFileTime_Detour,
                             (LPVOID *)&GetSystemTimeAsFileTime_Original, HOOK_GET_SYSTEM_TIME_AS_FILE_TIME)) {
        LogError("Failed to create and enable GetSystemTimeAsFileTime hook");
        return false;
    }

    // Hook GetSystemTimePreciseAsFileTime
    if (!CreateAndEnableHook(GetSystemTimePreciseAsFileTime, GetSystemTimePreciseAsFileTime_Detour,
                             (LPVOID *)&GetSystemTimePreciseAsFileTime_Original, HOOK_GET_SYSTEM_TIME_PRECISE_AS_FILE_TIME)) {
        LogError("Failed to create and enable GetSystemTimePreciseAsFileTime hook");
        return false;
    }

    // Hook GetLocalTime
    if (!CreateAndEnableHook(GetLocalTime, GetLocalTime_Detour, (LPVOID *)&GetLocalTime_Original, HOOK_GET_LOCAL_TIME)) {
        LogError("Failed to create and enable GetLocalTime hook");
        return false;
    }

    // Hook NtQuerySystemTime (dynamically loaded)
    HMODULE ntdll = GetModuleHandleA("ntdll.dll");
    if (ntdll) {
        LPVOID ntQuerySystemTime = GetProcAddress(ntdll, "NtQuerySystemTime");
        if (ntQuerySystemTime) {
            if (!CreateAndEnableHook(ntQuerySystemTime, NtQuerySystemTime_Detour, (LPVOID *)&NtQuerySystemTime_Original, HOOK_NT_QUERY_SYSTEM_TIME)) {
                LogError("Failed to create and enable NtQuerySystemTime hook");
                return false;
            }
        } else {
            LogInfo("NtQuerySystemTime not available on this system");
        }
    } else {
        LogInfo("ntdll.dll not available for NtQuerySystemTime hook");
    }

    g_timeslowdown_hooks_installed.store(true);
    LogInfo("Timeslowdown hooks installed successfully");

    // Mark Time Slowdown hooks as installed
    display_commanderhooks::HookSuppressionManager::GetInstance().MarkHookInstalled(display_commanderhooks::HookType::TIMESLOWDOWN);

    return true;
}

void UninstallTimeslowdownHooks() {
    if (!g_timeslowdown_hooks_installed.load()) {
        LogInfo("Timeslowdown hooks not installed");
        return;
    }

    // Disable all hooks
    MH_DisableHook(MH_ALL_HOOKS);

    // Remove hooks
    MH_RemoveHook(QueryPerformanceCounter);
    MH_RemoveHook(QueryPerformanceFrequency);
    MH_RemoveHook(GetTickCount);
    MH_RemoveHook(GetTickCount64);
    if (timeGetTime_Direct) {
        MH_RemoveHook(timeGetTime_Direct);
    }
    MH_RemoveHook(GetSystemTime);
    MH_RemoveHook(GetSystemTimeAsFileTime);
    MH_RemoveHook(GetSystemTimePreciseAsFileTime);
    MH_RemoveHook(GetLocalTime);

    // Remove NtQuerySystemTime hook if it was created
    if (NtQuerySystemTime_Original) {
        HMODULE ntdll = GetModuleHandleA("ntdll.dll");
        if (ntdll) {
            LPVOID ntQuerySystemTime = GetProcAddress(ntdll, "NtQuerySystemTime");
            if (ntQuerySystemTime) {
                MH_RemoveHook(ntQuerySystemTime);
            }
        }
    }

    // Clean up
    QueryPerformanceCounter_Original = nullptr;
    QueryPerformanceFrequency_Original = nullptr;
    GetTickCount_Original = nullptr;
    GetTickCount64_Original = nullptr;
    timeGetTime_Original = nullptr;
    timeGetTime_Direct = nullptr;
    GetSystemTime_Original = nullptr;
    GetSystemTimeAsFileTime_Original = nullptr;
    GetSystemTimePreciseAsFileTime_Original = nullptr;
    GetLocalTime_Original = nullptr;
    NtQuerySystemTime_Original = nullptr;

    // Clean up state
    g_timeslowdown_state.store(std::make_shared<TimeslowdownState>());

    // Reset hook types to None (atomic variables don't need explicit cleanup)
    g_query_performance_counter_hook_type.store(TimerHookType::None);
    g_get_tick_count_hook_type.store(TimerHookType::None);
    g_get_tick_count64_hook_type.store(TimerHookType::None);
    g_time_get_time_hook_type.store(TimerHookType::None);
    g_get_system_time_hook_type.store(TimerHookType::None);
    g_get_system_time_as_file_time_hook_type.store(TimerHookType::None);
    g_get_system_time_precise_as_file_time_hook_type.store(TimerHookType::None);
    g_get_local_time_hook_type.store(TimerHookType::None);
    g_nt_query_system_time_hook_type.store(TimerHookType::None);

    g_timeslowdown_hooks_installed.store(false);
    LogInfo("Timeslowdown hooks uninstalled successfully");
}

bool AreTimeslowdownHooksInstalled() { return g_timeslowdown_hooks_installed.load(); }

void SetTimeslowdownMultiplier(float multiplier) {
    if (multiplier <= 0.0f) {
        LogError("Invalid timeslowdown multiplier: %f (must be > 0)", multiplier);
        return;
    }

    settings::g_experimentalTabSettings.timeslowdown_multiplier.SetValue(multiplier);
    LogInfo("Timeslowdown multiplier set to: %f", multiplier);
}

float GetTimeslowdownMultiplier() { return settings::g_experimentalTabSettings.timeslowdown_multiplier.GetValue(); }

bool IsTimeslowdownEnabled() { return settings::g_experimentalTabSettings.timeslowdown_enabled.GetValue(); }

void SetTimeslowdownEnabled(bool enabled) {
    settings::g_experimentalTabSettings.timeslowdown_enabled.SetValue(enabled);
    LogInfo("Timeslowdown %s", enabled ? "enabled" : "disabled");
}

// Efficient enum-based hook type configuration (DLL-safe)
void SetTimerHookTypeById(TimerHookIdentifier id, TimerHookType type) {
    switch (id) {
        case TimerHookIdentifier::QueryPerformanceCounter:
            g_query_performance_counter_hook_type.store(type);
            break;
        case TimerHookIdentifier::GetTickCount:
            g_get_tick_count_hook_type.store(type);
            break;
        case TimerHookIdentifier::GetTickCount64:
            g_get_tick_count64_hook_type.store(type);
            break;
        case TimerHookIdentifier::TimeGetTime:
            g_time_get_time_hook_type.store(type);
            break;
        case TimerHookIdentifier::GetSystemTime:
            g_get_system_time_hook_type.store(type);
            break;
        case TimerHookIdentifier::GetSystemTimeAsFileTime:
            g_get_system_time_as_file_time_hook_type.store(type);
            break;
        case TimerHookIdentifier::GetSystemTimePreciseAsFileTime:
            g_get_system_time_precise_as_file_time_hook_type.store(type);
            break;
        case TimerHookIdentifier::GetLocalTime:
            g_get_local_time_hook_type.store(type);
            break;
        case TimerHookIdentifier::NtQuerySystemTime:
            g_nt_query_system_time_hook_type.store(type);
            break;
        default:
            return; // Invalid identifier
    }
}

// Individual hook type configuration (DLL-safe) - kept for backward compatibility
void SetTimerHookType(const char *hook_name, TimerHookType type) {
    TimerHookIdentifier id = GetHookIdentifierByName(hook_name);
    if (id != TimerHookIdentifier::Count) {
        SetTimerHookTypeById(id, type);
        LogInfo("Timer hook %s set to %s", hook_name, type == TimerHookType::None ? "None" : "Enabled");
    }
}

// Efficient enum-based functions
TimerHookType GetTimerHookTypeById(TimerHookIdentifier id) {
    return GetHookTypeById(id);
}

bool IsTimerHookEnabledById(TimerHookIdentifier id) {
    TimerHookType type = GetTimerHookTypeById(id);
    return type == TimerHookType::Enabled;
}

uint64_t GetTimerHookCallCountById(TimerHookIdentifier id) {
    switch (id) {
        case TimerHookIdentifier::QueryPerformanceCounter:
            return g_qpc_call_count.load(std::memory_order_relaxed);
        case TimerHookIdentifier::GetTickCount:
            return g_get_tick_count_call_count.load(std::memory_order_relaxed);
        case TimerHookIdentifier::GetTickCount64:
            return g_get_tick_count64_call_count.load(std::memory_order_relaxed);
        case TimerHookIdentifier::TimeGetTime:
            return g_time_get_time_call_count.load(std::memory_order_relaxed);
        case TimerHookIdentifier::GetSystemTime:
            return g_get_system_time_call_count.load(std::memory_order_relaxed);
        case TimerHookIdentifier::GetSystemTimeAsFileTime:
            return g_get_system_time_as_file_time_call_count.load(std::memory_order_relaxed);
        case TimerHookIdentifier::GetSystemTimePreciseAsFileTime:
            return g_get_system_time_precise_as_file_time_call_count.load(std::memory_order_relaxed);
        case TimerHookIdentifier::GetLocalTime:
            return g_get_local_time_call_count.load(std::memory_order_relaxed);
        case TimerHookIdentifier::NtQuerySystemTime:
            return g_nt_query_system_time_call_count.load(std::memory_order_relaxed);
        default:
            return 0;
    }
}

// Backward compatibility functions
TimerHookType GetTimerHookType(const char *hook_name) { return GetHookTypeByName(hook_name); }

bool IsTimerHookEnabled(const char *hook_name) {
    TimerHookType type = GetTimerHookType(hook_name);
    return type == TimerHookType::Enabled;
}

uint64_t GetTimerHookCallCount(const char *hook_name) {
    TimerHookIdentifier id = GetHookIdentifierByName(hook_name);
    return GetTimerHookCallCountById(id);
}

// UI helper functions for efficient hook management
static const TimerHookIdentifier g_all_hook_identifiers[] = {
    TimerHookIdentifier::QueryPerformanceCounter,
    TimerHookIdentifier::GetTickCount,
    TimerHookIdentifier::GetTickCount64,
    TimerHookIdentifier::TimeGetTime,
    TimerHookIdentifier::GetSystemTime,
    TimerHookIdentifier::GetSystemTimeAsFileTime,
    TimerHookIdentifier::GetSystemTimePreciseAsFileTime,
    TimerHookIdentifier::GetLocalTime,
    TimerHookIdentifier::NtQuerySystemTime
};

const TimerHookIdentifier* GetAllHookIdentifiers() {
    return g_all_hook_identifiers;
}

const char* GetHookNameById(TimerHookIdentifier id) {
    switch (id) {
        case TimerHookIdentifier::QueryPerformanceCounter:
            return HOOK_QUERY_PERFORMANCE_COUNTER;
        case TimerHookIdentifier::GetTickCount:
            return HOOK_GET_TICK_COUNT;
        case TimerHookIdentifier::GetTickCount64:
            return HOOK_GET_TICK_COUNT64;
        case TimerHookIdentifier::TimeGetTime:
            return HOOK_TIME_GET_TIME;
        case TimerHookIdentifier::GetSystemTime:
            return HOOK_GET_SYSTEM_TIME;
        case TimerHookIdentifier::GetSystemTimeAsFileTime:
            return HOOK_GET_SYSTEM_TIME_AS_FILE_TIME;
        case TimerHookIdentifier::GetSystemTimePreciseAsFileTime:
            return HOOK_GET_SYSTEM_TIME_PRECISE_AS_FILE_TIME;
        case TimerHookIdentifier::GetLocalTime:
            return HOOK_GET_LOCAL_TIME;
        case TimerHookIdentifier::NtQuerySystemTime:
            return HOOK_NT_QUERY_SYSTEM_TIME;
        default:
            return nullptr;
    }
}

// QPC calling module tracking functions
std::vector<std::wstring> GetQPCallingModules() {
    utils::SRWLockShared lock(g_qpc_modules_srwlock);
    std::vector<std::wstring> module_names;
    module_names.reserve(g_qpc_calling_modules.size());

    for (HMODULE hModule : g_qpc_calling_modules) {
        module_names.push_back(GetModuleNameFromHandle(hModule));
    }

    return module_names;
}

std::vector<std::pair<HMODULE, std::wstring>> GetQPCallingModulesWithHandles() {
    utils::SRWLockShared lock(g_qpc_modules_srwlock);
    std::vector<std::pair<HMODULE, std::wstring>> modules;
    modules.reserve(g_qpc_calling_modules.size());

    for (HMODULE hModule : g_qpc_calling_modules) {
        modules.push_back(std::make_pair(hModule, GetModuleNameFromHandle(hModule)));
    }

    return modules;
}

void ClearQPCallingModules() {
    utils::SRWLockExclusive lock(g_qpc_modules_srwlock);
    g_qpc_calling_modules.clear();
    g_qpc_module_enabled_states.clear();
}

bool IsQPCModuleEnabled(HMODULE hModule) {
    if (hModule == nullptr) {
        return false; // Default disabled
    }

    utils::SRWLockShared lock(g_qpc_modules_srwlock);
    auto it = g_qpc_module_enabled_states.find(hModule);
    if (it != g_qpc_module_enabled_states.end()) {
        return it->second;
    }
    return false; // Default disabled if not in map
}

void SetQPCModuleEnabled(HMODULE hModule, bool enabled) {
    if (hModule == nullptr) {
        return;
    }

    utils::SRWLockExclusive lock(g_qpc_modules_srwlock);
    g_qpc_module_enabled_states[hModule] = enabled;
    // Also ensure it's in the calling modules set
    g_qpc_calling_modules.insert(hModule);
}

void LoadQPCEnabledModulesFromSettings(const std::string& enabled_modules_str) {
    if (enabled_modules_str.empty()) {
        return;
    }

    utils::SRWLockExclusive lock(g_qpc_modules_srwlock);

    // Parse comma-separated module names
    std::stringstream ss(enabled_modules_str);
    std::string module_name;
    std::set<std::wstring> enabled_module_names;

    while (std::getline(ss, module_name, ',')) {
        // Trim whitespace
        module_name.erase(0, module_name.find_first_not_of(" \t"));
        module_name.erase(module_name.find_last_not_of(" \t") + 1);
        if (!module_name.empty()) {
            // Convert to wide string for comparison
            std::wstring wmodule_name(module_name.begin(), module_name.end());
            enabled_module_names.insert(wmodule_name);
        }
    }

    // Update enabled states based on module names
    for (HMODULE hModule : g_qpc_calling_modules) {
        std::wstring module_name = GetModuleNameFromHandle(hModule);
        bool enabled = enabled_module_names.find(module_name) != enabled_module_names.end();
        g_qpc_module_enabled_states[hModule] = enabled;
    }
}

std::string SaveQPCEnabledModulesToSettings() {
    utils::SRWLockShared lock(g_qpc_modules_srwlock);

    std::vector<std::wstring> enabled_modules;
    for (const auto& pair : g_qpc_module_enabled_states) {
        if (pair.second) { // If enabled
            std::wstring module_name = GetModuleNameFromHandle(pair.first);
            if (!module_name.empty() && module_name != L"<null>" && module_name != L"<unknown>") {
                enabled_modules.push_back(module_name);
            }
        }
    }

    // Convert to comma-separated string
    std::string result;
    for (size_t i = 0; i < enabled_modules.size(); ++i) {
        // Convert wide string to narrow string
        std::string narrow_name(enabled_modules[i].begin(), enabled_modules[i].end());
        if (i > 0) {
            result += ",";
        }
        result += narrow_name;
    }

    return result;
}

} // namespace display_commanderhooks
