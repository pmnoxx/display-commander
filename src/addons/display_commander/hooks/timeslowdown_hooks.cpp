#include "timeslowdown_hooks.hpp"
#include "../utils.hpp"
#include <MinHook.h>
#include <atomic>

namespace renodx::hooks {

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

// Hook state
static std::atomic<bool> g_timeslowdown_hooks_installed{false};

// Timeslowdown configuration
static std::atomic<double> g_timeslowdown_multiplier{1.0};
static std::atomic<bool> g_timeslowdown_enabled{false};

// Atomic pointer for thread-safe state access
static std::atomic<TimeslowdownState*> g_timeslowdown_state{new TimeslowdownState()};

const bool min_enabled = false;

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

    // Get current state atomically
    TimeslowdownState* current_state = g_timeslowdown_state.load();

    // if time slow down is enabled, or was enabled before, then apply the time slow down
    if (current_state->original_quad_ts > 0 || g_timeslowdown_enabled.load()) {
        LONGLONG now_qpc = lpPerformanceCount->QuadPart;
        long double new_multiplier = g_timeslowdown_enabled.load() ? g_timeslowdown_multiplier.load() : 1.0;

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

    // Clean up state
    TimeslowdownState* state = g_timeslowdown_state.exchange(nullptr);
    delete state;

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
