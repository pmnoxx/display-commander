#pragma once

#include <atomic>
#include <array>
#include <string>

namespace display_commanderhooks {

// HID API call statistics structure
struct HIDCallStats {
    std::atomic<uint64_t> total_calls{0};
    std::atomic<uint64_t> successful_calls{0};
    std::atomic<uint64_t> failed_calls{0};
    std::atomic<uint64_t> blocked_calls{0};

    void increment_total() { total_calls.fetch_add(1); }
    void increment_successful() { successful_calls.fetch_add(1); }
    void increment_failed() { failed_calls.fetch_add(1); }
    void increment_blocked() { blocked_calls.fetch_add(1); }
    void reset() {
        total_calls.store(0);
        successful_calls.store(0);
        failed_calls.store(0);
        blocked_calls.store(0);
    }
};

// HID API types
enum HIDAPIType {
    HID_CREATEFILE_A = 0,
    HID_CREATEFILE_W,
    HID_READFILE,
    HID_WRITEFILE,
    HID_DEVICEIOCONTROL,
    HID_HIDD_GETINPUTREPORT,
    HID_HIDD_GETATTRIBUTES,
    HID_HIDD_GETPREPARSEDDATA,
    HID_HIDD_FREEPREPARSEDDATA,
    HID_HIDD_GETCAPS,
    HID_HIDD_GETMANUFACTURERSTRING,
    HID_HIDD_GETPRODUCTSTRING,
    HID_HIDD_GETSERIALNUMBERSTRING,
    HID_HIDD_GETNUMINPUTBUFFERS,
    HID_HIDD_SETNUMINPUTBUFFERS,
    HID_HIDD_GETFEATURE,
    HID_HIDD_SETFEATURE,
    HID_COUNT
};

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

// Global HID statistics
extern std::array<HIDCallStats, HID_COUNT> g_hid_api_stats;
extern HIDDeviceStats g_hid_device_stats;

// HID statistics access functions
const HIDCallStats& GetHIDAPIStats(HIDAPIType api_type);
const HIDDeviceStats& GetHIDDeviceStats();
void ResetAllHIDStats();
int GetHIDAPICount();
const char* GetHIDAPIName(HIDAPIType api_type);

// Helper functions for device type detection
bool IsDualSenseDevice(const std::string& device_path);
bool IsDualSenseDevice(const std::wstring& device_path);
bool IsXboxDevice(const std::string& device_path);
bool IsXboxDevice(const std::wstring& device_path);
bool IsHIDDevice(const std::string& device_path);
bool IsHIDDevice(const std::wstring& device_path);

} // namespace display_commanderhooks
