#pragma once


#include <windows.h>
#include <atomic>
#include <memory>
#include <string>
#include <vector>
#include <thread>
#include <chrono>
#include "dxgi/custom_fps_limiter_manager.hpp"
#include "dxgi/custom_fps_limiter.hpp"

// Forward declarations
class ReflexManager;
class GlobalWindowState;
class SpinLock;
class BackgroundWindowManager;
class CustomFpsLimiterManager;
class DXGIDeviceInfoManager;

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
extern std::atomic<int> s_selected_aspect_ratio;
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
extern std::atomic<bool> s_enforce_desired_window;
extern std::atomic<int> s_window_mode;
extern std::atomic<int> s_windowed_width;
extern std::atomic<int> s_windowed_height;
extern std::atomic<int> s_aspect_index;

// Continuous monitoring and rendering
extern std::atomic<bool> s_continuous_monitoring_enabled;
extern std::atomic<bool> s_continuous_rendering_enabled;
extern std::atomic<bool> s_force_continuous_rendering;

// Atomic variables
extern std::atomic<int> g_comp_query_counter;
extern std::atomic<int> g_comp_last_logged;
extern std::atomic<reshade::api::swapchain*> g_last_swapchain_ptr; // Using void* to avoid reshade dependency
extern std::atomic<uint64_t> g_init_apply_generation;
extern std::atomic<HWND> g_last_swapchain_hwnd;
extern std::atomic<bool> g_shutdown;
extern std::atomic<bool> g_muted_applied;
extern std::atomic<bool> g_volume_applied;
extern std::atomic<float> g_default_fps_limit;

// Reflex-related atomic variables
extern std::atomic<uint64_t> g_reflex_frame_id;
extern std::atomic<bool> g_reflex_hooks_installed;
extern std::atomic<bool> g_reflex_settings_changed;

// Global instances
extern std::unique_ptr<ReflexManager> g_reflexManager;
extern GlobalWindowState g_window_state;
extern SpinLock g_window_state_lock;
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

// Backbuffer dimensions
extern std::atomic<int> g_last_backbuffer_width;
extern std::atomic<int> g_last_backbuffer_height;

// Background/foreground state
extern std::atomic<bool> g_app_in_background;

// Performance stats (FPS/frametime) shared state
extern std::atomic<uint32_t> g_perf_ring_head;
extern PerfSample g_perf_ring[];
extern std::atomic<double> g_perf_time_seconds;
extern std::atomic<bool> g_perf_reset_requested;
extern std::string g_perf_text_shared;
extern SpinLock g_perf_text_lock;

// Vector variables
extern std::vector<MonitorInfo> g_monitors;

// Colorspace variables (using int to avoid reshade dependency)
extern reshade::api::color_space g_current_colorspace;
extern std::string g_hdr10_override_status;
extern std::string g_hdr10_override_timestamp;

// Experimental/Unstable features toggle
extern std::atomic<bool> s_enable_unstable_reshade_features;

// Resolution Override Settings (Experimental)
extern std::atomic<bool> s_enable_resolution_override;
extern std::atomic<int> s_override_resolution_width;
extern std::atomic<int> s_override_resolution_height;

// Keyboard Shortcut Settings (Experimental)
extern std::atomic<bool> s_enable_mute_unmute_shortcut;

// Monitoring thread
extern std::atomic<bool> g_monitoring_thread_running;
extern std::thread g_monitoring_thread;

// Timing
extern std::chrono::steady_clock::time_point g_attach_time;

// Import the global variable
extern std::atomic<int> s_spoof_fullscreen_state;