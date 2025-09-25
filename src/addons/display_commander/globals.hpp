#pragma once


#include <windows.h>
#include <atomic>
#include <memory>
#include <string>
#include <vector>
#include <thread>
#include "dxgi/custom_fps_limiter_manager.hpp"
#include "latent_sync/latent_sync_manager.hpp"
#include "display_cache.hpp"
#include <d3d11.h>
#include <reshade.hpp>
#include <wrl/client.h>



// Constants
#define ImTextureID ImU64
#define DEBUG_LEVEL_0

// Forward declarations

class SpinLock;
class BackgroundWindowManager;
class CustomFpsLimiterManager;
class LatentSyncManager;
class LatencyManager;

// DLL initialization state
extern std::atomic<bool> g_dll_initialization_complete;

// Module handle for pinning/unpinning
extern HMODULE g_hmodule;

// Shared DXGI factory to avoid redundant CreateDXGIFactory calls
extern std::atomic<Microsoft::WRL::ComPtr<IDXGIFactory1>*> g_shared_dxgi_factory;

// Helper function to get shared DXGI factory (thread-safe)
Microsoft::WRL::ComPtr<IDXGIFactory1> GetSharedDXGIFactory();

// Enums
enum class DxgiBypassMode : std::uint8_t { kUnknown, kComposed, kOverlay, kIndependentFlip };
enum class WindowStyleMode : std::uint8_t { KEEP, BORDERLESS, OVERLAPPED_WINDOW };
enum class FpsLimiterMode : std::uint8_t { kNone = 0, kCustom = 1, kLatentSync = 2 };
enum class WindowMode : std::uint8_t { kFullscreen = 0, kAspectRatio = 1 };
enum class AspectRatioType : std::uint8_t {
    k3_2 = 0,      // 3:2
    k4_3 = 1,      // 4:3
    k16_10 = 2,    // 16:10
    k16_9 = 3,     // 16:9
    k19_9 = 4,     // 19:9
    k19_5_9 = 5,   // 19.5:9
    k21_9 = 6,     // 21:9
    k32_9 = 7      // 32:9
};

enum class WindowAlignment : std::uint8_t {
    kCenter = 0,       // Center (default)
    kTopLeft = 1,      // Top Left
    kTopRight = 2,     // Top Right
    kBottomLeft = 3,   // Bottom Left
    kBottomRight = 4   // Bottom Right
};

enum class ScreensaverMode : std::uint8_t {
    kDefault = 0,           // Default (no change)
    kDisableWhenFocused = 1, // Disable when focused
    kDisable = 2            // Disable
};

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
extern std::atomic<bool> s_apply_display_settings_at_start;

// Window management
extern std::atomic<bool> s_prevent_always_on_top;
extern std::atomic<WindowMode> s_window_mode;
extern std::atomic<AspectRatioType> s_aspect_index;

// Prevent Fullscreen
extern std::atomic<bool> s_prevent_fullscreen;

// Fix HDR10 color space when backbuffer is RGB10A2
extern std::atomic<bool> s_fix_hdr10_colorspace;

// Window Management Settings
extern std::atomic<WindowAlignment> s_window_alignment; // Window alignment when repositioning is needed
extern std::atomic<int> s_target_monitor_index;
extern std::atomic<int> s_dxgi_composition_state;

// Mouse position spoofing for auto-click sequences
extern std::atomic<bool> s_spoof_mouse_position;
extern std::atomic<int> s_spoofed_mouse_x;
extern std::atomic<int> s_spoofed_mouse_y;

// Render blocking in background

// Present blocking in background

// NVAPI Settings
extern std::atomic<bool> s_nvapi_fullscreen_prevention;
extern std::atomic<bool> s_nvapi_hdr_logging;
extern std::atomic<float> s_nvapi_hdr_interval_sec;

// Audio Settings

// Keyboard Shortcuts
extern std::atomic<bool> s_enable_mute_unmute_shortcut;
extern std::atomic<bool> s_enable_background_toggle_shortcut;

