#pragma once

#define ImTextureID ImU64

#define DEBUG_LEVEL_0

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windef.h>
#include <dxgi1_3.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <deps/imgui/imgui.h>
#include <include/reshade.hpp>

#include <thread>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <vector>

#include "utils.hpp"
#include "reflex/reflex_management.hpp"
#include "renodx/settings.hpp"


// WASAPI per-app volume control
#include <mmdeviceapi.h>
#include <audiopolicy.h>

// Audio management functions
bool SetMuteForCurrentProcess(bool mute);
bool SetVolumeForCurrentProcess(float volume_0_100);
void RunBackgroundAudioMonitor();

// Forward declarations
void ComputeDesiredSize(int& out_w, int& out_h);

std::vector<std::string> MakeMonitorLabels();

// External declarations
extern std::atomic<int> g_last_backbuffer_width;
extern std::atomic<int> g_last_backbuffer_height;

// Enums
enum class DxgiBypassMode : std::uint8_t { kUnknown, kComposed, kOverlay, kIndependentFlip };
enum class WindowStyleMode : std::uint8_t { KEEP, BORDERLESS, OVERLAPPED_WINDOW };

// Forward declarations that depend on enums
DxgiBypassMode GetIndependentFlipState(reshade::api::swapchain* swapchain);

// Reflex management functions
bool InstallReflexHooks();
void UninstallReflexHooks();
void SetReflexLatencyMarkers(reshade::api::swapchain* swapchain);
void SetReflexSleepMode(reshade::api::swapchain* swapchain);

// Frame lifecycle hooks for custom FPS limiter
void OnBeginRenderPass(reshade::api::command_list* cmd_list, uint32_t count, const reshade::api::render_pass_render_target_desc* rts, const reshade::api::render_pass_depth_stencil_desc* ds);
void OnEndRenderPass(reshade::api::command_list* cmd_list);

// Global flags for Reflex management
extern std::atomic<bool> g_reflex_settings_changed;

// Continuous monitoring system
extern float s_continuous_monitoring_enabled;
extern std::atomic<bool> g_monitoring_thread_running;
extern std::thread g_monitoring_thread;

// Continuous rendering system
extern float s_continuous_rendering_enabled;
extern float s_force_continuous_rendering;
extern float s_continuous_rendering_throttle;
// CONTINUOUS RENDERING THREAD VARIABLES REMOVED - Focus spoofing is now handled by Win32 hooks

// FOCUS LOSS DETECTION VARIABLES REMOVED - Focus spoofing is now handled by Win32 hooks

// Structs
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
  int new_style = 0;
  int new_ex_style = 0;
  WindowStyleMode style_mode = WindowStyleMode::BORDERLESS;
  std::string reason = "unknown";
  
  int show_cmd = 0;
  
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
    style_mode = WindowStyleMode::BORDERLESS;
    reason = "unknown";
  }
};

// Global variables (extern declarations)
// UI mode removed - now using new tab system

// Reflex settings
extern float s_reflex_enabled;
extern float s_reflex_low_latency_mode;
extern float s_reflex_low_latency_boost;
extern float s_reflex_use_markers;

extern float s_windowed_width;
extern float s_windowed_height;
extern float s_window_mode;
extern float s_remove_top_bar;
extern float s_suppress_move_resize_messages;

extern float s_suppress_maximize;
extern float s_aspect_index;
// Window alignment when repositioning is needed (0 = None, 1 = Top Left, 2 = Top Right, 3 = Bottom Left, 4 = Bottom Right)
extern float s_move_to_zero_if_out;

// Prevent Fullscreen
extern float s_prevent_fullscreen;

// NVAPI Fullscreen Prevention
extern float s_nvapi_fullscreen_prevention;
// NVAPI HDR logging
extern float s_nvapi_hdr_logging;
extern float s_nvapi_hdr_interval_sec;
// NVAPI Force HDR10
extern float s_nvapi_force_hdr10;

// Spoof Fullscreen State (for applications that query fullscreen status)
extern float s_spoof_fullscreen_state;
extern float s_mute_in_background;
extern float s_mute_in_background_if_other_audio;
extern float s_audio_volume_percent;
extern float s_audio_mute;
extern float s_fps_limit_background;
extern float s_fps_limit;
extern float s_custom_fps_limit;
extern const float s_custom_fps_limiter_enabled;
extern float s_target_monitor_index;
extern float s_dxgi_composition_state;

extern float s_spoof_window_focus;

