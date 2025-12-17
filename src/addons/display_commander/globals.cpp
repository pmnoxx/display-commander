#include "globals.hpp"
#include "background_window.hpp"
#include "dxgi/custom_fps_limiter.hpp"
#include "latency/latency_manager.hpp"
#include "settings/developer_tab_settings.hpp"
#include "settings/experimental_tab_settings.hpp"
#include "settings/main_tab_settings.hpp"
#include "settings/swapchain_tab_settings.hpp"
#include "settings/streamline_tab_settings.hpp"
#include "settings/hotkeys_tab_settings.hpp"
#include "settings/hook_suppression_settings.hpp"
#include "utils.hpp"
#include "utils/general_utils.hpp"
#include "utils/logging.hpp"
#include "utils/srwlock_wrapper.hpp"
#include <algorithm>
#include "../../../external/nvapi/nvapi.h"

#include <d3d11.h>
#include <reshade.hpp>
#include <wrl/client.h>

#include <atomic>
#include <array>

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


// NVAPI Fullscreen Prevention


// Mouse position spoofing for auto-click sequences
std::atomic<bool> s_spoof_mouse_position{false}; // disabled by default
std::atomic<int> s_spoofed_mouse_x{0};
std::atomic<int> s_spoofed_mouse_y{0};

// SetCursor detour - store last cursor value atomically
std::atomic<HCURSOR> s_last_cursor_value{nullptr};

// ShowCursor detour - store last show cursor state atomically
std::atomic<BOOL> s_last_show_cursor_arg{TRUE};

// Keyboard Shortcuts

// Auto-click enabled state (atomic, not loaded from config)
std::atomic<bool> g_auto_click_enabled{false};

// NVAPI Settings
std::atomic<bool> s_restart_needed_nvapi{false};

// Performance: background FPS cap

// VSync and tearing controls

// Monitor and display settings
std::atomic<DxgiBypassMode> s_dxgi_composition_state{DxgiBypassMode::kUnset};

// Continue rendering in background

// DirectInput hook suppression
std::atomic<bool> s_suppress_dinput_hooks{false}; // Disabled by default

// Logging level control (default to Debug = everything logged)
std::atomic<LogLevel> g_min_log_level{LogLevel::Debug};

// Input blocking in background (0.0f off, 1.0f on)

// Render blocking in background

// Present blocking in background


// Hide HDR capabilities from applications

// D3D9 to D3D9Ex upgrade
//std::atomic<bool> s_enable_d3d9e_upgrade{true}; // Enabled by default
std::atomic<bool> s_d3d9e_upgrade_successful{false}; // Track if upgrade was successful
std::atomic<bool> g_used_flipex{false}; // Track if FLIPEX is currently being used

// ReShade runtimes for input blocking (multiple runtimes support)
std::vector<reshade::api::effect_runtime *> g_reshade_runtimes;
SRWLOCK g_reshade_runtimes_lock = SRWLOCK_INIT;

// Prevent always on top behavior

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

// Track if resolution was successfully applied at least once
std::atomic<bool> s_resolution_applied_at_least_once{false}; // Disabled by default

// Atomic variables
std::atomic<int> g_comp_query_counter{0};
std::atomic<DxgiBypassMode> g_comp_last_logged{DxgiBypassMode::kUnset};
std::atomic<void*> g_last_swapchain_ptr_unsafe{nullptr}; // TODO: unsafe remove later
std::atomic<int> g_last_reshade_device_api{0};
std::atomic<uint32_t> g_last_api_version{0};
std::atomic<std::shared_ptr<reshade::api::swapchain_desc>> g_last_swapchain_desc{nullptr};
std::atomic<uint64_t> g_init_apply_generation{0};
std::atomic<HWND> g_last_swapchain_hwnd{nullptr};
std::atomic<bool> g_shutdown{false};
std::atomic<bool> g_muted_applied{false};

// Continuous monitoring system
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

// Global frame ID for latency management
std::atomic<uint64_t> g_global_frame_id{0};

