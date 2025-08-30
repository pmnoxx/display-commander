#include "latent_sync_limiter.hpp"
#include "../addon.hpp"
#include "../utils.hpp"
#include <dxgi1_6.h>
#include <sstream>
#include "../display/query_display.hpp"

static uint64_t s_last_scan_time = 0;




namespace dxgi::fps_limiter {
    extern LONGLONG get_now_ticks();
    std::atomic<LONGLONG> ticks_per_refresh{0};
    std::atomic<double> correction_lines_delta{0};
    std::atomic<double> m_on_present_ticks{0.0};

static inline FARPROC LoadProcCached(FARPROC& slot, const wchar_t* mod, const char* name) {
    if (slot != nullptr) return slot;
    HMODULE h = GetModuleHandleW(mod);
    if (h == nullptr) h = LoadLibraryW(mod);
    if (h != nullptr) slot = GetProcAddress(h, name);
    return slot;
}

LatentSyncLimiter::LatentSyncLimiter() {
    // Create and automatically start the VBlank monitor
    m_vblank_monitor = std::make_unique<VBlankMonitor>();
    m_vblank_monitor->StartMonitoring();
}

LatentSyncLimiter::~LatentSyncLimiter() {
    // Stop and clean up the VBlank monitor
    if (m_vblank_monitor) {
        m_vblank_monitor->StopMonitoring();
        m_vblank_monitor.reset();
    }
    
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
    if ( reinterpret_cast<NTSTATUS (WINAPI*)(D3DKMT_OPENADAPTERFROMGDIDISPLAYNAME*)>(m_pfnOpenAdapterFromGdiDisplayName)(&openReq) == STATUS_SUCCESS ) {
        m_hAdapter = openReq.hAdapter;
        m_vidpn_source_id = openReq.VidPnSourceId;
        m_bound_display_name = name;

        // Try to cache refresh rate from global state if available
        auto window_state = g_window_state.load();
        if (window_state) {
            m_refresh_hz = window_state->current_monitor_refresh_rate.ToHz();
        }
        return true;
    }

    return false;
}

void LatentSyncLimiter::LimitFrameRate() {
    if (!m_enabled) return;

    // If no target FPS set, simply wait for one vblank to pace to refresh
    const bool cap_to_fps = (m_target_fps > 0.0f);

    D3DKMT_GETSCANLINE scan{};
    scan.hAdapter = m_hAdapter;
    scan.VidPnSourceId = m_vidpn_source_id;

    // Compute adjusted scanline window accounting for average Present duration
    int monitor_height = GetCurrentMonitorHeight();
  
    HWND hwnd = g_last_swapchain_hwnd.load();
    DisplayTimingInfo current_display_timing = GetDisplayTimingInfoForWindow(hwnd);
    
    LONGLONG total_height = current_display_timing.total_height;
    LONGLONG active_height = current_display_timing.active_height;
    LONGLONG mid_vblank_scanline = (active_height + total_height) / 2;

    LONGLONG now_ticks = get_now_ticks();

    extern double expected_current_scanline(LONGLONG now_ticks, int total_height, bool add_correction);
    double expected_scanline = expected_current_scanline(now_ticks, total_height, true);
    double target_line = mid_vblank_scanline - (total_height * m_on_present_ticks.load()) / ticks_per_refresh.load() - 80.0 + s_scanline_offset.load();

    double diff_lines = target_line - expected_scanline;
    if (diff_lines < 0) {
        diff_lines += total_height;
    }

    double delta_wait_time = diff_lines * (1.0 * ticks_per_refresh.load() / total_height);

    LONGLONG wait_target = now_ticks + delta_wait_time;

    {
        std::ostringstream oss;
        oss << " mid_vblank_scanline: " << mid_vblank_scanline;
        oss << " ticks_per_refresh: " << ticks_per_refresh.load();
        oss << " wait_target: " << wait_target;
        oss << " now_ticks: " << now_ticks;
        oss << " correction_ticks_delta: " << correction_lines_delta.load();
        oss << " m_on_present_ticks: " << m_on_present_ticks;
        oss << " target_line: " << target_line;
        LogInfo(oss.str().c_str());   
    }

    LONGLONG start_ticks = now_ticks;
    while (true) {
        now_ticks = get_now_ticks();
        if (now_ticks - start_ticks > delta_wait_time) {
            break;
        }
    }
    {
        double expected_scanline = expected_current_scanline(now_ticks, total_height, true);
        std::ostringstream oss;
        oss << " fpslimiter expected_current_scanline: " << expected_scanline;
        oss << " target_line: " << target_line;
        LogInfo(oss.str().c_str());   
    }
}

void LatentSyncLimiter::OnPresentBegin() {
    LARGE_INTEGER t; QueryPerformanceCounter(&t);
    m_qpc_present_begin = t.QuadPart;
}

void LatentSyncLimiter::OnPresentEnd() {
    if (m_qpc_present_begin == 0) {
        std::ostringstream oss;
        oss << "Present duration(real): 0 ticks";
        LogInfo(oss.str().c_str());   
        return;
    }
    LARGE_INTEGER now; QueryPerformanceCounter(&now);
    LONGLONG dt = now.QuadPart - m_qpc_present_begin;
    m_qpc_present_begin = 0;
    {
        std::vector<DisplayTimingInfo> timing_info = QueryDisplayTimingInfo();
        LONGLONG total_height = timing_info[0].total_height;

        LONGLONG now_ticks = get_now_ticks();
        std::ostringstream oss;
        oss << "OnPresentEnd: ";
        extern double expected_current_scanline(LONGLONG now_ticks, int total_height, bool add_correction);
        oss << " predicted scanline: " << int(expected_current_scanline(now_ticks, total_height, true));
        LogInfo(oss.str().c_str());   
    }
    // Exponential moving average of Present duration in ticks
    const double alpha = 0.10; // smooth but responsive
    if (m_avg_present_ticks <= 0.0)
        m_avg_present_ticks = static_cast<double>(dt);
    else
        m_avg_present_ticks = alpha * static_cast<double>(dt) + (1.0 - alpha) * m_avg_present_ticks;

    std::ostringstream oss;
    oss << "Present duration: " << m_avg_present_ticks / 10000.0 << " ms " << m_avg_present_ticks << " ticks";
    LogInfo(oss.str().c_str());
    m_on_present_ticks.store(m_avg_present_ticks);   
}

void LatentSyncLimiter::StartVBlankMonitoring() {
    if (!m_vblank_monitor) {
        m_vblank_monitor = std::make_unique<VBlankMonitor>();
    }
    m_vblank_monitor->StartMonitoring();
}

void LatentSyncLimiter::StopVBlankMonitoring() {
    if (m_vblank_monitor) {
        m_vblank_monitor->StopMonitoring();
    }
}

} // namespace dxgi::fps_limiter


