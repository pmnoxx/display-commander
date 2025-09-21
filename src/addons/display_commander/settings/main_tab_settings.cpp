#include "main_tab_settings.hpp"
#include "../addon.hpp"
#include "../hooks/api_hooks.hpp"
#include "../adhd_multi_monitor/adhd_simple_api.hpp"
#include <windows.h>

// Atomic variables used by main tab settings
std::atomic<bool> s_background_feature_enabled{false}; // Disabled by default
std::atomic<int> s_scanline_offset{0};
std::atomic<int> s_vblank_sync_divisor{1};
std::atomic<int> s_fps_limiter_injection{0}; // FPS_LIMITER_INJECTION_ONPRESENTFLAGS
std::atomic<float> s_fps_limit{0.f};
std::atomic<float> s_fps_limit_background{30.f};
std::atomic<float> s_present_pacing_delay_percentage{0.0f}; // Default to 0% (no delay)
std::atomic<bool> s_force_vsync_on{false};
std::atomic<bool> s_force_vsync_off{false};
std::atomic<bool> s_prevent_tearing{false};
std::atomic<float> s_audio_volume_percent{100.f};
std::atomic<bool> s_audio_mute{false};
std::atomic<bool> s_mute_in_background{false};
std::atomic<bool> s_mute_in_background_if_other_audio{false};
std::atomic<bool> s_block_input_in_background{true};
std::atomic<bool> s_block_input_without_reshade{false};
std::atomic<bool> s_no_render_in_background{false};
std::atomic<bool> s_no_present_in_background{false};
std::atomic<ScreensaverMode> s_screensaver_mode{ScreensaverMode::kDefault};