// Global frame ID for UI drawing tracking
std::atomic<uint64_t> g_last_ui_drawn_frame_id{0};

// Global frame ID when XInput was last successfully detected
std::atomic<uint64_t> g_last_xinput_detected_frame_id{0};

// Global Swapchain Tracking Manager instance
SwapchainTrackingManager g_swapchainTrackingManager;

// Backbuffer dimensions
std::atomic<int> g_last_backbuffer_width{0};
std::atomic<int> g_last_backbuffer_height{0};
// Background/foreground state (updated by monitoring thread)
std::atomic<bool> g_app_in_background{false};

// FPS limiter mode: 0 = Disabled, 1 = Reflex, 2 = OnPresentSync, 3 = OnPresentSyncLowLatency, 4 = VBlank Scanline Sync (VBlank)
std::atomic<FpsLimiterMode> s_fps_limiter_mode{FpsLimiterMode::kDisabled};

// FPS limiter injection timing: 0 = Default (Direct DX9/10/11/12), 1 = Fallback(1) (Through ReShade), 2 = Fallback(2) (Through ReShade)

// Scanline offset

// VBlank Sync Divisor (like VSync /2 /3 /4) - 0 to 8, default 1 (0 = off)

// Performance stats (FPS/frametime) shared state
std::atomic<uint32_t> g_perf_ring_head{0};
PerfSample g_perf_ring[kPerfRingCapacity] = {};
std::atomic<double> g_perf_time_seconds{0.0};
std::atomic<bool> g_perf_reset_requested{false};
std::atomic<std::shared_ptr<const std::string>> g_perf_text_shared{std::make_shared<const std::string>("")};

// Volume overlay display tracking
std::atomic<LONGLONG> g_volume_change_time_ns{0};
std::atomic<float> g_volume_display_value{0.0f};

// Action notification system for overlay display
std::atomic<ActionNotification> g_action_notification{ActionNotification{ActionNotificationType::None, 0, 0.0f, false, {0}}};

// Vector variables
std::atomic<std::shared_ptr<const std::vector<MonitorInfo>>> g_monitors{std::make_shared<std::vector<MonitorInfo>>()};

// Colorspace variables - removed, now queried directly in UI

// HDR10 override status (thread-safe, updated by background thread, read by UI thread)
// Use UpdateHdr10OverrideStatus() to update, or g_hdr10_override_status.load()->c_str() to read
std::atomic<std::shared_ptr<const std::string>> g_hdr10_override_status{std::make_shared<std::string>("Not applied")};

// HDR10 override timestamp (thread-safe, updated by background thread, read by UI thread)
// Use UpdateHdr10OverrideTimestamp() to update, or g_hdr10_override_timestamp.load()->c_str() to read
std::atomic<std::shared_ptr<const std::string>> g_hdr10_override_timestamp{std::make_shared<std::string>("Never")};

// Config save failure path (thread-safe, updated by config manager, read by UI thread)
std::atomic<std::shared_ptr<const std::string>> g_config_save_failure_path{nullptr};

// Monitor labels cache removed - UI now uses GetDisplayInfoForUI() directly for better reliability

// Keyboard Shortcut Settings (moved to earlier in file)

// Performance optimization settings
std::atomic<LONGLONG> g_flush_before_present_time_ns{0};

// Stopwatch state
std::atomic<bool> g_stopwatch_running{false};
std::atomic<LONGLONG> g_stopwatch_start_time_ns{0};
std::atomic<LONGLONG> g_stopwatch_elapsed_time_ns{0};

// Game playtime tracking (time from game start)
std::atomic<LONGLONG> g_game_start_time_ns{0};

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

// Helper function to get flip state based on API type
DxgiBypassMode GetFlipStateForAPI(int api) {
    // For D3D9, use FlipEx state instead of DXGI composition state
    if (api == static_cast<int>(reshade::api::device_api::d3d9)) {
        bool using_flipex = g_used_flipex.load();
        return using_flipex ? DxgiBypassMode::kIndependentFlip : DxgiBypassMode::kComposed;
    } else {
        return s_dxgi_composition_state.load();
    }
}

