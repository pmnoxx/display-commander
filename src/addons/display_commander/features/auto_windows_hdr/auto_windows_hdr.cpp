// Source Code <Display Commander> // follow this order for includes in all files + add this comment at the top
#include "auto_windows_hdr.hpp"
#include "display/hdr_control.hpp"
#include "settings/main_tab_settings.hpp"
#include "utils/logging.hpp"

// Libraries <standard C++>
#include <atomic>

// Libraries <Windows.h> — before other Windows headers
#include <Windows.h>

namespace display_commander::features::auto_windows_hdr {

namespace {
std::atomic<HMONITOR> s_hdr_auto_enabled_monitor{nullptr};
std::atomic<bool> s_we_auto_enabled_hdr{false};
}  // namespace

void OnSwapchainInitTryAutoEnableWindowsHdr(HWND hwnd) {
    if (settings::g_mainTabSettings.auto_enable_windows_hdr.GetValue()) {
        HMONITOR monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
        if (monitor) {
            bool supported = false;
            bool enabled = false;
            if (display_commander::display::hdr_control::GetHdrStateForMonitor(monitor, &supported, &enabled)) {
                LogInfo("[Auto Enable Windows HDR] Display is HDR capable: %s, enabled: %s", supported ? "YES" : "NO",
                        enabled ? "YES" : "NO");
                if (display_commander::display::hdr_control::SetHdrForMonitor(monitor, true)) {
                    LogInfo("[Auto Enable Windows HDR] Successfully enabled Windows HDR for display");
                    s_hdr_auto_enabled_monitor.store(monitor);
                    s_we_auto_enabled_hdr.store(true);
                }
            }
        }
    }
}

void OnSwapchainDestroyMaybeRevertAutoHdr(HWND hwnd) {
    if (!s_we_auto_enabled_hdr.load()) {
        return;
    }
    const HMONITOR stored = s_hdr_auto_enabled_monitor.load();
    LogInfo("[Auto Enable Windows HDR] OnSwapchainDestroyMaybeRevertAutoHdr: stored monitor: %p", stored);
    if (stored == nullptr) {
        return;
    }
    if (!hwnd) {
        return;
    }
    const HMONITOR monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    if (monitor == stored) {
        LogInfo("[Auto Enable Windows HDR] OnSwapchainDestroyMaybeRevertAutoHdr: monitor matches stored, disabling HDR");
        display_commander::display::hdr_control::SetHdrForMonitor(monitor, false);
        s_we_auto_enabled_hdr.store(false);
        s_hdr_auto_enabled_monitor.store(nullptr);
    }
}

void OnProcessExitRevertAutoHdrIfNeeded() {
    if (!s_we_auto_enabled_hdr.load()) {
        return;
    }
    const HMONITOR stored = s_hdr_auto_enabled_monitor.load();
    LogInfo("[Auto Enable Windows HDR] OnProcessExitRevertAutoHdrIfNeeded: stored monitor: %p", stored);
    if (stored == nullptr) {
        return;
    }
    display_commander::display::hdr_control::SetHdrForMonitor(stored, false);
    s_we_auto_enabled_hdr.store(false);
    s_hdr_auto_enabled_monitor.store(nullptr);
}

}  // namespace display_commander::features::auto_windows_hdr