namespace settings {

MainTabSettings::MainTabSettings()
    : window_mode("window_mode", 0, {"Borderless Windowed (Aspect Ratio)", "Borderless Fullscreen"}, "renodx_main_tab"),
    aspect_index("aspect_index", 3, {"3:2", "4:3", "16:10", "16:9", "19:9", "19.5:9", "21:9", "32:9"}, "renodx_main_tab"), // Default to 16:9
    target_monitor_index("target_monitor_index", 0, {"Auto", "Monitor 1", "Monitor 2", "Monitor 3", "Monitor 4", "Monitor 5", "Monitor 6", "Monitor 7", "Monitor 8", "Monitor 9", "Monitor 10"}, "renodx_main_tab"),
    background_feature("background_feature", s_background_feature_enabled, false, "renodx_main_tab"),
    alignment("alignment", 0, {"Center", "Top Left", "Top Right", "Bottom Left", "Bottom Right"}, "renodx_main_tab"),
    fps_limiter_mode("fps_limiter_mode", 0, {"None", "Precise Frame Rate Limiter", "VBlank Scanline Sync for VSync-OFF"}, "renodx_main_tab"),
    scanline_offset("scanline_offset", s_scanline_offset, 0, -1000, 1000, "renodx_main_tab"),
    vblank_sync_divisor("vblank_sync_divisor", s_vblank_sync_divisor, 1, 0, 8, "renodx_main_tab"),
    fps_limiter_injection("fps_limiter_injection", s_fps_limiter_injection, 0, 0, 2, "renodx_main_tab"),
    fps_limit("fps_limit", s_fps_limit, 0.0f, 0.0f, 240.0f, "renodx_main_tab"),
    fps_limit_background("fps_limit_background", s_fps_limit_background, 30.0f, 0.0f, 240.0f, "renodx_main_tab"),
    present_pacing_delay_percentage("present_pacing_delay_percentage", s_present_pacing_delay_percentage, 0.0f, 0.0f, 100.0f, "renodx_main_tab"),
    force_vsync_on("force_vsync_on", s_force_vsync_on, false, "renodx_main_tab"),
    force_vsync_off("force_vsync_off", s_force_vsync_off, false, "renodx_main_tab"),
    prevent_tearing("prevent_tearing", s_prevent_tearing, false, "renodx_main_tab"),
    audio_volume_percent("audio_volume_percent", s_audio_volume_percent, 100.0f, 0.0f, 100.0f, "renodx_main_tab"),
    audio_mute("audio_mute", s_audio_mute, false, "renodx_main_tab"),
    mute_in_background("mute_in_background", s_mute_in_background, false, "renodx_main_tab"),
    mute_in_background_if_other_audio("mute_in_background_if_other_audio", s_mute_in_background_if_other_audio, false, "renodx_main_tab"),
    audio_volume_auto_apply("audio_volume_auto_apply", false, "renodx_main_tab"),
    block_input_in_background("block_input_in_background", s_block_input_in_background, true, "renodx_main_tab"),
    block_input_without_reshade("block_input_without_reshade", s_block_input_without_reshade, false, "renodx_main_tab"),
    no_render_in_background("no_render_in_background", s_no_render_in_background, false, "renodx_main_tab"),
    no_present_in_background("no_present_in_background", s_no_present_in_background, false, "renodx_main_tab"),
    show_test_overlay("show_test_overlay", false, "renodx_main_tab"),
    target_display("target_display", "", "renodx_main_tab"),
    adhd_multi_monitor_enabled("adhd_multi_monitor_enabled", false, "renodx_main_tab"),
    screensaver_mode("screensaver_mode", s_screensaver_mode, static_cast<int>(ScreensaverMode::kDefault), {"Default (no change)", "Disable in Background", "Disable"}, "renodx_main_tab") {

    // Initialize the all_settings_ vector
    all_settings_ = {
        &window_mode,
        &aspect_index,
        &target_monitor_index,
        &background_feature,
        &alignment,
        &fps_limiter_mode,
        &scanline_offset,
        &vblank_sync_divisor,
        &fps_limiter_injection,
        &fps_limit,
        &fps_limit_background,
        &present_pacing_delay_percentage,
        &force_vsync_on,
        &force_vsync_off,
        &prevent_tearing,
        &audio_volume_percent,
        &audio_mute,
        &mute_in_background,
        &mute_in_background_if_other_audio,
        &audio_volume_auto_apply,
        &block_input_in_background,
        &block_input_without_reshade,
        &no_render_in_background,
        &no_present_in_background,
        &show_test_overlay,
        &target_display,
        &adhd_multi_monitor_enabled,
        &screensaver_mode,
    };
}

// TODO add initialization of other settings
void MainTabSettings::LoadSettings() {
    LogInfo("MainTabSettings::LoadSettings() called");
    LoadTabSettings(all_settings_);

    // Apply ADHD Multi-Monitor Mode settings after loading
    adhd_multi_monitor::api::SetEnabled(adhd_multi_monitor_enabled.GetValue());

    LogInfo("MainTabSettings::LoadSettings() completed");
}

std::vector<ui::new_ui::SettingBase*> MainTabSettings::GetAllSettings() {
    return all_settings_;
}

// Helper function to convert wstring to string
std::string WStringToString(const std::wstring& wstr) {
    if (wstr.empty()) return std::string();

    int size = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (size <= 0) return std::string();

    std::string result(size - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, &result[0], size, nullptr, nullptr);
    return result;
}

// Function to get display device ID from a window
std::string GetDisplayDeviceIdFromWindow(HWND hwnd) {
    if (hwnd == nullptr || !IsWindow(hwnd)) {
        return "No Window";
    }

    // Get the monitor that contains the window
    HMONITOR hmon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    if (hmon == nullptr) {
        return "No Monitor";
    }

    // Get monitor information
    MONITORINFOEXW mi{};
    mi.cbSize = sizeof(mi);
    if (!GetMonitorInfoW(hmon, &mi)) {
        return "Monitor Info Failed";
    }

    // Convert wide string to UTF-8 string
    return WStringToString(mi.szDevice);
}

// Function to update the target display setting with current game window
void UpdateTargetDisplayFromGameWindow() {
    // Get the game window from the API hooks
    HWND game_window = renodx::hooks::GetGameWindow();

    std::string display_id = GetDisplayDeviceIdFromWindow(game_window);
    settings::g_mainTabSettings.target_display.SetValue(display_id);
}

} // namespace settings
