#include "query_display.hpp"
#include "../utils/logging.hpp"
#include <windows.h>

#include <dxgi.h>
#include <wingdi.h>

// Helper methods for calculated values
double DisplayTimingInfo::GetPixelClockMHz() const { return static_cast<double>(pixel_clock_hz) / 1000000.0; }

double DisplayTimingInfo::GetHSyncFreqHz() const {
    return static_cast<double>(hsync_freq_numerator) / static_cast<double>(hsync_freq_denominator);
}

double DisplayTimingInfo::GetHSyncFreqKHz() const { return GetHSyncFreqHz() / 1000.0; }

double DisplayTimingInfo::GetVSyncFreqHz() const {
    return static_cast<double>(vsync_freq_numerator) / static_cast<double>(vsync_freq_denominator);
}

// Format timing info similar to Special-K log format
std::wstring DisplayTimingInfo::GetFormattedString() const {
    wchar_t buffer[512];
    swprintf_s(buffer,
               L"Display:%s :: Adapter:%u::Target:%u :: PixelClock=%.1f MHz, vSyncFreq=%.3f Hz, hSyncFreq=%.3f kHz, "
               L"activeSize=(%ux%u), totalSize=(%ux%u), Standard=%u",
               display_name.empty() ? L"UNKNOWN" : display_name.c_str(), adapter_id, target_id, GetPixelClockMHz(),
               GetVSyncFreqHz(), GetHSyncFreqKHz(), active_width, active_height, total_width, total_height,
               video_standard);
    return buffer;
}

// Query display timing information for all active displays
std::vector<DisplayTimingInfo> QueryDisplayTimingInfo() {
    std::vector<DisplayTimingInfo> results;

    UINT32 path_count = 0, mode_count = 0;

    // Get required buffer sizes
    if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &path_count, &mode_count) != ERROR_SUCCESS) {
        return results;
    }

    if (path_count == 0 || mode_count == 0) {
        return results;
    }

    std::vector<DISPLAYCONFIG_PATH_INFO> paths(path_count);
    std::vector<DISPLAYCONFIG_MODE_INFO> modes(mode_count);

    // Query display configuration
    if (QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &path_count, paths.data(), &mode_count, modes.data(), nullptr)
        != ERROR_SUCCESS) {
        return results;
    }

    // Process each active path
    for (UINT32 path_idx = 0; path_idx < path_count; ++path_idx) {
        const auto& path = paths[path_idx];

        // Only process active paths
        if (!(path.flags & DISPLAYCONFIG_PATH_ACTIVE) || !(path.sourceInfo.statusFlags & DISPLAYCONFIG_SOURCE_IN_USE)) {
            continue;
        }

        // Get target mode info
        int mode_idx = (path.flags & DISPLAYCONFIG_PATH_SUPPORT_VIRTUAL_MODE) ? path.targetInfo.targetModeInfoIdx
                                                                              : path.targetInfo.modeInfoIdx;

        if (mode_idx < 0 || static_cast<UINT32>(mode_idx) >= mode_count) {
            continue;
        }

        const auto& mode_info = modes[mode_idx];

        // Only process target mode info
        if (mode_info.infoType != DISPLAYCONFIG_MODE_INFO_TYPE_TARGET) {
            continue;
        }

        DisplayTimingInfo timing_info = {};

        // Use adapter ID as device identifier
        timing_info.adapter_id = path.sourceInfo.adapterId.LowPart;

        // Use target ID as display target identifier
        timing_info.target_id = path.targetInfo.id;

        // Extract timing information
        const auto& video_signal = mode_info.targetMode.targetVideoSignalInfo;
        timing_info.pixel_clock_hz = video_signal.pixelRate;
        timing_info.hsync_freq_numerator = video_signal.hSyncFreq.Numerator;
        timing_info.hsync_freq_denominator = video_signal.hSyncFreq.Denominator;
        timing_info.vsync_freq_numerator = video_signal.vSyncFreq.Numerator;
        timing_info.vsync_freq_denominator = video_signal.vSyncFreq.Denominator;
        timing_info.active_width = video_signal.activeSize.cx;
        timing_info.active_height = video_signal.activeSize.cy;
        timing_info.total_width = video_signal.totalSize.cx;
        timing_info.total_height = video_signal.totalSize.cy;
        timing_info.video_standard = static_cast<uint32_t>(video_signal.videoStandard);

        // Query display name using Special-K's approach
        // This uses DISPLAYCONFIG_TARGET_DEVICE_NAME to get the monitor's friendly device name
        // which is the same approach Special-K uses in their display querying code
        DISPLAYCONFIG_TARGET_DEVICE_NAME getTargetName = {};
        getTargetName.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME;
        getTargetName.header.size = sizeof(DISPLAYCONFIG_TARGET_DEVICE_NAME);
        getTargetName.header.adapterId = path.sourceInfo.adapterId;
        getTargetName.header.id = path.targetInfo.id;

        // Also query the source device name to get the GDI device name that matches GetMonitorInfoW
        DISPLAYCONFIG_SOURCE_DEVICE_NAME getSourceName = {};
        getSourceName.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
        getSourceName.header.size = sizeof(DISPLAYCONFIG_SOURCE_DEVICE_NAME);
        getSourceName.header.adapterId = path.sourceInfo.adapterId;
        getSourceName.header.id = path.sourceInfo.id;

        if (DisplayConfigGetDeviceInfo(&getTargetName.header) == ERROR_SUCCESS) {
            timing_info.display_name = getTargetName.monitorFriendlyDeviceName;
            timing_info.device_path = getTargetName.monitorDevicePath;
            timing_info.connector_instance = getTargetName.connectorInstance;

            // Get the GDI device name that matches GetMonitorInfoW format
            if (DisplayConfigGetDeviceInfo(&getSourceName.header) == ERROR_SUCCESS) {
                timing_info.gdi_device_name = getSourceName.viewGdiDeviceName;
            } else {
                timing_info.gdi_device_name = L"UNKNOWN";
            }

            // Log the device information for debugging (Debug level to respect log level setting)
            LogDebug("[QueryDisplay] QueryDisplayTimingInfo: Found display [path_idx=%u]:\n    display_name: '%s'\n    device_path: '%s'\n    gdi_device_name: '%s'\n    adapter_id: %u\n    target_id: %u",
                path_idx,
                WideCharToUTF8(timing_info.display_name).c_str(),
                WideCharToUTF8(timing_info.device_path).c_str(),
                WideCharToUTF8(timing_info.gdi_device_name).c_str(),
                timing_info.adapter_id,
                timing_info.target_id);
        } else {
            timing_info.display_name = L"UNKNOWN";
            timing_info.device_path = L"UNKNOWN";
            timing_info.gdi_device_name = L"UNKNOWN";
            timing_info.connector_instance = UINT32_MAX;

            LogDebug("[QueryDisplay] QueryDisplayTimingInfo: Failed to get device info for display");
        }

        results.push_back(timing_info);
    }

    return results;
}

