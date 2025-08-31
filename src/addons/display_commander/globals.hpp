#pragma once


#include <windows.h>
#include <atomic>
#include <memory>
#include <string>
#include <vector>
#include <thread>
#include "dxgi/custom_fps_limiter_manager.hpp"
#include "display_cache.hpp"
#include "dxgi/dxgi_device_info.hpp"

// Constants
#define ImTextureID ImU64
#define DEBUG_LEVEL_0

// Forward declarations
class ReflexManager;
class SpinLock;
class BackgroundWindowManager;
class CustomFpsLimiterManager;
class DXGIDeviceInfoManager;

// Enums
enum class DxgiBypassMode : std::uint8_t { kUnknown, kComposed, kOverlay, kIndependentFlip };
enum class WindowStyleMode : std::uint8_t { KEEP, BORDERLESS, OVERLAPPED_WINDOW };

// Structures
struct GlobalWindowState {
  int desired_width = 0;
  int desired_height = 0;
  int target_x = 0;
  int target_y = 0;
  int target_w = 0;
  int target_h = 0;
  bool needs_resize = false;
  bool needs_move = false;
  bool style_changed = false;
  bool style_changed_ex = false;
  int new_style = 0;
  int new_ex_style = 0;
  WindowStyleMode style_mode = WindowStyleMode::BORDERLESS;
  const char* reason = "unknown";
  
  int show_cmd = 0;
  int current_monitor_index = 0;
  display_cache::RationalRefreshRate current_monitor_refresh_rate;
  
  // Current display dimensions
  int display_width = 0;
  int display_height = 0;
  
  void reset() {
    desired_width = 0;
    desired_height = 0;
    target_x = 0;
    target_y = 0;
    target_w = 0;
    target_h = 0;
    needs_resize = false;
    needs_move = false;
    style_changed = false;
    style_changed_ex = false;
    style_mode = WindowStyleMode::BORDERLESS;
    reason = "unknown";
    current_monitor_index = 0;
    current_monitor_refresh_rate = display_cache::RationalRefreshRate();
    display_width = 0;
    display_height = 0;
  }
};

// Performance stats structure
struct PerfSample {
    double timestamp_seconds;
    float fps;
};

// Monitor info structure
struct MonitorInfo;

// External variable declarations - centralized here to avoid duplication

// Desktop Resolution Override
extern std::atomic<int> s_selected_monitor_index;

// Display Tab Enhanced Settings
extern std::atomic<int> s_selected_resolution_index;
extern std::atomic<int> s_selected_refresh_rate_index;
extern std::atomic<bool> s_initial_auto_selection_done;

// Auto-restore and auto-apply settings
extern std::atomic<bool> s_auto_restore_resolution_on_close;
extern std::atomic<bool> s_auto_apply_resolution_change;
extern std::atomic<bool> s_auto_apply_refresh_rate_change;

// Reflex settings
extern std::atomic<bool> s_reflex_enabled;
extern std::atomic<bool> s_reflex_low_latency_mode;
extern std::atomic<bool> s_reflex_low_latency_boost;
extern std::atomic<bool> s_reflex_use_markers;
extern std::atomic<bool> s_reflex_debug_output;

// Window management
extern std::atomic<bool> s_prevent_always_on_top;
extern std::atomic<bool> s_background_feature_enabled;
extern std::atomic<int> s_window_mode;
extern std::atomic<int> s_windowed_width;
extern std::atomic<int> s_windowed_height;
extern std::atomic<int> s_aspect_index;

// Prevent Fullscreen
extern std::atomic<bool> s_prevent_fullscreen;

// Fix HDR10 color space when backbuffer is RGB10A2
extern std::atomic<bool> s_fix_hdr10_colorspace;