// FPS Limiter Settings

// VSync and Tearing Controls

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
//extern std::atomic<reshade::api::swapchain*> g_last_swapchain_ptr; // Using void* to avoid reshade dependency
extern std::atomic<uint64_t> g_init_apply_generation;
extern std::atomic<HWND> g_last_swapchain_hwnd;
extern std::atomic<bool> g_shutdown;
extern std::atomic<bool> g_muted_applied;

// Global instances
extern std::atomic<std::shared_ptr<GlobalWindowState>> g_window_state;
extern BackgroundWindowManager g_backgroundWindowManager;

// Custom FPS Limiter Manager
namespace dxgi::fps_limiter {
    extern std::unique_ptr<CustomFpsLimiterManager> g_customFpsLimiterManager;
}

// Latent Sync Manager
namespace dxgi::latent_sync {
    extern std::unique_ptr<LatentSyncManager> g_latentSyncManager;
}

// Latency Manager
extern std::unique_ptr<LatencyManager> g_latencyManager;

// Direct atomic variables for latency tracking (UI access)
extern std::atomic<float> g_current_latency_ms;
extern std::atomic<float> g_average_latency_ms;
extern std::atomic<float> g_min_latency_ms;
extern std::atomic<float> g_max_latency_ms;
extern std::atomic<uint64_t> g_current_frame;

// Present duration tracking
extern std::atomic<LONGLONG> g_present_duration_ns;

// Simulation duration tracking
extern std::atomic<LONGLONG> g_simulation_duration_ns;

// FPS limiter start duration tracking (nanoseconds)
extern std::atomic<LONGLONG> fps_sleep_before_on_present_ns;

// FPS limiter start duration tracking (nanoseconds)
extern std::atomic<LONGLONG> fps_sleep_after_on_present_ns;

// FPS limiter start duration tracking (nanoseconds)
extern std::atomic<LONGLONG> g_reshade_overhead_duration_ns;

// Render submit duration tracking (nanoseconds)
extern std::atomic<LONGLONG> g_render_submit_duration_ns;

// Render start time tracking
extern std::atomic<LONGLONG> g_submit_start_time_ns;

// Backbuffer dimensions
extern std::atomic<int> g_last_backbuffer_width;
extern std::atomic<int> g_last_backbuffer_height;

// Background/foreground state
extern std::atomic<bool> g_app_in_background;

// FPS limiter mode: 0 = None, 1 = Custom (Sleep/Spin), 2 = VBlank Scanline Sync (VBlank)
extern std::atomic<FpsLimiterMode> s_fps_limiter_mode;

// FPS limiter injection timing: 0 = OnPresentFlags (recommended), 1 = OnPresentUpdateBefore2, 2 = OnPresentUpdateBefore
#define FPS_LIMITER_INJECTION_ONPRESENTFLAGS 0
#define FPS_LIMITER_INJECTION_ONPRESENTUPDATEBEFORE2 1
#define FPS_LIMITER_INJECTION_ONPRESENTUPDATEBEFORE 2

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

// Keyboard Shortcut Settings (Experimental)
extern std::atomic<bool> s_enable_mute_unmute_shortcut;

// Performance optimization settings
extern std::atomic<bool> g_flush_before_present;

// Sleep delay after present as percentage of frame time - 0% to 100%
extern std::atomic<float> s_sleep_after_present_frame_time_percentage;


// Monitoring thread
extern std::atomic<bool> g_monitoring_thread_running;
extern std::thread g_monitoring_thread;

// Render thread tracking
extern std::atomic<DWORD> g_render_thread_id;

// Import the global variable
extern std::atomic<int> s_spoof_fullscreen_state;
extern std::atomic<bool> s_continue_rendering;