// Swapchain event counters - reset on each swapchain creation
// Separate event counter arrays for each category
std::array<std::atomic<uint32_t>, NUM_RESHADE_EVENTS> g_reshade_event_counters = {};
std::array<std::atomic<uint32_t>, NUM_DXGI_CORE_EVENTS> g_dxgi_core_event_counters = {};
std::array<std::atomic<uint32_t>, NUM_DXGI_SC1_EVENTS> g_dxgi_sc1_event_counters = {};
std::array<std::atomic<uint32_t>, NUM_DXGI_SC2_EVENTS> g_dxgi_sc2_event_counters = {};
std::array<std::atomic<uint32_t>, NUM_DXGI_SC3_EVENTS> g_dxgi_sc3_event_counters = {};
std::array<std::atomic<uint32_t>, NUM_DXGI_FACTORY_EVENTS> g_dxgi_factory_event_counters = {};
std::array<std::atomic<uint32_t>, NUM_DXGI_SC4_EVENTS> g_dxgi_sc4_event_counters = {};
std::array<std::atomic<uint32_t>, NUM_DXGI_OUTPUT_EVENTS> g_dxgi_output_event_counters = {};
std::array<std::atomic<uint32_t>, NUM_DX9_EVENTS> g_dx9_event_counters = {};
std::array<std::atomic<uint32_t>, NUM_STREAMLINE_EVENTS> g_streamline_event_counters = {};
std::array<std::atomic<uint32_t>, NUM_D3D11_TEXTURE_EVENTS> g_d3d11_texture_event_counters = {};
std::array<std::atomic<uint32_t>, NUM_D3D_SAMPLER_EVENTS> g_d3d_sampler_event_counters = {};
std::array<std::atomic<uint32_t>, NUM_SAMPLER_FILTER_MODES> g_sampler_filter_mode_counters = {};
std::array<std::atomic<uint32_t>, NUM_SAMPLER_ADDRESS_MODES> g_sampler_address_mode_counters = {};
std::array<std::atomic<uint32_t>, MAX_ANISOTROPY_LEVELS> g_sampler_anisotropy_level_counters = {};

// NVAPI event counters - separate from swapchain events
std::array<std::atomic<uint32_t>, NUM_NVAPI_EVENTS> g_nvapi_event_counters = {}; // Array for NVAPI events

// NVAPI sleep timestamp tracking
std::atomic<uint64_t> g_nvapi_last_sleep_timestamp_ns{0}; // Last NVAPI_D3D_Sleep call timestamp in nanoseconds
std::atomic<bool> g_native_reflex_detected{false}; // Native Reflex detected via SetLatencyMarker calls

std::atomic<uint32_t> g_swapchain_event_total_count{0}; // Total events across all types

// OpenGL hook counters
std::array<std::atomic<uint64_t>, NUM_OPENGL_HOOKS> g_opengl_hook_counters = {}; // Array for all OpenGL hook events
std::atomic<uint64_t> g_opengl_hook_total_count{0}; // Total OpenGL hook events across all types

// Display settings hook counters
std::array<std::atomic<uint64_t>, NUM_DISPLAY_SETTINGS_HOOKS> g_display_settings_hook_counters = {}; // Array for all display settings hook events
std::atomic<uint64_t> g_display_settings_hook_total_count{0}; // Total display settings hook events across all types

// Present pacing delay as percentage of frame time - 0% to 100%
// This adds a delay after present to improve frame pacing and reduce CPU usage
// Higher values create more consistent frame timing but may increase latency
// 0% = no delay, 100% = full frame time delay between simulation start and present

std::atomic<LONGLONG> late_amount_ns{0};

// GPU completion measurement using EnqueueSetEvent
std::atomic<HANDLE> g_gpu_completion_event{nullptr};  // Event handle for GPU completion measurement
std::atomic<LONGLONG> g_gpu_completion_time_ns{0};  // Last measured GPU completion time
std::atomic<LONGLONG> g_gpu_duration_ns{0};  // Last measured GPU duration (smoothed)

