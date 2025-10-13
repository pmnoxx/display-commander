#include "experimental_tab_settings.hpp"
#include "../globals.hpp"

namespace settings {



ExperimentalTabSettings::ExperimentalTabSettings()
    : auto_click_enabled("AutoClickEnabled", false, "DisplayCommander.Experimental")
    , mouse_spoofing_enabled("MouseSpoofingEnabled", true, "DisplayCommander.Experimental")
    , sequence_enabled("SequenceEnabled", 5, 0, 0, 1, "DisplayCommander.Experimental")  // 5 elements, default 0 (disabled), range 0-1
    , sequence_x("SequenceX", 5, 0, -10000, 10000, "DisplayCommander.Experimental")     // 5 elements, default 0, range -10000 to 10000
    , sequence_y("SequenceY", 5, 0, -10000, 10000, "DisplayCommander.Experimental")     // 5 elements, default 0, range -10000 to 10000
    , sequence_interval("SequenceInterval", 5, 3000, 100, 60000, "DisplayCommander.Experimental") // 5 elements, default 3000ms, range 100-60000ms
    , backbuffer_format_override_enabled("BackbufferFormatOverrideEnabled", false, "DisplayCommander.Experimental")
    , backbuffer_format_override("BackbufferFormatOverride", 0, {
        "R8G8B8A8_UNORM (8-bit)",
        "R10G10B10A2_UNORM (10-bit)",
        "R16G16B16A16_FLOAT (16-bit HDR)"
    }, "DisplayCommander.Experimental")
    , buffer_resolution_upgrade_enabled("BufferResolutionUpgradeEnabled", false, "DisplayCommander.Experimental")
    , buffer_resolution_upgrade_width("BufferResolutionUpgradeWidth", 1280, 320, 7680, "DisplayCommander.Experimental")
    , buffer_resolution_upgrade_height("BufferResolutionUpgradeHeight", 720, 240, 4320, "DisplayCommander.Experimental")
    , buffer_resolution_upgrade_scale_factor("BufferResolutionUpgradeScaleFactor", 2, 1, 4, "DisplayCommander.Experimental")
    , buffer_resolution_upgrade_mode("BufferResolutionUpgradeMode", 0, {
        "Upgrade 1280x720 by Scale Factor",
        "Upgrade by Scale Factor",
        "Upgrade Custom Resolution"
    }, "DisplayCommander.Experimental")
    , texture_format_upgrade_enabled("TextureFormatUpgradeEnabled", false, "DisplayCommander.Experimental")
    , sleep_hook_enabled("SleepHookEnabled", false, "DisplayCommander.Experimental")
    , sleep_multiplier("SleepMultiplier", 1.0f, 0.1f, 10.0f, "DisplayCommander.Experimental")
    , min_sleep_duration_ms("MinSleepDurationMs", 0, 0, 10000, "DisplayCommander.Experimental")
    , max_sleep_duration_ms("MaxSleepDurationMs", 0, 0, 10000, "DisplayCommander.Experimental")
    , timeslowdown_enabled("TimeslowdownEnabled", false, "DisplayCommander.Experimental")
    , timeslowdown_multiplier("TimeslowdownMultiplier", 1.0f, 0.1f, 10.0f, "DisplayCommander.Experimental")
    , timeslowdown_max_multiplier("TimeslowdownMaxMultiplier", 10.0f, 1.0f, 1000.0f, "DisplayCommander.Experimental")
    , query_performance_counter_hook("QueryPerformanceCounterHook", 0, {
        "None",
        "Enabled"
    }, "DisplayCommander.Experimental")
    , get_tick_count_hook("GetTickCountHook", 0, {
        "None",
        "Enabled"
    }, "DisplayCommander.Experimental")
    , get_tick_count64_hook("GetTickCount64Hook", 0, {
        "None",
        "Enabled"
    }, "DisplayCommander.Experimental")
    , time_get_time_hook("TimeGetTimeHook", 0, {
        "None",
        "Enabled"
    }, "DisplayCommander.Experimental")
    , get_system_time_hook("GetSystemTimeHook", 0, {
        "None",
        "Enabled"
    }, "DisplayCommander.Experimental")
    , get_system_time_as_file_time_hook("GetSystemTimeAsFileTimeHook", 0, {
        "None",
        "Enabled"
    }, "DisplayCommander.Experimental")
    , get_system_time_precise_as_file_time_hook("GetSystemTimePreciseAsFileTimeHook", 0, {
        "None",
        "Enabled"
    }, "DisplayCommander.Experimental")
    , get_local_time_hook("GetLocalTimeHook", 0, {
        "None",
        "Enabled"
    }, "DisplayCommander.Experimental")
    , nt_query_system_time_hook("NtQuerySystemTimeHook", 0, {
        "None",
        "Enabled"
    }, "DisplayCommander.Experimental")
    , dlss_indicator_enabled("DlssIndicatorEnabled", false, "DisplayCommander.Experimental")
    , d3d9_flipex_enabled("D3D9FlipExEnabled", false, "DisplayCommander.Experimental")
    , disable_flip_chain_enabled("DisableFlipChainEnabled", false, "DisplayCommander.Experimental")
    , enable_flip_chain_enabled("EnableFlipChainEnabled", false, "DisplayCommander.Experimental")
    , suppress_dinput_hooks("SuppressDInputHooks", false, "DisplayCommander.Experimental")
{
    // Initialize the all_settings_ vector
    all_settings_ = {
        &auto_click_enabled,
        &mouse_spoofing_enabled,
        &sequence_enabled, &sequence_x, &sequence_y, &sequence_interval,
        &backbuffer_format_override_enabled, &backbuffer_format_override,
        &buffer_resolution_upgrade_enabled, &buffer_resolution_upgrade_width, &buffer_resolution_upgrade_height,
        &buffer_resolution_upgrade_scale_factor, &buffer_resolution_upgrade_mode,
        &texture_format_upgrade_enabled,
        &sleep_hook_enabled, &sleep_multiplier, &min_sleep_duration_ms, &max_sleep_duration_ms,
        &timeslowdown_enabled, &timeslowdown_multiplier, &timeslowdown_max_multiplier,
        &query_performance_counter_hook, &get_tick_count_hook, &get_tick_count64_hook,
        &time_get_time_hook, &get_system_time_hook,
        &get_system_time_as_file_time_hook, &get_system_time_precise_as_file_time_hook,
        &get_local_time_hook, &nt_query_system_time_hook,
        &dlss_indicator_enabled,
        &d3d9_flipex_enabled,
        &disable_flip_chain_enabled,
        &enable_flip_chain_enabled,
        &suppress_dinput_hooks,
    };
}

void ExperimentalTabSettings::LoadAll() {
    // Load max multiplier first to ensure proper range validation for the multiplier
    timeslowdown_max_multiplier.Load();

    // Set the max range for the multiplier before loading it
    timeslowdown_multiplier.SetMax(timeslowdown_max_multiplier.GetValue());

    // Load all other settings (excluding max multiplier since we already loaded it)
    std::vector<SettingBase *> settings_to_load;
    for (auto *setting : all_settings_) {
        if (setting != &timeslowdown_max_multiplier) {
            settings_to_load.push_back(setting);
        }
    }
    LoadTabSettings(settings_to_load);
}

std::vector<SettingBase*> ExperimentalTabSettings::GetAllSettings() {
    return all_settings_;
}

} // namespace settings
