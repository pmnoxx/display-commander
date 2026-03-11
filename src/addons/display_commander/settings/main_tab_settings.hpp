#pragma once

#include "../performance_types.hpp"
#include "../ui/new_ui/settings_wrapper.hpp"
#include "globals.hpp"

#include <atomic>
#include <vector>

// System volume (not a main-tab setting; used for volume sync)
extern std::atomic<float> s_system_volume_percent;

namespace settings {

// Settings manager for the main tab
class MainTabSettings {
   public:
    MainTabSettings();
    ~MainTabSettings() = default;

    // Load all settings from DisplayCommander config
    void LoadSettings();

    // Display Settings
    ui::new_ui::ComboSettingEnum<WindowMode> window_mode;
    ui::new_ui::ComboSetting aspect_index;
    ui::new_ui::ComboSetting window_aspect_width;
    ui::new_ui::ComboSetting alignment;

    // ADHD Multi-Monitor Mode Settings
    ui::new_ui::BoolSetting adhd_single_monitor_enabled_for_game_display;
    ui::new_ui::BoolSetting adhd_multi_monitor_enabled;

    // FPS Settings
    /** When true, FPS limiter is active (mode from fps_limiter_mode). When false, no limiting. Default on. */
    ui::new_ui::BoolSetting fps_limiter_enabled;
    ui::new_ui::ComboSetting fps_limiter_mode;
    ui::new_ui::IntSetting scanline_offset;
    ui::new_ui::IntSetting vblank_sync_divisor;
    ui::new_ui::FloatSetting fps_limit;
    ui::new_ui::FloatSetting fps_limit_background;
    /** When true, cap FPS to fps_limit_background when window is in background. When false, use same limit as
     * foreground. Default off. */
    ui::new_ui::BoolSetting background_fps_enabled;
    ui::new_ui::BoolSetting suppress_reflex_sleep;
    /** When true and native Reflex is not active, addon injects Reflex (sleep + markers). Default false. */
    ui::new_ui::BoolSetting inject_reflex;
    ui::new_ui::ComboSetting onpresent_sync_low_latency_ratio;
    ui::new_ui::ComboSettingEnum<OnPresentReflexMode> onpresent_reflex_mode;
    ui::new_ui::ComboSettingEnum<OnPresentReflexMode> reflex_limiter_reflex_mode;  // Used when FPS limiter is Reflex
    ui::new_ui::ComboSettingEnum<OnPresentReflexMode>
        reflex_disabled_limiter_mode;  // Used when FPS limiter is off (checkbox unchecked) or mode is LatentSync
    ui::new_ui::BoolSetting pcl_stats_enabled;
    ui::new_ui::BoolSetting use_reflex_markers_as_fps_limiter;
    /** Max queued frames when using Reflex markers as FPS limiter. 0 = game default; 1–6 = limit. */
    ui::new_ui::ComboSetting reflex_fps_limiter_max_queued_frames;
    /** FPS limiter preset when game has native Reflex. See FpsLimiterPreset in globals.hpp. */
    ui::new_ui::ComboSettingEnum<FpsLimiterPreset> native_reflex_fps_preset;
    /** When IsNativeFramePacingInSync: use Streamline proxy swap chain Present/Present1 for FPS limiter. */
    ui::new_ui::BoolSetting use_streamline_proxy_fps_limiter;
    ui::new_ui::BoolSetting native_pacing_sim_start_only;
    ui::new_ui::BoolSetting delay_present_start_after_sim_enabled;
    ui::new_ui::FloatSetting delay_present_start_frames;
    ui::new_ui::BoolSetting safe_mode_fps_limiter;
    /** Selected ReShade runtime index (0 = first). When multiple runtimes exist, non-zero selects that runtime. */
    ui::new_ui::IntSetting selected_reshade_runtime_index;

