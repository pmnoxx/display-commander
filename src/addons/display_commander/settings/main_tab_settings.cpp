#include "main_tab_settings.hpp"
#include "../adhd_multi_monitor/adhd_simple_api.hpp"
#include "../hooks/api_hooks.hpp"
#include "../utils.hpp"

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
    : window_mode("window_mode", s_window_mode, static_cast<int>(WindowMode::kFullscreen), {"Borderless Fullscreen", "Borderless Windowed (Aspect Ratio)"}, "DisplayCommander"),
      aspect_index("aspect_index", 3, {"3:2", "4:3", "16:10", "16:9", "19:9", "19.5:9", "21:9", "32:9"},
                   "DisplayCommander"), // Default to 16:9
      target_monitor_index("target_monitor_index", 0,
                           {"Monitor 1", "Monitor 2", "Monitor 3", "Monitor 4", "Monitor 5", "Monitor 6", "Monitor 7",
                            "Monitor 8", "Monitor 9", "Monitor 10"},
                           "DisplayCommander"),
      background_feature("background_feature", s_background_feature_enabled, false, "DisplayCommander"),
      alignment("alignment", 0, {"Center", "Top Left", "Top Right", "Bottom Left", "Bottom Right"}, "DisplayCommander"),
      fps_limiter_mode("fps_limiter_mode", 0,
                       {"None", "Precise Frame Rate Limiter", "VBlank Scanline Sync for VSync-OFF"}, "DisplayCommander"),
      scanline_offset("scanline_offset", s_scanline_offset, 0, -1000, 1000, "DisplayCommander"),
      vblank_sync_divisor("vblank_sync_divisor", s_vblank_sync_divisor, 1, 0, 8, "DisplayCommander"),
      fps_limiter_injection("fps_limiter_injection", s_fps_limiter_injection, 0, 0, 2, "DisplayCommander"),
      fps_limit("fps_limit", s_fps_limit, 0.0f, 0.0f, 240.0f, "DisplayCommander"),
      fps_limit_background("fps_limit_background", s_fps_limit_background, 30.0f, 0.0f, 240.0f, "DisplayCommander"),
      present_pacing_delay_percentage("present_pacing_delay_percentage", s_present_pacing_delay_percentage, 0.0f, 0.0f,
                                      100.0f, "DisplayCommander"),
      force_vsync_on("force_vsync_on", s_force_vsync_on, false, "DisplayCommander"),
      force_vsync_off("force_vsync_off", s_force_vsync_off, false, "DisplayCommander"),
      prevent_tearing("prevent_tearing", s_prevent_tearing, false, "DisplayCommander"),
      audio_volume_percent("audio_volume_percent", s_audio_volume_percent, 100.0f, 0.0f, 100.0f, "DisplayCommander"),
      audio_mute("audio_mute", s_audio_mute, false, "DisplayCommander"),
      mute_in_background("mute_in_background", s_mute_in_background, false, "DisplayCommander"),
      mute_in_background_if_other_audio("mute_in_background_if_other_audio", s_mute_in_background_if_other_audio, false,
                                        "DisplayCommander"),
      audio_volume_auto_apply("audio_volume_auto_apply", false, "DisplayCommander"),
      block_input_in_background("block_input_in_background", s_block_input_in_background, true, "DisplayCommander"),
      block_input_without_reshade("block_input_without_reshade", s_block_input_without_reshade, false,
                                  "DisplayCommander"),
      no_render_in_background("no_render_in_background", s_no_render_in_background, false, "DisplayCommander"),
      no_present_in_background("no_present_in_background", s_no_present_in_background, false, "DisplayCommander"),
      show_test_overlay("show_test_overlay", false, "DisplayCommander"),
      target_display("target_display", "", "DisplayCommander"),
      game_window_display_device_id("game_window_display_device_id", "", "DisplayCommander"),
      adhd_multi_monitor_enabled("adhd_multi_monitor_enabled", false, "DisplayCommander"),
      screensaver_mode("screensaver_mode", s_screensaver_mode, static_cast<int>(ScreensaverMode::kDefault),
                       {"Default (no change)", "Disable when Focused", "Disable"}, "DisplayCommander") {

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
        &game_window_display_device_id,
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

std::vector<ui::new_ui::SettingBase *> MainTabSettings::GetAllSettings() { return all_settings_; }

// Helper function to convert wstring to string
std::string WStringToString(const std::wstring &wstr) {
    if (wstr.empty())
        return std::string();

    int size = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (size <= 0)
        return std::string();

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

    // Get the full device ID using EnumDisplayDevices with EDD_GET_DEVICE_INTERFACE_NAME
    DISPLAY_DEVICEW displayDevice;
    ZeroMemory(&displayDevice, sizeof(displayDevice));
    displayDevice.cb = sizeof(displayDevice);

    DWORD deviceIndex = 0;
    while (EnumDisplayDevicesW(NULL, deviceIndex, &displayDevice, 0)) {
        // Check if this is the device we're looking for
        if (wcscmp(displayDevice.DeviceName, mi.szDevice) == 0) {
            // Found the matching device, now get the full device ID
            DISPLAY_DEVICEW monitorDevice;
            ZeroMemory(&monitorDevice, sizeof(monitorDevice));
            monitorDevice.cb = sizeof(monitorDevice);

            DWORD monitorIndex = 0;
            while (EnumDisplayDevicesW(displayDevice.DeviceName, monitorIndex, &monitorDevice,
                                       EDD_GET_DEVICE_INTERFACE_NAME)) {
                // Return the full device ID (DeviceID contains the full path like DISPLAY\AUS32B4\5&24D3239D&1&UID4353)
                if (wcslen(monitorDevice.DeviceID) > 0) {
                    return WStringToString(monitorDevice.DeviceID);
                }
                monitorIndex++;
            }
            break;
        }
        deviceIndex++;
    }

    // Fallback to simple device name if full device ID not found
    return WStringToString(mi.szDevice);
}

// Function to save the display device ID for the game window
void SaveGameWindowDisplayDeviceId(HWND hwnd) {
    std::string device_id = GetDisplayDeviceIdFromWindow(hwnd);
    settings::g_mainTabSettings.game_window_display_device_id.SetValue(device_id);

    std::ostringstream oss;
    oss << "Saved game window display device ID: " << device_id;
    LogInfo(oss.str().c_str());
}

// Function to update the target display setting with current game window
void UpdateTargetDisplayFromGameWindow() {
    // Get the game window from the API hooks
    HWND game_window = display_commanderhooks::GetGameWindow();

    std::string display_id = GetDisplayDeviceIdFromWindow(game_window);
    settings::g_mainTabSettings.target_display.SetValue(display_id);
}

void UpdateFpsLimitMaximums() {
    // Only update if display cache is initialized
    if (!display_cache::g_displayCache.IsInitialized()) {
        return;
    }

    // Get the maximum refresh rate across all monitors
    double max_refresh_rate = display_cache::g_displayCache.GetMaxRefreshRateAcrossAllMonitors();

    // Update the maximum values for FPS limit settings
    // Add some buffer (e.g., 10%) to allow for slightly higher FPS than max refresh rate
    float max_fps = static_cast<float>(max_refresh_rate * 1.1f);

    // Ensure minimum of 60 FPS and maximum of 1000 FPS for safety
    if (max_fps < 60.0f)
        max_fps = 60.0f;
    if (max_fps > 1000.0f)
        max_fps = 1000.0f;

    // Update the maximum values
    g_mainTabSettings.fps_limit.SetMax(max_fps);
    g_mainTabSettings.fps_limit_background.SetMax(max_fps);

    LogInfo("Updated FPS limit maximum to %.1f FPS (based on max monitor refresh rate of %.1f Hz)", max_fps,
            max_refresh_rate);
}

} // namespace settings
