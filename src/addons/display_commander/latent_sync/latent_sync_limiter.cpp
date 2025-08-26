#include "latent_sync_limiter.hpp"
#include "../addon.hpp"
#include <dxgi1_6.h>
#include <cwchar>
#include <algorithm>

namespace dxgi::fps_limiter {

static inline FARPROC LoadProcCached(FARPROC& slot, const wchar_t* mod, const char* name) {
    if (slot != nullptr) return slot;
    HMODULE h = GetModuleHandleW(mod);
    if (h == nullptr) h = LoadLibraryW(mod);
    if (h != nullptr) slot = GetProcAddress(h, name);
    return slot;
}

LatentSyncLimiter::LatentSyncLimiter() {}

LatentSyncLimiter::~LatentSyncLimiter() {
    if (m_hAdapter != 0) {
        if (LoadProcCached(m_pfnCloseAdapter, L"gdi32.dll", "D3DKMTCloseAdapter")) {
            D3DKMT_CLOSEADAPTER closeReq{}; closeReq.hAdapter = m_hAdapter;
            reinterpret_cast<NTSTATUS (WINAPI*)(const D3DKMT_CLOSEADAPTER*)>(m_pfnCloseAdapter)(&closeReq);
        }
        m_hAdapter = 0;
    }
}

bool LatentSyncLimiter::EnsureAdapterBinding() {
    HWND hwnd = g_last_swapchain_hwnd.load();
    if (hwnd == nullptr) hwnd = GetForegroundWindow();
    return UpdateDisplayBindingFromWindow(hwnd);
}

std::wstring LatentSyncLimiter::GetDisplayNameFromWindow(HWND hwnd) {
    std::wstring result;
    if (hwnd == nullptr) return result;
    HMONITOR hmon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    MONITORINFOEXW mi{}; mi.cbSize = sizeof(mi);
    if (GetMonitorInfoW(hmon, &mi)) {
        result = mi.szDevice; // e.g. "\\.\DISPLAY1"
    }
    return result;
}

bool LatentSyncLimiter::UpdateDisplayBindingFromWindow(HWND hwnd) {
    // Resolve display name
    std::wstring name = GetDisplayNameFromWindow(hwnd);
    if (name.empty()) return false;
    if (name == m_bound_display_name && m_hAdapter != 0) return true;

    // Rebind
    if (m_hAdapter != 0) {
        if (LoadProcCached(m_pfnCloseAdapter, L"gdi32.dll", "D3DKMTCloseAdapter")) {
            D3DKMT_CLOSEADAPTER closeReq{}; closeReq.hAdapter = m_hAdapter;
            reinterpret_cast<NTSTATUS (WINAPI*)(const D3DKMT_CLOSEADAPTER*)>(m_pfnCloseAdapter)(&closeReq);
        }
        m_hAdapter = 0;
    }

    if (!LoadProcCached(m_pfnOpenAdapterFromGdiDisplayName, L"gdi32.dll", "D3DKMTOpenAdapterFromGdiDisplayName"))
        return false;

    D3DKMT_OPENADAPTERFROMGDIDISPLAYNAME openReq{};
    wcsncpy_s(openReq.DeviceName, name.c_str(), _TRUNCATE);
    if ( reinterpret_cast<NTSTATUS (WINAPI*)(D3DKMT_OPENADAPTERFROMGDIDISPLAYNAME*)>(m_pfnOpenAdapterFromGdiDisplayName)(&openReq) == 0 ) {
        m_hAdapter = openReq.hAdapter;
        m_vidpn_source_id = openReq.VidPnSourceId;
        m_bound_display_name = name;

        // Try to cache refresh rate from global state if available
        g_window_state_lock.lock();
        m_refresh_hz = g_window_state.current_monitor_refresh_rate.ToHz();
        g_window_state_lock.unlock();
        return true;
    }

    return false;
}

void LatentSyncLimiter::LimitFrameRate() {
    if (!m_enabled) return;

    // If no target FPS set, simply wait for one vblank to pace to refresh
    const bool cap_to_fps = (m_target_fps > 0.0f);

    if (!EnsureAdapterBinding()) return;

    if (!LoadProcCached(m_pfnWaitForVerticalBlankEvent, L"gdi32.dll", "D3DKMTWaitForVerticalBlankEvent"))
        return;

    // Wait for next vertical blank
    D3DKMT_WAITFORVERTICALBLANKEVENT req{};
    req.hAdapter = m_hAdapter;
    req.hDevice = 0;
    req.VidPnSourceId = m_vidpn_source_id;
    reinterpret_cast<NTSTATUS (WINAPI*)(const D3DKMT_WAITFORVERTICALBLANKEVENT*)>(m_pfnWaitForVerticalBlankEvent)(&req);

    if (!cap_to_fps) {
        return;
    }

    // If target FPS is below refresh, skip additional vblanks to approximate the desired FPS
    double refresh = m_refresh_hz;
    if (refresh <= 0.0) {
        g_window_state_lock.lock();
        refresh = g_window_state.current_monitor_refresh_rate.ToHz();
        g_window_state_lock.unlock();
        if (refresh <= 0.0) refresh = 60.0;
        m_refresh_hz = refresh;
    }

    if (m_target_fps >= refresh - 0.001) {
        return; // at/above refresh, single vblank wait is fine
    }

    // Compute how many vblanks to wait between presents: n = round(refresh/target)
    int presents_per_frame = (int)max(1.0, std::round(refresh / m_target_fps));
    for (int i = 1; i < presents_per_frame; ++i) {
        reinterpret_cast<NTSTATUS (WINAPI*)(const D3DKMT_WAITFORVERTICALBLANKEVENT*)>(m_pfnWaitForVerticalBlankEvent)(&req);
    }
}

} // namespace dxgi::fps_limiter