    // VSync & Tearing
    /** DXGI only: 0=No override, 1=Force ON, 2=FORCED 1/2, 3=FORCED 1/3, 4=FORCED 1/4 (NO VRR), 5=FORCED OFF. Applied
     * at Present. */
    ui::new_ui::ComboSetting vsync_override;
    /** DXGI only: 0=No override, 1–16=SetMaximumFrameLatency value. Applied per
     * swapchain in OnPresentUpdateBefore. */
    ui::new_ui::ComboSetting max_frame_latency_override;
    ui::new_ui::BoolSetting force_vsync_on;
    ui::new_ui::BoolSetting force_vsync_off;
    ui::new_ui::BoolSetting prevent_tearing;
    ui::new_ui::BoolSetting limit_real_frames;
    /** 0 = No override (game default), 1–4 = force backbuffer count at swapchain creation. Requires restart. */
    ui::new_ui::ComboSetting backbuffer_count_override;
    /** DXGI only: when game uses FLIP_SEQUENTIAL, upgrade to FLIP_DISCARD in OnCreateSwapchainCapture2. */
    ui::new_ui::BoolSetting force_flip_discard_upgrade;

    // Audio Settings
    ui::new_ui::FloatSetting audio_volume_percent;
    ui::new_ui::BoolSetting audio_mute;
    ui::new_ui::BoolSetting mute_in_background;
    ui::new_ui::BoolSetting mute_in_background_if_other_audio;
    ui::new_ui::BoolSetting audio_volume_auto_apply;

    // Input Remapping Settings
    ui::new_ui::BoolSetting enable_default_chords;
    ui::new_ui::BoolSetting guide_button_solo_ui_toggle_only;

    // Input Blocking Settings
    ui::new_ui::ComboSettingEnum<InputBlockingMode> keyboard_input_blocking;
    ui::new_ui::ComboSettingEnum<InputBlockingMode> mouse_input_blocking;
    ui::new_ui::ComboSettingEnum<InputBlockingMode> gamepad_input_blocking;
    ui::new_ui::BoolSetting clip_cursor_enabled;

    // Render Blocking (Background) Settings
    ui::new_ui::BoolSetting no_render_in_background;
    ui::new_ui::BoolSetting no_present_in_background;

    // CPU Settings
    ui::new_ui::IntSetting cpu_cores;

    // Test Overlay Settings
    ui::new_ui::BoolSetting show_test_overlay;
    ui::new_ui::BoolSetting show_fps_counter;
    ui::new_ui::BoolSetting show_native_fps;
    ui::new_ui::BoolSetting show_refresh_rate;
    ui::new_ui::BoolSetting show_vrr_status;
    ui::new_ui::BoolSetting show_actual_refresh_rate;
    ui::new_ui::BoolSetting vrr_debug_mode;
    ui::new_ui::BoolSetting show_flip_status;
    ui::new_ui::BoolSetting show_display_commander_ui;
    /** When true, continuous monitoring will open the standalone (independent) settings window. ReShade only. */
    ui::new_ui::BoolSetting show_independent_window;
    ui::new_ui::FloatSetting display_commander_ui_window_x;
    ui::new_ui::FloatSetting display_commander_ui_window_y;
    ui::new_ui::BoolSetting show_labels;
    ui::new_ui::BoolSetting show_clock;
    ui::new_ui::BoolSetting show_frame_time_graph;
    ui::new_ui::BoolSetting show_frame_time_stats;
    ui::new_ui::BoolSetting show_native_frame_time_graph;
    ui::new_ui::BoolSetting show_frame_timeline_bar;
    ui::new_ui::BoolSetting show_refresh_rate_frame_times;
    ui::new_ui::IntSetting refresh_rate_monitor_poll_ms;  // Only used when show_refresh_rate_frame_times is true
    ui::new_ui::BoolSetting show_refresh_rate_frame_time_stats;
    /** Show DXGI-based VRR status in performance overlay (RefreshRateMonitor heuristic). Requires DXGI refresh rate / VRR detection enabled in Advanced tab. */
    ui::new_ui::BoolSetting show_dxgi_vrr_status;
    /** Show DXGI refresh rate (Hz) in performance overlay (from RefreshRateMonitor / GetFrameStatistics). Requires DXGI refresh rate / VRR detection enabled in Advanced tab. */
    ui::new_ui::BoolSetting show_dxgi_refresh_rate;
    ui::new_ui::BoolSetting show_cpu_usage;
    ui::new_ui::BoolSetting show_cpu_fps;
    ui::new_ui::BoolSetting show_fg_mode;
    ui::new_ui::BoolSetting show_dlss_internal_resolution;
    ui::new_ui::BoolSetting show_dlss_status;
    ui::new_ui::BoolSetting show_dlss_quality_preset;  // Quality preset: Performance, Balanced, Quality, etc.
    ui::new_ui::BoolSetting show_dlss_render_preset;   // Render preset: A, B, C, D, E, etc. (letter presets)
    ui::new_ui::BoolSetting show_stopwatch;
    ui::new_ui::BoolSetting show_playtime;
    ui::new_ui::BoolSetting show_overlay_vu_bars;
    ui::new_ui::BoolSetting show_overlay_vram;
    ui::new_ui::BoolSetting show_overlay_texture_stats;
    ui::new_ui::FloatSetting overlay_background_alpha;
    ui::new_ui::FloatSetting overlay_chart_alpha;
    ui::new_ui::FloatSetting overlay_graph_scale;
    ui::new_ui::FloatSetting overlay_graph_max_scale;
    ui::new_ui::FloatSetting overlay_vertical_spacing;
    ui::new_ui::FloatSetting overlay_horizontal_spacing;

