#include "latent_sync_limiter.hpp"
#include <dxgi1_6.h>
#include "../globals.hpp"
#include "../settings/main_tab_settings.hpp"
#include "../utils/logging.hpp"
#include "utils/timing.hpp"

static uint64_t s_last_scan_time = 0;

HANDLE m_timer_handle = nullptr;

namespace dxgi::fps_limiter {
std::atomic<LONGLONG> ns_per_refresh{0};
std::atomic<double> correction_lines_delta{0};
std::atomic<double> m_on_present_ns{0.0};

extern long double expected_current_scanline_uncapped_ns(LONGLONG now_ns, LONGLONG total_height, bool add_correction);

inline static FARPROC LoadProcCached(FARPROC& slot, const wchar_t* mod, const char* name) {
    if (slot != nullptr) return slot;
    HMODULE h = GetModuleHandleW(mod);
    if (h == nullptr) h = LoadLibraryW(mod);
    if (h != nullptr) slot = GetProcAddress(h, name);
    return slot;
}

LatentSyncLimiter::LatentSyncLimiter() {
    // Create VBlank monitor but don't start it yet - it will be started when SetEnabled(true) is called
    m_vblank_monitor = std::make_unique<VBlankMonitor>();
    // Don't start monitoring here - it will be started when the limiter is enabled
}

LatentSyncLimiter::~LatentSyncLimiter() {
    // Stop and clean up the VBlank monitor
    if (m_vblank_monitor) {
        m_vblank_monitor->StopMonitoring();
        m_vblank_monitor.reset();
    }

    if (m_hAdapter != 0) {
        if (LoadProcCached(m_pfnCloseAdapter, L"gdi32.dll", "D3DKMTCloseAdapter")) {
            D3DKMT_CLOSEADAPTER closeReq{};
            closeReq.hAdapter = m_hAdapter;
            reinterpret_cast<NTSTATUS(WINAPI*)(const D3DKMT_CLOSEADAPTER*)>(m_pfnCloseAdapter)(&closeReq);
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
    MONITORINFOEXW mi{};
    mi.cbSize = sizeof(mi);
    if (GetMonitorInfoW(hmon, &mi)) {
        result = mi.szDevice;  // e.g. "\\.\DISPLAY1"
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
            D3DKMT_CLOSEADAPTER closeReq{};
            closeReq.hAdapter = m_hAdapter;
            reinterpret_cast<NTSTATUS(WINAPI*)(const D3DKMT_CLOSEADAPTER*)>(m_pfnCloseAdapter)(&closeReq);
        }
        m_hAdapter = 0;
    }

    if (!LoadProcCached(m_pfnOpenAdapterFromGdiDisplayName, L"gdi32.dll", "D3DKMTOpenAdapterFromGdiDisplayName"))
        return false;

    D3DKMT_OPENADAPTERFROMGDIDISPLAYNAME openReq{};
    wcsncpy_s(openReq.DeviceName, name.c_str(), _TRUNCATE);
    if (reinterpret_cast<NTSTATUS(WINAPI*)(D3DKMT_OPENADAPTERFROMGDIDISPLAYNAME*)>(m_pfnOpenAdapterFromGdiDisplayName)(
            &openReq)
        == STATUS_SUCCESS) {
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

long double last_wait_target_ns = 0.0L;

void LatentSyncLimiter::LimitFrameRate() {
    if (s_vblank_sync_divisor.load() == 0) {
        return;
    }
    StartVBlankMonitoring();

    extern std::atomic<LONGLONG> g_latent_sync_total_height;
    extern std::atomic<LONGLONG> g_latent_sync_active_height;
    LONGLONG total_height = g_latent_sync_total_height.load();
    LONGLONG active_height = g_latent_sync_active_height.load();
    double mid_vblank_scanline = (active_height + total_height) / 2.0;

    if (total_height == 0 || active_height == 0 || ns_per_refresh.load() == 0) {
        LogError("LatentSyncLimiter::LimitFrameRate: unitialized values");
        return;
    }

    LONGLONG now_ns = utils::get_now_ns();

    long double current_scanline_uncapped = expected_current_scanline_uncapped_ns(now_ns, total_height, true);

    long double target_line = mid_vblank_scanline - (m_on_present_ns.load() * total_height / ns_per_refresh.load())
                              - 60.0 + s_scanline_offset.load();

    long double next_scanline_uncapped =
        current_scanline_uncapped - fmod(current_scanline_uncapped, (long double)(total_height)) + target_line;

    long double last_scanline_uncapped = expected_current_scanline_uncapped_ns(last_wait_target_ns, total_height, true);
    long double expected_next_scanline_uncapped =
        max(last_scanline_uncapped, current_scanline_uncapped - 2 * total_height) + total_height;

    while (abs(next_scanline_uncapped - expected_next_scanline_uncapped)
           > abs((next_scanline_uncapped - total_height) - expected_next_scanline_uncapped)) {
        next_scanline_uncapped -= total_height;
    }
    while (abs(next_scanline_uncapped - expected_next_scanline_uncapped)
           > abs((next_scanline_uncapped + total_height) - expected_next_scanline_uncapped)) {
        next_scanline_uncapped += total_height;
    }

    long double diff_lines = next_scanline_uncapped - current_scanline_uncapped;

    long double delta_wait_time_ns = 1.0L * diff_lines * ns_per_refresh.load() / total_height;

    // When vblank_sync_divisor is 0 (off), don't add any additional wait time
    LONGLONG additional_wait_ns = ns_per_refresh.load() * (s_vblank_sync_divisor.load() - 1);
    LONGLONG wait_target_ns = now_ns + delta_wait_time_ns + additional_wait_ns;

    if (wait_target_ns >= utils::get_now_ns()) {
        if (delta_wait_time_ns > utils::SEC_TO_NS) {
            LogError("LatentSyncLimiter::LimitFrameRate: delta_wait_time_ns > utils::SEC_TO_NS");
            return;
        }
        utils::wait_until_ns(wait_target_ns, m_timer_handle);
    }
    last_wait_target_ns = utils::get_now_ns();
}

void LatentSyncLimiter::OnPresentEnd() {
    LONGLONG now_ns = utils::get_now_ns();
    LONGLONG dt_ns = now_ns - g_present_start_time_ns.load();

    if (m_on_present_ns.load() <= 0.0)
        m_on_present_ns.store(dt_ns);
    else {
        const double alpha = 0.01;
        m_on_present_ns.store(alpha * static_cast<double>(dt_ns) + (1.0 - alpha) * m_on_present_ns.load());
    }
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

}  // namespace dxgi::fps_limiter
