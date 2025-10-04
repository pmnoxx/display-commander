#include "globals.hpp"
#include "background_window.hpp"
#include "dxgi/custom_fps_limiter.hpp"
#include "latency/latency_manager.hpp"
#include "settings/developer_tab_settings.hpp"
#include "settings/experimental_tab_settings.hpp"
#include "settings/main_tab_settings.hpp"
#include "utils.hpp"

#include <d3d11.h>
#include <reshade.hpp>
#include <wrl/client.h>

#include <atomic>

// Global variables
// UI mode removed - now using new tab system

// DLL initialization state - prevents DXGI calls during DllMain
std::atomic<bool> g_dll_initialization_complete{false};

// Module handle for pinning/unpinning
HMODULE g_hmodule = nullptr;

// Shared DXGI factory to avoid redundant CreateDXGIFactory calls
std::atomic<Microsoft::WRL::ComPtr<IDXGIFactory1> *> g_shared_dxgi_factory{nullptr};

// Window settings
std::atomic<WindowMode> s_window_mode{WindowMode::kFullscreen}; // kFullscreen = Borderless Fullscreen (default),
                                                                // kAspectRatio = Borderless Windowed (Aspect Ratio)

std::atomic<AspectRatioType> s_aspect_index{AspectRatioType::k16_9}; // Default to 16:9
std::atomic<int> s_aspect_width{0}; // 0 = Display Width, 1 = 3840, 2 = 2560, etc.

// Window alignment when repositioning is needed (0 = Center, 1 = Top Left, 2 = Top Right, 3 = Bottom Left, 4 = Bottom
// Right)
std::atomic<WindowAlignment> s_window_alignment{WindowAlignment::kCenter}; // default to center (slot 0)

// Prevent Fullscreen
std::atomic<bool> s_prevent_fullscreen{false};

// NVAPI Fullscreen Prevention
std::atomic<bool> s_nvapi_fullscreen_prevention{false}; // disabled by default
// NVAPI Auto-enable for specific games
std::atomic<bool> s_nvapi_auto_enable{true}; // enabled by default
// NVAPI HDR logging
std::atomic<bool> s_nvapi_hdr_logging{false};
std::atomic<float> s_nvapi_hdr_interval_sec{5.f};

// Spoof Fullscreen State (for applications that query fullscreen status)
std::atomic<SpoofFullscreenState> s_spoof_fullscreen_state{SpoofFullscreenState::Disabled};

// Mouse position spoofing for auto-click sequences
std::atomic<bool> s_spoof_mouse_position{false}; // disabled by default
std::atomic<int> s_spoofed_mouse_x{0};
std::atomic<int> s_spoofed_mouse_y{0};

// Keyboard Shortcuts
std::atomic<bool> s_enable_mute_unmute_shortcut{true};
std::atomic<bool> s_enable_background_toggle_shortcut{true};
std::atomic<bool> s_enable_timeslowdown_shortcut{true};

// Performance: background FPS cap

// VSync and tearing controls

// Monitor and display settings
std::atomic<int> s_dxgi_composition_state{0};

// Continue rendering in background (like Special-K's background render feature)
std::atomic<bool> s_continue_rendering{false}; // Disabled by default

// Input blocking in background (0.0f off, 1.0f on)

// Render blocking in background

// Present blocking in background

// Fix HDR10 color space when backbuffer is RGB10A2
std::atomic<bool> s_nvapi_fix_hdr10_colorspace{false};

// Hide HDR capabilities from applications
std::atomic<bool> s_hide_hdr_capabilities{false};

// ReShade runtime for input blocking
std::atomic<reshade::api::effect_runtime *> g_reshade_runtime = nullptr;

// Prevent always on top behavior
std::atomic<bool> s_prevent_always_on_top{true}; // Prevent games from staying on top by default

// Background feature - show black window behind game when not fullscreen

// Desktop Resolution Override
std::atomic<int> s_selected_monitor_index{0}; // Primary monitor by default

// Display Tab Enhanced Settings
std::atomic<int> s_selected_resolution_index{0};   // Default to first available resolution
std::atomic<int> s_selected_refresh_rate_index{0}; // Default to first available refresh rate

std::atomic<bool> s_initial_auto_selection_done{false}; // Track if we've done initial auto-selection

// Auto-restore resolution on game close
std::atomic<bool> s_auto_restore_resolution_on_close{true}; // Enabled by default