    // GPU Measurement Settings
    ui::new_ui::IntSetting gpu_measurement_enabled;

    // Frame Time Graph Settings
    ui::new_ui::ComboSettingEnum<FrameTimeMode> frame_time_mode;

    // Display Information
    /** Extended device ID of the target display (for window move, Win+Left/Right, etc.). */
    ui::new_ui::StringSetting target_extended_display_device_id;
    /** Extended device ID of the display containing the game window (from GetExtendedDisplayDeviceIdFromWindow). */
    ui::new_ui::StringSetting game_window_extended_display_device_id;
    ui::new_ui::StringSetting selected_extended_display_device_id;

    // Prevent display sleep & screensaver
    ui::new_ui::ComboSettingEnum<ScreensaverMode> screensaver_mode;
    /** Windows taskbar: 0 = no change, 1 = hide when in foreground, 2 = always hide. */
    ui::new_ui::ComboSettingEnum<TaskbarHideMode> taskbar_hide_mode;

    // Advanced Settings
    ui::new_ui::BoolSetting advanced_settings_enabled;

    // Logging Level
    ui::new_ui::ComboSettingEnum<LogLevel> log_level;

    // Individual Tab Visibility Settings
    ui::new_ui::BoolSetting show_advanced_tab;
    ui::new_ui::BoolSetting show_window_info_tab;
    ui::new_ui::BoolSetting show_swapchain_tab;
    ui::new_ui::BoolSetting show_important_info_tab;
    ui::new_ui::BoolSetting show_controller_tab;
    ui::new_ui::BoolSetting show_hook_stats_tab;
    ui::new_ui::BoolSetting show_streamline_tab;
    ui::new_ui::BoolSetting show_experimental_tab;
    ui::new_ui::BoolSetting show_reshade_tab;
    ui::new_ui::BoolSetting show_performance_tab;
    ui::new_ui::BoolSetting show_vulkan_tab;
    /** When enabled, install NvLowLatencyVk hooks when NvLowLatencyVk.dll is loaded (Vulkan Reflex frame pacing). */
    ui::new_ui::BoolSetting vulkan_nvll_hooks_enabled;
    /** When enabled, hook vulkan-1.dll vkGetDeviceProcAddr and wrap vkSetLatencyMarkerNV (VK_NV_low_latency2) for frame
     * pacing. */
    ui::new_ui::BoolSetting vulkan_vk_loader_hooks_enabled;
    /** When enabled, append VK_NV_low_latency2, VK_KHR_present_id, VK_KHR_timeline_semaphore in vkCreateDevice (Special
     * K style). */
    ui::new_ui::BoolSetting vulkan_append_reflex_extensions;

