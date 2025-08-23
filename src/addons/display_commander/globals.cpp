#include "addon.hpp"
#include "reflex/reflex_management.hpp"
#include "background_window.hpp" // Added this line
#include "dxgi/custom_fps_limiter_manager.hpp"
#include "dxgi/dxgi_device_info.hpp"

// Global variables
// UI mode removed - now using new tab system

// Window settings
float s_windowed_width = 3440.f; // 21:9 ultrawide width
float s_windowed_height = 1440.f; // 21:9 ultrawide height
float s_window_mode = 0.f; // 0 = Borderless Windowed (Aspect Ratio), 1 = Borderless Windowed (Width/Height), 2 = Borderless Fullscreen
float s_remove_top_bar = 1.f; // Suppress top bar/border messages enabled by default for borderless windows
float s_suppress_move_resize_messages = 1.f; // Suppress move/resize messages by default

float s_suppress_maximize = 1.f; // Suppress maximize messages by default
float s_aspect_index = 4.f; // 21:9 ultrawide

// Window alignment when repositioning is needed (0 = None, 1 = Top Left, 2 = Top Right, 3 = Bottom Left, 4 = Bottom Right, 5 = Center)
float s_move_to_zero_if_out = 2.f; // default to top right


// Prevent Fullscreen
float s_prevent_fullscreen = 0.f;

// NVAPI Fullscreen Prevention
float s_nvapi_fullscreen_prevention = 1.f; // enabled by default
// NVAPI HDR logging
float s_nvapi_hdr_logging = 0.f;
float s_nvapi_hdr_interval_sec = 5.f;
// NVAPI Force HDR10
float s_nvapi_force_hdr10 = 0.f;

// Spoof Fullscreen State (for applications that query fullscreen status)
float s_spoof_fullscreen_state = 0.f;
float s_mute_in_background = 0.f;
float s_mute_in_background_if_other_audio = 0.f;
float s_audio_volume_percent = 100.f;
float s_audio_mute = 0.f;

// Performance: background FPS cap
float s_fps_limit_background = 30.f;

// FPS limit for foreground
float s_fps_limit = 0.f;

// Custom FPS Limiter settings
float s_custom_fps_limit = 0.f;
const float s_custom_fps_limiter_enabled = 1.0f; // Always enabled

// Monitor and display settings
float s_target_monitor_index = 0.f;
float s_dxgi_composition_state = 0.f;

float s_spoof_window_focus = 0.f;

// Fix HDR10 color space when backbuffer is RGB10A2
float s_fix_hdr10_colorspace = 0.f;

// ReShade runtime for input blocking
std::atomic<reshade::api::effect_runtime*> g_reshade_runtime = nullptr;

// Window minimize prevention
float s_prevent_windows_minimize = 0.f;

// Prevent always on top behavior
float s_prevent_always_on_top = 1.f; // Prevent games from staying on top by default

// Background feature - show black window behind game when not fullscreen
float s_background_feature_enabled = 0.f; // Disabled by default

// Enforce desired window settings
float s_enforce_desired_window = 1.f; // Enable window enforcement

// Desktop Resolution Override
float s_selected_monitor_index = 0.f; // Primary monitor by default

// Display Tab Enhanced Settings
float s_selected_aspect_ratio = 1.f; // Default to 16:9
float s_selected_resolution_index = 0.f; // Default to first available resolution
float s_selected_refresh_rate_index = 0.f; // Default to first available refresh rate

bool s_initial_auto_selection_done = false; // Track if we've done initial auto-selection

// Auto-restore resolution on game close
bool s_auto_restore_resolution_on_close = true; // Enabled by default

// Auto-apply resolution and refresh rate changes
bool s_auto_apply_resolution_change = false; // Disabled by default
bool s_auto_apply_refresh_rate_change = false; // Disabled by default

// Reflex settings
float s_reflex_enabled = 1.f; // Enabled by default
float s_reflex_low_latency_mode = 0.f; // Low latency mode disabled by default
float s_reflex_low_latency_boost = 0.f; // Boost disabled by default
float s_reflex_use_markers = 0.f; // Use markers disabled by default
float s_reflex_debug_output = 0.f; // Debug output disabled by default

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
float s_continuous_monitoring_enabled = 1.f; // Enabled by default
std::atomic<bool> g_monitoring_thread_running{false};
std::thread g_monitoring_thread;

// Continuous rendering system
float s_continuous_rendering_enabled = 0.f; // Off by default
float s_continuous_rendering_throttle = 2.f; // Throttle to every 2nd frame by default
float s_force_continuous_rendering = 1.f; // Force continuous rendering on every frame (enabled by default for focus spoofing)
// CONTINUOUS RENDERING THREAD VARIABLES REMOVED - Focus spoofing is now handled by Win32 hooks

// FOCUS LOSS DETECTION VARIABLES REMOVED - Focus spoofing is now handled by Win32 hooks

// Global window state instance
GlobalWindowState g_window_state;

// Global background window manager instance
BackgroundWindowManager g_backgroundWindowManager;

// Global Custom FPS Limiter Manager instance
namespace renodx::dxgi::fps_limiter {
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

// Vector variables
std::vector<MonitorInfo> g_monitors;

// Colorspace variables
reshade::api::color_space g_current_colorspace = reshade::api::color_space::unknown;
std::string g_hdr10_override_status = "Not applied";
std::string g_hdr10_override_timestamp = "Never";
