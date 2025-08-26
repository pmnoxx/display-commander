#include "latent_sync_limiter.hpp"
#include "../addon.hpp"
#include "../utils.hpp"
#include <dxgi1_6.h>
#include <cwchar>
#include <algorithm>

static uint64_t s_last_scan_time = 0;

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

    // Choose pacing path based on global latent sync mode
    const int mode = s_latent_sync_mode.load();

    if (mode == 1) {
        // Scanline-based pacing (experimental)
        if (!LoadProcCached(m_pfnGetScanLine, L"gdi32.dll", "D3DKMTGetScanLine"))
            return;

        D3DKMT_GETSCANLINE scan{};
        scan.hAdapter = m_hAdapter;
        scan.VidPnSourceId = m_vidpn_source_id;

        // Poll until we are in vblank once to start the frame at a consistent phase
        // This mirrors a minimal Special-K approach without full calibration
        int safety = 0;
        int monitor_height = GetCurrentMonitorHeight();
        int actual_threshold = min(monitor_height - 1, static_cast<int>(s_scanline_threshold.load() * monitor_height));
        int actual_threshold_end = actual_threshold + s_scanline_window.load();
        while (true) {
            if (reinterpret_cast<NTSTATUS (WINAPI*)(D3DKMT_GETSCANLINE*)>(m_pfnGetScanLine)(&scan) == 0) {
                if (scan.InVerticalBlank) {
                    if (s_scanline_threshold.load() == 100 || s_scanline_threshold.load() == 0) break;
                    std::this_thread::sleep_for(std::chrono::microseconds(1));
                } else {
                    if (scan.ScanLine >= actual_threshold && scan.ScanLine < actual_threshold_end) {
                        break;
                    }
                    if (scan.ScanLine < actual_threshold - 500) {
                        std::this_thread::sleep_for(std::chrono::microseconds(1));
                    }
                }
            }
        }
        return;

    } else {
        // VBlank-based pacing (current)
        if (!LoadProcCached(m_pfnWaitForVerticalBlankEvent, L"gdi32.dll", "D3DKMTWaitForVerticalBlankEvent"))
            return;

        // Wait for next vertical blank
        D3DKMT_WAITFORVERTICALBLANKEVENT req{};
        req.hAdapter = m_hAdapter;
        req.hDevice = 0;
        req.VidPnSourceId = m_vidpn_source_id;
        reinterpret_cast<NTSTATUS (WINAPI*)(const D3DKMT_WAITFORVERTICALBLANKEVENT*)>(m_pfnWaitForVerticalBlankEvent)(&req);
    }

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

    if (mode == 1) {
        // Scanline: approximate additional wait by watching for additional vblank edges
        // Compute number of refresh intervals per desired frame
        int presents_per_frame = (int)max(1.0, std::round(refresh / m_target_fps));
        if (presents_per_frame <= 1) return;

        if (!LoadProcCached(m_pfnGetScanLine, L"gdi32.dll", "D3DKMTGetScanLine"))
            return;
        D3DKMT_GETSCANLINE scan{};
        scan.hAdapter = m_hAdapter;
        scan.VidPnSourceId = m_vidpn_source_id;
        for (int i = 1; i < presents_per_frame; ++i) {
            // Wait for next vblank edge: first ensure we leave current vblank, then wait until we enter next
            int guard = 0;
            // Phase 1: leave current vblank (if in it)
            while (guard++ < 5000) {
                if (reinterpret_cast<NTSTATUS (WINAPI*)(D3DKMT_GETSCANLINE*)>(m_pfnGetScanLine)(&scan) == 0) {
                    if (!scan.InVerticalBlank) break;
                }
                Sleep(0);
            }
            // Phase 2: wait until next vblank
            guard = 0;
            while (guard++ < 5000) {
                if (reinterpret_cast<NTSTATUS (WINAPI*)(D3DKMT_GETSCANLINE*)>(m_pfnGetScanLine)(&scan) == 0) {
                    if (scan.InVerticalBlank) break;
                }
                Sleep(0);
            }
        }
    } else {
        // Compute how many vblanks to wait between presents: n = round(refresh/target)
        int presents_per_frame = (int)max(1.0, std::round(refresh / m_target_fps));
        D3DKMT_WAITFORVERTICALBLANKEVENT req{};
        req.hAdapter = m_hAdapter;
        req.hDevice = 0;
        req.VidPnSourceId = m_vidpn_source_id;
        for (int i = 1; i < presents_per_frame; ++i) {
            reinterpret_cast<NTSTATUS (WINAPI*)(const D3DKMT_WAITFORVERTICALBLANKEVENT*)>(m_pfnWaitForVerticalBlankEvent)(&req);
        }
    }
}

} // namespace dxgi::fps_limiter