    // Brightness (ReShade effect driven by DC)
    /** When true, Brightness/AutoHDR controls are active and DC adds EffectSearchPaths/TextureSearchPaths to ReShade.
     * When false, the whole Brightness and AutoHDR section is disabled and paths are not added. Default on. */
    ui::new_ui::BoolSetting brightness_autohdr_section_enabled;
    ui::new_ui::FloatSetting brightness_percent;
    /** Decode only: how to interpret backbuffer (DECODE_METHOD). Default scRGB (1). */
    ui::new_ui::ComboSetting swapchain_colorspace;  // 0=Auto, 1=scRGB, 2=HDR10, 3=sRGB, 4=Gamma 2.2, 5=None
    /** Encode only: output color space (ENCODE_METHOD). */
    ui::new_ui::ComboSetting brightness_colorspace;  // 0=Auto, 1=scRGB, 2=HDR10, 3=sRGB, 4=Gamma 2.2, 5=None
    ui::new_ui::FloatSetting gamma_value;            // 0.5–2.0, 1.0 = neutral (DisplayCommander_Control.fx Gamma)
    ui::new_ui::FloatSetting contrast_value;         // 0.0–2.0, 1.0 = neutral (DisplayCommander_Control.fx Contrast)
    ui::new_ui::FloatSetting saturation_value;       // 0.0–2.0, 1.0 = neutral (DisplayCommander_Control.fx Saturation)
    ui::new_ui::FloatSetting hue_degrees;            // -15 to +15, 0 = neutral (DisplayCommander_Control.fx HueDegrees)
    /** When enabled, upgrades swap chain to HDR (scRGB 16-bit float) on create_swapchain/init_swapchain (DXGI only). */
    ui::new_ui::BoolSetting swapchain_hdr_upgrade;
    /** 0 = scRGB (default), 1 = HDR10. Only used when swapchain_hdr_upgrade is true. */
    ui::new_ui::ComboSetting swapchain_hdr_upgrade_mode;
    ui::new_ui::BoolSetting
        auto_hdr;  // When enabled, runs DisplayCommander_PerceptualBoost.fx (requires Generic RenoDX for SDR->HDR)
    ui::new_ui::FloatSetting auto_hdr_strength;  // Profile 3 EffectStrength_P3 (0.0–2.0), only used when AutoHDR on

    // HDR Control (Resolution Control / auto enable-disable Windows HDR)
    ui::new_ui::BoolSetting auto_enable_disable_hdr;
    // Override HDR static metadata (ignore source MaxCLL/MaxFALL): inject MaxMDL 1000 on swapchain init (DXGI
    // SetHDRMetaData). Sony/display fix.
    ui::new_ui::BoolSetting auto_apply_maxmdl_1000_hdr_metadata;

    // Ansel Control
    ui::new_ui::BoolSetting skip_ansel_loading;

    // Sampler State Override Settings
    ui::new_ui::BoolSetting force_anisotropic_filtering;
    ui::new_ui::BoolSetting upgrade_min_mag_mip_linear;
    ui::new_ui::BoolSetting upgrade_compare_min_mag_mip_linear;
    ui::new_ui::BoolSetting upgrade_min_mag_linear_mip_point;
    ui::new_ui::BoolSetting upgrade_compare_min_mag_linear_mip_point;
    ui::new_ui::IntSetting max_anisotropy;
    ui::new_ui::FloatSetting force_mipmap_lod_bias;

   private:
    std::vector<ui::new_ui::SettingBase*> all_settings_;
};

// Global instance
extern MainTabSettings g_mainTabSettings;

// Utility functions
/** Applies FPS limiter preset values. No-op for FpsLimiterPreset::kCustom. */
void ApplyNativeReflexPreset(FpsLimiterPreset preset);
/** Returns the extended display device ID for the monitor containing the window. */
std::string GetExtendedDisplayDeviceIdFromWindow(HWND hwnd);
void SaveGameWindowDisplayDeviceId(HWND hwnd);
void UpdateTargetDisplayFromGameWindow();
void UpdateFpsLimitMaximums();
void UpdateOverlaySpacingMaximums();
void UpdateCpuCoresMaximum();

}  // namespace settings
