#include "vblank_monitor.hpp"
#include <dxgi1_6.h>
#include <sstream>
#include <iomanip>
#include <iostream>
#include <winnt.h>
#include "../utils.hpp"
#include "../display/query_display.hpp"

// Forward declaration of the global variable
extern std::atomic<HWND> g_last_swapchain_hwnd;

namespace dxgi::fps_limiter {
extern std::atomic<LONGLONG> ticks_per_refresh;
extern std::atomic<double> correction_lines_delta;
}

// Simple logging wrapper to avoid dependency issues
namespace {
    void LogMessage(const std::string& msg) {
        std::cout << "[VBlankMonitor] " << msg << std::endl;
    }
}

namespace dxgi::fps_limiter {

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
        // Fallback to first display if monitor info query fails
        auto timing_info = QueryDisplayTimingInfo();
        if (!timing_info.empty()) {
            return timing_info[0];
        }
        return DisplayTimingInfo{}; // Return empty struct
    }

    // Query all display timing info
    auto timing_info = QueryDisplayTimingInfo();
    
    // Find the display that matches our monitor
    for (const auto& timing : timing_info) {
        // Try to match by device path first (most reliable)
        if (!timing.device_path.empty() && timing.device_path == mi.szDevice) {
            return timing;
        }
        
        // Fallback: try to match by display name if device path doesn't match
        if (!timing.display_name.empty() && timing.display_name == mi.szDevice) {
            return timing;
        }
    }

    // If no match found, return the first available timing info
    if (!timing_info.empty()) {
        
        return timing_info[0];
    }

    // Return empty struct if nothing is available
    return DisplayTimingInfo{};
}

VBlankMonitor::VBlankMonitor() {
    m_last_state_change = std::chrono::steady_clock::now();
    m_vblank_start_time = m_last_state_change;
    m_active_start_time = m_last_state_change;

}

VBlankMonitor::~VBlankMonitor() {
    StopMonitoring();
    
    if (m_hAdapter != 0) {
        if (LoadProcCached(m_pfnCloseAdapter, L"gdi32.dll", "D3DKMTCloseAdapter")) {
            D3DKMT_CLOSEADAPTER closeReq{}; 
            closeReq.hAdapter = m_hAdapter;
            reinterpret_cast<NTSTATUS (WINAPI*)(const D3DKMT_CLOSEADAPTER*)>(m_pfnCloseAdapter)(&closeReq);
        }
        m_hAdapter = 0;
    }
}

void VBlankMonitor::StartMonitoring() {
    if (m_monitoring.load()) return;
    
    m_should_stop = false;
    m_monitor_thread = std::thread(&VBlankMonitor::MonitoringThread, this);
    m_monitoring = true;
    
    LogMessage("VBlank monitoring thread started");
}

void VBlankMonitor::StopMonitoring() {
    if (!m_monitoring.load()) return;
    
    m_should_stop = true;
    if (m_monitor_thread.joinable()) {
        m_monitor_thread.join();
    }
    m_monitoring = false;
    
    LogMessage("VBlank monitoring thread stopped");
}

bool VBlankMonitor::BindToDisplay(HWND hwnd) {
    return UpdateDisplayBindingFromWindow(hwnd);
}

std::wstring VBlankMonitor::GetDisplayNameFromWindow(HWND hwnd) {
    std::wstring result;
    if (hwnd == nullptr) return result;
    
    HMONITOR hmon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    MONITORINFOEXW mi{}; 
    mi.cbSize = sizeof(mi);
    
    if (GetMonitorInfoW(hmon, &mi)) {
        result = mi.szDevice; // e.g. "\\.\DISPLAY1"
    }
    return result;
}

