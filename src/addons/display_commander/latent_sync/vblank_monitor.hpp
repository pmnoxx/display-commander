#pragma once

#include <windows.h>
#include <chrono>
#include <string>
#include <thread>
#include <atomic>
#include <mutex>

// Forward declaration
struct DisplayTimingInfo;

// External timing function


// Minimal D3DKMT interop (definitions adapted from d3dkmthk.h)
typedef UINT D3DDDI_VIDEO_PRESENT_SOURCE_ID;
typedef UINT D3DKMT_HANDLE;
typedef LONG NTSTATUS;

// NTSTATUS constants
#ifndef STATUS_SUCCESS
#define STATUS_SUCCESS ((NTSTATUS)0x00000000L)
#endif
#ifndef STATUS_ACCESS_VIOLATION
#define STATUS_ACCESS_VIOLATION ((NTSTATUS)0xC0000005L)
#endif
#ifndef STATUS_INVALID_PARAMETER
#define STATUS_INVALID_PARAMETER ((NTSTATUS)0xC000000DL)
#endif
#ifndef STATUS_OBJECT_NAME_NOT_FOUND
#define STATUS_OBJECT_NAME_NOT_FOUND ((NTSTATUS)0xC0000034L)
#endif
#ifndef STATUS_OBJECT_PATH_NOT_FOUND
#define STATUS_OBJECT_PATH_NOT_FOUND ((NTSTATUS)0xC000003AL)
#endif

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

// Global variables for VBlank monitoring
extern std::atomic<LONGLONG> ns_per_refresh;
extern std::atomic<LONGLONG> g_latent_sync_total_height;
extern std::atomic<LONGLONG> g_latent_sync_active_height;

// Helper function to get the correct DisplayTimingInfo for a specific window
DisplayTimingInfo GetDisplayTimingInfoForWindow(HWND hwnd);


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

    // Get detailed timing information
    std::string GetDetailedStatsString() const;

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

    // Helper function to normalize a value using modulo arithmetic to range [-range/2, range/2]
    static long fmod_normalized(long double value, long range);

private:
    // Sentinel for uninitialized/invalid VidPn source id
    static constexpr D3DDDI_VIDEO_PRESENT_SOURCE_ID kInvalidVidPnSource = static_cast<D3DDDI_VIDEO_PRESENT_SOURCE_ID>(-1);

    // Monitoring state
    std::thread m_monitor_thread;
    std::atomic<bool> m_monitoring{false};
    std::atomic<bool> m_should_stop{false};

    // Display binding
    D3DKMT_HANDLE m_hAdapter = 0;
    D3DDDI_VIDEO_PRESENT_SOURCE_ID m_vidpn_source_id = kInvalidVidPnSource;
    std::wstring m_bound_display_name;

    // Dynamic gdi32 entry points
    FARPROC m_pfnOpenAdapterFromGdiDisplayName = nullptr; // "D3DKMTOpenAdapterFromGdiDisplayName"
    FARPROC m_pfnCloseAdapter = nullptr;                  // "D3DKMTCloseAdapter"
    FARPROC m_pfnGetScanLine = nullptr;                   // "D3DKMTGetScanLine"
};

} // namespace dxgi::fps_limiter