// Window Management Settings
extern std::atomic<bool> s_remove_top_bar;
extern std::atomic<int> s_move_to_zero_if_out; // 0 = Disabled, 1 = Move to zero if out, 2 = Move to zero if out and windowed
extern std::atomic<int> s_target_monitor_index;
extern std::atomic<int> s_dxgi_composition_state;
extern std::atomic<bool> s_block_input_in_background;

// NVAPI Settings
extern std::atomic<bool> s_nvapi_fullscreen_prevention;
extern std::atomic<bool> s_nvapi_hdr_logging;
extern std::atomic<float> s_nvapi_hdr_interval_sec;

// Audio Settings
extern std::atomic<bool> s_mute_in_background;
extern std::atomic<bool> s_mute_in_background_if_other_audio;
extern std::atomic<float> s_audio_volume_percent;
extern std::atomic<bool> s_audio_mute;

// FPS Limiter Settings
extern std::atomic<float> s_fps_limit_background;
extern std::atomic<float> s_fps_limit;
extern std::atomic<float> s_fps_extra_wait_ms;
extern std::atomic<bool> s_custom_fps_limiter_enabled;

// VSync and Tearing Controls
extern std::atomic<bool> s_force_vsync_on;
extern std::atomic<bool> s_force_vsync_off;
extern std::atomic<bool> s_prevent_tearing;

// ReShade Integration
extern std::atomic<reshade::api::effect_runtime*> g_reshade_runtime;
extern void (*g_custom_fps_limiter_callback)();

// Monitor Management
extern std::atomic<std::shared_ptr<const std::vector<std::string>>> g_monitor_labels;

// Continuous monitoring and rendering
extern std::atomic<bool> s_continuous_monitoring_enabled;

// Atomic variables
extern std::atomic<int> g_comp_query_counter;
extern std::atomic<int> g_comp_last_logged;
extern std::atomic<reshade::api::swapchain*> g_last_swapchain_ptr; // Using void* to avoid reshade dependency
extern std::atomic<uint64_t> g_init_apply_generation;
extern std::atomic<HWND> g_last_swapchain_hwnd;
extern std::atomic<bool> g_shutdown;
extern std::atomic<bool> g_muted_applied;

// Reflex-related atomic variables
extern std::atomic<bool> g_reflex_settings_changed;

// Global instances
extern std::unique_ptr<ReflexManager> g_reflexManager;
extern std::atomic<std::shared_ptr<GlobalWindowState>> g_window_state;
extern BackgroundWindowManager g_backgroundWindowManager;

// Custom FPS Limiter Manager
namespace dxgi::fps_limiter {
    extern std::unique_ptr<CustomFpsLimiterManager> g_customFpsLimiterManager;
}

// DXGI Device Info Manager
extern std::unique_ptr<DXGIDeviceInfoManager> g_dxgiDeviceInfoManager;

// Direct atomic variables for latency tracking (UI access)
extern std::atomic<float> g_current_latency_ms;
extern std::atomic<float> g_pcl_av_latency_ms;
extern std::atomic<float> g_average_latency_ms;
extern std::atomic<float> g_min_latency_ms;
extern std::atomic<float> g_max_latency_ms;
extern std::atomic<uint64_t> g_current_frame;
extern std::atomic<bool> g_reflex_active;

// Present duration tracking
extern std::atomic<LONGLONG> g_present_duration_ns;

// Simulation duration tracking
extern std::atomic<LONGLONG> g_simulation_duration_ns;

// FPS limiter start duration tracking (nanoseconds)
extern std::atomic<LONGLONG> fps_sleep_before_on_present_ns;

// FPS limiter start duration tracking (nanoseconds)
extern std::atomic<LONGLONG> fps_sleep_after_on_present_ns;


// Render start time tracking
extern std::atomic<LONGLONG> g_render_start_time;

// Backbuffer dimensions
extern std::atomic<int> g_last_backbuffer_width;
extern std::atomic<int> g_last_backbuffer_height;

// Background/foreground state
extern std::atomic<bool> g_app_in_background;