// GPU completion failure tracking
std::atomic<const char*> g_gpu_fence_failure_reason{nullptr};  // Reason why GPU fence creation/usage failed (nullptr if no failure)

// Sim-start-to-display latency measurement
std::atomic<LONGLONG> g_sim_start_ns_for_measurement{0};  // g_sim_start_ns captured when EnqueueGPUCompletion is called
std::atomic<bool> g_present_update_after2_called{false};  // Tracks if OnPresentUpdateAfter2 was called
std::atomic<bool> g_gpu_completion_callback_finished{false};  // Tracks if GPU completion callback finished
std::atomic<LONGLONG> g_sim_to_display_latency_ns{0};  // Measured sim-start-to-display latency (smoothed)

// GPU late time measurement (how much later GPU finishes compared to OnPresentUpdateAfter2)
std::atomic<LONGLONG> g_present_update_after2_time_ns{0};  // Time when OnPresentUpdateAfter2 was called
std::atomic<LONGLONG> g_gpu_completion_callback_time_ns{0};  // Time when GPU completion callback finished
std::atomic<LONGLONG> g_gpu_late_time_ns{0};  // GPU late time (0 if GPU finished first, otherwise difference)

// NVIDIA Reflex minimal controls (disabled by default)


// DLSS-G (DLSS Frame Generation) status
std::atomic<bool> g_dlss_g_loaded{false};
std::atomic<std::shared_ptr<const std::string>> g_dlss_g_version{std::make_shared<const std::string>("Unknown")};

// NGX Feature status tracking (set in CreateFeature detours)
std::atomic<bool> g_dlss_enabled{false};          // DLSS Super Resolution enabled
std::atomic<bool> g_dlssg_enabled{false};         // DLSS Frame Generation enabled
std::atomic<bool> g_ray_reconstruction_enabled{false}; // Ray Reconstruction enabled

// NVAPI SetSleepMode tracking
std::atomic<std::shared_ptr<NV_SET_SLEEP_MODE_PARAMS>> g_last_nvapi_sleep_mode_params{nullptr};
std::atomic<IUnknown*> g_last_nvapi_sleep_mode_dev_ptr{nullptr};

// NVAPI Reflex timing tracking
std::atomic<LONGLONG> g_sleep_reflex_injected_ns{0};
std::atomic<LONGLONG> g_sleep_reflex_native_ns{0};
std::atomic<LONGLONG> g_sleep_reflex_native_ns_smooth{0};
std::atomic<LONGLONG> g_sleep_reflex_injected_ns_smooth{0};

// Reflex debug counters
std::atomic<uint32_t> g_reflex_sleep_count{0};
std::atomic<uint32_t> g_reflex_apply_sleep_mode_count{0};
std::atomic<LONGLONG> g_reflex_sleep_duration_ns{0};

// Individual marker type counters
std::atomic<uint32_t> g_reflex_marker_simulation_start_count{0};
std::atomic<uint32_t> g_reflex_marker_simulation_end_count{0};
std::atomic<uint32_t> g_reflex_marker_rendersubmit_start_count{0};
std::atomic<uint32_t> g_reflex_marker_rendersubmit_end_count{0};
std::atomic<uint32_t> g_reflex_marker_present_start_count{0};
std::atomic<uint32_t> g_reflex_marker_present_end_count{0};
std::atomic<uint32_t> g_reflex_marker_input_sample_count{0};

// DX11 Proxy HWND for filtering
HWND g_proxy_hwnd = nullptr;


// Experimental tab settings global instance
namespace settings {
ExperimentalTabSettings g_experimentalTabSettings;
DeveloperTabSettings g_developerTabSettings;
MainTabSettings g_mainTabSettings;
SwapchainTabSettings g_swapchainTabSettings;
StreamlineTabSettings g_streamlineTabSettings;
HotkeysTabSettings g_hotkeysTabSettings;
HookSuppressionSettings g_hook_suppression_settings;
// Function to load all settings at startup
void LoadAllSettingsAtStartup() {
    g_developerTabSettings.LoadAll();
    g_experimentalTabSettings.LoadAll();
    g_mainTabSettings.LoadSettings();
    g_swapchainTabSettings.LoadAll();
    g_streamlineTabSettings.LoadAll();
    g_hotkeysTabSettings.LoadAll();
    g_hook_suppression_settings.LoadAll();
    LogInfo("All settings loaded at startup");
}

} // namespace settings

