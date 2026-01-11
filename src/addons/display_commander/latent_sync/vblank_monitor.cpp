#include "vblank_monitor.hpp"
#include "../display/query_display.hpp"
#include "../globals.hpp"
#include "../hooks/api_hooks.hpp"
#include "../utils/logging.hpp"
#include "utils/timing.hpp"
#include <dxgi1_6.h>
#include <sstream>
#include <winnt.h>

// Forward declaration of the global variable
extern std::atomic<HWND> g_last_swapchain_hwnd;

namespace dxgi::fps_limiter {
std::atomic<LONGLONG> g_latent_sync_total_height{0};
std::atomic<LONGLONG> g_latent_sync_active_height{0};

extern std::atomic<LONGLONG> ns_per_refresh;
extern std::atomic<double> correction_lines_delta;
} // namespace dxgi::fps_limiter

// Simple logging wrapper to avoid dependency issues
namespace {
void LogMessage(const std::string &msg) { LogInfo("[VBlankMonitor] %s", msg.c_str()); }
} // namespace

namespace dxgi::fps_limiter {

// Helper function to normalize a value using modulo arithmetic to range [-range/2, range/2]
long VBlankMonitor::fmod_normalized(long double value, long range) {
    long result = static_cast<long>(fmod(value, static_cast<long double>(range)));
    if (result > range / 2) {
        result -= range;
    }
    if (result < -range / 2) {
        result += range;
    }
    return result;
}

// Helper function to get the correct DisplayTimingInfo for a specific window
DisplayTimingInfo GetDisplayTimingInfoForWindow(HWND hwnd) {
    if (hwnd == nullptr) {
        // Fallback to first display if no window provided
        auto timing_info = QueryDisplayTimingInfo();
        if (!timing_info.empty()) {
            return timing_info[0];
        }
        return DisplayTimingInfo{}; // Return empty struct
    }

    // Get the monitor that the window is on
    HMONITOR hmon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    if (hmon == nullptr) {
        // Fallback to first display if monitor query fails
        auto timing_info = QueryDisplayTimingInfo();
        if (!timing_info.empty()) {
            return timing_info[0];
        }
        return DisplayTimingInfo{}; // Return empty struct
    }

    // Get monitor info to extract device name
    MONITORINFOEXW mi{};
    mi.cbSize = sizeof(mi);
    if (!GetMonitorInfoW(hmon, &mi)) {
        LogInfo("GetDisplayTimingInfoForMonitor: GetMonitorInfoW failed");
        // Fallback to first display if monitor info query fails
        auto timing_info = QueryDisplayTimingInfo();
        if (!timing_info.empty()) {
            LogInfo("GetDisplayTimingInfoForMonitor: Using fallback timing_info[0] due to GetMonitorInfoW failure");
            return timing_info[0];
        }
        LogInfo("GetDisplayTimingInfoForMonitor: No timing info available, returning empty struct");
        return DisplayTimingInfo{}; // Return empty struct
    }

    // Query all display timing info
    auto timing_info = QueryDisplayTimingInfo();

    // Find the display that matches our monitor
    for (size_t i = 0; i < timing_info.size(); ++i) {
        const auto &timing = timing_info[i];

        // Try to match by GDI device name first (most reliable for GetMonitorInfoW)
        if (!timing.gdi_device_name.empty() && timing.gdi_device_name == mi.szDevice) {
            std::ostringstream oss;
            oss << "GetDisplayTimingInfoForMonitor: MATCH FOUND by gdi_device_name at index " << i;
            LogInfo(oss.str().c_str());
            return timing;
        }

        // Fallback: try to match by device path
        if (!timing.device_path.empty() && timing.device_path == mi.szDevice) {
            std::ostringstream oss;
            oss << "GetDisplayTimingInfoForMonitor: MATCH FOUND by device_path at index " << i;
            LogInfo(oss.str().c_str());
            return timing;
        }

        // Fallback: try to match by display name if device path doesn't match
        if (!timing.display_name.empty() && timing.display_name == mi.szDevice) {
            std::ostringstream oss;
            oss << "GetDisplayTimingInfoForMonitor: MATCH FOUND by display_name at index " << i;
            LogInfo(oss.str().c_str());
            return timing;
        }

        LogInfo("GetDisplayTimingInfoForMonitor: No match for this entry");
    }

    LogInfo("GetDisplayTimingInfoForMonitor: No exact match found, falling back to first entry");
    if (!timing_info.empty()) {
        return timing_info[0];
    }
    // Return empty struct if nothing is available
    LogInfo("GetDisplayTimingInfoForMonitor: No timing info available, returning empty struct");
    return DisplayTimingInfo{};
}

VBlankMonitor::VBlankMonitor() {}

VBlankMonitor::~VBlankMonitor() {
    StopMonitoring();

    if (m_hAdapter != 0) {
        if (LoadProcCached(m_pfnCloseAdapter, L"gdi32.dll", "D3DKMTCloseAdapter")) {
            D3DKMT_CLOSEADAPTER closeReq{};
            closeReq.hAdapter = m_hAdapter;
            reinterpret_cast<NTSTATUS(WINAPI *)(const D3DKMT_CLOSEADAPTER *)>(m_pfnCloseAdapter)(&closeReq);
        }
        m_hAdapter = 0;
    }
}

void VBlankMonitor::StartMonitoring() {
    if (m_monitoring.load())
        return;

    m_should_stop = false;
    m_monitor_thread = std::thread(&VBlankMonitor::MonitoringThread, this);
    m_monitoring = true;

    LogMessage("VBlank monitoring thread started");
    LogInfo("VBlank monitoring thread: StartMonitoring() called - thread created and started");
}

void VBlankMonitor::StopMonitoring() {
    if (!m_monitoring.load())
        return;

    LogInfo("VBlank monitoring thread: StopMonitoring() called - stopping thread...");
    m_should_stop = true;
    if (m_monitor_thread.joinable()) {
        m_monitor_thread.join();
    }
    m_monitoring = false;

    LogMessage("VBlank monitoring thread stopped");
    LogInfo("VBlank monitoring thread: StopMonitoring() completed - thread joined and stopped");
}

bool VBlankMonitor::BindToDisplay(HWND hwnd) { return UpdateDisplayBindingFromWindow(hwnd); }

std::wstring VBlankMonitor::GetDisplayNameFromWindow(HWND hwnd) {
    std::wstring result;
    if (hwnd == nullptr)
        return result;

    HMONITOR hmon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    MONITORINFOEXW mi{};
    mi.cbSize = sizeof(mi);

    if (GetMonitorInfoW(hmon, &mi)) {
        result = mi.szDevice; // e.g. "\\.\DISPLAY1"
    }
    return result;
}

bool VBlankMonitor::UpdateDisplayBindingFromWindow(HWND hwnd) {
    {
        std::ostringstream oss;
        oss << "UpdateDisplayBindingFromWindow: hwnd=" << hwnd;
        LogInfo(oss.str().c_str());
    }
    // Resolve display name
    std::wstring name;
    if (hwnd != nullptr) {
        name = GetDisplayNameFromWindow(hwnd);
        {
            std::ostringstream oss;
            oss << "Resolved display name from window: '" << WideCharToUTF8(name) << "'";
            LogInfo(oss.str().c_str());
        }
    } else {
        // Fallback: use first available display
        auto timing_info = QueryDisplayTimingInfo();
        if (!timing_info.empty()) {
            name = timing_info[0].display_name;
            {
                std::ostringstream oss;
                oss << "Using fallback display name: '" << WideCharToUTF8(name) << "'";
                LogInfo(oss.str().c_str());
            }

            // Log all available display names for debugging
            {
                std::ostringstream oss;
                oss << "All available display names:";
                for (size_t i = 0; i < timing_info.size(); ++i) {
                    oss << "\n  [" << i << "] display_name: '" << WideCharToUTF8(timing_info[i].display_name) << "'";
                    oss << "\n      device_path: '" << WideCharToUTF8(timing_info[i].device_path) << "'";
                }
                LogInfo(oss.str().c_str());
            }
        }
    }

    if (name.empty()) {
        LogInfo("No display name available for binding");
        return false;
    }

    if (name == m_bound_display_name && m_hAdapter != 0) {
        std::ostringstream oss;
        oss << "Already bound to display: " << WideCharToUTF8(name) << ", hAdapter=" << m_hAdapter
            << ", VidPnSourceId=" << m_vidpn_source_id;
        LogInfo(oss.str().c_str());
        return true;
    }

    // Rebind
    if (m_hAdapter != 0) {
        {
            std::ostringstream oss;
            oss << "Closing existing adapter handle before rebind: hAdapter=" << m_hAdapter;
            LogInfo(oss.str().c_str());
        }
        if (LoadProcCached(m_pfnCloseAdapter, L"gdi32.dll", "D3DKMTCloseAdapter")) {
            D3DKMT_CLOSEADAPTER closeReq{};
            closeReq.hAdapter = m_hAdapter;
            reinterpret_cast<NTSTATUS(WINAPI *)(const D3DKMT_CLOSEADAPTER *)>(m_pfnCloseAdapter)(&closeReq);
        }
        m_hAdapter = 0;
    }

    if (!LoadProcCached(m_pfnOpenAdapterFromGdiDisplayName, L"gdi32.dll", "D3DKMTOpenAdapterFromGdiDisplayName")) {
        LogInfo("Failed to load D3DKMTOpenAdapterFromGdiDisplayName");
        return false;
    }

    D3DKMT_OPENADAPTERFROMGDIDISPLAYNAME openReq{};
    wcsncpy_s(openReq.DeviceName, name.c_str(), _TRUNCATE);

    {
        std::ostringstream oss;
        oss << "Attempting to open adapter for display: '" << WideCharToUTF8(name) << "'";
        LogInfo(oss.str().c_str());
    }

    auto open_status = reinterpret_cast<NTSTATUS(WINAPI *)(D3DKMT_OPENADAPTERFROMGDIDISPLAYNAME *)>(
        m_pfnOpenAdapterFromGdiDisplayName)(&openReq);
    if (open_status == STATUS_SUCCESS) {
        {
            std::ostringstream oss;
            oss << "D3DKMTOpenAdapterFromGdiDisplayName succeeded: hAdapter=" << openReq.hAdapter
                << ", VidPnSourceId=" << openReq.VidPnSourceId;
            LogInfo(oss.str().c_str());
        }
        m_hAdapter = openReq.hAdapter;
        m_vidpn_source_id = openReq.VidPnSourceId;
        m_bound_display_name = name;

        std::ostringstream oss;
        oss << "VBlank monitor successfully bound to display: " << WideCharToUTF8(name) << " (Adapter: " << m_hAdapter
            << ", VidPnSourceId: " << m_vidpn_source_id << ")";
        LogInfo(oss.str().c_str());

        // VidPnSourceId of 0 is valid; no special handling required

        return true;
    } else {
        std::ostringstream oss;
        oss << "Failed to open adapter for display: " << WideCharToUTF8(name) << " (Status: " << open_status << ")";
        LogInfo(oss.str().c_str());

        // Provide more specific error information
        if (open_status == STATUS_OBJECT_NAME_NOT_FOUND) {
            LogInfo("STATUS_OBJECT_NAME_NOT_FOUND: The display name may not exist or may not be accessible");
        } else if (open_status == STATUS_OBJECT_PATH_NOT_FOUND) {
            LogInfo("STATUS_OBJECT_PATH_NOT_FOUND: The display path may be invalid or the display may not be ready");
        } else if (open_status == 0xC0000022) { // STATUS_ACCESS_DENIED
            LogInfo("STATUS_ACCESS_DENIED: Insufficient privileges to access the display adapter");
        } else if (open_status == STATUS_INVALID_PARAMETER) {
            LogInfo("STATUS_INVALID_PARAMETER: The display name format may be incorrect");
        }

        LogInfo("This may indicate the display is not fully initialized or the D3DKMT system is not ready");
    }

    return false;
}

bool VBlankMonitor::EnsureAdapterBinding() {
    {
        std::ostringstream oss;
        oss << "EnsureAdapterBinding: hAdapter=" << m_hAdapter << ", VidPnSourceId=" << m_vidpn_source_id
            << ", bound_display_name=" << WideCharToUTF8(m_bound_display_name);
        LogInfo(oss.str().c_str());
    }
    if (m_hAdapter != 0) {
        LogInfo("EnsureAdapterBinding: adapter handle already valid, skipping rebind");
        return true;
    }

    // Try to bind to foreground window if no specific binding
    HWND hwnd = display_commanderhooks::GetForegroundWindow_Direct();
    {
        std::ostringstream oss;
        oss << "EnsureAdapterBinding: foreground hwnd=" << hwnd;
        LogInfo(oss.str().c_str());
    }
    if (hwnd != nullptr) {
        bool ok = UpdateDisplayBindingFromWindow(hwnd);
        LogInfo(ok ? "EnsureAdapterBinding: bound using foreground window"
                   : "EnsureAdapterBinding: failed to bind using foreground window");
        return ok;
    }

    // Fallback: try to bind to any available display
    auto timing_info = QueryDisplayTimingInfo();
    if (!timing_info.empty()) {
        // Try to bind to the first available display
        std::wstring display_name = timing_info[0].display_name;
        {
            std::ostringstream oss;
            oss << "EnsureAdapterBinding: fallback display_name='" << WideCharToUTF8(display_name) << "'";
            LogInfo(oss.str().c_str());
        }
        if (!display_name.empty()) {
            bool ok = UpdateDisplayBindingFromWindow(nullptr); // This will use the fallback logic
            LogInfo(ok ? "EnsureAdapterBinding: bound using fallback display"
                       : "EnsureAdapterBinding: failed to bind using fallback display");
            return ok;
        }
    }

    LogInfo("EnsureAdapterBinding: no displays available to bind");
    return false;
}

long double expected_current_scanline_uncapped_ns(LONGLONG now_ns, LONGLONG total_height, bool add_correction) {
    long double cur_scanline = 1.0L * total_height * now_ns / ns_per_refresh.load();
    if (add_correction) {
        cur_scanline += correction_lines_delta.load();
    }
    return cur_scanline;
}

void VBlankMonitor::MonitoringThread() {
    ::LogInfo("VBlank monitoring thread: entering main loop");
    ::LogInfo("VBlank monitoring thread: STARTED - monitoring scanlines for frame pacing");

    // wait until latent sync mode is active
    while (s_fps_limiter_mode.load() != FpsLimiterMode::kLatentSync) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    }

