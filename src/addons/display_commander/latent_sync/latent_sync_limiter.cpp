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

    // Scanline-based pacing (experimental)
    if (!LoadProcCached(m_pfnGetScanLine, L"gdi32.dll", "D3DKMTGetScanLine"))
        return;

    D3DKMT_GETSCANLINE scan{};
    scan.hAdapter = m_hAdapter;
    scan.VidPnSourceId = m_vidpn_source_id;

    // Compute adjusted scanline window accounting for average Present duration
    int monitor_height = GetCurrentMonitorHeight();
    s_scanline_threshold.store(0.99);
    int base_threshold = min(monitor_height - 1, static_cast<int>(s_scanline_threshold.load() * monitor_height));
    int window = s_scanline_window.load();

    int adjusted_threshold = base_threshold;
    if (monitor_height > 0 && m_refresh_hz > 0.0 && m_avg_present_ticks > 0.0) {
        static LONGLONG s_qpc_freq = 0;
        if (s_qpc_freq == 0) {
            LARGE_INTEGER f; if (QueryPerformanceFrequency(&f)) s_qpc_freq = f.QuadPart;
        }
        if (s_qpc_freq > 0) {
            const double ticks_per_refresh = static_cast<double>(s_qpc_freq) / m_refresh_hz;
            const double ticks_per_scanline = ticks_per_refresh / static_cast<double>(monitor_height);
            const int present_scanlines = static_cast<int>(std::lround((m_avg_present_ticks * 0.5) / ticks_per_scanline));
            adjusted_threshold = max(0, base_threshold - max(0, present_scanlines));
        }
    }
    int adjusted_threshold_end = adjusted_threshold + window;

    // Poll until we are in desired window to start the frame at a consistent phase
    int safety = 0;
    while (true) {
        if (reinterpret_cast<NTSTATUS (WINAPI*)(D3DKMT_GETSCANLINE*)>(m_pfnGetScanLine)(&scan) == 0) {
            if (scan.InVerticalBlank) {
                if (s_scanline_threshold.load() == 100 || s_scanline_threshold.load() == 0) break;
                std::this_thread::sleep_for(std::chrono::microseconds(1));
            } else {
                if (scan.ScanLine >= adjusted_threshold && scan.ScanLine < adjusted_threshold_end) {
                    break;
                }
                if (scan.ScanLine < adjusted_threshold - 500) {
                    std::this_thread::sleep_for(std::chrono::microseconds(1));
                }
            }
        }
    }
}

void LatentSyncLimiter::OnPresentBegin() {
    LARGE_INTEGER t; QueryPerformanceCounter(&t);
    m_qpc_present_begin = t.QuadPart;
}

void LatentSyncLimiter::OnPresentEnd() {
    if (m_qpc_present_begin == 0) return;
    LARGE_INTEGER now; QueryPerformanceCounter(&now);
    LONGLONG dt = now.QuadPart - m_qpc_present_begin;
    m_qpc_present_begin = 0;

    // Exponential moving average of Present duration in ticks
    const double alpha = 0.10; // smooth but responsive
    if (m_avg_present_ticks <= 0.0)
        m_avg_present_ticks = static_cast<double>(dt);
    else
        m_avg_present_ticks = alpha * static_cast<double>(dt) + (1.0 - alpha) * m_avg_present_ticks;

    std::ostringstream oss;
    oss << "Present duration: " << m_avg_present_ticks / 1000.0 << " ms";
    LogInfo(oss.str().c_str());
}

} // namespace dxgi::fps_limiter


