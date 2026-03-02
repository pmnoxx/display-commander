#pragma once

#include <atomic>
#include <memory>
#include <string>
#include <vector>
#include <windows.h>

namespace display_initial_state {

// Structure to hold initial display state information
struct InitialDisplayState {
    std::wstring device_name;   // Device name (e.g., "\\\\.\\DISPLAY1")
    std::wstring friendly_name; // Friendly display name
    int display_id;             // Display ID (1, 2, 3, etc.)
    int width;                  // Current resolution width
    int height;                 // Current resolution height
    UINT32 refresh_numerator;   // Refresh rate numerator
    UINT32 refresh_denominator; // Refresh rate denominator
    bool is_primary;            // Whether this is the primary display
    HMONITOR monitor_handle;    // Monitor handle for reference

    InitialDisplayState()
        : display_id(0), width(0), height(0), refresh_numerator(0), refresh_denominator(1), is_primary(false),
          monitor_handle(nullptr) {}

    // Get refresh rate as double
    double GetRefreshRateHz() const {
        if (refresh_denominator == 0)
            return 0.0;
        return static_cast<double>(refresh_numerator) / static_cast<double>(refresh_denominator);
    }
};

// Main class to manage initial display state
class InitialDisplayStateManager {
  private:
    std::atomic<std::shared_ptr<std::vector<InitialDisplayState>>> initial_states_;
    std::atomic<bool> is_captured_;

  public:
    InitialDisplayStateManager()
        : initial_states_(std::make_shared<std::vector<InitialDisplayState>>()), is_captured_(false) {}

    // Capture initial display state and print to log
    bool CaptureInitialState();

    // Get the captured initial states
    std::shared_ptr<std::vector<InitialDisplayState>> GetInitialStates() const {
        return initial_states_.load(std::memory_order_acquire);
    }

    // Get initial state for a specific device name
    const InitialDisplayState *GetInitialStateForDevice(const std::wstring &device_name) const;

    // Print all captured states to log
    void PrintInitialStates() const;
};

// Global instance
extern InitialDisplayStateManager g_initialDisplayState;

} // namespace display_initial_state