// Forward declaration for tab settings
namespace settings {
    class ExperimentalTabSettings;
    class DeveloperTabSettings;
    class MainTabSettings;
    extern ExperimentalTabSettings g_experimentalTabSettings;
    extern DeveloperTabSettings g_developerTabSettings;
    extern MainTabSettings g_mainTabSettings;
}

// Swapchain event counters - reset on each swapchain creation
extern std::atomic<uint32_t> g_swapchain_event_counters[40]; // Array for all On* events
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
    SWAPCHAIN_EVENT_DRAW_OR_DISPATCH_INDIRECT = 15,
    // New power saving event counters
    SWAPCHAIN_EVENT_DISPATCH = 16,
    SWAPCHAIN_EVENT_DISPATCH_MESH = 17,
    SWAPCHAIN_EVENT_DISPATCH_RAYS = 18,
    SWAPCHAIN_EVENT_COPY_RESOURCE = 19,
    SWAPCHAIN_EVENT_UPDATE_BUFFER_REGION = 20,
    SWAPCHAIN_EVENT_UPDATE_BUFFER_REGION_COMMAND = 21,
    SWAPCHAIN_EVENT_BIND_RESOURCE = 22,
    SWAPCHAIN_EVENT_MAP_RESOURCE = 23,
    // Additional frame-specific GPU operations for power saving
    SWAPCHAIN_EVENT_COPY_BUFFER_REGION = 24,
    SWAPCHAIN_EVENT_COPY_BUFFER_TO_TEXTURE = 25,
    SWAPCHAIN_EVENT_COPY_TEXTURE_TO_BUFFER = 26,
    SWAPCHAIN_EVENT_COPY_TEXTURE_REGION = 27,
    SWAPCHAIN_EVENT_RESOLVE_TEXTURE_REGION = 28,
    SWAPCHAIN_EVENT_CLEAR_RENDER_TARGET_VIEW = 29,
    SWAPCHAIN_EVENT_CLEAR_DEPTH_STENCIL_VIEW = 30,
    SWAPCHAIN_EVENT_CLEAR_UNORDERED_ACCESS_VIEW_UINT = 31,
    SWAPCHAIN_EVENT_CLEAR_UNORDERED_ACCESS_VIEW_FLOAT = 32,
    SWAPCHAIN_EVENT_GENERATE_MIPMAPS = 33,
    SWAPCHAIN_EVENT_BLIT = 34,
    SWAPCHAIN_EVENT_BEGIN_QUERY = 35,
    SWAPCHAIN_EVENT_END_QUERY = 36,
    SWAPCHAIN_EVENT_RESOLVE_QUERY_DATA = 37
};

// Unsorted TODO: Add in correct order above
extern std::atomic<LONGLONG> g_present_start_time_ns;

// Present pacing delay as percentage of frame time - 0% to 100%



extern std::atomic<LONGLONG> late_amount_ns;

// NVIDIA Reflex minimal controls
extern std::atomic<bool> s_reflex_enable;        // Enable NVIDIA Reflex integration
extern std::atomic<bool> s_reflex_low_latency;   // Low Latency Mode
extern std::atomic<bool> s_reflex_boost;         // Low Latency Boost
extern std::atomic<bool> s_reflex_use_markers;   // Use markers to optimize
extern std::atomic<bool> s_enable_reflex_logging; // Enable Reflex logging

// DLSS-FG Detection state
extern std::atomic<bool> g_dlssfg_detected;

// DLLS-G (DLSS Frame Generation) status
extern std::atomic<bool> g_dlls_g_loaded;        // DLLS-G loaded status
extern std::atomic<std::shared_ptr<const std::string>> g_dlls_g_version; // DLLS-G version string

// DLSS Preset Detection
extern std::atomic<bool> g_dlss_preset_detected; // DLSS preset detection status
extern std::atomic<std::shared_ptr<const std::string>> g_dlss_preset_name; // Current DLSS preset name
extern std::atomic<std::shared_ptr<const std::string>> g_dlss_quality_mode; // Current DLSS quality mode