bool VBlankMonitor::UpdateDisplayBindingFromWindow(HWND hwnd) {
    // Resolve display name
    std::wstring name = GetDisplayNameFromWindow(hwnd);
    if (name.empty()) return false;
    if (name == m_bound_display_name && m_hAdapter != 0) return true;

    // Rebind
    if (m_hAdapter != 0) {
        if (LoadProcCached(m_pfnCloseAdapter, L"gdi32.dll", "D3DKMTCloseAdapter")) {
            D3DKMT_CLOSEADAPTER closeReq{}; 
            closeReq.hAdapter = m_hAdapter;
            reinterpret_cast<NTSTATUS (WINAPI*)(const D3DKMT_CLOSEADAPTER*)>(m_pfnCloseAdapter)(&closeReq);
        }
        m_hAdapter = 0;
    }

    if (!LoadProcCached(m_pfnOpenAdapterFromGdiDisplayName, L"gdi32.dll", "D3DKMTOpenAdapterFromGdiDisplayName"))
        return false;

    D3DKMT_OPENADAPTERFROMGDIDISPLAYNAME openReq{};
    wcsncpy_s(openReq.DeviceName, name.c_str(), _TRUNCATE);
    
    if (reinterpret_cast<NTSTATUS (WINAPI*)(D3DKMT_OPENADAPTERFROMGDIDISPLAYNAME*)>(m_pfnOpenAdapterFromGdiDisplayName)(&openReq) == 0) {
        m_hAdapter = openReq.hAdapter;
        m_vidpn_source_id = openReq.VidPnSourceId;
        m_bound_display_name = name;
        
        std::ostringstream oss;
        oss << "VBlank monitor bound to display: " << name.length() << " characters";
        ::LogInfo(oss.str().c_str());
        return true;
    }

    return false;
}

bool VBlankMonitor::EnsureAdapterBinding() {
    return (m_hAdapter != 0);
    
    // Try to bind to foreground window if no specific binding
 //   HWND hwnd = GetForegroundWindow();
   // return UpdateDisplayBindingFromWindow(hwnd);
}

LONGLONG get_now_ticks() {
    LARGE_INTEGER now_ticks = { };
    QueryPerformanceCounter(&now_ticks);
    return now_ticks.QuadPart;
}

double expected_current_scanline(LONGLONG now_ticks, int total_height, bool add_correction) {
    double cur_scanline = (now_ticks % ticks_per_refresh.load()) / (1.0 * ticks_per_refresh.load() / total_height);
    if (add_correction) {
        cur_scanline += correction_lines_delta.load();
    }
    if (cur_scanline < 0) {
        cur_scanline += total_height;
    }
    if (cur_scanline > total_height) {
        cur_scanline -= total_height;
    }
    return cur_scanline;
}