// Auto-apply resolution and refresh rate changes
std::atomic<bool> s_auto_apply_resolution_change{false};   // Disabled by default
std::atomic<bool> s_auto_apply_refresh_rate_change{false}; // Disabled by default

// Apply display settings at game start
std::atomic<bool> s_apply_display_settings_at_start{false}; // Disabled by default

// Atomic variables
std::atomic<int> g_comp_query_counter{0};
std::atomic<int> g_comp_last_logged{0};
// std::atomic<reshade::api::swapchain*> g_last_swapchain_ptr{nullptr};
std::atomic<uint64_t> g_init_apply_generation{0};
std::atomic<HWND> g_last_swapchain_hwnd{nullptr};
std::atomic<bool> g_shutdown{false};
std::atomic<bool> g_muted_applied{false};

// Continuous monitoring system
std::atomic<bool> s_continuous_monitoring_enabled{true}; // Enabled by default
std::atomic<bool> g_monitoring_thread_running{false};
std::thread g_monitoring_thread;

// Render thread tracking
std::atomic<DWORD> g_render_thread_id{0};

// Global window state instance
std::atomic<std::shared_ptr<GlobalWindowState>> g_window_state = std::make_shared<GlobalWindowState>();

// Global background window manager instance
BackgroundWindowManager g_backgroundWindowManager;

// Global Custom FPS Limiter Manager instance
namespace dxgi::fps_limiter {
std::unique_ptr<CustomFpsLimiter> g_customFpsLimiter = std::make_unique<CustomFpsLimiter>();
}

// Global Latent Sync Manager instance
namespace dxgi::latent_sync {
std::unique_ptr<LatentSyncManager> g_latentSyncManager = std::make_unique<LatentSyncManager>();
}

// Global DXGI Device Info Manager instance
// std::unique_ptr<DXGIDeviceInfoManager> g_dxgiDeviceInfoManager = std::make_unique<DXGIDeviceInfoManager>();

// Global Latency Manager instance
std::unique_ptr<LatencyManager> g_latencyManager = std::make_unique<LatencyManager>();

// Direct atomic variables for latency tracking (UI access)
std::atomic<float> g_current_latency_ms{0.0f};
std::atomic<float> g_average_latency_ms{0.0f};
std::atomic<float> g_min_latency_ms{0.0f};
std::atomic<float> g_max_latency_ms{0.0f};
std::atomic<uint64_t> g_current_frame{0};

// DLSS-FG Detection state
std::atomic<bool> g_dlssfg_detected{false};

// Backbuffer dimensions
std::atomic<int> g_last_backbuffer_width{0};
std::atomic<int> g_last_backbuffer_height{0};
// Background/foreground state (updated by monitoring thread)
std::atomic<bool> g_app_in_background{false};

// FPS limiter mode: 0 = Disabled, 1 = OnPresentSync, 2 = OnPresentSyncLowLatency, 3 = VBlank Scanline Sync (VBlank)
std::atomic<FpsLimiterMode> s_fps_limiter_mode{FpsLimiterMode::kDisabled};

// FPS limiter injection timing: 0 = OnPresentFlags (recommended), 1 = OnPresentUpdateBefore2, 2 = OnPresentUpdateBefore

// Scanline offset

// VBlank Sync Divisor (like VSync /2 /3 /4) - 0 to 8, default 1 (0 = off)

// Performance stats (FPS/frametime) shared state
std::atomic<uint32_t> g_perf_ring_head{0};
PerfSample g_perf_ring[kPerfRingCapacity] = {};
std::atomic<double> g_perf_time_seconds{0.0};
std::atomic<bool> g_perf_reset_requested{false};
std::atomic<std::shared_ptr<const std::string>> g_perf_text_shared{std::make_shared<const std::string>("")};

// Vector variables
std::atomic<std::shared_ptr<const std::vector<MonitorInfo>>> g_monitors{std::make_shared<std::vector<MonitorInfo>>()};

// Colorspace variables
reshade::api::color_space g_current_colorspace = reshade::api::color_space::unknown;

// HDR10 override status (thread-safe, updated by background thread, read by UI thread)
// Use UpdateHdr10OverrideStatus() to update, or g_hdr10_override_status.load()->c_str() to read
std::atomic<std::shared_ptr<const std::string>> g_hdr10_override_status{std::make_shared<std::string>("Not applied")};

