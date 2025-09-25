#pragma once

#include "vblank_monitor.hpp"
#include <memory>
#include <string>
#include <windows.h>


// D3DKMT definitions are now provided by vblank_monitor.hpp

namespace dxgi::fps_limiter {

class LatentSyncLimiter {
  public:
    LatentSyncLimiter();
    ~LatentSyncLimiter();

    void LimitFrameRate();

    void OnFrameBegin() {}
    void OnFrameEnd() {}

    // Present timing hooks (called around Present)
    void OnPresentEnd();

    // VBlank monitoring
    void StartVBlankMonitoring();
    void StopVBlankMonitoring();

    // VBlank monitoring statistics
    bool IsVBlankMonitoringActive() const { return m_vblank_monitor && m_vblank_monitor->IsMonitoring(); }

  private:
    bool EnsureAdapterBinding();
    bool UpdateDisplayBindingFromWindow(HWND hwnd);
    static std::wstring GetDisplayNameFromWindow(HWND hwnd);

  private:
    // VBlank pacing state
    double m_vblank_accumulator = 0.0;

    // VBlank monitor instance
    std::unique_ptr<VBlankMonitor> m_vblank_monitor;

    // Display binding
    D3DKMT_HANDLE m_hAdapter = 0;
    D3DDDI_VIDEO_PRESENT_SOURCE_ID m_vidpn_source_id = 0;
    std::wstring m_bound_display_name; // e.g. L"\\.\DISPLAY1"

    // Cached refresh rate
    double m_refresh_hz = 0.0;

    // Dynamic gdi32 entry points
    FARPROC m_pfnOpenAdapterFromGdiDisplayName = nullptr; // "D3DKMTOpenAdapterFromGdiDisplayName"
    FARPROC m_pfnCloseAdapter = nullptr;                  // "D3DKMTCloseAdapter"
    FARPROC m_pfnWaitForVerticalBlankEvent = nullptr;     // "D3DKMTWaitForVerticalBlankEvent"
    FARPROC m_pfnGetScanLine = nullptr;                   // "D3DKMTGetScanLine"
};

} // namespace dxgi::fps_limiter