void VBlankMonitor::MonitoringThread() {
    ::LogInfo("VBlank monitoring thread: entering main loop");
    

    if (!EnsureAdapterBinding()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    if (!LoadProcCached(m_pfnGetScanLine, L"gdi32.dll", "D3DKMTGetScanLine")) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    LARGE_INTEGER liQpcFreq = { };
    QueryPerformanceFrequency(&liQpcFreq);
    ticks_per_refresh.store(liQpcFreq.QuadPart);

    // Get the current window HWND and corresponding display timing info
    HWND hwnd = g_last_swapchain_hwnd.load();
    DisplayTimingInfo current_display_timing = GetDisplayTimingInfoForWindow(hwnd);
    
    // Log all available timing info for debugging
    std::vector<DisplayTimingInfo> all_timing_info = QueryDisplayTimingInfo();
    {
        std::ostringstream oss;
        oss << "ticks_per_refresh: " << ticks_per_refresh.load();
        LogInfo(oss.str().c_str());   
        
        for (const auto& timing : all_timing_info) {
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

    // Use the current display timing info for calculations
    ticks_per_refresh.store((current_display_timing.vsync_freq_numerator > 0) ?
        (current_display_timing.vsync_freq_denominator * ticks_per_refresh.load()) /
        (current_display_timing.vsync_freq_numerator) : 1);

    LONGLONG min_scanline_duration = 0;
    LONGLONG correction_ticks_local = 0;

    int lastScanLine = 0;
    while (!m_should_stop.load()) {
        LONGLONG ticks_per_refresh_local = ticks_per_refresh.load();

        D3DKMT_GETSCANLINE scan{};
        scan.hAdapter = m_hAdapter;
        scan.VidPnSourceId = m_vidpn_source_id;

        LONGLONG start_ticks = get_now_ticks();

        int expected_scanline = expected_current_scanline(start_ticks, current_display_timing.total_height, true);

        // don't run during vblank
        if (expected_scanline >= 50 && expected_scanline <= 300 &&reinterpret_cast<NTSTATUS (WINAPI*)(D3DKMT_GETSCANLINE*)>(m_pfnGetScanLine)(&scan) == 0) {
            LONGLONG end_ticks = get_now_ticks();
            LONGLONG duration = end_ticks - start_ticks;

            LONGLONG mid_point = (start_ticks + end_ticks) / 2; // TODO: all values could be multipled by 2 for better precision

            // shortest encountered duration
            if (min_scanline_duration == 0 || duration < min_scanline_duration) {
                min_scanline_duration = duration;
            }
            if (duration < 2 * min_scanline_duration) {
                double expected_scanline = expected_current_scanline(mid_point, current_display_timing.total_height, false);
                
                double new_correction_lines_delta = scan.ScanLine - expected_scanline; 
                correction_lines_delta.store(new_correction_lines_delta);
            }
        }
        std::this_thread::sleep_for(std::chrono::microseconds(100)); // 0.1ms

        // auto switch to the correct monitor
        if (expected_scanline < lastScanLine) {
            hwnd = g_last_swapchain_hwnd.load();
            current_display_timing = GetDisplayTimingInfoForWindow(hwnd);
        }

        lastScanLine = expected_scanline;
    }
    
    ::LogInfo("VBlank monitoring thread: exiting main loop");
}

double VBlankMonitor::GetVBlankPercentage() const {
    auto total_time = m_total_vblank_time + m_total_active_time;
    if (total_time.count() == 0) return 0.0;
    
    return (static_cast<double>(m_total_vblank_time.count()) / total_time.count()) * 100.0;
}

std::chrono::milliseconds VBlankMonitor::GetAverageVBlankDuration() const {
    if (m_vblank_count.load() == 0) return std::chrono::milliseconds(0);
    
    auto avg_ticks = m_total_vblank_time.count() / m_vblank_count.load();
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::duration(avg_ticks));
}

std::chrono::milliseconds VBlankMonitor::GetAverageActiveDuration() const {
    auto active_count = m_state_change_count.load() - m_vblank_count.load();
    if (active_count <= 0) return std::chrono::milliseconds(0);
    
    auto avg_ticks = m_total_active_time.count() / active_count;
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::duration(avg_ticks));
}

std::string VBlankMonitor::GetDetailedStatsString() const {
    std::ostringstream oss;
    
    auto total_time = m_total_vblank_time + m_total_active_time;
    auto total_ms = std::chrono::duration_cast<std::chrono::milliseconds>(total_time).count();
    
    oss << "VBlank Monitor Statistics:\n";
    oss << "  Total monitoring time: " << total_ms << " ms\n";
    oss << "  VBlank count: " << m_vblank_count.load() << "\n";
    oss << "  State changes: " << m_state_change_count.load() << "\n";
    oss << "  VBlank percentage: " << std::fixed << std::setprecision(2) << GetVBlankPercentage() << "%\n";
    oss << "  Avg VBlank duration: " << GetAverageVBlankDuration().count() << " ms\n";
    oss << "  Avg Active duration: " << GetAverageActiveDuration().count() << " ms\n";
    
    return oss.str();
}

std::string VBlankMonitor::GetLastTransitionInfo() const {
    std::ostringstream oss;
    
    {
        std::lock_guard<std::mutex> lock(m_stats_mutex);
        
        if (m_last_vblank_duration.count() > 0) {
            auto vblank_ms = std::chrono::duration_cast<std::chrono::microseconds>(m_last_vblank_duration).count() / 1000.0;
            oss << "Last VBlank duration: " << std::fixed << std::setprecision(2) << vblank_ms << " ms";
        }
        
        if (m_last_active_duration.count() > 0) {
            if (oss.str().length() > 0) oss << " | ";
            auto active_ms = std::chrono::duration_cast<std::chrono::microseconds>(m_last_active_duration).count() / 1000.0;
            oss << "Last Active duration: " << std::fixed << std::setprecision(2) << active_ms << " ms";
        }
    }
    
    return oss.str();
}

} // namespace dxgi::fps_limiter
