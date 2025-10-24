#include "../utils/logging.hpp"
#include "vblank_monitor.hpp"
#include <memory>
#include <windows.h>

// Example of how to integrate VBlankMonitor with existing code
namespace dxgi::fps_limiter {

// Global instance of the VBlank monitor
static std::unique_ptr<VBlankMonitor> g_vblank_monitor;

// Function to start VBlank monitoring
void StartVBlankMonitoring() {
    if (!g_vblank_monitor) {
        g_vblank_monitor = std::make_unique<VBlankMonitor>();
    }

    if (g_vblank_monitor && !g_vblank_monitor->IsMonitoring()) {
        g_vblank_monitor->StartMonitoring();
        LogInfo("VBlank monitoring started via integration");
    }
}

// Function to stop VBlank monitoring
void StopVBlankMonitoring() {
    if (g_vblank_monitor && g_vblank_monitor->IsMonitoring()) {
        g_vblank_monitor->StopMonitoring();
        LogInfo("VBlank monitoring stopped via integration");
    }
}
// Function to bind monitor to a specific window
bool BindVBlankMonitorToWindow(HWND hwnd) {
    if (!g_vblank_monitor) {
        g_vblank_monitor = std::make_unique<VBlankMonitor>();
    }

    return g_vblank_monitor->BindToDisplay(hwnd);
}

// Function to check if monitoring is active
bool IsVBlankMonitoringActive() { return g_vblank_monitor && g_vblank_monitor->IsMonitoring(); }

} // namespace dxgi::fps_limiter