// FPS limiter mode: 0 = Custom (Sleep/Spin), 1 = VBlank Scanline Sync (VBlank)
extern std::atomic<int> s_fps_limiter_mode;

// FPS limiter injection timing: 0 = OnPresentFlags (recommended), 1 = OnPresentUpdateBefore2, 2 = OnPresentUpdateBefore
extern std::atomic<int> s_fps_limiter_injection;

// Scanline offset
extern std::atomic<int> s_scanline_offset;

// Performance stats (FPS/frametime) shared state
extern std::atomic<uint32_t> g_perf_ring_head;
extern PerfSample g_perf_ring[];
extern std::atomic<double> g_perf_time_seconds;
extern std::atomic<bool> g_perf_reset_requested;
extern std::atomic<std::shared_ptr<const std::string>> g_perf_text_shared;

// Lock-free ring buffer for recent FPS samples (60s window at ~240 Hz -> 14400 max)
constexpr size_t kPerfRingCapacity = 16384;


// Vector variables
extern std::atomic<std::shared_ptr<const std::vector<MonitorInfo>>> g_monitors;

// Colorspace variables (using int to avoid reshade dependency)
extern reshade::api::color_space g_current_colorspace;
extern std::atomic<std::shared_ptr<const std::string>> g_hdr10_override_status;
extern std::atomic<std::shared_ptr<const std::string>> g_hdr10_override_timestamp;

// Helper function for updating HDR10 override status atomically
void UpdateHdr10OverrideStatus(const std::string& status);

// Helper function for updating HDR10 override timestamp atomically
void UpdateHdr10OverrideTimestamp(const std::string& timestamp);

// Experimental/Unstable features toggle
extern std::atomic<bool> s_enable_unstable_reshade_features;



// Keyboard Shortcut Settings (Experimental)
extern std::atomic<bool> s_enable_mute_unmute_shortcut;

// Performance optimization settings
extern std::atomic<bool> g_flush_before_present;

// Monitoring thread
extern std::atomic<bool> g_monitoring_thread_running;
extern std::thread g_monitoring_thread;

// Import the global variable
extern std::atomic<int> s_spoof_fullscreen_state;
extern std::atomic<int> s_spoof_window_focus;

// Swapchain event counters - reset on each swapchain creation
extern std::atomic<uint32_t> g_swapchain_event_counters[16]; // Array for all On* events
extern std::atomic<uint32_t> g_swapchain_event_total_count; // Total events across all types

// Swapchain event counter indices
enum SwapchainEventIndex {
    SWAPCHAIN_EVENT_BEGIN_RENDER_PASS = 0,
    SWAPCHAIN_EVENT_END_RENDER_PASS = 1,
    SWAPCHAIN_EVENT_CREATE_SWAPCHAIN_CAPTURE = 2,
    SWAPCHAIN_EVENT_INIT_SWAPCHAIN = 3,
    SWAPCHAIN_EVENT_PRESENT_UPDATE_AFTER = 4,
    SWAPCHAIN_EVENT_PRESENT_UPDATE_BEFORE = 5,
    SWAPCHAIN_EVENT_PRESENT_UPDATE_BEFORE2 = 6,
    SWAPCHAIN_EVENT_INIT_COMMAND_LIST = 7,
    SWAPCHAIN_EVENT_EXECUTE_COMMAND_LIST = 8,
    SWAPCHAIN_EVENT_BIND_PIPELINE = 9,
    SWAPCHAIN_EVENT_INIT_COMMAND_QUEUE = 10,
    SWAPCHAIN_EVENT_RESET_COMMAND_LIST = 11,
    SWAPCHAIN_EVENT_PRESENT_FLAGS = 12,
    SWAPCHAIN_EVENT_DRAW = 13,
    SWAPCHAIN_EVENT_DRAW_INDEXED = 14,
    SWAPCHAIN_EVENT_DRAW_OR_DISPATCH_INDIRECT = 15
};