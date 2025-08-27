#include "vblank_monitor.hpp"
#include <dxgi1_6.h>
#include <cwchar>
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <locale>
#include <codecvt>
#include <iostream>
#include "../utils.hpp"
namespace dxgi::fps_limiter {
extern std::atomic<double> s_vblank_ms;
extern std::atomic<double> s_active_ms;
extern std::atomic<bool> s_vblank_seen;
}

// Simple logging wrapper to avoid dependency issues
namespace {
    void LogMessage(const std::string& msg) {
        std::cout << "[VBlankMonitor] " << msg << std::endl;
    }
}

namespace dxgi::fps_limiter {

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
    if (m_hAdapter != 0) return true;
    
    // Try to bind to foreground window if no specific binding
    HWND hwnd = GetForegroundWindow();
    return UpdateDisplayBindingFromWindow(hwnd);
}

void VBlankMonitor::MonitoringThread() {
    ::LogInfo("VBlank monitoring thread: entering main loop");
    
    uint64_t lastScanLine = 0;

    while (!m_should_stop.load()) {
        if (!EnsureAdapterBinding()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        if (!LoadProcCached(m_pfnGetScanLine, L"gdi32.dll", "D3DKMTGetScanLine")) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        D3DKMT_GETSCANLINE scan{};
        scan.hAdapter = m_hAdapter;
        scan.VidPnSourceId = m_vidpn_source_id;

        if (reinterpret_cast<NTSTATUS (WINAPI*)(D3DKMT_GETSCANLINE*)>(m_pfnGetScanLine)(&scan) == 0) {
            bool current_vblank = scan.InVerticalBlank != 0;
            if (!current_vblank) {
                if (scan.ScanLine < lastScanLine) {
                    LogMessage("VBlank detected");
                    current_vblank = true;
                }
                lastScanLine = scan.ScanLine;
            }


            bool state_changed = (current_vblank != m_last_vblank_state.load());
            
            if (state_changed) {
                auto now = std::chrono::steady_clock::now();
                
                // Calculate duration of previous state
                if (m_last_vblank_state.load()) {
                    // Was in vblank, now active
                    auto vblank_duration = now - m_vblank_start_time;
                    m_total_vblank_time += vblank_duration;
                    m_vblank_count++;
                    
                    {
                        std::lock_guard<std::mutex> lock(m_stats_mutex);
                        m_last_vblank_duration = vblank_duration;
                        m_last_vblank_to_active = now;
                    }
                    
                    // Log transition from vblank to active
                    auto vblank_ms = std::chrono::duration_cast<std::chrono::microseconds>(vblank_duration).count() / 1000.0;
                    std::ostringstream oss;
                    oss << "VBlank -> Active: spent " << std::fixed << std::setprecision(2) << vblank_ms << " ms in vblank";
                    dxgi::fps_limiter::s_vblank_ms.store(vblank_ms);
                    dxgi::fps_limiter::s_vblank_seen.store(true);
                    ::LogInfo(oss.str().c_str());
                    
                    m_active_start_time = now;
                } else {
                    // Was active, now in vblank
                    auto active_duration = now - m_active_start_time;
                    m_total_active_time += active_duration;
                    
                    {
                        std::lock_guard<std::mutex> lock(m_stats_mutex);
                        m_last_active_duration = active_duration;
                        m_last_active_to_vblank = now;
                    }
                    
                    // Log transition from active to vblank
                    auto active_ms = std::chrono::duration_cast<std::chrono::microseconds>(active_duration).count() / 1000.0;
                    std::ostringstream oss;
                    oss << "Active -> VBlank: spent " << std::fixed << std::setprecision(2) << active_ms << " ms in active";
                    dxgi::fps_limiter::s_active_ms.store(active_ms);
                    ::LogInfo(oss.str().c_str());
                    
                    m_vblank_start_time = now;
                }
                
                m_last_vblank_state = current_vblank;
                m_last_state_change = now;
                m_state_change_count++;
            }
        }
        
        // Small sleep to avoid excessive CPU usage
        std::this_thread::sleep_for(std::chrono::microseconds(100));
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