// Fake NVAPI manager global instance
nvapi::FakeNvapiManager g_fakeNvapiManager;

// NGX Parameter Storage global instance
UnifiedParameterMap g_ngx_parameters;
UnifiedParameterMap g_ngx_parameter_overrides;
std::atomic<NVSDK_NGX_Parameter*> g_last_ngx_parameter{nullptr};

// NGX Counters global instance
NGXCounters g_ngx_counters;

// Get DLSS/DLSS-G summary from NGX parameters
DLSSGSummary GetDLSSGSummary() {
    DLSSGSummary summary;

    // Use the new global tracking variables for more accurate status
    summary.dlss_active = g_dlss_enabled.load();
    summary.dlss_g_active = g_dlssg_enabled.load();
    summary.ray_reconstruction_active = g_ray_reconstruction_enabled.load();

    // Get resolutions - using correct parameter names
    unsigned int internal_width, internal_height, output_width, output_height;
    bool has_internal_width = g_ngx_parameters.get_as_uint("DLSS.Render.Subrect.Dimensions.Width", internal_width);
    bool has_internal_height = g_ngx_parameters.get_as_uint("DLSS.Render.Subrect.Dimensions.Height", internal_height);
    bool has_output_width = g_ngx_parameters.get_as_uint("OutWidth", output_width);
    bool has_output_height = g_ngx_parameters.get_as_uint("OutHeight", output_height);

    if (has_internal_width && has_internal_height) {
        summary.internal_resolution = std::to_string(internal_width) + "x" + std::to_string(internal_height);
    }
    if (has_output_width && has_output_height) {
        summary.output_resolution = std::to_string(output_width) + "x" + std::to_string(output_height);
    }

    // Calculate scaling ratio
    if (has_internal_width && has_internal_height && has_output_width && has_output_height &&
        internal_width > 0 && internal_height > 0) {
        float scale_x = static_cast<float>(output_width) / internal_width;
        char buffer[32];
        snprintf(buffer, sizeof(buffer), "%.2fx", scale_x);
        summary.scaling_ratio = std::string(buffer);
    }

    // Get quality preset based on PerfQualityValue (like Special-K does)
    unsigned int perf_quality;
    if (g_ngx_parameters.get_as_uint("PerfQualityValue", perf_quality)) {
        switch (perf_quality) {
            case 0: // NVSDK_NGX_PerfQuality_Value_MaxPerf
                summary.quality_preset = "Performance";
                break;
            case 1: // NVSDK_NGX_PerfQuality_Value_Balanced
                summary.quality_preset = "Balanced";
                break;
            case 2: // NVSDK_NGX_PerfQuality_Value_MaxQuality
                summary.quality_preset = "Quality";
                break;
            case 3: // NVSDK_NGX_PerfQuality_Value_UltraPerformance
                summary.quality_preset = "Ultra Performance";
                break;
            case 4: // NVSDK_NGX_PerfQuality_Value_UltraQuality
                summary.quality_preset = "Ultra Quality";
                break;
            case 5: // NVSDK_NGX_PerfQuality_Value_DLAA
                summary.quality_preset = "DLAA";
                break;
            default:
                summary.quality_preset = "Unknown";
                break;
        }
    }

    // Get camera information
    float aspect_ratio;
    if (g_ngx_parameters.get_as_float("DLSSG.CameraAspectRatio", aspect_ratio)) {
        char buffer[32];
        snprintf(buffer, sizeof(buffer), "%.4f", aspect_ratio);
        summary.aspect_ratio = std::string(buffer);
    }

    float fov;
    if (g_ngx_parameters.get_as_float("DLSSG.CameraFOV", fov)) {
        char buffer[32];
        snprintf(buffer, sizeof(buffer), "%.4f", fov);
        summary.fov = std::string(buffer);
    }

    // Get jitter offset
    float jitter_x, jitter_y;
    bool has_jitter_x = g_ngx_parameters.get_as_float("DLSSG.JitterOffsetX", jitter_x);
    bool has_jitter_y = g_ngx_parameters.get_as_float("DLSSG.JitterOffsetY", jitter_y);
    if (!has_jitter_x) {
        has_jitter_x = g_ngx_parameters.get_as_float("Jitter.Offset.X", jitter_x);
    }
    if (!has_jitter_y) {
        has_jitter_y = g_ngx_parameters.get_as_float("Jitter.Offset.Y", jitter_y);
    }
    if (has_jitter_x && has_jitter_y) {
        char buffer[64];
        snprintf(buffer, sizeof(buffer), "%.4f, %.4f", jitter_x, jitter_y);
        summary.jitter_offset = std::string(buffer);
    }

    // Get exposure information
    float pre_exposure, exposure_scale;
    bool has_pre_exposure = g_ngx_parameters.get_as_float("DLSS.Pre.Exposure", pre_exposure);
    bool has_exposure_scale = g_ngx_parameters.get_as_float("DLSS.Exposure.Scale", exposure_scale);
    if (has_pre_exposure && has_exposure_scale) {
        char buffer[64];
        snprintf(buffer, sizeof(buffer), "Pre: %.2f, Scale: %.2f", pre_exposure, exposure_scale);
        summary.exposure = std::string(buffer);
    }

    // Get depth inversion status
    int depth_inverted;
    if (g_ngx_parameters.get_as_int("DLSSG.DepthInverted", depth_inverted)) {
        summary.depth_inverted = (depth_inverted == 1) ? "Yes" : "No";
    }

    // Get HDR status
    int hdr_enabled;
    if (g_ngx_parameters.get_as_int("DLSSG.ColorBuffersHDR", hdr_enabled)) {
        summary.hdr_enabled = (hdr_enabled == 1) ? "Yes" : "No";
    }

    // Get motion vectors status
    int motion_included;
    if (g_ngx_parameters.get_as_int("DLSSG.CameraMotionIncluded", motion_included)) {
        summary.motion_vectors_included = (motion_included == 1) ? "Yes" : "No";
    }

    // Get frame time delta
    float frame_time;
    if (g_ngx_parameters.get_as_float("FrameTimeDeltaInMsec", frame_time)) {
        char buffer[32];
        snprintf(buffer, sizeof(buffer), "%.2f ms", frame_time);
        summary.frame_time_delta = std::string(buffer);
    }

    // Get sharpness
    float sharpness;
    if (g_ngx_parameters.get_as_float("Sharpness", sharpness)) {
        char buffer[32];
        snprintf(buffer, sizeof(buffer), "%.3f", sharpness);
        summary.sharpness = std::string(buffer);
    }

    // Get tonemapper type
    unsigned int tonemapper;
    if (g_ngx_parameters.get_as_uint("TonemapperType", tonemapper)) {
        summary.tonemapper_type = std::to_string(tonemapper);
    }

    // Get DLSS-G frame generation mode
    int enable_interp;
    if (g_ngx_parameters.get_as_int("DLSSG.EnableInterp", enable_interp)) {
        if (enable_interp == 1) {
            // DLSS-G is enabled, check MultiFrameCount for mode
            unsigned int multi_frame_count;
            if (g_ngx_parameters.get_as_uint("DLSSG.MultiFrameCount", multi_frame_count)) {
                if (multi_frame_count == 1) {
                    summary.fg_mode = "2x";
                } else if (multi_frame_count == 2) {
                    summary.fg_mode = "3x";
                } else if (multi_frame_count == 3) {
                    summary.fg_mode = "4x";
                } else {
                    char buffer[16];
                    snprintf(buffer, sizeof(buffer), "%dx", multi_frame_count + 1);
                    summary.fg_mode = std::string(buffer);
                }
            } else {
                summary.fg_mode = "Active (mode unknown)";
            }
        } else {
            summary.fg_mode = "Disabled";
        }
    } else {
        summary.fg_mode = "Unknown";
    }

    // Get NVIDIA Optical Flow Accelerator (OFA) status
    int ofa_enabled;
    if (g_ngx_parameters.get_as_int("Enable.OFA", ofa_enabled)) {
        summary.ofa_enabled = (ofa_enabled == 1) ? "Yes" : "No";
    }

    // Get DLL versions for DLSS and DLSS-G
    // Check for nvngx_dlss.dll (DLSS Super FResolution)
    HMODULE dlss_handle = GetModuleHandleW(L"nvngx_dlss.dll");
    if (dlss_handle != nullptr) {
        wchar_t dlss_path[MAX_PATH];
        DWORD path_length = GetModuleFileNameW(dlss_handle, dlss_path, MAX_PATH);
        if (path_length > 0) {
            summary.dlss_dll_version = GetDLLVersionString(std::wstring(dlss_path));
        } else {
            summary.dlss_dll_version = "Loaded (path unknown)";
        }
    } else {
        summary.dlss_dll_version = "Not loaded";
    }

    // Check for nvngx_dlssg.dll (DLSS Frame Generation)
    HMODULE dlssg_handle = GetModuleHandleW(L"nvngx_dlssg.dll");
    if (dlssg_handle != nullptr) {
        wchar_t dlssg_path[MAX_PATH];
        DWORD path_length = GetModuleFileNameW(dlssg_handle, dlssg_path, MAX_PATH);
        if (path_length > 0) {
            summary.dlssg_dll_version = GetDLLVersionString(std::wstring(dlssg_path));
        } else {
            summary.dlssg_dll_version = "Loaded (path unknown)";
        }
    } else {
        summary.dlssg_dll_version = "Not loaded";
    }

    // Check for nvngx_dlssd.dll (DLSS Denoising)
    HMODULE dlssd_handle = GetModuleHandleW(L"nvngx_dlssd.dll");
    if (dlssd_handle != nullptr) {
        wchar_t dlssd_path[MAX_PATH];
        DWORD path_length = GetModuleFileNameW(dlssd_handle, dlssd_path, MAX_PATH);
        if (path_length > 0) {
            summary.dlssd_dll_version = GetDLLVersionString(std::wstring(dlssd_path));
        } else {
            summary.dlssd_dll_version = "Loaded (path unknown)";
        }
    } else {
        summary.dlssd_dll_version = "Not loaded";
    }

    // Determine supported DLSS SR presets based on DLSS DLL version
    summary.supported_dlss_presets = GetSupportedDLSSSRPresetsFromVersionString(summary.dlss_dll_version);

    // Determine supported DLSS RR presets based on DLSS DLL version
    summary.supported_dlss_rr_presets = GetSupportedDLSSRRPresetsFromVersionString(summary.dlss_dll_version);

    return summary;
}

