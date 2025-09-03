#pragma once

#include <windows.h>
#include <vector>
#include <string>

// Structure to hold display timing information
struct DisplayTimingInfo {
    uint32_t adapter_id;        // GPU adapter identifier
    uint32_t target_id;         // Display target identifier
    uint32_t pixel_clock_hz;
    uint32_t hsync_freq_numerator;
    uint32_t hsync_freq_denominator;
    uint32_t vsync_freq_numerator;
    uint32_t vsync_freq_denominator;
    uint32_t active_width;
    uint32_t active_height;
    uint32_t total_width;
    uint32_t total_height;
    uint32_t video_standard;
    std::wstring display_name;  // Monitor friendly device name
    std::wstring device_path;  // Monitor device path
    std::wstring gdi_device_name;  // GDI device name (matches GetMonitorInfoW format)
    uint32_t connector_instance;  // Connector instance

    // Helper methods for calculated values
    double GetPixelClockMHz() const;
    double GetHSyncFreqHz() const;
    double GetHSyncFreqKHz() const;
    double GetVSyncFreqHz() const;

    // Format timing info similar to Special-K log format
    std::wstring GetFormattedString() const;
};

// Query display timing information for all active displays
extern std::vector<DisplayTimingInfo> QueryDisplayTimingInfo();

// Query display timing info for a specific monitor
extern std::vector<DisplayTimingInfo> QueryDisplayTimingInfoForMonitor(HMONITOR monitor);

// Demonstration function: Log all display timing information (similar to Special-K)
extern void LogAllDisplayTimingInfo();

// Utility function to convert wstring to string (similar to Special-K's SK_WideCharToUTF8)
std::string WideCharToUTF8(const std::wstring& in);

// Get current display settings using QueryDisplayConfig for precise refresh rate
bool GetCurrentDisplaySettingsQueryConfig(HMONITOR monitor, int& width, int& height,
                                         uint32_t& refresh_numerator, uint32_t& refresh_denominator,
                                         int& x, int& y);