// HDR10 override timestamp (thread-safe, updated by background thread, read by UI thread)
// Use UpdateHdr10OverrideTimestamp() to update, or g_hdr10_override_timestamp.load()->c_str() to read
std::atomic<std::shared_ptr<const std::string>> g_hdr10_override_timestamp{std::make_shared<std::string>("Never")};

// Monitor labels cache removed - UI now uses GetDisplayInfoForUI() directly for better reliability

// Keyboard Shortcut Settings (moved to earlier in file)

// Performance optimization settings
std::atomic<bool> g_flush_before_present =
    true; // Flush command queue before present to reduce latency (enabled by default)

// Helper function for updating HDR10 override status atomically
void UpdateHdr10OverrideStatus(const std::string &status) {
    g_hdr10_override_status.store(std::make_shared<std::string>(status));
}

// Helper function for updating HDR10 override timestamp atomically
void UpdateHdr10OverrideTimestamp(const std::string &timestamp) {
    g_hdr10_override_timestamp.store(std::make_shared<std::string>(timestamp));
}

// Helper function to get shared DXGI factory (thread-safe)
Microsoft::WRL::ComPtr<IDXGIFactory1> GetSharedDXGIFactory() {
    // Skip DXGI calls during DLL initialization to avoid loader lock violations
    if (!g_dll_initialization_complete.load()) {
        return nullptr;
    }

    // Check if factory already exists
    auto factory_ptr = g_shared_dxgi_factory.load();
    if (factory_ptr && *factory_ptr) {
        return *factory_ptr;
    }

    // Create new factory
    auto new_factory_ptr = std::make_unique<Microsoft::WRL::ComPtr<IDXGIFactory1>>();
    LogInfo("Creating shared DXGI factory");
    HRESULT hr = CreateDXGIFactory1(IID_PPV_ARGS(new_factory_ptr->GetAddressOf()));
    if (FAILED(hr)) {
        LogWarn("Failed to create shared DXGI factory");
        return nullptr;
    }

    // Try to store the new factory atomically
    Microsoft::WRL::ComPtr<IDXGIFactory1> *expected = nullptr;
    if (g_shared_dxgi_factory.compare_exchange_strong(expected, new_factory_ptr.get())) {
        LogInfo("Shared DXGI factory created successfully");
        (void)new_factory_ptr.release(); // Don't delete, it's now managed by the atomic
        return *g_shared_dxgi_factory.load();
    } else {
        // Another thread created the factory first, use the existing one
        return *expected;
    }
}

// Swapchain event counters - reset on each swapchain creation
std::atomic<uint32_t> g_swapchain_event_counters[NUM_EVENTS] = {}; // Array for all On* events

std::atomic<uint32_t> g_swapchain_event_total_count{0}; // Total events across all types

// Present pacing delay as percentage of frame time - 0% to 100%
// This adds a delay after present to improve frame pacing and reduce CPU usage
// Higher values create more consistent frame timing but may increase latency
// 0% = no delay, 100% = full frame time delay between simulation start and present

std::atomic<LONGLONG> late_amount_ns{0};

// NVIDIA Reflex minimal controls (disabled by default)
std::atomic<bool> s_reflex_enable{false};
std::atomic<bool> s_reflex_enable_current_frame{false};
std::atomic<bool> s_reflex_low_latency{false};
std::atomic<bool> s_reflex_boost{false};
std::atomic<bool> s_reflex_use_markers{true};
std::atomic<bool> s_enable_reflex_logging{false}; // Disabled by default

// DLLS-G (DLSS Frame Generation) status
std::atomic<bool> g_dlls_g_loaded{false};
std::atomic<std::shared_ptr<const std::string>> g_dlls_g_version{std::make_shared<const std::string>("Unknown")};

// DLSS Preset Detection
std::atomic<bool> g_dlss_preset_detected{false};
std::atomic<std::shared_ptr<const std::string>> g_dlss_preset_name{std::make_shared<const std::string>("Unknown")};
std::atomic<std::shared_ptr<const std::string>> g_dlss_quality_mode{std::make_shared<const std::string>("Unknown")};

// Experimental tab settings global instance
namespace settings {
ExperimentalTabSettings g_experimentalTabSettings;
DeveloperTabSettings g_developerTabSettings;
MainTabSettings g_mainTabSettings;
} // namespace settings