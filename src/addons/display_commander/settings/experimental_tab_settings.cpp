#include "experimental_tab_settings.hpp"
#include <climits>
#include <cstdlib>
#include "../globals.hpp"
#include "../hooks/loadlibrary_hooks.hpp"
#include "../hooks/system/timeslowdown_hooks.hpp"

namespace settings {

ExperimentalTabSettings::ExperimentalTabSettings()
    : timeslowdown_enabled("TimeslowdownEnabled", false, "DisplayCommander.Experimental"),
      timeslowdown_compatibility_mode("TimeslowdownCompatibilityMode", false, "DisplayCommander.Experimental"),
      timeslowdown_multiplier("TimeslowdownMultiplier", 1.0f, 0.1f, 10.0f, "DisplayCommander.Experimental"),
      timeslowdown_max_multiplier("TimeslowdownMaxMultiplier", 10.0f, 1.0f, 1000.0f, "DisplayCommander.Experimental"),
      query_performance_counter_hook("QueryPerformanceCounterHook", 0,
                                     {"None", "Enabled", "Enable Render Thread", "Enable Non-Render Thread"},
                                     "DisplayCommander.Experimental"),
      get_tick_count_hook("GetTickCountHook", 0, {"None", "Enabled"}, "DisplayCommander.Experimental"),
      get_tick_count64_hook("GetTickCount64Hook", 0, {"None", "Enabled"}, "DisplayCommander.Experimental"),
      time_get_time_hook("TimeGetTimeHook", 0, {"None", "Enabled"}, "DisplayCommander.Experimental"),
      get_system_time_hook("GetSystemTimeHook", 0, {"None", "Enabled"}, "DisplayCommander.Experimental"),
      get_system_time_as_file_time_hook("GetSystemTimeAsFileTimeHook", 0, {"None", "Enabled"},
                                        "DisplayCommander.Experimental"),
      get_system_time_precise_as_file_time_hook("GetSystemTimePreciseAsFileTimeHook", 0, {"None", "Enabled"},
                                                "DisplayCommander.Experimental"),
      get_local_time_hook("GetLocalTimeHook", 0, {"None", "Enabled"}, "DisplayCommander.Experimental"),
      nt_query_system_time_hook("NtQuerySystemTimeHook", 0, {"None", "Enabled"}, "DisplayCommander.Experimental"),
      qpc_enabled_modules("QPCEnabledModules", "", "DisplayCommander.Experimental"),
      d3d9_flipex_enabled("D3D9FlipExEnabled", false, "DisplayCommander.Experimental"),
      d3d9_flipex_enabled_no_reshade("D3D9FlipExEnabledNoReShade", false, "DisplayCommander.Experimental"),
      d3d9_fix_create_texture_dimensions("D3D9FixCreateTextureDimensions", true, "DisplayCommander.Experimental"),
      enable_flip_chain_enabled("EnableFlipChainEnabled", false, "DisplayCommander.Experimental"),
      suppress_dinput_hooks("SuppressDInputHooks", true, "DisplayCommander.Experimental"),
      debug_output_log_to_reshade("DebugOutputLogToReShade", true, "DisplayCommander.Experimental"),
      debug_output_show_stats("DebugOutputShowStats", true, "DisplayCommander.Experimental"),
      dinput_device_state_blocking("DInputDeviceStateBlocking", true, "DisplayCommander.Experimental"),
      force_anisotropic_filtering("ForceAnisotropicFiltering", false, "DisplayCommander.Experimental"),
      upgrade_min_mag_mip_linear("UpgradeMinMagMipLinear", false, "DisplayCommander.Experimental"),
      upgrade_compare_min_mag_mip_linear("UpgradeCompareMinMagMipLinear", false, "DisplayCommander.Experimental"),
      upgrade_min_mag_linear_mip_point("UpgradeMinMagLinearMipPoint", false, "DisplayCommander.Experimental"),
      upgrade_compare_min_mag_linear_mip_point("UpgradeCompareMinMagLinearMipPoint", false,
                                               "DisplayCommander.Experimental"),
      thread_tracking_enabled("ThreadTrackingEnabled", false, "DisplayCommander.Experimental"),
      performance_measurement_enabled("PerformanceMeasurementEnabled", false, "DisplayCommander.Experimental"),
      perf_measure_overlay_enabled("PerfMeasureOverlayEnabled", true, "DisplayCommander.Experimental"),
      perf_measure_overlay_show_volume_enabled("PerfMeasureOverlayShowVolumeEnabled", true,
                                               "DisplayCommander.Experimental"),
      perf_measure_overlay_show_vrr_status_enabled("PerfMeasureOverlayShowVrrStatusEnabled", true,
                                                   "DisplayCommander.Experimental"),
      perf_measure_handle_present_before_enabled("PerfMeasureHandlePresentBeforeEnabled", true,
                                                 "DisplayCommander.Experimental"),
      perf_measure_handle_present_before_device_query_enabled("PerfMeasureHandlePresentBeforeDeviceQueryEnabled", true,
                                                              "DisplayCommander.Experimental"),
      perf_measure_handle_present_before_record_frame_time_enabled(
          "PerfMeasureHandlePresentBeforeRecordFrameTimeEnabled", true, "DisplayCommander.Experimental"),
      perf_measure_handle_present_before_frame_statistics_enabled(
          "PerfMeasureHandlePresentBeforeFrameStatisticsEnabled", true, "DisplayCommander.Experimental"),
      perf_measure_track_present_statistics_enabled("PerfMeasureTrackPresentStatisticsEnabled", true,
                                                    "DisplayCommander.Experimental"),
      perf_measure_on_present_flags2_enabled("PerfMeasureOnPresentFlags2Enabled", true,
                                             "DisplayCommander.Experimental"),
      perf_measure_handle_present_after_enabled("PerfMeasureHandlePresentAfterEnabled", true,
                                                "DisplayCommander.Experimental"),
      perf_measure_enqueue_gpu_completion_enabled("PerfMeasureEnqueueGPUCompletionEnabled", true,
                                                  "DisplayCommander.Experimental"),
      perf_measure_on_present_update_before_enabled("PerfMeasureOnPresentUpdateBeforeEnabled", true,
                                                    "DisplayCommander.Experimental"),
      performance_suppression_enabled("PerformanceSuppressionEnabled", false, "DisplayCommander.Experimental"),
      perf_suppress_overlay("PerfSuppressOverlay", false, "DisplayCommander.Experimental"),
      perf_suppress_overlay_show_volume("PerfSuppressOverlayShowVolume", false, "DisplayCommander.Experimental"),
      perf_suppress_overlay_show_vrr_status("PerfSuppressOverlayShowVrrStatus", false, "DisplayCommander.Experimental"),
      perf_suppress_handle_present_before("PerfSuppressHandlePresentBefore", false, "DisplayCommander.Experimental"),
      perf_suppress_handle_present_before_device_query("PerfSuppressHandlePresentBeforeDeviceQuery", false,
                                                       "DisplayCommander.Experimental"),
      perf_suppress_handle_present_before_record_frame_time("PerfSuppressHandlePresentBeforeRecordFrameTime", false,
                                                            "DisplayCommander.Experimental"),
      perf_suppress_handle_present_before_frame_statistics("PerfSuppressHandlePresentBeforeFrameStatistics", false,
                                                           "DisplayCommander.Experimental"),
      perf_suppress_track_present_statistics("PerfSuppressTrackPresentStatistics", false,
                                             "DisplayCommander.Experimental"),
      perf_suppress_on_present_flags2("PerfSuppressOnPresentFlags2", false, "DisplayCommander.Experimental"),
      perf_suppress_handle_present_after("PerfSuppressHandlePresentAfter", false, "DisplayCommander.Experimental"),
      perf_suppress_enqueue_gpu_completion("PerfSuppressEnqueueGPUCompletion", false, "DisplayCommander.Experimental"),
      perf_suppress_on_present_update_before("PerfSuppressOnPresentUpdateBefore", false,
                                             "DisplayCommander.Experimental"),
      show_volume("ShowVolume", false, "DisplayCommander.Experimental"),
      translate_mouse_position("TranslateMousePosition", false, "DisplayCommander.Experimental"),
      translate_mouse_position_override_width("TranslateMousePositionOverrideWidth", 0, 0, 7680,
                                              "DisplayCommander.Experimental"),
      translate_mouse_position_override_height("TranslateMousePositionOverrideHeight", 0, 0, 4320,
                                               "DisplayCommander.Experimental"),
      spoof_game_resolution_in_size_messages("SpoofGameResolutionInSizeMessages", false,
                                             "DisplayCommander.Experimental"),
      spoof_game_resolution_override_width("SpoofGameResolutionOverrideWidth", 0, 0, 7680,
                                           "DisplayCommander.Experimental"),
      spoof_game_resolution_override_height("SpoofGameResolutionOverrideHeight", 0, 0, 4320,
                                            "DisplayCommander.Experimental"),
      apply_changes_on_create_swapchain("ApplyChangesOnCreateSwapchain", false, "DisplayCommander.Experimental"),
      // Input testing settings - Mouse
      test_block_mouse_messages("TestBlockMouseMessages", false, "DisplayCommander.Experimental"),
      test_block_mouse_getcursorpos("TestBlockMouseGetCursorPos", false, "DisplayCommander.Experimental"),
      test_block_mouse_setcursorpos("TestBlockMouseSetCursorPos", false, "DisplayCommander.Experimental"),
      test_block_mouse_getkeystate("TestBlockMouseGetKeyState", false, "DisplayCommander.Experimental"),
      test_block_mouse_rawinput("TestBlockMouseRawInput", false, "DisplayCommander.Experimental"),
      test_block_mouse_mouseevent("TestBlockMouseMouseEvent", false, "DisplayCommander.Experimental"),
      test_block_mouse_clipcursor("TestBlockMouseClipCursor", false, "DisplayCommander.Experimental"),
      test_block_mouse_capture("TestBlockMouseCapture", false, "DisplayCommander.Experimental"),
      // Input testing settings - Keyboard
      test_block_keyboard_messages("TestBlockKeyboardMessages", false, "DisplayCommander.Experimental"),
      test_block_keyboard_getkeystate("TestBlockKeyboardGetKeyState", false, "DisplayCommander.Experimental"),
      test_block_keyboard_getasynckeystate("TestBlockKeyboardGetAsyncKeyState", false, "DisplayCommander.Experimental"),
      test_block_keyboard_getkeyboardstate("TestBlockKeyboardGetKeyboardState", false, "DisplayCommander.Experimental"),
      test_block_keyboard_rawinput("TestBlockKeyboardRawInput", false, "DisplayCommander.Experimental"),
      test_block_keyboard_keybdevent("TestBlockKeyboardKeybdEvent", false, "DisplayCommander.Experimental"),
      test_block_keyboard_sendinput("TestBlockKeyboardSendInput", false, "DisplayCommander.Experimental"),
      show_advanced_profile_settings("ShowAdvancedProfileSettings", false, "DisplayCommander.Experimental") {
    // Initialize the all_settings_ vector
    all_settings_ = {
        &timeslowdown_enabled,
        &timeslowdown_compatibility_mode,
        &timeslowdown_multiplier,
        &timeslowdown_max_multiplier,
        &query_performance_counter_hook,
        &get_tick_count_hook,
        &get_tick_count64_hook,
        &time_get_time_hook,
        &get_system_time_hook,
        &get_system_time_as_file_time_hook,
        &get_system_time_precise_as_file_time_hook,
        &get_local_time_hook,
        &nt_query_system_time_hook,
        &qpc_enabled_modules,
        &d3d9_flipex_enabled,
        &d3d9_flipex_enabled_no_reshade,
        &d3d9_fix_create_texture_dimensions,
        &enable_flip_chain_enabled,
        &suppress_dinput_hooks,
        &debug_output_log_to_reshade,
        &debug_output_show_stats,
        &dinput_device_state_blocking,
        &force_anisotropic_filtering,
        &upgrade_min_mag_mip_linear,
        &upgrade_compare_min_mag_mip_linear,
        &upgrade_min_mag_linear_mip_point,
        &upgrade_compare_min_mag_linear_mip_point,
        &thread_tracking_enabled,
        &performance_measurement_enabled,
        &perf_measure_overlay_enabled,
        &perf_measure_overlay_show_volume_enabled,
        &perf_measure_overlay_show_vrr_status_enabled,
        &perf_measure_handle_present_before_enabled,
        &perf_measure_handle_present_before_device_query_enabled,
        &perf_measure_handle_present_before_record_frame_time_enabled,
        &perf_measure_handle_present_before_frame_statistics_enabled,
        &perf_measure_track_present_statistics_enabled,
        &perf_measure_on_present_flags2_enabled,
        &perf_measure_handle_present_after_enabled,
        &perf_measure_enqueue_gpu_completion_enabled,
        &perf_measure_on_present_update_before_enabled,
        &performance_suppression_enabled,
        &perf_suppress_overlay,
        &perf_suppress_overlay_show_volume,
        &perf_suppress_overlay_show_vrr_status,
        &perf_suppress_handle_present_before,
        &perf_suppress_handle_present_before_device_query,
        &perf_suppress_handle_present_before_record_frame_time,
        &perf_suppress_handle_present_before_frame_statistics,
        &perf_suppress_track_present_statistics,
        &perf_suppress_on_present_flags2,
        &perf_suppress_handle_present_after,
        &perf_suppress_enqueue_gpu_completion,
        &perf_suppress_on_present_update_before,
        &show_volume,
        &show_advanced_profile_settings,
        &translate_mouse_position,
        &translate_mouse_position_override_width,
        &translate_mouse_position_override_height,
        &spoof_game_resolution_in_size_messages,
        &spoof_game_resolution_override_width,
        &spoof_game_resolution_override_height,
        &apply_changes_on_create_swapchain,
        // Input testing settings - Mouse
        &test_block_mouse_messages,
        &test_block_mouse_getcursorpos,
        &test_block_mouse_setcursorpos,
        &test_block_mouse_getkeystate,
        &test_block_mouse_rawinput,
        &test_block_mouse_mouseevent,
        &test_block_mouse_clipcursor,
        &test_block_mouse_capture,
        // Input testing settings - Keyboard
        &test_block_keyboard_messages,
        &test_block_keyboard_getkeystate,
        &test_block_keyboard_getasynckeystate,
        &test_block_keyboard_getkeyboardstate,
        &test_block_keyboard_rawinput,
        &test_block_keyboard_keybdevent,
        &test_block_keyboard_sendinput,
    };
}

void ExperimentalTabSettings::LoadAll() {
    // Load max multiplier first to ensure proper range validation for the multiplier
    timeslowdown_max_multiplier.Load();

    // Set the max range for the multiplier before loading it
    timeslowdown_multiplier.SetMax(timeslowdown_max_multiplier.GetValue());

    // Load all other settings (excluding max multiplier since we already loaded it)
    std::vector<SettingBase*> settings_to_load;
    for (auto* setting : all_settings_) {
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
}

std::vector<SettingBase*> ExperimentalTabSettings::GetAllSettings() { return all_settings_; }

}  // namespace settings
