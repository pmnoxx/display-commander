#include "addon.hpp"
#include "reflex/reflex_management.hpp"
#include "background_window.hpp" // Added this line
#include "dxgi/custom_fps_limiter_manager.hpp"
#include "dxgi/dxgi_device_info.hpp"
#include <atomic>

// Global variables
// UI mode removed - now using new tab system

// Window settings
std::atomic<int> s_windowed_width{3440}; // 21:9 ultrawide width
std::atomic<int> s_windowed_height{1440}; // 21:9 ultrawide height
std::atomic<int> s_window_mode{0}; // 0 = Borderless Windowed (Aspect Ratio), 1 = Borderless Windowed (Width/Height), 2 = Borderless Fullscreen
std::atomic<bool> s_remove_top_bar{true}; // Suppress top bar/border messages enabled by default for borderless windows
std::atomic<bool> s_suppress_move_resize_messages{true}; // Suppress move/resize messages by default

std::atomic<bool> s_suppress_maximize{true}; // Suppress maximize messages by default
std::atomic<int> s_aspect_index{4}; // 0 = 16:9, 1 = 16:10, 2 = 4:3, 3 = 3:2, 4 = 1:1, 5 = 1:2, 6 = 2:3, 7 = 3:4, 8 = 9:16, 9 = 10:16

// Window alignment when repositioning is needed (0 = None, 1 = Top Left, 2 = Top Right, 3 = Bottom Left, 4 = Bottom Right, 5 = Center)
std::atomic<int> s_move_to_zero_if_out{2}; // default to top right


// Prevent Fullscreen
std::atomic<bool> s_prevent_fullscreen{false};

// NVAPI Fullscreen Prevention
std::atomic<bool> s_nvapi_fullscreen_prevention{false}; // disabled by default
// NVAPI HDR logging
std::atomic<bool> s_nvapi_hdr_logging{false};
std::atomic<float> s_nvapi_hdr_interval_sec{5.f};
std::atomic<bool> s_nvapi_force_hdr10{false}; // disabled by default

// Spoof Fullscreen State (for applications that query fullscreen status)
std::atomic<int> s_spoof_fullscreen_state{0}; // 0 = Disabled, 1 = Spoof as Fullscreen, 2 = Spoof as Windowed
std::atomic<bool> s_mute_in_background{false};
std::atomic<bool> s_mute_in_background_if_other_audio{false};
std::atomic<float> s_audio_volume_percent{100.f};
std::atomic<bool> s_audio_mute{false};

// Performance: background FPS cap
std::atomic<float> s_fps_limit_background{30.f};
// FPS limit for foreground
std::atomic<float> s_fps_limit{0.f};
// Extra wait applied by custom FPS limiter (ms)
std::atomic<float> s_fps_extra_wait_ms{0.f};
// Custom FPS Limiter settings
std::atomic<float> s_custom_fps_limit{0.f};
std::atomic<bool> s_custom_fps_limiter_enabled{true}; // Always enabled

// VSync and tearing controls
std::atomic<bool> s_force_vsync_on{false};
std::atomic<bool> s_force_vsync_off{false};
std::atomic<bool> s_allow_tearing{false};
std::atomic<bool> s_prevent_tearing{false};

// Monitor and display settings
std::atomic<int> s_target_monitor_index{0};
std::atomic<int> s_dxgi_composition_state{0};

std::atomic<int> s_spoof_window_focus{0}; // 0 = Disabled, 1 = Spoof as Focused, 2 = Spoof as Unfocused

// Input blocking in background (0.0f off, 1.0f on)
std::atomic<bool> s_block_input_in_background{false};

// Fix HDR10 color space when backbuffer is RGB10A2
std::atomic<bool> s_fix_hdr10_colorspace{false};

// ReShade runtime for input blocking
std::atomic<reshade::api::effect_runtime*> g_reshade_runtime = nullptr;

// Window minimize prevention
std::atomic<bool> s_prevent_windows_minimize{false};

// Prevent always on top behavior
std::atomic<bool> s_prevent_always_on_top{true}; // Prevent games from staying on top by default

// Background feature - show black window behind game when not fullscreen
std::atomic<bool> s_background_feature_enabled{false}; // Disabled by default

// Enforce desired window settings
std::atomic<bool> s_enforce_desired_window{true}; // Enable window enforcement

// Desktop Resolution Override
std::atomic<int> s_selected_monitor_index{0}; // Primary monitor by default

// Display Tab Enhanced Settings
std::atomic<int> s_selected_aspect_ratio{1}; // Default to 16:9
std::atomic<int> s_selected_resolution_index{0}; // Default to first available resolution
std::atomic<int> s_selected_refresh_rate_index{0}; // Default to first available refresh rate

std::atomic<bool> s_initial_auto_selection_done{false}; // Track if we've done initial auto-selection

// Auto-restore resolution on game close
std::atomic<bool> s_auto_restore_resolution_on_close{true}; // Enabled by default

