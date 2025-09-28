#include "display_initial_state.hpp"
#include "display_cache.hpp"
#include "utils.hpp"

namespace display_initial_state {

// Global instance
InitialDisplayStateManager g_initialDisplayState;

bool InitialDisplayStateManager::CaptureInitialState() {
    if (is_captured_.load(std::memory_order_acquire)) {
        LogInfo("Initial display state already captured, skipping...");
        return true;
    }

    LogInfo("=== CAPTURING INITIAL DISPLAY STATE ===");

    // Ensure display cache is initialized
    if (!display_cache::g_displayCache.IsInitialized()) {
        if (!display_cache::g_displayCache.Initialize()) {
            LogError("Failed to initialize display cache for initial state capture");
            return false;
        }
    }

    // Create new state vector
    auto new_states = std::make_shared<std::vector<InitialDisplayState>>();

    // Get display count
    size_t display_count = display_cache::g_displayCache.GetDisplayCount();
    if (display_count == 0) {
        LogError("No displays found during initial state capture");
        return false;
    }

    LogInfo("Found %zu displays, capturing initial state...", display_count);

    // Capture state for each display
    for (size_t i = 0; i < display_count; ++i) {
        const auto *display = display_cache::g_displayCache.GetDisplay(i);
        if (!display) {
            LogWarn("Display %zu is null, skipping", i);
            continue;
        }

        InitialDisplayState state;
        state.device_name = display->extended_device_id;
        state.friendly_name = display->friendly_name;
        state.display_id = static_cast<int>(i + 1); // 1-based display ID
        state.width = display->width;
        state.height = display->height;
        state.refresh_numerator = display->current_refresh_rate.numerator;
        state.refresh_denominator = display->current_refresh_rate.denominator;
        state.is_primary = display->is_primary;
        state.monitor_handle = display->monitor_handle;

        new_states->push_back(state);

        // Get extended device ID for better identification
        std::string extended_device_id = display_cache::g_displayCache.GetExtendedDeviceIdFromMonitor(display->monitor_handle);

        // Print individual display info with enhanced details
        LogInfo("Display %d: %S (%S) - %dx%d @ %u/%u (%.6fHz) %s", state.display_id, state.device_name.c_str(),
                state.friendly_name.c_str(), state.width, state.height, state.refresh_numerator,
                state.refresh_denominator, state.GetRefreshRateHz(), state.is_primary ? "[PRIMARY]" : "");

        // Print additional debug information
        LogInfo("  Extended Device ID: %s", extended_device_id.c_str());
        LogInfo("  Monitor Handle: 0x%p", display->monitor_handle);
    }

    // Atomically store the new states
    initial_states_.store(new_states, std::memory_order_release);
    is_captured_.store(true, std::memory_order_release);

    LogInfo("=== INITIAL DISPLAY STATE CAPTURED ===");
    LogInfo("Total displays captured: %zu", new_states->size());

    // Print summary
    PrintInitialStates();

    return true;
}

const InitialDisplayState *InitialDisplayStateManager::GetInitialStateForDevice(const std::wstring &device_name) const {
    auto states = initial_states_.load(std::memory_order_acquire);
    if (!states)
        return nullptr;

    for (const auto &state : *states) {
        if (state.device_name == device_name) {
            return &state;
        }
    }
    return nullptr;
}

const InitialDisplayState *InitialDisplayStateManager::GetInitialStateForDisplayId(int display_id) const {
    auto states = initial_states_.load(std::memory_order_acquire);
    if (!states)
        return nullptr;

    for (const auto &state : *states) {
        if (state.display_id == display_id) {
            return &state;
        }
    }
    return nullptr;
}

void InitialDisplayStateManager::PrintInitialStates() const {
    auto states = initial_states_.load(std::memory_order_acquire);
    if (!states || states->empty()) {
        LogInfo("No initial display states captured");
        return;
    }

    LogInfo("=== INITIAL DISPLAY STATES SUMMARY ===");
    for (const auto &state : *states) {
        // Get extended device ID for this display
        std::string extended_device_id = display_cache::g_displayCache.GetExtendedDeviceIdFromMonitor(state.monitor_handle);

        // Convert friendly name to string for logging
        std::string friendly_name_str(state.friendly_name.begin(), state.friendly_name.end());
        std::string device_name_str(state.device_name.begin(), state.device_name.end());

        LogInfo("Display %d: %s (%s) - %dx%d @ %u/%u (%.6fHz) %s",
                state.display_id, device_name_str.c_str(), friendly_name_str.c_str(),
                state.width, state.height, state.refresh_numerator, state.refresh_denominator,
                state.GetRefreshRateHz(), state.is_primary ? "[PRIMARY]" : "");
        LogInfo("  Extended Device ID: %s", extended_device_id.c_str());
    }
    LogInfo("=== END DISPLAY STATES SUMMARY ===");
}

} // namespace display_initial_state
