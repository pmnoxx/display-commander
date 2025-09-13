#include "timeslowdown_hooks.hpp"
#include "../utils.hpp"
#include "../globals.hpp"
#include "../settings/experimental_tab_settings.hpp"
#include <MinHook.h>
#include <atomic>
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

namespace renodx::hooks {

// Hook type constants
const char* HOOK_QUERY_PERFORMANCE_COUNTER = "QueryPerformanceCounter";
const char* HOOK_GET_TICK_COUNT = "GetTickCount";
const char* HOOK_GET_TICK_COUNT64 = "GetTickCount64";
const char* HOOK_TIME_GET_TIME = "timeGetTime";
const char* HOOK_GET_SYSTEM_TIME = "GetSystemTime";
const char* HOOK_GET_SYSTEM_TIME_AS_FILE_TIME = "GetSystemTimeAsFileTime";
const char* HOOK_GET_SYSTEM_TIME_PRECISE_AS_FILE_TIME = "GetSystemTimePreciseAsFileTime";
const char* HOOK_GET_LOCAL_TIME = "GetLocalTime";
const char* HOOK_NT_QUERY_SYSTEM_TIME = "NtQuerySystemTime";

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
GetSystemTime_pfn GetSystemTime_Original = nullptr;
GetSystemTimeAsFileTime_pfn GetSystemTimeAsFileTime_Original = nullptr;
GetSystemTimePreciseAsFileTime_pfn GetSystemTimePreciseAsFileTime_Original = nullptr;
GetLocalTime_pfn GetLocalTime_Original = nullptr;
NtQuerySystemTime_pfn NtQuerySystemTime_Original = nullptr;

// Hook state
static std::atomic<bool> g_timeslowdown_hooks_installed{false};

// Timeslowdown configuration - now using global instance

// Individual hook type configurations - using atomic variables for DLL safety
static std::atomic<TimerHookType> g_query_performance_counter_hook_type{TimerHookType::None};
static std::atomic<TimerHookType> g_get_tick_count_hook_type{TimerHookType::None};
static std::atomic<TimerHookType> g_get_tick_count64_hook_type{TimerHookType::None};
static std::atomic<TimerHookType> g_time_get_time_hook_type{TimerHookType::None};
static std::atomic<TimerHookType> g_get_system_time_hook_type{TimerHookType::None};
static std::atomic<TimerHookType> g_get_system_time_as_file_time_hook_type{TimerHookType::None};
static std::atomic<TimerHookType> g_get_system_time_precise_as_file_time_hook_type{TimerHookType::None};
static std::atomic<TimerHookType> g_get_local_time_hook_type{TimerHookType::None};
static std::atomic<TimerHookType> g_nt_query_system_time_hook_type{TimerHookType::None};

// Atomic pointer for thread-safe state access
static std::atomic<TimeslowdownState*> g_timeslowdown_state{new TimeslowdownState()};

const bool min_enabled = false;

// Helper function to get hook type by name (DLL-safe)
TimerHookType GetHookTypeByName(const char* hook_name) {
    if (strcmp(hook_name, HOOK_QUERY_PERFORMANCE_COUNTER) == 0) {
        return g_query_performance_counter_hook_type.load();
    } else if (strcmp(hook_name, HOOK_GET_TICK_COUNT) == 0) {
        return g_get_tick_count_hook_type.load();
    } else if (strcmp(hook_name, HOOK_GET_TICK_COUNT64) == 0) {
        return g_get_tick_count64_hook_type.load();
    } else if (strcmp(hook_name, HOOK_TIME_GET_TIME) == 0) {
        return g_time_get_time_hook_type.load();
    } else if (strcmp(hook_name, HOOK_GET_SYSTEM_TIME) == 0) {
        return g_get_system_time_hook_type.load();
    } else if (strcmp(hook_name, HOOK_GET_SYSTEM_TIME_AS_FILE_TIME) == 0) {
        return g_get_system_time_as_file_time_hook_type.load();
    } else if (strcmp(hook_name, HOOK_GET_SYSTEM_TIME_PRECISE_AS_FILE_TIME) == 0) {
        return g_get_system_time_precise_as_file_time_hook_type.load();
    } else if (strcmp(hook_name, HOOK_GET_LOCAL_TIME) == 0) {
        return g_get_local_time_hook_type.load();
    } else if (strcmp(hook_name, HOOK_NT_QUERY_SYSTEM_TIME) == 0) {
        return g_nt_query_system_time_hook_type.load();
    }
    return TimerHookType::None;
}

// Helper function to check if a hook should be applied (DLL-safe)
bool ShouldApplyHook(const char* hook_name) {
    TimerHookType type = GetHookTypeByName(hook_name);

    if (type == TimerHookType::None) {
        return false;
    }

    if (type == TimerHookType::RenderThreadOnly) {
        DWORD render_thread_id = g_render_thread_id.load();
        if (render_thread_id == 0) {
            return false; // Render thread unknown
        }
        return (GetCurrentThreadId() == render_thread_id);
    }

    if (type == TimerHookType::EverythingExceptRenderThread) {
        DWORD render_thread_id = g_render_thread_id.load();
        if (render_thread_id == 0) {
            return true; // Render thread unknown, apply to all threads
        }
        return (GetCurrentThreadId() != render_thread_id);
    }

    return true; // TimerHookType::Enabled
}

// Hooked QueryPerformanceCounter function
BOOL WINAPI QueryPerformanceCounter_Detour(LARGE_INTEGER* lpPerformanceCount) {
    if (!QueryPerformanceCounter_Original) {
        return QueryPerformanceCounter(lpPerformanceCount);
    }

    // Call original function first
    BOOL result = QueryPerformanceCounter_Original(lpPerformanceCount);
    if (result == FALSE || lpPerformanceCount == nullptr) {
        return result;
    }

    // Check if this hook should be applied
    if (!ShouldApplyHook(HOOK_QUERY_PERFORMANCE_COUNTER)) {
        return result;
    }

    // Get current state atomically
    TimeslowdownState* current_state = g_timeslowdown_state.load();

    // if time slow down is enabled, or was enabled before, then apply the time slow down
    if (current_state->original_quad_ts > 0 || settings::g_experimentalTabSettings.timeslowdown_enabled.GetValue()) {
        LONGLONG now_qpc = lpPerformanceCount->QuadPart;
        float new_multiplier = settings::g_experimentalTabSettings.timeslowdown_enabled.GetValue() ? settings::g_experimentalTabSettings.timeslowdown_multiplier.GetValue() : 1.0f;

        // Check if we need to update state
        bool needs_update = (current_state->original_quad_ts == 0) || (current_state->multiplier != new_multiplier);

        if (needs_update) {
            // Create new state with updated values
            auto new_state = new TimeslowdownState(*current_state);

            if (new_state->original_quad_ts == 0) {
                new_state->original_quad_ts = now_qpc;
                new_state->original_quad_value = now_qpc;
            } else if (current_state->multiplier != new_multiplier) {
                if (min_enabled) {
                    new_state->original_quad_value = max(now_qpc, current_state->original_quad_value + (now_qpc - current_state->original_quad_ts) * current_state->multiplier);
                } else {
                    new_state->original_quad_value = current_state->original_quad_value + (now_qpc - current_state->original_quad_ts) * current_state->multiplier;
                }
                new_state->original_quad_ts = now_qpc;
            }

            new_state->multiplier = new_multiplier;

            // Atomically update the state and clean up old state
            TimeslowdownState* old_state = g_timeslowdown_state.exchange(new_state);
            delete old_state;
            current_state = new_state;
        }

        // Apply timeslowdown calculation
        if (min_enabled) {
            lpPerformanceCount->QuadPart = min(now_qpc, current_state->original_quad_value + (now_qpc - current_state->original_quad_ts) * current_state->multiplier);
        } else {
            lpPerformanceCount->QuadPart = current_state->original_quad_value + (now_qpc - current_state->original_quad_ts) * current_state->multiplier;
        }
    }

    return true;
}

// Hooked QueryPerformanceFrequency function
BOOL WINAPI QueryPerformanceFrequency_Detour(LARGE_INTEGER* lpFrequency) {
    if (!QueryPerformanceFrequency_Original) {
        return QueryPerformanceFrequency(lpFrequency);
    }

    // Call original function first
    BOOL result = QueryPerformanceFrequency_Original(lpFrequency);

    return result;
}

// Hooked GetTickCount function
DWORD WINAPI GetTickCount_Detour() {
    if (!GetTickCount_Original) {
        return GetTickCount();
    }

    // Call original function first
    DWORD result = GetTickCount_Original();

    // Check if this hook should be applied
    if (!ShouldApplyHook(HOOK_GET_TICK_COUNT)) {
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

// Hooked GetTickCount64 function
ULONGLONG WINAPI GetTickCount64_Detour() {
    if (!GetTickCount64_Original) {
        return GetTickCount64();
    }

    // Call original function first
    ULONGLONG result = GetTickCount64_Original();

    // Check if this hook should be applied
    if (!ShouldApplyHook(HOOK_GET_TICK_COUNT64)) {
        return result;
    }

    // Apply timeslowdown if enabled
    if (settings::g_experimentalTabSettings.timeslowdown_enabled.GetValue()) {
        float multiplier = settings::g_experimentalTabSettings.timeslowdown_multiplier.GetValue();
        if (multiplier > 0.0) {
            result = static_cast<ULONGLONG>(result * multiplier);
        }
    }

    return result;
}

// Hooked timeGetTime function
DWORD WINAPI timeGetTime_Detour() {
    if (!timeGetTime_Original) {
        return timeGetTime();
    }

    // Call original function first
    DWORD result = timeGetTime_Original();

    // Check if this hook should be applied
    if (!ShouldApplyHook(HOOK_TIME_GET_TIME)) {
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
    if (!GetSystemTime_Original) {
        GetSystemTime(lpSystemTime);
        return;
    }

    // Call original function first
    GetSystemTime_Original(lpSystemTime);

    // Check if this hook should be applied
    if (!ShouldApplyHook(HOOK_GET_SYSTEM_TIME)) {
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
    if (!GetSystemTimeAsFileTime_Original) {
        GetSystemTimeAsFileTime(lpSystemTimeAsFileTime);
        return;
    }

    // Call original function first
    GetSystemTimeAsFileTime_Original(lpSystemTimeAsFileTime);

    // Check if this hook should be applied
    if (!ShouldApplyHook(HOOK_GET_SYSTEM_TIME_AS_FILE_TIME)) {
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
    if (!GetSystemTimePreciseAsFileTime_Original) {
        GetSystemTimePreciseAsFileTime(lpSystemTimeAsFileTime);
        return;
    }

    // Call original function first
    GetSystemTimePreciseAsFileTime_Original(lpSystemTimeAsFileTime);

    // Check if this hook should be applied
    if (!ShouldApplyHook(HOOK_GET_SYSTEM_TIME_PRECISE_AS_FILE_TIME)) {
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
    if (!GetLocalTime_Original) {
        GetLocalTime(lpSystemTime);
        return;
    }

    // Call original function first
    GetLocalTime_Original(lpSystemTime);

    // Check if this hook should be applied
    if (!ShouldApplyHook(HOOK_GET_LOCAL_TIME)) {
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

    // Check if this hook should be applied
    if (!ShouldApplyHook(HOOK_NT_QUERY_SYSTEM_TIME)) {
        return result;
    }

    // Apply timeslowdown if enabled
    if (settings::g_experimentalTabSettings.timeslowdown_enabled.GetValue() && SystemTime != nullptr && NT_SUCCESS(result)) {
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

    // Initialize MinHook (only if not already initialized)
    MH_STATUS init_status = MH_Initialize();
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
    if (MH_CreateHook(QueryPerformanceCounter, QueryPerformanceCounter_Detour,
                      (LPVOID*)&QueryPerformanceCounter_Original) != MH_OK) {
        LogError("Failed to create QueryPerformanceCounter hook");
        return false;
    }

    // Hook QueryPerformanceFrequency
    if (MH_CreateHook(QueryPerformanceFrequency, QueryPerformanceFrequency_Detour,
                      (LPVOID*)&QueryPerformanceFrequency_Original) != MH_OK) {
        LogError("Failed to create QueryPerformanceFrequency hook");
        return false;
    }

    // Hook GetTickCount
    if (MH_CreateHook(GetTickCount, GetTickCount_Detour,
                      (LPVOID*)&GetTickCount_Original) != MH_OK) {
        LogError("Failed to create GetTickCount hook");
        return false;
    }

    // Hook GetTickCount64
    if (MH_CreateHook(GetTickCount64, GetTickCount64_Detour,
                      (LPVOID*)&GetTickCount64_Original) != MH_OK) {
        LogError("Failed to create GetTickCount64 hook");
        return false;
    }

    // Hook timeGetTime
    if (MH_CreateHook(timeGetTime, timeGetTime_Detour,
                      (LPVOID*)&timeGetTime_Original) != MH_OK) {
        LogError("Failed to create timeGetTime hook");
        return false;
    }

    // Hook GetSystemTime
    if (MH_CreateHook(GetSystemTime, GetSystemTime_Detour,
                      (LPVOID*)&GetSystemTime_Original) != MH_OK) {
        LogError("Failed to create GetSystemTime hook");
        return false;
    }

    // Hook GetSystemTimeAsFileTime
    if (MH_CreateHook(GetSystemTimeAsFileTime, GetSystemTimeAsFileTime_Detour,
                      (LPVOID*)&GetSystemTimeAsFileTime_Original) != MH_OK) {
        LogError("Failed to create GetSystemTimeAsFileTime hook");
        return false;
    }


    // Hook GetSystemTimePreciseAsFileTime
    if (MH_CreateHook(GetSystemTimePreciseAsFileTime, GetSystemTimePreciseAsFileTime_Detour,
                      (LPVOID*)&GetSystemTimePreciseAsFileTime_Original) != MH_OK) {
        LogError("Failed to create GetSystemTimePreciseAsFileTime hook");
        return false;
    }

    // Hook GetLocalTime
    if (MH_CreateHook(GetLocalTime, GetLocalTime_Detour,
                      (LPVOID*)&GetLocalTime_Original) != MH_OK) {
        LogError("Failed to create GetLocalTime hook");
        return false;
    }

    // Hook NtQuerySystemTime (dynamically loaded)
    HMODULE ntdll = GetModuleHandleA("ntdll.dll");
    if (ntdll) {
        LPVOID ntQuerySystemTime = GetProcAddress(ntdll, "NtQuerySystemTime");
        if (ntQuerySystemTime) {
            if (MH_CreateHook(ntQuerySystemTime, NtQuerySystemTime_Detour,
                              (LPVOID*)&NtQuerySystemTime_Original) != MH_OK) {
                LogError("Failed to create NtQuerySystemTime hook");
                return false;
            }
        } else {
            LogInfo("NtQuerySystemTime not available on this system");
        }
    } else {
        LogInfo("ntdll.dll not available for NtQuerySystemTime hook");
    }

    // Enable all hooks
    if (MH_EnableHook(MH_ALL_HOOKS) != MH_OK) {
        LogError("Failed to enable timeslowdown hooks");
        return false;
    }

    g_timeslowdown_hooks_installed.store(true);
    LogInfo("Timeslowdown hooks installed successfully");

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
    MH_RemoveHook(timeGetTime);
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
    GetSystemTime_Original = nullptr;
    GetSystemTimeAsFileTime_Original = nullptr;
    GetSystemTimePreciseAsFileTime_Original = nullptr;
    GetLocalTime_Original = nullptr;
    NtQuerySystemTime_Original = nullptr;

    // Clean up state
    TimeslowdownState* state = g_timeslowdown_state.exchange(nullptr);
    delete state;

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

bool AreTimeslowdownHooksInstalled() {
    return g_timeslowdown_hooks_installed.load();
}

void SetTimeslowdownMultiplier(float multiplier) {
    if (multiplier <= 0.0f) {
        LogError("Invalid timeslowdown multiplier: %f (must be > 0)", multiplier);
        return;
    }

    settings::g_experimentalTabSettings.timeslowdown_multiplier.SetValue(multiplier);
    LogInfo("Timeslowdown multiplier set to: %f", multiplier);
}

float GetTimeslowdownMultiplier() {
    return settings::g_experimentalTabSettings.timeslowdown_multiplier.GetValue();
}

bool IsTimeslowdownEnabled() {
    return settings::g_experimentalTabSettings.timeslowdown_enabled.GetValue();
}

void SetTimeslowdownEnabled(bool enabled) {
    settings::g_experimentalTabSettings.timeslowdown_enabled.SetValue(enabled);
    LogInfo("Timeslowdown %s", enabled ? "enabled" : "disabled");
}

// Individual hook type configuration (DLL-safe)
void SetTimerHookType(const char* hook_name, TimerHookType type) {
    if (strcmp(hook_name, HOOK_QUERY_PERFORMANCE_COUNTER) == 0) {
        g_query_performance_counter_hook_type.store(type);
    } else if (strcmp(hook_name, HOOK_GET_TICK_COUNT) == 0) {
        g_get_tick_count_hook_type.store(type);
    } else if (strcmp(hook_name, HOOK_GET_TICK_COUNT64) == 0) {
        g_get_tick_count64_hook_type.store(type);
    } else if (strcmp(hook_name, HOOK_TIME_GET_TIME) == 0) {
        g_time_get_time_hook_type.store(type);
    } else if (strcmp(hook_name, HOOK_GET_SYSTEM_TIME) == 0) {
        g_get_system_time_hook_type.store(type);
    } else if (strcmp(hook_name, HOOK_GET_SYSTEM_TIME_AS_FILE_TIME) == 0) {
        g_get_system_time_as_file_time_hook_type.store(type);
    } else if (strcmp(hook_name, HOOK_GET_SYSTEM_TIME_PRECISE_AS_FILE_TIME) == 0) {
        g_get_system_time_precise_as_file_time_hook_type.store(type);
    } else if (strcmp(hook_name, HOOK_GET_LOCAL_TIME) == 0) {
        g_get_local_time_hook_type.store(type);
    } else if (strcmp(hook_name, HOOK_NT_QUERY_SYSTEM_TIME) == 0) {
        g_nt_query_system_time_hook_type.store(type);
    }

    LogInfo("Timer hook %s set to %s", hook_name,
        type == TimerHookType::None ? "None" :
        type == TimerHookType::Enabled ? "Enabled" :
        type == TimerHookType::RenderThreadOnly ? "RenderThreadOnly" : "EverythingExceptRenderThread");
}

TimerHookType GetTimerHookType(const char* hook_name) {
    return GetHookTypeByName(hook_name);
}

bool IsTimerHookEnabled(const char* hook_name) {
    TimerHookType type = GetTimerHookType(hook_name);
    return type == TimerHookType::Enabled || type == TimerHookType::RenderThreadOnly || type == TimerHookType::EverythingExceptRenderThread;
}

bool IsTimerHookRenderThreadOnly(const char* hook_name) {
    return GetTimerHookType(hook_name) == TimerHookType::RenderThreadOnly;
}

bool IsTimerHookEverythingExceptRenderThread(const char* hook_name) {
    return GetTimerHookType(hook_name) == TimerHookType::EverythingExceptRenderThread;
}

} // namespace renodx::hooks
