#pragma once

#include <windows.h>
#include <chrono>
#include <string>

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

struct D3DKMT_WAITFORVERTICALBLANKEVENT {
    D3DKMT_HANDLE hAdapter;
    D3DKMT_HANDLE hDevice; // Unused here
    D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId;
};

struct D3DKMT_GETSCANLINE {
    D3DKMT_HANDLE hAdapter;
    D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId;
    BOOLEAN InVerticalBlank;
    UINT    ScanLine;
};

namespace dxgi::fps_limiter {

class LatentSyncLimiter {
public:
    LatentSyncLimiter();
    ~LatentSyncLimiter();

    void LimitFrameRate();

    void OnFrameBegin() {}
    void OnFrameEnd() {}

    void SetTargetFps(float fps) { m_target_fps = fps; }
    float GetTargetFps() const { return m_target_fps; }

    void SetEnabled(bool enabled) { m_enabled = enabled; }
    bool IsEnabled() const { return m_enabled; }

private:
    bool EnsureAdapterBinding();
    bool UpdateDisplayBindingFromWindow(HWND hwnd);
    static std::wstring GetDisplayNameFromWindow(HWND hwnd);

private:
    float m_target_fps = 0.0f;
    bool  m_enabled = false;

    // VBlank pacing state
    double m_vblank_accumulator = 0.0;

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


