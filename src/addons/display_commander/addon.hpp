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
#include <memory>

#include "utils.hpp"
#include "reflex/reflex_management.hpp"
#include "renodx/settings.hpp"
#include "display_cache.hpp"
#include "globals.hpp"


// Lightweight spinlock for short critical sections
class SpinLock {
public:
    void lock() {
        while (_flag.test_and_set(std::memory_order_acquire)) {
        }
    }
    void unlock() {
        _flag.clear(std::memory_order_release);
    }
private:
    std::atomic_flag _flag = ATOMIC_FLAG_INIT;
};

// Shared monitor labels cache (updated off the UI thread) - lock-free publication
extern std::atomic<std::shared_ptr<const std::vector<std::string>>> g_monitor_labels;


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
extern std::atomic<bool> g_app_in_background;

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
extern std::atomic<bool> s_continuous_monitoring_enabled;
extern std::atomic<bool> g_monitoring_thread_running;
extern std::thread g_monitoring_thread;

// Continuous rendering system
extern std::atomic<bool> s_continuous_rendering_enabled;
extern std::atomic<bool> s_force_continuous_rendering;
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
    style_mode = WindowStyleMode::BORDERLESS;
    reason = "unknown";
    current_monitor_index = 0;
    current_monitor_refresh_rate = display_cache::RationalRefreshRate();
    display_width = 0;
    display_height = 0;
  }
};

// Global variables (extern declarations)
// UI mode removed - now using new tab system

extern std::atomic<int> s_windowed_width;
extern std::atomic<int> s_windowed_height;
extern std::atomic<int> s_window_mode; // 0 = Windowed, 1 = Borderless, 2 = Overlapped Window
extern std::atomic<bool> s_remove_top_bar;
extern std::atomic<bool> s_suppress_move_resize_messages;

extern std::atomic<bool> s_suppress_maximize;
extern std::atomic<int> s_aspect_index; // 0 = 16:9, 1 = 16:10, 2 = 4:3, 3 = 3:2, 4 = 1:1, 5 = 1:2, 6 = 2:3, 7 = 3:4, 8 = 9:16, 9 = 10:16
// Window alignment when repositioning is needed (0 = None, 1 = Top Left, 2 = Top Right, 3 = Bottom Left, 4 = Bottom Right)
extern std::atomic<int> s_move_to_zero_if_out; // 0 = Disabled, 1 = Move to zero if out, 2 = Move to zero if out and windowed

// Prevent Fullscreen
extern std::atomic<bool> s_prevent_fullscreen;

// NVAPI Fullscreen Prevention
extern std::atomic<bool> s_nvapi_fullscreen_prevention;
// NVAPI HDR logging
extern std::atomic<bool> s_nvapi_hdr_logging;
extern std::atomic<float> s_nvapi_hdr_interval_sec;

// Spoof Fullscreen State (for applications that query fullscreen status)
extern std::atomic<int> s_spoof_fullscreen_state;
extern std::atomic<bool> s_mute_in_background;
extern std::atomic<bool> s_mute_in_background_if_other_audio;
extern std::atomic<float> s_audio_volume_percent;
extern std::atomic<bool> s_audio_mute;
extern std::atomic<float> s_fps_limit_background;
extern std::atomic<float> s_fps_limit;
// Extra wait time for FPS limiter in milliseconds
extern std::atomic<float> s_fps_extra_wait_ms;
extern std::atomic<float> s_custom_fps_limit;
extern std::atomic<bool> s_custom_fps_limiter_enabled;
// VSync and tearing controls
extern std::atomic<bool> s_force_vsync_on;
extern std::atomic<bool> s_force_vsync_off;
extern std::atomic<bool> s_allow_tearing;
extern std::atomic<bool> s_prevent_tearing;
extern std::atomic<int> s_target_monitor_index;
extern std::atomic<int> s_dxgi_composition_state;

// Input blocking in background
extern std::atomic<bool> s_block_input_in_background;

extern std::atomic<int> g_comp_query_counter;
extern std::atomic<int> g_comp_last_logged;
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
extern std::atomic<bool> s_fix_hdr10_colorspace;

// Window minimize prevention
extern std::atomic<bool> s_prevent_windows_minimize;

// Prevent always on top behavior
extern std::atomic<bool> s_prevent_always_on_top;

// Background feature - show black window behind game when not fullscreen
extern std::atomic<bool> s_background_feature_enabled;

// Global Reflex manager instance
extern std::unique_ptr<ReflexManager> g_reflexManager;
// Global flag for Reflex settings changes
extern std::atomic<bool> g_reflex_settings_changed;

// Global window state instance
extern GlobalWindowState g_window_state;
extern SpinLock g_window_state_lock;

// Lock-free ring buffer for recent FPS samples (60s window at ~240 Hz -> 14400 max)
constexpr size_t kPerfRingCapacity = 16384;
extern std::atomic<uint32_t> g_perf_ring_head;
extern PerfSample g_perf_ring[kPerfRingCapacity];
extern std::atomic<double> g_perf_time_seconds;
extern std::atomic<bool> g_perf_reset_requested;
extern std::string g_perf_text_shared;
extern SpinLock g_perf_text_lock;


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
void OnPresentUpdateBefore(reshade::api::command_queue *, reshade::api::swapchain* swapchain, const reshade::api::rect* source_rect, const reshade::api::rect* dest_rect, uint32_t dirty_rect_count, const reshade::api::rect* dirty_rects);
void OnPresentUpdateBefore2(reshade::api::effect_runtime* runtime);
void OnPresentUpdateAfter(reshade::api::command_queue* /*queue*/, reshade::api::swapchain* swapchain);

// Additional event handlers for frame timing and composition
void OnInitCommandList(reshade::api::command_list* cmd_list);
void OnExecuteCommandList(reshade::api::command_queue* queue, reshade::api::command_list* cmd_list);
void OnBindPipeline(reshade::api::command_list* cmd_list, reshade::api::pipeline_stage stages, reshade::api::pipeline pipeline);

// HDR10 colorspace override status for UI display
extern std::atomic<std::shared_ptr<std::string>> g_hdr10_override_status;

// HDR10 colorspace override timestamp for UI display
extern std::atomic<std::shared_ptr<std::string>> g_hdr10_override_timestamp;

// Helper function for updating HDR10 override status atomically
void UpdateHdr10OverrideStatus(const std::string& status);

// Helper function for updating HDR10 override timestamp atomically
void UpdateHdr10OverrideTimestamp(const std::string& timestamp);

// Note: GetIndependentFlipState is implemented in the .cpp file as it's complex

// Swapchain sync interval accessors
// Returns UINT32_MAX when using application's default, or <0-4> for explicit intervals, or UINT_MAX if unknown
uint32_t GetSwapchainSyncInterval(reshade::api::swapchain* swapchain);

// Event to capture sync interval from swapchain creation
bool OnCreateSwapchainCapture(reshade::api::device_api api, reshade::api::swapchain_desc& desc, void* hwnd);

// Experimental/Unstable features toggle
extern std::atomic<bool> s_enable_unstable_reshade_features;

// Resolution Override Settings (Experimental)
// Removed: s_enable_resolution_override is now handled by BoolSetting in developer tab settings
extern std::atomic<int> s_override_resolution_width;
extern std::atomic<int> s_override_resolution_height;

// Keyboard Shortcut Settings (Experimental)
extern std::atomic<bool> s_enable_mute_unmute_shortcut;