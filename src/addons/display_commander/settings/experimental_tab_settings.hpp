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
using ui::new_ui::StringSetting;
using ui::new_ui::SettingBase;

// Settings manager for the experimental tab
class ExperimentalTabSettings {
  public:
    ExperimentalTabSettings();
    ~ExperimentalTabSettings() = default;

    // Load all settings from ReShade config
    void LoadAll();

    // Get all settings for loading
    std::vector<SettingBase *> GetAllSettings();

    // Master auto-click enable
    BoolSetting auto_click_enabled;

    // Mouse position spoofing for auto-click sequences
    BoolSetting mouse_spoofing_enabled;

    // Click sequences (up to 5) - using arrays for cleaner code
    FixedIntArraySetting sequence_enabled;  // 0 = disabled, 1 = enabled
    FixedIntArraySetting sequence_x;        // X coordinates
    FixedIntArraySetting sequence_y;        // Y coordinates
    FixedIntArraySetting sequence_interval; // Click intervals in ms

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

  private:
    std::vector<SettingBase *> all_settings_;
};

} // namespace settings