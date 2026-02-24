#include "standalone_ui_settings_bridge.hpp"
#include "../globals.hpp"
#include "../hooks/api_hooks.hpp"
#include "../settings/main_tab_settings.hpp"

namespace standalone_ui_settings {

int GetFpsLimiterMode() {
    return settings::g_mainTabSettings.fps_limiter_mode.GetValue();
}
void SetFpsLimiterMode(int value) {
    settings::g_mainTabSettings.fps_limiter_mode.SetValue(value);
}

float GetFpsLimit() {
    return settings::g_mainTabSettings.fps_limit.GetValue();
}
void SetFpsLimit(float value) {
    settings::g_mainTabSettings.fps_limit.SetValue(value);
}

bool GetAudioMute() {
    return settings::g_mainTabSettings.audio_mute.GetValue();
}
void SetAudioMute(bool value) {
    settings::g_mainTabSettings.audio_mute.SetValue(value);
}

float GetAudioVolumePercent() {
    return ::s_audio_volume_percent.load();
}
void SetAudioVolumePercent(float value) {
    ::s_audio_volume_percent.store(value);
    settings::g_mainTabSettings.audio_volume_percent.SetValue(value);
}

bool GetMuteInBackground() {
    return settings::g_mainTabSettings.mute_in_background.GetValue();
}
void SetMuteInBackground(bool value) {
    settings::g_mainTabSettings.mute_in_background.SetValue(value);
}

int GetWindowMode() {
    return settings::g_mainTabSettings.window_mode.GetValue();
}
void SetWindowMode(int value) {
    settings::g_mainTabSettings.window_mode.SetValue(value);
}

std::string GetTargetDisplayDeviceId() {
    return settings::g_mainTabSettings.target_extended_display_device_id.GetValue();
}
void SetTargetDisplayDeviceId(const std::string& value) {
    settings::g_mainTabSettings.target_extended_display_device_id.SetValue(value);
}

double GetCurrentFps() {
    const uint32_t count = ::g_perf_ring.GetCount();
    double total_time = 0.0;
    uint32_t sample_count = 0;
    for (uint32_t i = 0; i < count && i < ::kPerfRingCapacity; ++i) {
        const ::PerfSample& sample = ::g_perf_ring.GetSample(i);
        if (sample.dt == 0.0f || total_time >= 1.0) break;
        sample_count++;
        total_time += static_cast<double>(sample.dt);
    }
    if (sample_count > 0 && total_time >= 1.0)
        return sample_count / total_time;
    return 0.0;
}

uintptr_t GetLastSwapchainHwnd() {
    return reinterpret_cast<uintptr_t>(::g_last_swapchain_hwnd.load(std::memory_order_acquire));
}

void SetStandaloneUiHwnd(uintptr_t hwnd) {
    ::g_standalone_ui_hwnd.store(reinterpret_cast<HWND>(hwnd), std::memory_order_release);
}

HWND CreateWindowW_Direct(LPCWSTR lpClassName, LPCWSTR lpWindowName, DWORD dwStyle, int X, int Y, int nWidth,
                          int nHeight, HWND hWndParent, HMENU hMenu, HINSTANCE hInstance, LPVOID lpParam) {
    return display_commanderhooks::CreateWindowW_Direct(lpClassName, lpWindowName, dwStyle, X, Y, nWidth, nHeight,
                                                        hWndParent, hMenu, hInstance, lpParam);
}

}  // namespace standalone_ui_settings
