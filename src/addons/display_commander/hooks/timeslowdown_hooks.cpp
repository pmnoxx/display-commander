#include "timeslowdown_hooks.hpp"
#include "../utils.hpp"
#include <MinHook.h>
#include <atomic>

namespace renodx::hooks {

// Original function pointers
QueryPerformanceCounter_pfn QueryPerformanceCounter_Original = nullptr;
QueryPerformanceFrequency_pfn QueryPerformanceFrequency_Original = nullptr;

// Hook state
static std::atomic<bool> g_timeslowdown_hooks_installed{false};

// Timeslowdown configuration
static std::atomic<double> g_timeslowdown_multiplier{1.0};
static std::atomic<bool> g_timeslowdown_enabled{false};

const bool min_enabled = false;

// Hooked QueryPerformanceCounter function
BOOL WINAPI QueryPerformanceCounter_Detour(LARGE_INTEGER* lpPerformanceCount) {
    if (!QueryPerformanceCounter_Original) {
        return QueryPerformanceCounter(lpPerformanceCount);
    }

    static LONGLONG original_quad_ts = 0;
    static LONGLONG original_quad_value = 0;
    static long double multiplier = 1.0;


    // Call original function first
    BOOL result = QueryPerformanceCounter_Original(lpPerformanceCount);

    if (original_quad_ts > 0 || g_timeslowdown_enabled.load()) {
        if (original_quad_ts == 0) {
            original_quad_ts = lpPerformanceCount->QuadPart;
            original_quad_value = lpPerformanceCount->QuadPart;
        }

        if (result && lpPerformanceCount) {
            LONGLONG now_qpc = lpPerformanceCount->QuadPart;
            long double new_multiplier = g_timeslowdown_enabled.load() ? g_timeslowdown_multiplier.load() : 1.0;

            if (multiplier != new_multiplier) {
                if (min_enabled) {
                    original_quad_value = max(now_qpc, original_quad_value + (now_qpc - original_quad_ts) * multiplier);
                } else {
                    original_quad_value = original_quad_value + (now_qpc - original_quad_ts) * multiplier;
                }
                original_quad_ts = now_qpc;

                multiplier = new_multiplier;
            }

            if (min_enabled) {
                lpPerformanceCount->QuadPart = min(now_qpc, original_quad_value + (now_qpc - original_quad_ts) * multiplier);
            } else {
                lpPerformanceCount->QuadPart = original_quad_value + (now_qpc - original_quad_ts) * multiplier;
            }
        }
    }

    return result;
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

    // Clean up
    QueryPerformanceCounter_Original = nullptr;
    QueryPerformanceFrequency_Original = nullptr;

    g_timeslowdown_hooks_installed.store(false);
    LogInfo("Timeslowdown hooks uninstalled successfully");
}

bool AreTimeslowdownHooksInstalled() {
    return g_timeslowdown_hooks_installed.load();
}

void SetTimeslowdownMultiplier(double multiplier) {
    if (multiplier <= 0.0) {
        LogError("Invalid timeslowdown multiplier: %f (must be > 0)", multiplier);
        return;
    }

    g_timeslowdown_multiplier.store(multiplier);
    LogInfo("Timeslowdown multiplier set to: %f", multiplier);
}

double GetTimeslowdownMultiplier() {
    return g_timeslowdown_multiplier.load();
}

bool IsTimeslowdownEnabled() {
    return g_timeslowdown_enabled.load();
}

void SetTimeslowdownEnabled(bool enabled) {
    g_timeslowdown_enabled.store(enabled);
    LogInfo("Timeslowdown %s", enabled ? "enabled" : "disabled");
}

} // namespace renodx::hooks
