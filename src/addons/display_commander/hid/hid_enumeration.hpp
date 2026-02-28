#pragma once

#include <windows.h>
#include <atomic>
#include <string>
#include <vector>

namespace display_commander::hid {

/** Category for UI grouping (Controller tab HID subsection). */
enum class HidDeviceCategory {
    XInput,                      // Microsoft Xbox (VID 0x045E)
    DualSense,                   // Sony DualSense / DualSense Edge (VID 0x054C, PID 0x0CE6/0x0DF2)
    HidCompliantGameController,  // HID Usage Page 0x01, Usage Joystick/GamePad/MultiAxis (excl. XInput/DualSense)
    Other
};

/** One enumerated HID device. */
struct HidDeviceInfo {
    std::string path;  // Full device path (e.g. \?\hid#vid_054c&pid_0ce6#...)
    USHORT vendor_id{0};
    USHORT product_id{0};
    std::string product_string;  // From HidD_GetProductString if available; else empty
    HidDeviceCategory category{HidDeviceCategory::Other};
};

/**
 * Returns a copy of the cached HID device list (thread-safe).
 * Call RefreshHidDevicesSync or RequestHidDevicesRefresh to populate/update.
 */
std::vector<HidDeviceInfo> GetCachedHidDevices();

/**
 * Runs HID enumeration on the calling thread and updates the cache.
 * Uses SetupAPI + HidD_GetAttributes_Direct. Safe to call from UI thread
 * but may block for a short time.
 */
void RefreshHidDevicesSync();

/**
 * Starts background refresh; cache is updated when done.
 * IsRefreshing() returns true until the refresh completes.
 */
void RequestHidDevicesRefresh();

/** True while a background refresh is in progress. */
bool IsHidDevicesRefreshing();

/**
 * Returns the last enumeration error message (empty if none).
 * Cleared on next successful refresh.
 */
std::string GetLastHidEnumerationError();

}  // namespace display_commander::hid