extern std::atomic<int> g_comp_query_counter;
extern std::atomic<int> g_comp_last_logged;
// Last known swapchain pointer (for composition state queries)
extern std::atomic<reshade::api::swapchain*> g_last_swapchain_ptr;
extern std::atomic<uint64_t> g_init_apply_generation;
extern std::atomic<reshade::api::effect_runtime*> g_reshade_runtime;
extern std::chrono::steady_clock::time_point g_attach_time;
extern void (*g_custom_fps_limiter_callback)();
extern std::atomic<HWND> g_last_swapchain_hwnd;
extern std::atomic<bool> g_shutdown;
extern std::atomic<bool> g_muted_applied;
extern std::atomic<float> g_default_fps_limit;

extern std::vector<MonitorInfo> g_monitors;

// Fix HDR10 color space when backbuffer is RGB10A2
extern float s_fix_hdr10_colorspace;

// Window minimize prevention
extern float s_prevent_windows_minimize;

// Prevent always on top behavior
extern float s_prevent_always_on_top;

// Enforce desired window settings
extern float s_enforce_desired_window;

// Desktop Resolution Override
extern float s_selected_monitor_index;

// Display Tab Enhanced Settings
extern float s_selected_aspect_ratio;
extern float s_selected_resolution_index;
extern float s_selected_refresh_rate_index;
extern bool s_auto_restore_resolution_on_close;
extern bool s_auto_apply_resolution_change;
extern bool s_auto_apply_refresh_rate_change;

// Global Reflex manager instance
extern std::unique_ptr<ReflexManager> g_reflexManager;
// Global flag for Reflex settings changes
extern std::atomic<bool> g_reflex_settings_changed;

// Global window state instance
extern GlobalWindowState g_window_state;

// Direct atomic variables for latency tracking (UI access)
extern std::atomic<float> g_current_latency_ms;
extern std::atomic<float> g_pcl_av_latency_ms;
extern std::atomic<float> g_average_latency_ms;
extern std::atomic<float> g_min_latency_ms;
extern std::atomic<float> g_max_latency_ms;
extern std::atomic<uint64_t> g_current_frame;
extern std::atomic<bool> g_reflex_active;


// Function declarations
const char* DxgiBypassModeToString(DxgiBypassMode mode);
bool SetIndependentFlipState(reshade::api::swapchain* swapchain);
void ApplyWindowChange(HWND hwnd, const char* reason = "unknown", bool force_apply = false);
bool ShouldApplyWindowedForBackbuffer(int desired_w, int desired_h);

// Continuous monitoring functions
void StartContinuousMonitoring();
void StopContinuousMonitoring();
void ContinuousMonitoringThread();
bool NeedsWindowAdjustment(HWND hwnd, int& out_width, int& out_height, int& out_pos_x, int& out_pos_y, WindowStyleMode& out_style_mode);

// CONTINUOUS RENDERING FUNCTIONS REMOVED - Focus spoofing is now handled by Win32 hooks

// Swapchain event handlers
void OnInitSwapchain(reshade::api::swapchain* swapchain, bool resize);
void OnPresentUpdate(reshade::api::command_queue* queue, reshade::api::swapchain* swapchain, const reshade::api::rect* source_rect, const reshade::api::rect* dest_rect, uint32_t dirty_rect_count, const reshade::api::rect* dirty_rects);

// HDR10 colorspace fixing
void FixHDR10Colorspace(reshade::api::swapchain* swapchain);
// Background NVAPI HDR monitor
void RunBackgroundNvapiHdrMonitor();
void LogNvapiHdrOnce();

// Current swapchain colorspace for UI display
extern reshade::api::color_space g_current_colorspace;

// HDR10 colorspace override status for UI display
extern std::string g_hdr10_override_status;

// HDR10 colorspace override timestamp for UI display
extern std::string g_hdr10_override_timestamp;

// Note: GetIndependentFlipState is implemented in the .cpp file as it's complex

// Swapchain sync interval accessors
// Returns UINT32_MAX when using application's default, or <0-4> for explicit intervals, or UINT_MAX if unknown
uint32_t GetSwapchainSyncInterval(reshade::api::swapchain* swapchain);

// Event to capture sync interval from swapchain creation
#if RESHADE_API_VERSION >= 17
bool OnCreateSwapchainCapture(reshade::api::device_api api, reshade::api::swapchain_desc& desc, void* hwnd);
#else
bool OnCreateSwapchainCapture(reshade::api::swapchain_desc& desc, void* hwnd);
#endif