// Helper functions for ReShade runtime management
void AddReShadeRuntime(reshade::api::effect_runtime* runtime) {
    if (runtime == nullptr) {
        return;
    }

    utils::SRWLockExclusive lock(g_reshade_runtimes_lock);

    // Check if runtime is already in the vector
    auto it = std::find(g_reshade_runtimes.begin(), g_reshade_runtimes.end(), runtime);
    if (it == g_reshade_runtimes.end()) {
        g_reshade_runtimes.push_back(runtime);
        LogInfo("Added ReShade runtime to vector - Total runtimes: %zu", g_reshade_runtimes.size());
    }
}

void RemoveReShadeRuntime(reshade::api::effect_runtime* runtime) {
    if (runtime == nullptr) {
        return;
    }

    utils::SRWLockExclusive lock(g_reshade_runtimes_lock);

    auto it = std::find(g_reshade_runtimes.begin(), g_reshade_runtimes.end(), runtime);
    if (it != g_reshade_runtimes.end()) {
        g_reshade_runtimes.erase(it);
        LogInfo("Removed ReShade runtime from vector - Total runtimes: %zu", g_reshade_runtimes.size());
    }
}

reshade::api::effect_runtime* GetFirstReShadeRuntime() {
    utils::SRWLockShared lock(g_reshade_runtimes_lock);

    if (g_reshade_runtimes.empty()) {
        return nullptr;
    }

    return g_reshade_runtimes[0];
}

