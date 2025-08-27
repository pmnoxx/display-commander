#pragma once

#include <windows.h>
#include <chrono>
#include <string>
#include <thread>
#include <atomic>
#include <mutex>

// Minimal D3DKMT interop (definitions adapted from d3dkmthk.h)
typedef UINT D3DDDI_VIDEO_PRESENT_SOURCE_ID;
typedef UINT D3DKMT_HANDLE;
typedef LONG NTSTATUS;

struct D3DKMT_OPENADAPTERFROMGDIDISPLAYNAME {
    WCHAR DeviceName[32];
    D3DKMT_HANDLE hAdapter;
    LUID AdapterLuid;
    D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId;
};

struct D3DKMT_CLOSEADAPTER {
    D3DKMT_HANDLE hAdapter;
};

struct D3DKMT_GETSCANLINE {
    D3DKMT_HANDLE hAdapter;
    D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId;
    BOOLEAN InVerticalBlank;
    UINT    ScanLine;
};

namespace dxgi::fps_limiter {



class VBlankMonitor {
public:
    VBlankMonitor();
    ~VBlankMonitor();

    // Start/stop monitoring
    void StartMonitoring();
    void StopMonitoring();
    bool IsMonitoring() const { return m_monitoring.load(); }

    // Get statistics
    double GetVBlankPercentage() const;
    std::chrono::milliseconds GetAverageVBlankDuration() const;
    std::chrono::milliseconds GetAverageActiveDuration() const;
    uint64_t GetVBlankCount() const { return m_vblank_count.load(); }
    uint64_t GetStateChangeCount() const { return m_state_change_count.load(); }
    
    // Get detailed timing information
    std::string GetDetailedStatsString() const;
    std::string GetLastTransitionInfo() const;

    // Manual display binding (if needed)
    bool BindToDisplay(HWND hwnd);

private:
    void MonitoringThread();
    bool EnsureAdapterBinding();
    bool UpdateDisplayBindingFromWindow(HWND hwnd);
    static std::wstring GetDisplayNameFromWindow(HWND hwnd);
    
    // Dynamic function loading
    static inline FARPROC LoadProcCached(FARPROC& slot, const wchar_t* mod, const char* name) {
        if (slot != nullptr) return slot;
        HMODULE h = GetModuleHandleW(mod);
        if (h == nullptr) h = LoadLibraryW(mod);
        if (h != nullptr) slot = GetProcAddress(h, name);
        return slot;
    }

private:
    // Monitoring state
    std::thread m_monitor_thread;
    std::atomic<bool> m_monitoring{false};
    std::atomic<bool> m_should_stop{false};

    // Display binding
    D3DKMT_HANDLE m_hAdapter = 0;
    D3DDDI_VIDEO_PRESENT_SOURCE_ID m_vidpn_source_id = 0;
    std::wstring m_bound_display_name;

    // Dynamic gdi32 entry points
    FARPROC m_pfnOpenAdapterFromGdiDisplayName = nullptr; // "D3DKMTOpenAdapterFromGdiDisplayName"
    FARPROC m_pfnCloseAdapter = nullptr;                  // "D3DKMTCloseAdapter"
    FARPROC m_pfnGetScanLine = nullptr;                   // "D3DKMTGetScanLine"

    // VBlank state tracking
    std::atomic<bool> m_last_vblank_state{false};
    std::chrono::steady_clock::time_point m_last_state_change;
    std::chrono::steady_clock::time_point m_vblank_start_time;
    std::chrono::steady_clock::time_point m_active_start_time;
    LONGLONG m_vblank_start_time_ticks = 0;
    LONGLONG m_active_start_time_ticks = 0;

    // Timing statistics
    std::chrono::steady_clock::duration m_total_vblank_time{0};
    std::chrono::steady_clock::duration m_total_active_time{0};
    std::atomic<uint64_t> m_vblank_count{0};
    std::atomic<uint64_t> m_state_change_count{0};

    // Last transition info
    mutable std::mutex m_stats_mutex;
    std::chrono::steady_clock::time_point m_last_vblank_to_active;
    std::chrono::steady_clock::time_point m_last_active_to_vblank;
    std::chrono::steady_clock::duration m_last_vblank_duration{0};
    std::chrono::steady_clock::duration m_last_active_duration{0};
};

} // namespace dxgi::fps_limiter
