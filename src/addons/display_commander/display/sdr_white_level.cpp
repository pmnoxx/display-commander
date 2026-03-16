// Source Code <Display Commander>
#include "sdr_white_level.hpp"
#include "../globals.hpp"
#include "../utils/logging.hpp"

#include <Windows.h>

namespace display_commander::display::sdr_white_level {

namespace {

using DwmpSDRToHDRBoostPtr = HRESULT(__stdcall*)(HMONITOR, double);

DwmpSDRToHDRBoostPtr GetDwmpSDRToHDRBoost() {
    static DwmpSDRToHDRBoostPtr s_fn = nullptr;
    static bool s_tried = false;
    if (s_tried) {
        return s_fn;
    }
    s_tried = true;
    HMODULE dwm = LoadLibraryW(L"dwmapi.dll");
    if (dwm == nullptr) {
        return nullptr;
    }
    auto* p = GetProcAddress(dwm, MAKEINTRESOURCEA(171));
    s_fn = reinterpret_cast<DwmpSDRToHDRBoostPtr>(p);
    return s_fn;
}

}  // namespace

HMONITOR GetGameMonitorForSdrBrightness() {
    HWND hwnd = g_last_swapchain_hwnd.load(std::memory_order_acquire);
    if (hwnd != nullptr && IsWindow(hwnd) != FALSE) {
        HMONITOR mon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTOPRIMARY);
        if (mon != nullptr) {
            return mon;
        }
    }
    return MonitorFromWindow(nullptr, MONITOR_DEFAULTTOPRIMARY);
}

bool SetSdrWhiteLevelNits(HMONITOR monitor, float nits) {
    if (monitor == nullptr) {
        return false;
    }
    float clamped = nits;
    if (nits < static_cast<float>(kSdrNitsMin)) {
        clamped = static_cast<float>(kSdrNitsMin);
    } else if (nits > static_cast<float>(kSdrNitsMax)) {
        clamped = static_cast<float>(kSdrNitsMax);
    }
    double boost = static_cast<double>(clamped) / 80.0;

    DwmpSDRToHDRBoostPtr fn = GetDwmpSDRToHDRBoost();
    if (fn == nullptr) {
        LogWarn("SDR white level: DwmpSDRToHDRBoost not available (dwmapi ordinal 171)");
        return false;
    }
    HRESULT hr = fn(monitor, boost);
    if (FAILED(hr)) {
        LogWarn("SDR white level: DwmpSDRToHDRBoost failed HR=0x%08lX", static_cast<unsigned long>(hr));
        return false;
    }
    return true;
}

}  // namespace display_commander::display::sdr_white_level