std::vector<reshade::api::effect_runtime*> GetAllReShadeRuntimes() {
    utils::SRWLockShared lock(g_reshade_runtimes_lock);
    return g_reshade_runtimes;
}

size_t GetReShadeRuntimeCount() {
    utils::SRWLockShared lock(g_reshade_runtimes_lock);
    return g_reshade_runtimes.size();
}

// NGX preset initialization tracking
std::atomic<bool> g_ngx_presets_initialized{false};

// Swapchain wrapper statistics
SwapChainWrapperStats g_swapchain_wrapper_stats_proxy;
SwapChainWrapperStats g_swapchain_wrapper_stats_native;

// Track if DXGISwapChain4Wrapper::Present or Present1 has been called at least once
std::atomic<bool> g_swapchain_wrapper_present_called{false};

// Cached frame statistics (updated in present detour, read by monitoring thread)
std::atomic<std::shared_ptr<DXGI_FRAME_STATISTICS>> g_cached_frame_stats{nullptr};

// Get DLSS Model Profile
DLSSModelProfile GetDLSSModelProfile() {
    DLSSModelProfile profile;

    // Read Super Resolution preset values
    int sr_quality;
    if (g_ngx_parameters.get_as_int("DLSS.Hint.Render.Preset.Quality", sr_quality)) {
        profile.sr_quality_preset = sr_quality;
        profile.is_valid = true;
    }

    int sr_balanced;
    if (g_ngx_parameters.get_as_int("DLSS.Hint.Render.Preset.Balanced", sr_balanced)) {
        profile.sr_balanced_preset = sr_balanced;
    }

    int sr_performance;
    if (g_ngx_parameters.get_as_int("DLSS.Hint.Render.Preset.Performance", sr_performance)) {
        profile.sr_performance_preset = sr_performance;
    }

    int sr_ultra_performance;
    if (g_ngx_parameters.get_as_int("DLSS.Hint.Render.Preset.UltraPerformance", sr_ultra_performance)) {
        profile.sr_ultra_performance_preset = sr_ultra_performance;
    }

    int sr_ultra_quality;
    if (g_ngx_parameters.get_as_int("DLSS.Hint.Render.Preset.UltraQuality", sr_ultra_quality)) {
        profile.sr_ultra_quality_preset = sr_ultra_quality;
    }

    int sr_dlaa;
    if (g_ngx_parameters.get_as_int("DLSS.Hint.Render.Preset.DLAA", sr_dlaa)) {
        profile.sr_dlaa_preset = sr_dlaa;
    }

    // Read Ray Reconstruction preset values
    int rr_quality;
    if (g_ngx_parameters.get_as_int("RayReconstruction.Hint.Render.Preset.Quality", rr_quality)) {
        profile.rr_quality_preset = rr_quality;
    }

    int rr_balanced;
    if (g_ngx_parameters.get_as_int("RayReconstruction.Hint.Render.Preset.Balanced", rr_balanced)) {
        profile.rr_balanced_preset = rr_balanced;
    }

    int rr_performance;
    if (g_ngx_parameters.get_as_int("RayReconstruction.Hint.Render.Preset.Performance", rr_performance)) {
        profile.rr_performance_preset = rr_performance;
    }

    int rr_ultra_performance;
    if (g_ngx_parameters.get_as_int("RayReconstruction.Hint.Render.Preset.UltraPerformance", rr_ultra_performance)) {
        profile.rr_ultra_performance_preset = rr_ultra_performance;
    }

    int rr_ultra_quality;
    if (g_ngx_parameters.get_as_int("RayReconstruction.Hint.Render.Preset.UltraQuality", rr_ultra_quality)) {
        profile.rr_ultra_quality_preset = rr_ultra_quality;
    }

    return profile;
}