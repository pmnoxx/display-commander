#include "experimental_tab_settings.hpp"
#include "../globals.hpp"
#include "../hooks/timeslowdown_hooks.hpp"
#include "../hooks/loadlibrary_hooks.hpp"
#include <cstdlib>
#include <climits>

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
    , timeslowdown_compatibility_mode("TimeslowdownCompatibilityMode", false, "DisplayCommander.Experimental")
    , timeslowdown_multiplier("TimeslowdownMultiplier", 1.0f, 0.1f, 10.0f, "DisplayCommander.Experimental")
    , timeslowdown_max_multiplier("TimeslowdownMaxMultiplier", 10.0f, 1.0f, 1000.0f, "DisplayCommander.Experimental")
    , query_performance_counter_hook("QueryPerformanceCounterHook", 0, {
        "None",
        "Enabled",
        "Enable Render Thread",
        "Enable Non-Render Thread"
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
    , qpc_enabled_modules("QPCEnabledModules", "", "DisplayCommander.Experimental")
    , dlss_indicator_enabled("DlssIndicatorEnabled", false, "DisplayCommander.Experimental")
    , d3d9_flipex_enabled("D3D9FlipExEnabled", false, "DisplayCommander.Experimental")
    , reuse_swap_chain_experimental_enabled("ReuseSwapChainExperimentalEnabled", true, "DisplayCommander.Experimental")
    , enable_flip_chain_enabled("EnableFlipChainEnabled", false, "DisplayCommander.Experimental")
    , suppress_dinput_hooks("SuppressDInputHooks", false, "DisplayCommander.Experimental")
    , hid_suppression_enabled("HIDSuppressionEnabled", false, "DisplayCommander.Experimental")
    , hid_suppression_dualsense_only("HIDSuppressionDualSenseOnly", true, "DisplayCommander.Experimental")
    , hid_suppression_block_readfile("HIDSuppressionBlockReadFile", true, "DisplayCommander.Experimental")
    , hid_suppression_block_getinputreport("HIDSuppressionBlockGetInputReport", true, "DisplayCommander.Experimental")
    , hid_suppression_block_getattributes("HIDSuppressionBlockGetAttributes", true, "DisplayCommander.Experimental")
    , hid_suppression_block_createfile("HIDSuppressionBlockCreateFile", true, "DisplayCommander.Experimental")
    , debug_output_log_to_reshade("DebugOutputLogToReShade", true, "DisplayCommander.Experimental")
    , debug_output_show_stats("DebugOutputShowStats", true, "DisplayCommander.Experimental")
    , dinput_device_state_blocking("DInputDeviceStateBlocking", true, "DisplayCommander.Experimental")
    , up_down_key_press_enabled("UpDownKeyPressEnabled", false, "DisplayCommander.Experimental")
    , button_only_press_enabled("ButtonOnlyPressEnabled", false, "DisplayCommander.Experimental")
    , force_anisotropic_filtering("ForceAnisotropicFiltering", false, "DisplayCommander.Experimental")
    , upgrade_min_mag_mip_linear("UpgradeMinMagMipLinear", false, "DisplayCommander.Experimental")
    , upgrade_compare_min_mag_mip_linear("UpgradeCompareMinMagMipLinear", false, "DisplayCommander.Experimental")
    , upgrade_min_mag_linear_mip_point("UpgradeMinMagLinearMipPoint", false, "DisplayCommander.Experimental")
    , upgrade_compare_min_mag_linear_mip_point("UpgradeCompareMinMagLinearMipPoint", false, "DisplayCommander.Experimental")
    , dll_blocking_enabled("DLLBlockingEnabled", false, "DisplayCommander.Experimental")
    , blocked_dlls("BlockedDLLs", "", "DisplayCommander.Experimental")
    , rand_hook_enabled("RandHookEnabled", false, "DisplayCommander.Experimental")
    , rand_hook_value("RandHookValue", 0, INT_MIN, INT_MAX, "DisplayCommander.Experimental")
    , rand_s_hook_enabled("Rand_sHookEnabled", false, "DisplayCommander.Experimental")
    , rand_s_hook_value("Rand_sHookValue", 0, 0, UINT_MAX, "DisplayCommander.Experimental")
    , performance_measurement_enabled("PerformanceMeasurementEnabled", false, "DisplayCommander.Experimental")
    , perf_measure_overlay_enabled("PerfMeasureOverlayEnabled", true, "DisplayCommander.Experimental")
    , perf_measure_handle_present_before_enabled("PerfMeasureHandlePresentBeforeEnabled", true, "DisplayCommander.Experimental")
    , perf_measure_handle_present_before_device_query_enabled("PerfMeasureHandlePresentBeforeDeviceQueryEnabled", true, "DisplayCommander.Experimental")
    , perf_measure_handle_present_before_record_frame_time_enabled("PerfMeasureHandlePresentBeforeRecordFrameTimeEnabled", true, "DisplayCommander.Experimental")
    , perf_measure_handle_present_before_frame_statistics_enabled("PerfMeasureHandlePresentBeforeFrameStatisticsEnabled", true, "DisplayCommander.Experimental")
    , perf_measure_track_present_statistics_enabled("PerfMeasureTrackPresentStatisticsEnabled", true, "DisplayCommander.Experimental")
    , perf_measure_on_present_flags2_enabled("PerfMeasureOnPresentFlags2Enabled", true, "DisplayCommander.Experimental")
    , perf_measure_handle_present_after_enabled("PerfMeasureHandlePresentAfterEnabled", true, "DisplayCommander.Experimental")
    , perf_measure_flush_command_queue_from_swapchain_enabled("PerfMeasureFlushCommandQueueFromSwapchainEnabled", true, "DisplayCommander.Experimental")
    , perf_measure_enqueue_gpu_completion_enabled("PerfMeasureEnqueueGPUCompletionEnabled", true, "DisplayCommander.Experimental")
    , perf_measure_get_independent_flip_state_enabled("PerfMeasureGetIndependentFlipStateEnabled", true, "DisplayCommander.Experimental")
    , performance_suppression_enabled("PerformanceSuppressionEnabled", false, "DisplayCommander.Experimental")
    , perf_suppress_overlay("PerfSuppressOverlay", false, "DisplayCommander.Experimental")
    , perf_suppress_handle_present_before("PerfSuppressHandlePresentBefore", false, "DisplayCommander.Experimental")
    , perf_suppress_handle_present_before_device_query("PerfSuppressHandlePresentBeforeDeviceQuery", false, "DisplayCommander.Experimental")
    , perf_suppress_handle_present_before_record_frame_time("PerfSuppressHandlePresentBeforeRecordFrameTime", false, "DisplayCommander.Experimental")
    , perf_suppress_handle_present_before_frame_statistics("PerfSuppressHandlePresentBeforeFrameStatistics", false, "DisplayCommander.Experimental")
    , perf_suppress_track_present_statistics("PerfSuppressTrackPresentStatistics", false, "DisplayCommander.Experimental")
    , perf_suppress_on_present_flags2("PerfSuppressOnPresentFlags2", false, "DisplayCommander.Experimental")
    , perf_suppress_handle_present_after("PerfSuppressHandlePresentAfter", false, "DisplayCommander.Experimental")
    , perf_suppress_flush_command_queue_from_swapchain("PerfSuppressFlushCommandQueueFromSwapchain", false, "DisplayCommander.Experimental")
    , perf_suppress_enqueue_gpu_completion("PerfSuppressEnqueueGPUCompletion", false, "DisplayCommander.Experimental")
    , perf_suppress_get_independent_flip_state("PerfSuppressGetIndependentFlipState", false, "DisplayCommander.Experimental")
    , pclstats_etw_enabled("PclStatsEtwEnabled", false, "DisplayCommander.Experimental")
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
        &timeslowdown_enabled, &timeslowdown_compatibility_mode, &timeslowdown_multiplier, &timeslowdown_max_multiplier,
        &query_performance_counter_hook, &get_tick_count_hook, &get_tick_count64_hook,
        &time_get_time_hook, &get_system_time_hook,
        &get_system_time_as_file_time_hook, &get_system_time_precise_as_file_time_hook,
        &get_local_time_hook, &nt_query_system_time_hook,
        &qpc_enabled_modules,
        &dlss_indicator_enabled,
        &d3d9_flipex_enabled,
        &reuse_swap_chain_experimental_enabled,
        &enable_flip_chain_enabled,
        &suppress_dinput_hooks,
        &hid_suppression_enabled, &hid_suppression_dualsense_only, &hid_suppression_block_readfile,
        &hid_suppression_block_getinputreport, &hid_suppression_block_getattributes,
        &hid_suppression_block_createfile,
        &debug_output_log_to_reshade, &debug_output_show_stats,
        &dinput_device_state_blocking,
        &up_down_key_press_enabled,
        &button_only_press_enabled,
        &force_anisotropic_filtering,
        &upgrade_min_mag_mip_linear,
        &upgrade_compare_min_mag_mip_linear,
        &upgrade_min_mag_linear_mip_point,
        &upgrade_compare_min_mag_linear_mip_point,
        &dll_blocking_enabled,
        &blocked_dlls,
        &rand_hook_enabled,
        &rand_hook_value,
        &rand_s_hook_enabled,
        &rand_s_hook_value,
        &performance_measurement_enabled,
        &perf_measure_overlay_enabled,
        &perf_measure_handle_present_before_enabled,
        &perf_measure_handle_present_before_device_query_enabled,
        &perf_measure_handle_present_before_record_frame_time_enabled,
        &perf_measure_handle_present_before_frame_statistics_enabled,
        &perf_measure_track_present_statistics_enabled,
        &perf_measure_on_present_flags2_enabled,
        &perf_measure_handle_present_after_enabled,
        &perf_measure_flush_command_queue_from_swapchain_enabled,
        &perf_measure_enqueue_gpu_completion_enabled,
        &perf_measure_get_independent_flip_state_enabled,
        &performance_suppression_enabled,
        &perf_suppress_overlay,
        &perf_suppress_handle_present_before,
        &perf_suppress_handle_present_before_device_query,
        &perf_suppress_handle_present_before_record_frame_time,
        &perf_suppress_handle_present_before_frame_statistics,
        &perf_suppress_track_present_statistics,
        &perf_suppress_on_present_flags2,
        &perf_suppress_handle_present_after,
        &perf_suppress_flush_command_queue_from_swapchain,
        &perf_suppress_enqueue_gpu_completion,
        &perf_suppress_get_independent_flip_state,
        &pclstats_etw_enabled,
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
    LoadTabSettingsWithSmartLogging(settings_to_load, "Experimental Tab");

    // Load QPC enabled modules after settings are loaded
    qpc_enabled_modules.Load();
    if (!qpc_enabled_modules.GetValue().empty()) {
        display_commanderhooks::LoadQPCEnabledModulesFromSettings(qpc_enabled_modules.GetValue());
    }

    // Load blocked DLLs after settings are loaded (if DLL blocking is enabled)
    if (dll_blocking_enabled.GetValue()) {
        blocked_dlls.Load();
        if (!blocked_dlls.GetValue().empty()) {
            display_commanderhooks::LoadBlockedDLLsFromSettings(blocked_dlls.GetValue());
        }
    }
}

std::vector<SettingBase*> ExperimentalTabSettings::GetAllSettings() {
    return all_settings_;
}

} // namespace settings
