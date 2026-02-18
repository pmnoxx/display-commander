#pragma once

#include "../ui/new_ui/settings_wrapper.hpp"

#include <vector>

namespace settings {

// Bring setting types into scope
using ui::new_ui::BoolSetting;
using ui::new_ui::BoolSettingRef;
using ui::new_ui::ComboSetting;
using ui::new_ui::FixedIntArraySetting;
using ui::new_ui::FloatSetting;
using ui::new_ui::FloatSettingRef;
using ui::new_ui::IntSetting;
using ui::new_ui::SettingBase;
using ui::new_ui::StringSetting;

// Settings manager for the experimental tab
class ExperimentalTabSettings {
   public:
    ExperimentalTabSettings();
    ~ExperimentalTabSettings() = default;

    // Load all settings from ReShade config
    void LoadAll();

    // Get all settings for loading
    std::vector<SettingBase*> GetAllSettings();

    // Master auto-click enable
    BoolSetting auto_click_enabled;

    // Mouse position spoofing for auto-click sequences
    BoolSetting mouse_spoofing_enabled;

    // Click sequences (up to 5) - using arrays for cleaner code
    FixedIntArraySetting sequence_enabled;   // 0 = disabled, 1 = enabled
    FixedIntArraySetting sequence_x;         // X coordinates
    FixedIntArraySetting sequence_y;         // Y coordinates
    FixedIntArraySetting sequence_interval;  // Click intervals in ms

    // Backbuffer format override settings
    BoolSetting backbuffer_format_override_enabled;
    ComboSetting backbuffer_format_override;

    // Buffer resolution upgrade settings
    BoolSetting buffer_resolution_upgrade_enabled;
    IntSetting buffer_resolution_upgrade_width;
    IntSetting buffer_resolution_upgrade_height;
    IntSetting buffer_resolution_upgrade_scale_factor;
    ComboSetting buffer_resolution_upgrade_mode;

    // Texture format upgrade settings
    BoolSetting texture_format_upgrade_enabled;

    // Sleep hook settings
    BoolSetting sleep_hook_enabled;
    // Render-thread-only option removed
    FloatSetting sleep_multiplier;
    IntSetting min_sleep_duration_ms;
    IntSetting max_sleep_duration_ms;

    // Time slowdown settings
    BoolSetting timeslowdown_enabled;
    BoolSetting timeslowdown_compatibility_mode;
    FloatSetting timeslowdown_multiplier;
    FloatSetting timeslowdown_max_multiplier;

    // Individual timer hook settings
    ComboSetting query_performance_counter_hook;
    ComboSetting get_tick_count_hook;
    ComboSetting get_tick_count64_hook;
    ComboSetting time_get_time_hook;
    ComboSetting get_system_time_hook;
    ComboSetting get_system_time_as_file_time_hook;
    ComboSetting get_system_time_precise_as_file_time_hook;
    ComboSetting get_local_time_hook;
    ComboSetting nt_query_system_time_hook;

    // QPC enabled modules (comma-separated list of module names)
    StringSetting qpc_enabled_modules;

    // DLSS indicator settings
    BoolSetting dlss_indicator_enabled;

    // D3D9 FLIPEX upgrade settings
    BoolSetting d3d9_flipex_enabled;

    // Enable flip chain settings (DXGI only) - forces flip model
    BoolSetting enable_flip_chain_enabled;

    // DirectInput hook suppression settings
    BoolSetting suppress_dinput_hooks;

    // HID suppression settings
    BoolSetting hid_suppression_enabled;
    BoolSetting hid_suppression_dualsense_only;
    BoolSetting hid_suppression_block_readfile;
    BoolSetting hid_suppression_block_getinputreport;
    BoolSetting hid_suppression_block_getattributes;
    BoolSetting hid_suppression_block_createfile;

    // Debug output hook settings
    BoolSetting debug_output_log_to_reshade;
    BoolSetting debug_output_show_stats;

    // DirectInput device state blocking
    BoolSetting dinput_device_state_blocking;

    // Up/Down key press automation (9s up, 1s down, repeat)
    BoolSetting up_down_key_press_enabled;

    // Button-only press automation (Y/A buttons only, no stick movement)
    BoolSetting button_only_press_enabled;

    // Anisotropic filtering upgrade settings
    BoolSetting force_anisotropic_filtering;
    BoolSetting upgrade_min_mag_mip_linear;
    BoolSetting upgrade_compare_min_mag_mip_linear;
    BoolSetting upgrade_min_mag_linear_mip_point;
    BoolSetting upgrade_compare_min_mag_linear_mip_point;

    // DLL blocking feature
    BoolSetting dll_blocking_enabled;
    StringSetting blocked_dlls;

    // Rand hook settings
    BoolSetting rand_hook_enabled;
    IntSetting rand_hook_value;

    // Rand_s hook settings
    BoolSetting rand_s_hook_enabled;
    IntSetting rand_s_hook_value;