    // Note: This thread is started when VBlank Scanline Sync mode (FPS mode 1) is enabled
    ::LogInfo("VBlank monitoring thread: This thread runs when VBlank Scanline Sync mode is active");

    if (!EnsureAdapterBinding()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    if (!LoadProcCached(m_pfnGetScanLine, L"gdi32.dll", "D3DKMTGetScanLine")) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    // Get the current window HWND and corresponding display timing info
    HWND hwnd = g_last_swapchain_hwnd.load();
    DisplayTimingInfo current_display_timing = GetDisplayTimingInfoForWindow(hwnd);

    // Log all available timing info for debugging
    std::vector<DisplayTimingInfo> all_timing_info = QueryDisplayTimingInfo();
    {
        std::ostringstream oss;
        LogInfo(oss.str().c_str());

        for (const auto &timing : all_timing_info) {
            std::ostringstream oss2;
            oss2 << " display_name: " << WideCharToUTF8(timing.display_name) << "\n";
            oss2 << " device_path: " << WideCharToUTF8(timing.device_path) << "\n";
            oss2 << " connector_instance: " << (int)timing.connector_instance << "\n";
            oss2 << " adapter_id: " << (int)timing.adapter_id << "\n";
            oss2 << " target_id: " << (int)timing.target_id << "\n";
            oss2 << " pixel_clock_hz: " << timing.pixel_clock_hz << "\n";
            oss2 << " hsync_freq_numerator: " << timing.hsync_freq_numerator << "\n";
            oss2 << " hsync_freq_denominator: " << timing.hsync_freq_denominator << "\n";
            oss2 << " vsync_freq_numerator: " << timing.vsync_freq_numerator << "\n";
            oss2 << " vsync_freq_denominator: " << timing.vsync_freq_denominator << "\n";
            oss2 << " active_width: " << timing.active_width << "\n";
            oss2 << " active_height: " << timing.active_height << "\n";
            oss2 << " total_width: " << timing.total_width << "\n";
            oss2 << " total_height: " << timing.total_height << "\n";
            oss2 << " video_standard: " << timing.video_standard << "\n";

            LogInfo(oss2.str().c_str());
        }
    }

    // Ensure the function pointer is valid
    if (m_pfnGetScanLine == nullptr) {
        LogWarn("GetScanLine function pointer is null, attempting to reload...");
        if (!LoadProcCached(m_pfnGetScanLine, L"gdi32.dll", "D3DKMTGetScanLine")) {
            LogError("Failed to load GetScanLine function, sleeping...");
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            return;
        }
    }

    LONGLONG min_scanline_duration_ns = 0;
    LONGLONG correction_ticks_local = 0;
    LONGLONG last_display_timing_refresh_ns = 0;

    int lastScanLine = 0;
    while (!m_should_stop.load()) {
        // auto switch to the correct monitor
        {
            LONGLONG now_ts = utils::get_now_ns();
            if (last_display_timing_refresh_ns == 0 || last_display_timing_refresh_ns + 5 * utils::SEC_TO_NS < now_ts) {
                last_display_timing_refresh_ns = now_ts;

                hwnd = g_last_swapchain_hwnd.load();
                current_display_timing = GetDisplayTimingInfoForWindow(hwnd);

                // Also refresh adapter binding when switching monitors
                if (hwnd != nullptr) {
                    LogInfo("Switching monitors, refreshing adapter binding...");
                    UpdateDisplayBindingFromWindow(hwnd);
                }
                // Use the current display timing info for calculations
                ns_per_refresh.store((current_display_timing.vsync_freq_numerator > 0)
                                         ? (current_display_timing.vsync_freq_denominator * utils::SEC_TO_NS) /
                                               (current_display_timing.vsync_freq_numerator)
                                         : 1);

                g_latent_sync_total_height.store(current_display_timing.total_height);
                g_latent_sync_active_height.store(current_display_timing.active_height);
                if (!EnsureAdapterBinding()) {
                    LogInfo("Failed to establish adapter binding, sleeping...");
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    continue;
                }
            }
        }

        {
            D3DKMT_GETSCANLINE scan{};
            scan.hAdapter = m_hAdapter;
            scan.VidPnSourceId = m_vidpn_source_id;

            LONGLONG start_ns = utils::get_now_ns();
            auto nt_status = reinterpret_cast<NTSTATUS(WINAPI *)(D3DKMT_GETSCANLINE *)>(m_pfnGetScanLine)(&scan);
            LONGLONG end_ns = utils::get_now_ns();

            long double expected_scanline_tmp =
                expected_current_scanline_uncapped_ns(start_ns, current_display_timing.total_height, true);
            if (nt_status == STATUS_SUCCESS) {
                LONGLONG duration_ns = end_ns - start_ns;
                LONGLONG mid_point_ns =
                    (start_ns + end_ns) / 2; // TODO: all values could be multipled by 2 for better precision

                // shortest encountered duration
                if (min_scanline_duration_ns == 0 || duration_ns < min_scanline_duration_ns) {
                    min_scanline_duration_ns = duration_ns;
                }
                if (duration_ns < 2 * min_scanline_duration_ns) {
                    long double expected_scanline =
                        expected_current_scanline_uncapped_ns(mid_point_ns, current_display_timing.total_height, false);
                    long double new_correction_lines_delta =
                        fmod(scan.ScanLine - expected_scanline, (long double)(current_display_timing.total_height));
                    if (new_correction_lines_delta < 0) {
                        new_correction_lines_delta += current_display_timing.total_height;
                    }
                    long dt = fmod_normalized(new_correction_lines_delta - correction_lines_delta.load(),
                                              current_display_timing.total_height);

                    double alpha = 0.1;
                    correction_lines_delta.store(correction_lines_delta.load() + dt * alpha);
                }
            }

            std::this_thread::sleep_for(std::chrono::microseconds(100)); // 0.1ms

            lastScanLine = expected_scanline_tmp;
        }
    }

    ::LogInfo("VBlank monitoring thread: exiting main loop");
    ::LogInfo("VBlank monitoring thread: STOPPED - no longer monitoring scanlines");
}

} // namespace dxgi::fps_limiter