// Auto-apply resolution and refresh rate changes
std::atomic<bool> s_auto_apply_resolution_change{false}; // Disabled by default
std::atomic<bool> s_auto_apply_refresh_rate_change{false}; // Disabled by default

// Reflex settings
std::atomic<bool> s_reflex_enabled{true}; // Enabled by default
std::atomic<bool> s_reflex_low_latency_mode{false}; // Low latency mode disabled by default
std::atomic<bool> s_reflex_low_latency_boost{false}; // Boost disabled by default
std::atomic<bool> s_reflex_use_markers{false}; // Use markers disabled by default
std::atomic<bool> s_reflex_debug_output{false}; // Debug output disabled by default

// Atomic variables
std::atomic<int> g_comp_query_counter{0};
std::atomic<int> g_comp_last_logged{0};
std::atomic<reshade::api::swapchain*> g_last_swapchain_ptr{nullptr};
std::atomic<uint64_t> g_init_apply_generation{0};
std::chrono::steady_clock::time_point g_attach_time{std::chrono::steady_clock::now()};
std::atomic<HWND> g_last_swapchain_hwnd{nullptr};
std::atomic<bool> g_shutdown{false};
std::atomic<bool> g_muted_applied{false};
std::atomic<bool> g_volume_applied{false};
std::atomic<float> g_default_fps_limit{0.f};

// Reflex-related atomic variables
std::atomic<uint64_t> g_reflex_frame_id{0};
std::atomic<bool> g_reflex_hooks_installed{false};

// Global Reflex manager instance
std::unique_ptr<ReflexManager> g_reflexManager;

// Global flag for Reflex settings changes
std::atomic<bool> g_reflex_settings_changed{false};

// Continuous monitoring system
std::atomic<bool> s_continuous_monitoring_enabled{true}; // Enabled by default
std::atomic<bool> g_monitoring_thread_running{false};
std::thread g_monitoring_thread;

// Continuous rendering system
std::atomic<bool> s_continuous_rendering_enabled{false}; // Off by default
std::atomic<bool> s_force_continuous_rendering{true}; // Force continuous rendering on every frame (enabled by default for focus spoofing)
// CONTINUOUS RENDERING THREAD VARIABLES REMOVED - Focus spoofing is now handled by Win32 hooks

// FOCUS LOSS DETECTION VARIABLES REMOVED - Focus spoofing is now handled by Win32 hooks

// Global window state instance
GlobalWindowState g_window_state;
SpinLock g_window_state_lock;

// Global background window manager instance
BackgroundWindowManager g_backgroundWindowManager;

// Global Custom FPS Limiter Manager instance
namespace dxgi::fps_limiter {
std::unique_ptr<CustomFpsLimiterManager> g_customFpsLimiterManager = std::make_unique<CustomFpsLimiterManager>();
}
// Global DXGI Device Info Manager instance
std::unique_ptr<DXGIDeviceInfoManager> g_dxgiDeviceInfoManager = std::make_unique<DXGIDeviceInfoManager>();

// Direct atomic variables for latency tracking (UI access)
std::atomic<float> g_current_latency_ms{16.67f};
std::atomic<float> g_pcl_av_latency_ms{16.67f};
std::atomic<float> g_average_latency_ms{16.67f};
std::atomic<float> g_min_latency_ms{16.67f};
std::atomic<float> g_max_latency_ms{16.67f};
std::atomic<uint64_t> g_current_frame{0};
std::atomic<bool> g_reflex_active{false};

// Backbuffer dimensions
std::atomic<int> g_last_backbuffer_width{0};
std::atomic<int> g_last_backbuffer_height{0};
// Background/foreground state (updated by monitoring thread)
std::atomic<bool> g_app_in_background{false};

// Performance stats (FPS/frametime) shared state
std::atomic<uint32_t> g_perf_ring_head{0};
PerfSample g_perf_ring[kPerfRingCapacity] = {};
std::atomic<double> g_perf_time_seconds{0.0};
std::atomic<bool> g_perf_reset_requested{false};
std::string g_perf_text_shared;
SpinLock g_perf_text_lock;

// Vector variables
std::vector<MonitorInfo> g_monitors;

// Colorspace variables
reshade::api::color_space g_current_colorspace = reshade::api::color_space::unknown;
std::string g_hdr10_override_status = "Not applied";
std::string g_hdr10_override_timestamp = "Never";

// Monitor labels cache (updated by background thread) - lock-free publication
std::atomic<std::shared_ptr<const std::vector<std::string>>> g_monitor_labels{std::make_shared<const std::vector<std::string>>()} ;

// Experimental/Unstable features toggle
std::atomic<bool> s_enable_unstable_reshade_features = false; // Disabled by default

// Resolution Override Settings (Experimental)
std::atomic<bool> s_enable_resolution_override = false; // Disabled by default
std::atomic<int> s_override_resolution_width{1920}; // Default to 1920
std::atomic<int> s_override_resolution_height{1080}; // Default to 1080

// Keyboard Shortcut Settings (Experimental)
std::atomic<bool> s_enable_mute_unmute_shortcut = false; // Disabled by default
