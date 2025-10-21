#include "hid_statistics.hpp"
#include "../utils.hpp"
#include <algorithm>
#include <string>

namespace display_commanderhooks {

// Global HID statistics
std::array<HIDCallStats, HID_COUNT> g_hid_api_stats;
HIDDeviceStats g_hid_device_stats;

// HID API names
const char* HID_API_NAMES[HID_COUNT] = {
    "CreateFileA",
    "CreateFileW",
    "ReadFile",
    "WriteFile",
    "DeviceIoControl",
    "HidD_GetInputReport",
    "HidD_GetAttributes",
    "HidD_GetPreparsedData",
    "HidD_FreePreparsedData",
    "HidP_GetCaps",
    "HidD_GetManufacturerString",
    "HidD_GetProductString",
    "HidD_GetSerialNumberString",
    "HidD_GetNumInputBuffers",
    "HidD_SetNumInputBuffers",
    "HidD_GetFeature",
    "HidD_SetFeature"
};

const HIDCallStats& GetHIDAPIStats(HIDAPIType api_type) {
    return g_hid_api_stats[api_type];
}

const HIDDeviceStats& GetHIDDeviceStats() {
    return g_hid_device_stats;
}

void ResetAllHIDStats() {
    for (auto& stats : g_hid_api_stats) {
        stats.reset();
    }
    g_hid_device_stats.reset();
}

int GetHIDAPICount() {
    return HID_COUNT;
}

const char* GetHIDAPIName(HIDAPIType api_type) {
    return HID_API_NAMES[api_type];
}

bool IsDualSenseDevice(const std::string& device_path) {
    if (device_path.empty()) return false;

    std::string lower_path = device_path;
    std::transform(lower_path.begin(), lower_path.end(), lower_path.begin(), ::tolower);

    // Check for DualSense device identifiers
    return lower_path.find("vid_054c") != std::string::npos &&  // Sony VID
           (lower_path.find("pid_0ce6") != std::string::npos ||  // DualSense
            lower_path.find("pid_0df2") != std::string::npos);   // DualSense Edge
}

bool IsDualSenseDevice(const std::wstring& device_path) {
    if (device_path.empty()) return false;

    std::wstring lower_path = device_path;
    std::transform(lower_path.begin(), lower_path.end(), lower_path.begin(), ::towlower);

    // Check for DualSense device identifiers
    return lower_path.find(L"vid_054c") != std::wstring::npos &&  // Sony VID
           (lower_path.find(L"pid_0ce6") != std::wstring::npos ||  // DualSense
            lower_path.find(L"pid_0df2") != std::wstring::npos);   // DualSense Edge
}

bool IsXboxDevice(const std::string& device_path) {
    if (device_path.empty()) return false;

    std::string lower_path = device_path;
    std::transform(lower_path.begin(), lower_path.end(), lower_path.begin(), ::tolower);

    // Check for Xbox device identifiers
    return lower_path.find("vid_045e") != std::string::npos &&  // Microsoft VID
           (lower_path.find("pid_02ea") != std::string::npos ||  // Xbox One Controller
            lower_path.find("pid_02fd") != std::string::npos ||  // Xbox One S Controller
            lower_path.find("pid_0b12") != std::string::npos ||  // Xbox Elite Controller
            lower_path.find("pid_0b13") != std::string::npos);   // Xbox Elite Controller 2
}

bool IsXboxDevice(const std::wstring& device_path) {
    if (device_path.empty()) return false;

    std::wstring lower_path = device_path;
    std::transform(lower_path.begin(), lower_path.end(), lower_path.begin(), ::towlower);

    // Check for Xbox device identifiers
    return lower_path.find(L"vid_045e") != std::wstring::npos &&  // Microsoft VID
           (lower_path.find(L"pid_02ea") != std::wstring::npos ||  // Xbox One Controller
            lower_path.find(L"pid_02fd") != std::wstring::npos ||  // Xbox One S Controller
            lower_path.find(L"pid_0b12") != std::wstring::npos ||  // Xbox Elite Controller
            lower_path.find(L"pid_0b13") != std::wstring::npos);   // Xbox Elite Controller 2
}

bool IsHIDDevice(const std::string& device_path) {
    if (device_path.empty()) return false;

    std::string lower_path = device_path;
    std::transform(lower_path.begin(), lower_path.end(), lower_path.begin(), ::tolower);

    // Check for HID device path patterns
    return lower_path.find("hid#") != std::string::npos ||
           lower_path.find("hid\\") != std::string::npos ||
           lower_path.find("usb#") != std::string::npos ||
           lower_path.find("usb\\") != std::string::npos;
}

bool IsHIDDevice(const std::wstring& device_path) {
    if (device_path.empty()) return false;

    std::wstring lower_path = device_path;
    std::transform(lower_path.begin(), lower_path.end(), lower_path.begin(), ::towlower);

    // Check for HID device path patterns
    return lower_path.find(L"hid#") != std::wstring::npos ||
           lower_path.find(L"hid\\") != std::wstring::npos ||
           lower_path.find(L"usb#") != std::wstring::npos ||
           lower_path.find(L"usb\\") != std::wstring::npos;
}

} // namespace display_commanderhooks