    // Thread tracking for frame pacing debug (NvAPI latency markers + ChooseFpsLimiter call sites)
    BoolSetting thread_tracking_enabled;

    // Performance measurement (profiling) - default off
    BoolSetting performance_measurement_enabled;
    // Per-metric toggles (default on)
    BoolSetting perf_measure_overlay_enabled;
    BoolSetting perf_measure_overlay_show_volume_enabled;
    BoolSetting perf_measure_overlay_show_vrr_status_enabled;
    BoolSetting perf_measure_handle_present_before_enabled;
    BoolSetting perf_measure_handle_present_before_device_query_enabled;
    BoolSetting perf_measure_handle_present_before_record_frame_time_enabled;
    BoolSetting perf_measure_handle_present_before_frame_statistics_enabled;
    BoolSetting perf_measure_track_present_statistics_enabled;
    BoolSetting perf_measure_on_present_flags2_enabled;
    BoolSetting perf_measure_handle_present_after_enabled;
    BoolSetting perf_measure_flush_command_queue_from_swapchain_enabled;
    BoolSetting perf_measure_enqueue_gpu_completion_enabled;
    BoolSetting perf_measure_get_independent_flip_state_enabled;
    BoolSetting perf_measure_on_present_update_before_enabled;

    // Performance suppression (debug) - default off
    // WARNING: Suppressing these functions changes behavior and can break features; intended for short debugging
    // sessions.
    BoolSetting performance_suppression_enabled;
    BoolSetting perf_suppress_overlay;
    BoolSetting perf_suppress_overlay_show_volume;
    BoolSetting perf_suppress_overlay_show_vrr_status;
    BoolSetting perf_suppress_handle_present_before;
    BoolSetting perf_suppress_handle_present_before_device_query;
    BoolSetting perf_suppress_handle_present_before_record_frame_time;
    BoolSetting perf_suppress_handle_present_before_frame_statistics;
    BoolSetting perf_suppress_track_present_statistics;
    BoolSetting perf_suppress_on_present_flags2;
    BoolSetting perf_suppress_handle_present_after;
    BoolSetting perf_suppress_flush_command_queue_from_swapchain;
    BoolSetting perf_suppress_enqueue_gpu_completion;
    BoolSetting perf_suppress_get_independent_flip_state;
    BoolSetting perf_suppress_on_present_update_before;

    // Show volume overlay setting
    BoolSetting show_volume;

    // Show advanced NVIDIA profile settings (Ansel, FXAA, etc.) in addition to important settings
    BoolSetting show_advanced_profile_settings;

    // Translate mouse position from window resolution to render resolution (e.g. 3840x2160 -> 1920x1080)
    BoolSetting translate_mouse_position;
    // Override width/height when non-zero; when either is 0 use render width/height
    IntSetting translate_mouse_position_override_width;
    IntSetting translate_mouse_position_override_height;

    // Spoof WM_SIZE/WM_DISPLAYCHANGE lParam with game render resolution (g_game_render_width/height)
    BoolSetting spoof_game_resolution_in_size_messages;
    // Override X/Y when non-zero; when either is 0 use g_game_render_width/height
    IntSetting spoof_game_resolution_override_width;
    IntSetting spoof_game_resolution_override_height;

    // When true, OnCreateSwapchainCapture2 applies all modifications (prevent fullscreen, FLIPEX, format override,
    // resolution upgrade, etc.). When false, only capture of game resolution is done.
    BoolSetting apply_changes_on_create_swapchain;

    // Input testing settings - Mouse
    BoolSetting test_block_mouse_messages;      // WM_MOUSEMOVE, WM_LBUTTONDOWN, etc.
    BoolSetting test_block_mouse_getcursorpos;  // GetCursorPos
    BoolSetting test_block_mouse_setcursorpos;  // SetCursorPos
    BoolSetting test_block_mouse_getkeystate;   // GetKeyState/GetAsyncKeyState for mouse buttons
    BoolSetting test_block_mouse_rawinput;      // GetRawInputData/GetRawInputBuffer for mouse
    BoolSetting test_block_mouse_mouseevent;    // mouse_event
    BoolSetting test_block_mouse_clipcursor;    // ClipCursor
    BoolSetting test_block_mouse_capture;       // SetCapture/ReleaseCapture

    // Input testing settings - Keyboard
    BoolSetting test_block_keyboard_messages;          // WM_KEYDOWN, WM_CHAR, etc.
    BoolSetting test_block_keyboard_getkeystate;       // GetKeyState
    BoolSetting test_block_keyboard_getasynckeystate;  // GetAsyncKeyState
    BoolSetting test_block_keyboard_getkeyboardstate;  // GetKeyboardState
    BoolSetting test_block_keyboard_rawinput;          // GetRawInputData/GetRawInputBuffer for keyboard
    BoolSetting test_block_keyboard_keybdevent;        // keybd_event
    BoolSetting test_block_keyboard_sendinput;         // SendInput

   private:
    std::vector<SettingBase*> all_settings_;
};

}  // namespace settings
