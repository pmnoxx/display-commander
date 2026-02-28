#pragma once

#include <atomic>
#include <string>

namespace display_commanderhooks {

// HID device type statistics
struct HIDDeviceStats {
    std::atomic<uint64_t> total_devices{0};
    std::atomic<uint64_t> dualsense_devices{0};
    std::atomic<uint64_t> xbox_devices{0};
    std::atomic<uint64_t> generic_hid_devices{0};
    std::atomic<uint64_t> unknown_devices{0};

    void increment_total() { total_devices.fetch_add(1); }
    void increment_dualsense() { dualsense_devices.fetch_add(1); }
    void increment_xbox() { xbox_devices.fetch_add(1); }
    void increment_generic() { generic_hid_devices.fetch_add(1); }
    void increment_unknown() { unknown_devices.fetch_add(1); }
    void reset() {
        total_devices.store(0);
        dualsense_devices.store(0);
        xbox_devices.store(0);
        generic_hid_devices.store(0);
        unknown_devices.store(0);
    }
};

// Global HID device type statistics (call counts use g_hook_stats in windows_message_hooks)
extern HIDDeviceStats g_hid_device_stats;

// HID statistics access functions
const HIDDeviceStats& GetHIDDeviceStats();
void ResetAllHIDStats();

// Helper functions for device type detection
bool IsDualSenseDevice(const std::string& device_path);
bool IsDualSenseDevice(const std::wstring& device_path);
bool IsXboxDevice(const std::string& device_path);
bool IsXboxDevice(const std::wstring& device_path);
bool IsHIDDevice(const std::string& device_path);
bool IsHIDDevice(const std::wstring& device_path);

} // namespace display_commanderhooks
