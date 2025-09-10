#pragma once

#include "settings_wrapper.hpp"
#include <vector>

namespace ui::new_ui {

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
    BoolSettingRef mouse_spoofing_enabled;

    // Click sequences (up to 5)
    BoolSetting sequence_1_enabled;
    IntSetting sequence_1_x;
    IntSetting sequence_1_y;
    IntSetting sequence_1_interval;

    BoolSetting sequence_2_enabled;
    IntSetting sequence_2_x;
    IntSetting sequence_2_y;
    IntSetting sequence_2_interval;

    BoolSetting sequence_3_enabled;
    IntSetting sequence_3_x;
    IntSetting sequence_3_y;
    IntSetting sequence_3_interval;

    BoolSetting sequence_4_enabled;
    IntSetting sequence_4_x;
    IntSetting sequence_4_y;
    IntSetting sequence_4_interval;

    BoolSetting sequence_5_enabled;
    IntSetting sequence_5_x;
    IntSetting sequence_5_y;
    IntSetting sequence_5_interval;

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
    BoolSetting sleep_hook_render_thread_only;
    FloatSetting sleep_multiplier;
    IntSetting min_sleep_duration_ms;
    IntSetting max_sleep_duration_ms;

    // Time slowdown settings
    BoolSetting timeslowdown_enabled;
    FloatSetting timeslowdown_multiplier;

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

private:
    std::vector<SettingBase*> all_settings_;
};

// Global instance
extern ExperimentalTabSettings g_experimentalTabSettings;

} // namespace ui::new_ui