// Utility function to convert wstring to string (similar to Special-K's SK_WideCharToUTF8)
std::string WideCharToUTF8(const std::wstring& in) {
    if (in.empty()) {
        return "";
    }

    constexpr UINT wcFlags = WC_COMPOSITECHECK | WC_NO_BEST_FIT_CHARS;

    // Get the required buffer size for the conversion
    int len =
        ::WideCharToMultiByte(CP_UTF8, wcFlags, in.c_str(), static_cast<int>(in.length()), nullptr, 0, nullptr, FALSE);

    if (len == 0) {
        return "";
    }

    std::string out(len, '\0');

    // Perform the actual conversion
    if (::WideCharToMultiByte(CP_UTF8, wcFlags, in.c_str(), static_cast<int>(in.length()),
                              const_cast<char*>(out.data()), len, nullptr, FALSE)
        == 0) {
        return "";
    }

    // Replace any null characters with spaces to prevent truncation issues
    for (char& c : out) {
        if (c == '\0') {
            c = ' ';
        }
    }

    return out;
}

// Get current display settings using QueryDisplayConfig for precise refresh rate
bool GetCurrentDisplaySettingsQueryConfig(HMONITOR monitor, int& width, int& height, uint32_t& refresh_numerator,
                                          uint32_t& refresh_denominator, int& x, int& y, bool first_time_log,
                                          const std::vector<DISPLAYCONFIG_PATH_INFO>& paths,
                                          const std::vector<DISPLAYCONFIG_MODE_INFO>& modes) {
    if (paths.empty() || modes.empty()) {
        return false;
    }

    UINT32 path_count = static_cast<UINT32>(paths.size());
    UINT32 mode_count = static_cast<UINT32>(modes.size());

    // Find the path that matches our monitor
    for (UINT32 path_idx = 0; path_idx < path_count; ++path_idx) {
        const auto& path = paths[path_idx];

        // Only process active paths
        if (!(path.flags & DISPLAYCONFIG_PATH_ACTIVE) || !(path.sourceInfo.statusFlags & DISPLAYCONFIG_SOURCE_IN_USE)) {
            continue;
        }

        // Get the GDI device name to match with our monitor
        DISPLAYCONFIG_SOURCE_DEVICE_NAME getSourceName = {};
        getSourceName.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
        getSourceName.header.size = sizeof(DISPLAYCONFIG_SOURCE_DEVICE_NAME);
        getSourceName.header.adapterId = path.sourceInfo.adapterId;
        getSourceName.header.id = path.sourceInfo.id;

        if (DisplayConfigGetDeviceInfo(&getSourceName.header) != ERROR_SUCCESS) {
            continue;
        }

        // Get monitor info to compare device names
        MONITORINFOEXW mi;
        mi.cbSize = sizeof(mi);
        if (!GetMonitorInfoW(monitor, &mi)) {
            continue;
        }

        // Compare GDI device names (this should match the format from GetMonitorInfoW)
        if (wcscmp(getSourceName.viewGdiDeviceName, mi.szDevice) != 0) {
            continue;
        }

        // Found matching monitor, get the current mode info
        int mode_idx = (path.flags & DISPLAYCONFIG_PATH_SUPPORT_VIRTUAL_MODE) ? path.targetInfo.targetModeInfoIdx
                                                                              : path.targetInfo.modeInfoIdx;

        if (mode_idx < 0 || static_cast<UINT32>(mode_idx) >= mode_count) {
            continue;
        }

        const auto& mode_info = modes[mode_idx];

        // Only process target mode info
        if (mode_info.infoType != DISPLAYCONFIG_MODE_INFO_TYPE_TARGET) {
            continue;
        }

        // Extract current display settings
        const auto& video_signal = mode_info.targetMode.targetVideoSignalInfo;

        int desktop_width = static_cast<int>(video_signal.activeSize.cx);
        int desktop_height = static_cast<int>(video_signal.activeSize.cy);
        width = desktop_width;
        height = desktop_height;
        refresh_numerator = video_signal.vSyncFreq.Numerator;
        refresh_denominator = video_signal.vSyncFreq.Denominator;

        width = desktop_width;
        height = desktop_height;
        refresh_numerator = video_signal.vSyncFreq.Numerator;
        refresh_denominator = video_signal.vSyncFreq.Denominator;

        // Get position from source mode info
        int source_mode_idx = path.sourceInfo.modeInfoIdx;
        DISPLAYCONFIG_MODE_INFO source_mode = {};
        if (source_mode_idx >= 0 && static_cast<UINT32>(source_mode_idx) < mode_count) {
            source_mode = modes[source_mode_idx];
            if (source_mode.infoType == DISPLAYCONFIG_MODE_INFO_TYPE_SOURCE) {
                x = static_cast<int>(source_mode.sourceMode.position.x);
                y = static_cast<int>(source_mode.sourceMode.position.y);
                width = static_cast<int>(source_mode.sourceMode.width);
                height = static_cast<int>(source_mode.sourceMode.height);
            } else {
                x = y = 0;  // Default position if source mode not found
            }
        } else {
            x = y = 0;  // Default position if source mode index invalid
        }

        if (first_time_log) {
            std::string device_name_str = WideCharToUTF8(mi.szDevice);
            LogInfo(
                "[GetCurrentDisplaySettingsQueryConfig] monitor: %s, adapter_id: %d/%d, display_res: %dx%d, desktop_res: %dx%d, refresh: %d/%d, "
                "refresh_denominator: %d source_mode.infoType: %d",
                device_name_str.c_str(), path.sourceInfo.adapterId.LowPart, path.sourceInfo.adapterId.HighPart, desktop_width, desktop_height,  width, height, refresh_numerator, refresh_denominator, source_mode.infoType
            );
        }

        return true;
    }

    return false;
}