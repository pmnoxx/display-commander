#include "config/display_commander_config.hpp"
#include "display/display_initial_state.hpp"
#include "features/auto_windows_hdr/auto_windows_hdr.hpp"
#include "features/smooth_motion/smooth_motion.hpp"
#include "display/hdr_control.hpp"
#include "features/presentmon/presentmon_minimal_etw.hpp"
#include "globals.hpp"
#include "hooks/dxgi/dxgi_gpu_completion.hpp"
#include "hooks/dxgi/dxgi_present_hooks.hpp"
#include "hooks/nvidia/ngx_hooks.hpp"
#include "hooks/windows_hooks/window_proc_hooks.hpp"
#include "hooks/windows_hooks/windows_message_hooks.hpp"
#include "latency/gpu_completion_monitoring.hpp"
#include "latency/reflex_provider.hpp"
#include "latent_sync/latent_sync_limiter.hpp"
#include "latent_sync/refresh_rate_monitor_integration.hpp"
#include "modules/module_registry.hpp"
#include "nvapi/reflex_manager.hpp"
#include "performance_types.hpp"
#include "reshade_api_device.hpp"
#include "settings/advanced_tab_settings.hpp"
#include "settings/experimental_tab_settings.hpp"
#include "settings/main_tab_settings.hpp"

#include <d3d11.h>
#include <dxgi.h>
#include "swapchain_events.hpp"
#include "ui/new_ui/new_ui_main.hpp"
#include "utils/d3d9_api_version.hpp"
#include "utils/detour_call_tracker.hpp"
#include "utils/general_utils.hpp"
#include "utils/logging.hpp"
#include "utils/perf_measurement.hpp"
#include "utils/srwlock_wrapper.hpp"
#include "utils/timing.hpp"
#include "window_management/window_management.hpp"

#include <d3d9.h>
#include <dxgi1_4.h>
#include <minwindef.h>

// Forward declaration for VRR status query function (moved to continuous_monitoring.cpp)
// Function is in nvapi namespace, declared in nvapi/vrr_status.hpp

// Forward declaration for effective reflex mode (defined with GetTargetFps / reflex getters)
static OnPresentReflexMode GetEffectiveReflexMode();
static bool GetReflexLowLatency();
static bool GetReflexBoost();
static bool GetReflexSleepEnabled();
static bool GetReflexSendMarkers();

bool IsInjectedReflexEnabled() { return settings::g_mainTabSettings.inject_reflex.GetValue(); }

#include <algorithm>
#include <atomic>
#include <cmath>
#include <set>
#include <sstream>

std::atomic<bool> g_initialized_with_hwnd{false};

// ============================================================================
// D3D9 to D3D9Ex Upgrade Handler
// ============================================================================

bool OnCreateDevice(reshade::api::device_api api, uint32_t& api_version) {
    CALL_GUARD_NO_TS();
    LogInfo("OnCreateDevice: api: %d (%s), api_version: 0x%x", static_cast<int>(api), GetDeviceApiString(api),
            api_version);

    // Only process D3D9 API
    if (api != reshade::api::device_api::d3d9) {
        return false;
    }
    if (!settings::g_experimentalTabSettings.d3d9_flipex_enabled.GetValue()) {
        LogInfo("D3D9 to D3D9Ex upgrade disabled");
        return false;
    }

    // Check if already D3D9Ex
    if (api_version == static_cast<uint32_t>(display_commander::D3D9ApiVersion::D3D9Ex)) {
        LogInfo("D3D9Ex already detected, no upgrade needed");
        s_d3d9e_upgrade_successful.store(true);
        return true;  // Return true to work around ReShade not reporting D3D9Ex; correct API behavior would be return
                      // false
    }

    // Upgrade D3D9 to D3D9Ex
    LogInfo("Upgrading Direct3D 9 (0x%x) to Direct3D 9Ex (0x%x)", api_version,
            static_cast<uint32_t>(display_commander::D3D9ApiVersion::D3D9Ex));
    api_version = static_cast<uint32_t>(display_commander::D3D9ApiVersion::D3D9Ex);
    s_d3d9e_upgrade_successful.store(true);

    return true;
}

void OnInitDevice(reshade::api::device* device) {
    CALL_GUARD_NO_TS();
    LogInfo("OnInitDevice: device: %p", device);
    // Device initialization tracking
    if (device == nullptr) {
        return;
    }
    // Add any initialization logic here if needed
}

void OnDestroyDevice(reshade::api::device* device) {
    CALL_GUARD_NO_TS();
    LogInfo("OnDestroyDevice: device: %p", device);
    if (device == nullptr) {
        return;
    }

    display_commander::features::auto_windows_hdr::OnDestroyDeviceRevertAutoHdrIfNeeded();

    LogInfo("Device destroyed - performing cleanup operations device: %p", device);

    // Clean up NGX handle tracking
    CleanupNGXHooks();

    // Clean up GPU measurement fences
    display_commanderhooks::dxgi::CleanupGPUMeasurementFences();

    // Clean up device-specific resources
    // Note: Most cleanup is handled in DLL_PROCESS_DETACH, but this provides
    // device-specific cleanup when a device is destroyed during runtime

    // Reset any device-specific state (if needed in future)

    // Clean up any device-specific resources that need immediate cleanup
    // (Most resources are cleaned up in DLL_PROCESS_DETACH)

    //     LogInfo("Device cleanup completed");
}

void OnDestroyEffectRuntime(reshade::api::effect_runtime* runtime) {
    CALL_GUARD_NO_TS();
    if (runtime == nullptr) {
        return;
    }

    LogInfo("Effect runtime destroyed - performing cleanup operations runtime: %p", runtime);

    // Remove the runtime from the global runtime vector
    RemoveReShadeRuntime(runtime);
    LogInfo("Removed runtime from global runtime vector");

    // Reset any runtime-specific state
    // Note: Most cleanup is handled in DLL_PROCESS_DETACH, but this provides
    // runtime-specific cleanup when a runtime is destroyed during runtime

    LogInfo("Effect runtime cleanup completed");
}

void hookToSwapChain(reshade::api::swapchain* swapchain) {
    CALL_GUARD_NO_TS();
    HWND hwnd = static_cast<HWND>(swapchain->get_hwnd());
    if (hwnd == g_proxy_hwnd) {
        return;
    }
    static std::set<reshade::api::swapchain*> hooked_swapchains;

    static reshade::api::swapchain* last_swapchain = nullptr;
    if (last_swapchain == swapchain || swapchain == nullptr || swapchain->get_hwnd() == nullptr) {
        return;
    }
    if (hooked_swapchains.find(swapchain) != hooked_swapchains.end()) {
        return;
    }
    hooked_swapchains.insert(swapchain);
    last_swapchain = swapchain;

    LogInfo("[hookToSwapChain] swapchain: 0x%p", swapchain);

    // Store the current swapchain for UI access
    g_last_reshade_device_api.store(swapchain->get_device()->get_api());
    // Query and store API version/feature level
    uint32_t api_version = 0;
    if (swapchain->get_device()->get_property(reshade::api::device_properties::api_version, &api_version)) {
        g_last_api_version.store(api_version);
        LogInfo("[hookToSwapChain] Device API version/feature level: 0x%x", api_version);
    }

    // Schedule auto-apply even on resizes (generation counter ensures only latest
    // runs)
    if (hwnd == nullptr) {
        return;
    }
    g_last_swapchain_hwnd.store(hwnd);

    // Initialize if not already done
    DoInitializationWithHwnd(hwnd);

    auto api = swapchain->get_device()->get_api();

    // Hook DXGI Present calls for this swapchain
    // Get the underlying DXGI swapchain from the ReShade swapchain

    if (api == reshade::api::device_api::d3d10 || api == reshade::api::device_api::d3d11
        || api == reshade::api::device_api::d3d12) {
        auto* iunknown = reinterpret_cast<IUnknown*>(swapchain->get_native());

        Microsoft::WRL::ComPtr<IDXGISwapChain> dxgi_swapchain{};
        if (SUCCEEDED(iunknown->QueryInterface(IID_PPV_ARGS(&dxgi_swapchain)))) {
            if (display_commanderhooks::dxgi::HookSwapchain(dxgi_swapchain.Get())) {
                LogInfo("[hookToSwapChain] Successfully hooked DXGI Present calls for swapchain: 0x%p", iunknown);
            }
        } else {
            LogError("[hookToSwapChain] Failed to query interface for swapchain: 0x%p", iunknown);
        }
        return;
    }
    // Try to hook DX9 Present calls if this is a DX9 device
    // Get the underlying DX9 device from the ReShade device
    else if (api == reshade::api::device_api::d3d9) {
    } else if (api == reshade::api::device_api::vulkan) {
        LogInfo("[hookToSwapChain] Vulkan API detected, not supported yet");
    } else {
        LogError("[hookToSwapChain] Unsupported API: %d", api);
    }
}

// Centralized initialization method
void DoInitializationWithHwnd(HWND hwnd) {
    LogInfo("[DoInitializationWithHwnd] entry HWND: 0x%p", hwnd);
    bool expected = false;
    if (!g_initialized_with_hwnd.compare_exchange_strong(expected, true)) {
        LogInfo("[DoInit] already initialized, returning");
        return;  // Already initialized
    }

    // Initialize display cache
    LogInfo("[DoInitializationWithHwnd] before display_cache::Initialize");
    display_cache::g_displayCache.Initialize();

    // Capture initial display state for restoration
    LogInfo("[DoInitializationWithHwnd] before CaptureInitialState");
    display_initial_state::g_initialDisplayState.CaptureInitialState();

    // Initialize UI system (module registry runs inside; Controller module initializes input remapping)
    LogInfo("[DoInitializationWithHwnd] before InitializeNewUISystem");
    ui::new_ui::InitializeNewUISystem();
    LogInfo("[DoInitializationWithHwnd] before StartContinuousMonitoring");
    StartContinuousMonitoring();
    LogInfo("[DoInitializationWithHwnd] before StartGPUCompletionMonitoring");
    StartGPUCompletionMonitoring();
    LogInfo("[DoInitializationWithHwnd] after StartGPUCompletionMonitoring");

    // Initialize refresh rate monitoring
    LogInfo("[DoInitializationWithHwnd] before StartRefreshRateMonitoring");
    dxgi::fps_limiter::StartRefreshRateMonitoring();

    // Set up window hooks if we have a valid HWND
    if (hwnd != nullptr && IsWindow(hwnd)) {
        LogInfo("[DoInitializationWithHwnd] before InstallWindowProcHooks");

        // Install window procedure hooks (this also sets the game window)
        if (display_commanderhooks::InstallWindowProcHooks(hwnd)) {
            LogInfo("[DoInitializationWithHwnd]: Window procedure hooks installed successfully");
        } else {
            LogError("[DoInitializationWithHwnd]: Failed to install window procedure hooks");
        }

        // Save the display device ID for the game window
        LogInfo("[DoInitializationWithHwnd] before SaveGameWindowDisplayDeviceId");
        settings::SaveGameWindowDisplayDeviceId(hwnd);
    }

    LogInfo("[DoInitializationWithHwnd]: Initialization completed");

    // Retry XInput hooks for any XInput DL L already loaded (e.g. before first present)
    //LogInfo("[DoInitializationWithHwnd] before Controller module XInput hook retry");
   // modules::controller::RetryInstallXInputHooksIfEnabled();
   // LogInfo("[DoInitializationWithHwnd] after Controller module XInput hook retry");

    // Initialize keyboard tracking system
    LogInfo("[DoInitializationWithHwnd] before keyboard_tracker::Initialize");
    display_commanderhooks::keyboard_tracker::Initialize();
    LogInfo("[DoInitializationWithHwnd]: Keyboard tracking system initialized");
    LogInfo("[DoInitializationWithHwnd] after keyboard_tracker::Initialize");

    LogInfo("[DoInitializationWithHwnd] exit");
}

std::atomic<LONGLONG> g_present_start_time_ns{0};
std::atomic<LONGLONG> g_present_duration_ns{0};

// Render start time tracking
std::atomic<LONGLONG> g_submit_start_time_ns{0};

// Present after end time tracking
std::atomic<LONGLONG> g_frame_time_ns{0};
std::atomic<LONGLONG> g_sim_start_ns{0};

// Simulation duration tracking
std::atomic<LONGLONG> g_simulation_duration_ns{0};

// FPS limiter start duration tracking (nanoseconds)
std::atomic<LONGLONG> fps_sleep_before_on_present_ns{0};

// FPS limiter start duration tracking (nanoseconds)
std::atomic<LONGLONG> fps_sleep_after_on_present_ns{0};

// Reshade overhead tracking (nanoseconds)
std::atomic<LONGLONG> g_reshade_overhead_duration_ns{0};

// Render submit end time tracking (QPC)
std::atomic<LONGLONG> g_render_submit_end_time_ns{0};

// Render submit duration tracking (nanoseconds)
std::atomic<LONGLONG> g_render_submit_duration_ns{0};

namespace {

constexpr LONGLONG k_fps_limiter_late_frames_bucket_width_ns = 100 * utils::NS_TO_MS;
constexpr uint32_t k_fps_limiter_late_frames_bucket_count = 300;
constexpr LONGLONG k_fps_limiter_late_frames_visible_window_seconds = 5;

struct FpsLimiterLateFramesBucket {
    LONGLONG bucket_id = -1;
    uint32_t frames_count = 0;
    uint32_t late_frames_count = 0;
};

FpsLimiterLateFramesBucket g_fps_limiter_late_frames_buckets[k_fps_limiter_late_frames_bucket_count];
SRWLOCK g_fps_limiter_late_frames_lock = SRWLOCK_INIT;

void RecordFpsLimiterLateFrameSample(const LONGLONG frame_start_ns, const bool was_late) {
    if (frame_start_ns <= 0) {
        return;
    }

    const LONGLONG bucket_id = frame_start_ns / k_fps_limiter_late_frames_bucket_width_ns;
    if (bucket_id < 0) {
        return;
    }
    const uint32_t slot = static_cast<uint32_t>(bucket_id % k_fps_limiter_late_frames_bucket_count);

    utils::SRWLockExclusive lock(g_fps_limiter_late_frames_lock);
    FpsLimiterLateFramesBucket& bucket = g_fps_limiter_late_frames_buckets[slot];
    if (bucket.bucket_id != bucket_id) {
        bucket.bucket_id = bucket_id;
        bucket.frames_count = 0;
        bucket.late_frames_count = 0;
    }

    ++bucket.frames_count;
    if (was_late) {
        ++bucket.late_frames_count;
    }
}

}  // namespace

bool GetFpsLimiterLateFramesPercentage(double* out_percentage) {
    if (out_percentage == nullptr) {
        return false;
    }

    utils::SRWLockShared lock(g_fps_limiter_late_frames_lock);
    const LONGLONG now_ns = utils::get_now_ns();
    if (now_ns <= 0) {
        return false;
    }

    const LONGLONG current_bucket_id = now_ns / k_fps_limiter_late_frames_bucket_width_ns;
    const LONGLONG visible_window_bucket_count =
        (k_fps_limiter_late_frames_visible_window_seconds * utils::SEC_TO_NS) / k_fps_limiter_late_frames_bucket_width_ns;
    const LONGLONG first_included_bucket_id = current_bucket_id - (visible_window_bucket_count - 1);

    uint32_t frames_count = 0;
    uint32_t late_frames_count = 0;
    for (uint32_t i = 0; i < k_fps_limiter_late_frames_bucket_count; ++i) {
        const FpsLimiterLateFramesBucket& bucket = g_fps_limiter_late_frames_buckets[i];
        if (bucket.bucket_id < first_included_bucket_id || bucket.bucket_id > current_bucket_id) {
            continue;
        }

        frames_count += bucket.frames_count;
        late_frames_count += bucket.late_frames_count;
    }

    if (frames_count == 0) {
        return false;
    }

    *out_percentage = 100.0 * static_cast<double>(late_frames_count) / static_cast<double>(frames_count);
    return true;
}

void HandleRenderStartAndEndTimes() {
    LONGLONG expected = 0;
    if (g_submit_start_time_ns.load() == 0) {
        // we will use this frame id for pclstats frame id
        // From this point we will use frame.

        LONGLONG now_ns = utils::get_now_ns();
        LONGLONG present_after_end_time_ns = g_sim_start_ns.load();
        if (present_after_end_time_ns > 0 && g_submit_start_time_ns.compare_exchange_strong(expected, now_ns)) {
            const size_t submit_slot = static_cast<size_t>(g_global_frame_id.load() % kFrameDataBufferSize);
            if (g_frame_data[submit_slot].submit_start_time_ns.load() == 0) {
                g_frame_data[submit_slot].submit_start_time_ns.store(now_ns);
            }
            g_pclstats_frame_id.store(g_global_frame_id.load() + 1, std::memory_order_release);
            // Compare to g_present_after_end_time
            LONGLONG g_simulation_duration_ns_new = (now_ns - present_after_end_time_ns);
            g_simulation_duration_ns.store(
                UpdateRollingAverage(g_simulation_duration_ns_new, g_simulation_duration_ns.load()));

            if (s_reflex_enable_current_frame.load()) {
                if (GetReflexSendMarkers()) {
                    if (g_reflexProvider->IsInitialized()) {
                        g_reflexProvider->SetMarker(SIMULATION_END);
                        g_reflexProvider->SetMarker(RENDERSUBMIT_START);
                    }
                }
            }
        }
    }
}

void HandleEndRenderSubmit() {
    LONGLONG now_ns = utils::get_now_ns();
    g_render_submit_end_time_ns.store(now_ns);
    const size_t render_slot = static_cast<size_t>(g_global_frame_id.load() % kFrameDataBufferSize);
    if (g_frame_data[render_slot].render_submit_end_time_ns.load() == 0) {
        g_frame_data[render_slot].render_submit_end_time_ns.store(now_ns);
    }
    if (g_submit_start_time_ns.load() > 0) {
        LONGLONG g_render_submit_duration_ns_new = (now_ns - g_submit_start_time_ns.load());
        g_render_submit_duration_ns.store(
            UpdateRollingAverage(g_render_submit_duration_ns_new, g_render_submit_duration_ns.load()));
    }
}

void HandleOnPresentEnd() {
    LONGLONG now_ns = utils::get_now_ns();

    g_frame_time_ns.store(now_ns - g_sim_start_ns.load());
    g_sim_start_ns.store(now_ns);
    const size_t sim_slot = static_cast<size_t>(g_global_frame_id.load() % kFrameDataBufferSize);
    g_frame_data[sim_slot].sim_start_ns.store(now_ns);
    g_submit_start_time_ns.store(0);

    if (g_render_submit_end_time_ns.load() > 0) {
        LONGLONG g_reshade_overhead_duration_ns_new = (now_ns - g_render_submit_end_time_ns.load());
        g_reshade_overhead_duration_ns.store(
            UpdateRollingAverage(g_reshade_overhead_duration_ns_new, g_reshade_overhead_duration_ns.load()));
    }
}

void RecordFrameTime(FrameTimeMode reason) {
    // Filter calls based on the selected frame time mode
    FrameTimeMode frame_time_mode = static_cast<FrameTimeMode>(settings::g_mainTabSettings.frame_time_mode.GetValue());

    // Only record if the call reason matches the selected mode
    if (reason != frame_time_mode) {
        return;  // Skip recording for this call reason
    }

    static LONGLONG previous_ns = utils::get_now_ns();
    LONGLONG now_ns = utils::get_now_ns();
    const double elapsed = static_cast<double>(now_ns - previous_ns) / static_cast<double>(utils::SEC_TO_NS);
    g_perf_time_seconds.store(elapsed, std::memory_order_release);
    const double dt = elapsed;
    if (dt > 0.0) {
        PerfSample sample{.dt = static_cast<float>(dt)};
        g_perf_ring.Record(sample);
        previous_ns = now_ns;
    }
}

void RecordNativeFrameTime() {
    static LONGLONG previous_ns = utils::get_now_ns();
    LONGLONG now_ns = utils::get_now_ns();
    const double elapsed = static_cast<double>(now_ns - previous_ns) / static_cast<double>(utils::SEC_TO_NS);
    const double dt = elapsed;
    if (dt > 0.0) {
        PerfSample sample{.dt = static_cast<float>(dt)};
        g_native_frame_time_ring.Record(sample);
        previous_ns = now_ns;
    }
}

// Capture sync interval during create_swapchain
bool OnCreateSwapchainCapture2(reshade::api::device_api api, reshade::api::swapchain_desc& desc, void* hwnd) {
    CALL_GUARD_NO_TS();

    // INI-only: [CompatibilityFixes] swapchain_creation_delay (milliseconds, default 0).
    {
        int delay_ms = 0;
        if (display_commander::config::DisplayCommanderConfigManager::GetInstance().GetConfigValue(
                "CompatibilityFixes", "swapchain_creation_delay", delay_ms)) {
            if (delay_ms > 0) {
                constexpr int kMaxSwapchainCreationDelayMs = 600000;
                if (delay_ms > kMaxSwapchainCreationDelayMs) delay_ms = kMaxSwapchainCreationDelayMs;
                LogInfo("[CompatibilityFixes] swapchain_creation_delay: sleeping %d ms", delay_ms);
                Sleep(static_cast<DWORD>(delay_ms));
            }
        } else {
            display_commander::config::DisplayCommanderConfigManager::GetInstance().SetConfigValue(
                "CompatibilityFixes", "swapchain_creation_delay", 0);
        }
    }
    // Don't reset counters on swapchain creation - let them accumulate throughout the session

    g_reshade_create_swapchain_capture_count.fetch_add(1);

    if (hwnd == nullptr) return false;

    // Initialize if not already done
    DoInitializationWithHwnd(static_cast<HWND>(hwnd));

    // Capture game render resolution (before any modifications) - matches Special K's render_x/render_y
    g_game_render_width.store(desc.back_buffer.texture.width);
    g_game_render_height.store(desc.back_buffer.texture.height);
    auto apply_changes = settings::g_experimentalTabSettings.apply_changes_on_create_swapchain.GetValue();
    LogInfo("OnCreateSwapchainCapture2 - Game render resolution: %ux%u, apply changes: %s",
            desc.back_buffer.texture.width, desc.back_buffer.texture.height, apply_changes ? "YES" : "NO");

    if (apply_changes) {
        desc.back_buffer.texture.width =
            settings::g_experimentalTabSettings.spoof_game_resolution_override_width.GetValue();
        desc.back_buffer.texture.height =
            settings::g_experimentalTabSettings.spoof_game_resolution_override_height.GetValue();

        LogInfo("OnCreateSwapchainCapture2 - Game render resolution overridden: %ux%u", desc.back_buffer.texture.width,
                desc.back_buffer.texture.height);
    }

    // Check if this is a supported API (D3D9, D3D10, D3D11, D3D12)
    const bool is_d3d9 = (api == reshade::api::device_api::d3d9);
    const bool is_dxgi = (api == reshade::api::device_api::d3d12 || api == reshade::api::device_api::d3d11
                          || api == reshade::api::device_api::d3d10);

    // D3D9 FLIPEX upgrade logic (only for D3D9)
    if (is_d3d9) {
        // log desc
        {
            std::ostringstream oss;
            oss << "OnCreateSwapchainCapture - ";
            oss << "API: " << static_cast<int>(api) << ", ";
            oss << "Fullscreen: " << (desc.fullscreen_state ? "YES" : "NO") << ", ";
            // Warning: desc.back_buffer_count is the number of buffers, different meaning than what dxgi/vulkan/opengl use.
            oss << "Buffers: " << desc.back_buffer_count << ", ";
            oss << "Present Mode: " << D3DSwapEffectToString(desc.present_mode) << ", ";
            oss << "Sync Interval: " << desc.sync_interval << ", ";
            oss << "Device Creation Flags: " << D3DPresentFlagsToString(desc.present_flags) << ", ";
            oss << "Buffer: " << desc.back_buffer.texture.width << "x" << desc.back_buffer.texture.height << ", ";
            oss << "Buffer Format: " << (long long)desc.back_buffer.texture.format << ", ";
            oss << "Buffer Usage: " << (long long)desc.back_buffer.usage << ", ";
            oss << "Multisample: " << desc.back_buffer.texture.samples << ", ";
            LogInfo(oss.str().c_str());
        }

        bool modified = false;
        if (desc.fullscreen_state && ShouldPreventExclusiveFullscreen()) {
            LogInfo("D3D9: Changed fullscreen state from %s to %s", desc.fullscreen_state ? "YES" : "NO",
                    desc.fullscreen_state ? "NO" : "YES");
            desc.fullscreen_state = false;
            modified = true;
        }

        // Override buffer count if user selected 1–4
        const int buffer_override = settings::g_mainTabSettings.buffer_count_override.GetValue();
        if (buffer_override >= 1 && buffer_override <= 4
            && desc.back_buffer_count != static_cast<uint32_t>(buffer_override)) {
            LogInfo("D3D9: Overriding buffer count from %u to %d", desc.back_buffer_count, buffer_override);
            desc.back_buffer_count = static_cast<uint32_t>(buffer_override);
            modified = true;
        }

        // Apply FLIPEX if all requirements are met
        if (settings::g_experimentalTabSettings.d3d9_flipex_enabled.GetValue()
            && desc.present_mode != D3DSWAPEFFECT_FLIPEX) {
            if (desc.back_buffer_count < 3) {
                LogInfo("D3D9 FLIPEX: Increasing back buffer count from %u to 3 (required for FLIPEX)",
                        desc.back_buffer_count);
                desc.back_buffer_count = 3;
                modified = true;
            }
            if (!s_d3d9e_upgrade_successful.load()) {
                LogWarn("D3D9 FLIPEX: D3D9Ex upgrade not successful, skipping FLIPEX");
                return false;
            }
            assert(desc.back_buffer_count >= 2);
            LogInfo("D3D9 FLIPEX: Upgrading swap effect from %u to FLIPEX (5)", desc.present_mode);
            LogInfo("D3D9 FLIPEX: Full-screen: %s, Back buffers: %u", desc.fullscreen_state ? "YES" : "NO",
                    desc.back_buffer_count);

            desc.present_mode = D3DSWAPEFFECT_FLIPEX;
            if (desc.sync_interval != D3DPRESENT_INTERVAL_IMMEDIATE) {
                LogInfo("D3D9 FLIPEX: Setting sync interval to immediate");
                desc.sync_interval = D3DPRESENT_INTERVAL_IMMEDIATE;
                modified = true;
            }
            if ((desc.present_flags & D3DPRESENT_DONOTFLIP) != 0) {
                LogInfo("D3D9 FLIPEX: Stripping D3DPRESENT_DONOTFLIP flag");
                desc.present_flags &= ~D3DPRESENT_DONOTFLIP;  // only fullscreen mode is supported
                modified = true;
            }
            if ((desc.present_flags & D3DPRESENTFLAG_LOCKABLE_BACKBUFFER) != 0) {
                LogInfo("D3D9 FLIPEX: Stripping D3DPRESENTFLAG_LOCKABLE_BACKBUFFER flag");
                desc.present_flags &= ~D3DPRESENTFLAG_LOCKABLE_BACKBUFFER;
                modified = true;
            }
            if ((desc.present_flags & D3DPRESENTFLAG_DEVICECLIP) != 0) {
                LogInfo("D3D9 FLIPEX: Stripping D3DPRESENTFLAG_DEVICECLIP flag");
                desc.present_flags &= ~D3DPRESENTFLAG_DEVICECLIP;
                modified = true;
            }
            if (desc.back_buffer.texture.samples != 1) {
                LogInfo("D3D9 FLIPEX: Setting multisample type to 1");
                desc.back_buffer.texture.samples = 1;
                modified = true;
            }
            g_used_flipex.store(true);
            modified = true;

            static std::atomic<int> flipex_upgrade_count{0};
            flipex_upgrade_count.fetch_add(1);
            LogInfo("D3D9 FLIPEX: Successfully applied FLIPEX swap effect (upgrade count: %d)",
                    flipex_upgrade_count.load());
        } else {
            g_used_flipex.store(false);
            if (!settings::g_experimentalTabSettings.d3d9_flipex_enabled.GetValue()) {
                LogWarn("D3D9 FLIPEX: FLIPEX upgrade is not enabled. Present mode is %u", desc.present_mode);
            } else {
                LogInfo("D3D9 FLIPEX: FLIPEX upgrade is not enabled. Present mode is %u", desc.present_mode);
                // FLIPEX cannot be applied, set to false
            }
        }
        return modified;
    } else if (is_dxgi) {
        // Apply sync interval setting if enabled
        bool modified = false;

        uint32_t prev_present_flags = desc.present_flags;
        uint32_t prev_back_buffer_count = desc.back_buffer_count;
        uint32_t prev_present_mode = desc.present_mode;
        const bool is_flip = (desc.present_mode == DXGI_SWAP_EFFECT_FLIP_DISCARD
                              || desc.present_mode == DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL);

        /*
        uint32_t prev_sync_interval = UINT32_MAX;
        BREAKING CHANGES
        // Explicit VSYNC overrides take precedence over generic sync-interval
        // dropdown (applies to all APIs)
        if (settings::g_mainTabSettings.force_vsync_on.GetValue()) {
            desc.sync_interval = 1;  // VSYNC on
            modified = true;
        } else if (settings::g_mainTabSettings.force_vsync_off.GetValue()) {
            desc.sync_interval = 0;  // VSYNC off
            modified = true;
        }*/

        // DXGI-specific settings (only for D3D10/11/12)
        if (settings::g_mainTabSettings.prevent_tearing.GetValue()
            && (desc.present_flags & DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING) != 0) {
            desc.present_flags &= ~DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;
            modified = true;
        }

        // Override buffer count if user selected 1–4
        int buffer_override_dxgi = settings::g_mainTabSettings.buffer_count_override.GetValue();
        if (is_flip && (buffer_override_dxgi == 1)) {
            buffer_override_dxgi = 0;
        }
        if (buffer_override_dxgi >= 1 && buffer_override_dxgi <= 4
            && desc.back_buffer_count != static_cast<uint32_t>(buffer_override_dxgi)) {
            LogInfo("Increasing buffer count from %u to %d", desc.back_buffer_count, buffer_override_dxgi);
            desc.back_buffer_count = static_cast<uint32_t>(buffer_override_dxgi);
            modified = true;
        }

        // Skip forcing flip if another ReShade effect runtime already exists for this window
        // (e.g. previous swapchain not yet destroyed, or multiple swapchains). Forcing flip
        // in that case can conflict with the existing runtime.
        struct EnumerateHwndCtx {
            HWND hwnd = nullptr;
            bool found = false;
        } enum_ctx = {static_cast<HWND>(hwnd), false};
        EnumerateReShadeRuntimes(
            [](size_t, reshade::api::effect_runtime* rt, void* user_data) {
                auto* ctx = static_cast<EnumerateHwndCtx*>(user_data);
                if (rt != nullptr && rt->get_hwnd() == ctx->hwnd) {
                    ctx->found = true;
                    return true;
                }
                return false;
            },
            &enum_ctx);
        bool does_another_runtime_exists_for_same_hwnd = enum_ctx.found;

        // DXGI flip upgrade (Advanced: Enable flip chain) — forces flip model
        if (!does_another_runtime_exists_for_same_hwnd && !is_flip
            && settings::g_advancedTabSettings.enable_flip_chain.GetValue()) {
            // Check if current present mode is NOT a flip model

            if (desc.back_buffer_count < 2) {
                LogInfo("DXGI FLIP UPGRADE: Increasing buffer count from %u to 2", desc.back_buffer_count);

                desc.back_buffer_count = 2;
                modified = true;
            }
            if (desc.back_buffer.texture.samples != 1) {
                LogInfo("DXGI FLIP UPGRADE: Setting multisample type to 1");
                desc.back_buffer.texture.samples = 1;
                modified = true;
            }
            // Store original mode for logging
            uint32_t original_mode = desc.present_mode;

            // Force flip model swap chain (FLIP_DISCARD is more performant than FLIP_SEQUENTIAL)
            desc.present_mode = DXGI_SWAP_EFFECT_FLIP_DISCARD;
            modified = true;

            // Log the change
            std::ostringstream flip_oss;
            flip_oss << "DXGI FLIP UPGRADE: Changed present mode from ";
            if (original_mode == DXGI_SWAP_EFFECT_DISCARD) {
                flip_oss << "DISCARD";
            } else {
                flip_oss << "SEQUENTIAL";
            }
            flip_oss << " to FLIP_DISCARD (flip model swap chain)";
            LogInfo("%s", flip_oss.str().c_str());
        }

        // Force Flip Discard upgrade (Main tab DXGI subsection): FLIP_SEQUENTIAL → FLIP_DISCARD only
        if (desc.present_mode == DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL
            && settings::g_mainTabSettings.force_flip_discard_upgrade.GetValue()) {
            desc.present_mode = DXGI_SWAP_EFFECT_FLIP_DISCARD;
            modified = true;
            g_force_flip_discard_upgrade_done.store(true, std::memory_order_relaxed);
            LogInfo("DXGI Force Flip Discard: Upgraded FLIP_SEQUENTIAL to FLIP_DISCARD");
        }

        // Log sync interval and present flags with detailed explanation
        {
            std::ostringstream oss;
            oss << "Swapchain Creation - ";
            oss << "API: " << (is_d3d9 ? "D3D9" : "DXGI") << ", ";
            oss << "Sync Interval: " << desc.sync_interval << ", ";
            oss << "Present Mode: " << prev_present_mode << " -> " << desc.present_mode << ", ";
            oss << "Fullscreen: " << (desc.fullscreen_state ? "YES" : "NO") << ", ";
            oss << "Back Buffers: " << prev_back_buffer_count << " -> " << desc.back_buffer_count;

            if (is_dxgi) {
                oss << ", Device Creation Flags: 0x" << std::hex << prev_present_flags << " -> 0x"
                    << desc.present_flags;
            }

            oss << " BackBufferCount: " << prev_back_buffer_count << " -> " << desc.back_buffer_count;

            oss << " BackBuffer: " << desc.back_buffer.texture.width << "x" << desc.back_buffer.texture.height;

            oss << " BackBuffer Format: " << (long long)desc.back_buffer.texture.format;

            oss << " BackBuffer Usage: " << (long long)desc.back_buffer.usage;

            // Show which features are enabled in present_flags
            if (desc.present_flags == 0) {
                oss << " (No special flags)";
            } else {
                oss << " - Enabled features:";
                if (desc.present_flags & DXGI_SWAP_CHAIN_FLAG_NONPREROTATED) {
                    oss << " NONPREROTATED";
                }
                if (desc.present_flags & DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH) {
                    oss << " ALLOW_MODE_SWITCH";
                }
                if (desc.present_flags & DXGI_SWAP_CHAIN_FLAG_GDI_COMPATIBLE) {
                    oss << " GDI_COMPATIBLE";
                }
                if (desc.present_flags & DXGI_SWAP_CHAIN_FLAG_RESTRICTED_CONTENT) {
                    oss << " RESTRICTED_CONTENT";
                }
                if (desc.present_flags & DXGI_SWAP_CHAIN_FLAG_RESTRICT_SHARED_RESOURCE_DRIVER) {
                    oss << " RESTRICT_SHARED_RESOURCE_DRIVER";
                }
                if (desc.present_flags & DXGI_SWAP_CHAIN_FLAG_DISPLAY_ONLY) {
                    oss << " DISPLAY_ONLY";
                }
                if (desc.present_flags & DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT) {
                    oss << " FRAME_LATENCY_WAITABLE_OBJECT";
                }
                if (desc.present_flags & DXGI_SWAP_CHAIN_FLAG_FOREGROUND_LAYER) {
                    oss << " FOREGROUND_LAYER";
                }
                if (desc.present_flags & DXGI_SWAP_CHAIN_FLAG_FULLSCREEN_VIDEO) {
                    oss << " FULLSCREEN_VIDEO";
                }
                if (desc.present_flags & DXGI_SWAP_CHAIN_FLAG_YUV_VIDEO) {
                    oss << " YUV_VIDEO";
                }
                if (desc.present_flags & DXGI_SWAP_CHAIN_FLAG_HW_PROTECTED) {
                    oss << " HW_PROTECTED";
                }
                if (desc.present_flags & DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING) {
                    oss << " ALLOW_TEARING";
                }
                if (desc.present_flags & DXGI_SWAP_CHAIN_FLAG_RESTRICTED_TO_ALL_HOLOGRAPHIC_DISPLAYS) {
                    oss << " RESTRICTED_TO_ALL_HOLOGRAPHIC_DISPLAYS";
                }
            }

            LogInfo(oss.str().c_str());
            return modified;  // return true if we modified the desc
        }
    } else if (api == reshade::api::device_api::opengl) {
        // Log swapchain description
        {
            std::ostringstream oss;
            oss << "OnCreateSwapchainCapture - ";
            oss << "API: OpenGL, ";
            oss << "Fullscreen: " << (desc.fullscreen_state ? "YES" : "NO") << ", ";
            oss << "Back Buffers: " << desc.back_buffer_count << ", ";
            oss << "Present Mode: " << desc.present_mode << ", ";
            oss << "Sync Interval: " << desc.sync_interval << ", ";
            oss << "Present Flags: 0x" << std::hex << desc.present_flags << std::dec << ", ";
            oss << "Back Buffer: " << desc.back_buffer.texture.width << "x" << desc.back_buffer.texture.height << ", ";
            oss << "Back Buffer Format: " << static_cast<int>(desc.back_buffer.texture.format) << ", ";
            oss << "Back Buffer Usage: 0x" << std::hex << static_cast<uint64_t>(desc.back_buffer.usage) << std::dec
                << ", ";
            oss << "Multisample: " << desc.back_buffer.texture.samples;
            LogInfo(oss.str().c_str());
        }

        auto modified = false;
        uint32_t prev_sync_interval = desc.sync_interval;
        bool prev_fullscreen_state = desc.fullscreen_state;
        reshade::api::format prev_format = desc.back_buffer.texture.format;

        if (desc.fullscreen_state && ShouldPreventExclusiveFullscreen()) {
            LogInfo("OpenGL Swapchain: Changed fullscreen state from %s to %s", desc.fullscreen_state ? "YES" : "NO",
                    desc.fullscreen_state ? "NO" : "YES");
            desc.fullscreen_state = false;
            modified = true;
        }

        // Apply VSYNC overrides (applies to all APIs)
        if (settings::g_mainTabSettings.force_vsync_on.GetValue()) {
            desc.sync_interval = 1;  // VSYNC on
            modified = true;
        } else if (settings::g_mainTabSettings.force_vsync_off.GetValue()) {
            desc.sync_interval = 0;  // VSYNC off
            modified = true;
        }

        // Override buffer count if user selected 1–4
        const int buffer_override_gl = settings::g_mainTabSettings.buffer_count_override.GetValue();
        if (buffer_override_gl >= 1 && buffer_override_gl <= 4
            && desc.back_buffer_count != static_cast<uint32_t>(buffer_override_gl)) {
            LogInfo("OpenGL: Overriding buffer count from %u to %d", desc.back_buffer_count,
                    buffer_override_gl);
            desc.back_buffer_count = static_cast<uint32_t>(buffer_override_gl);
            modified = true;
        }

        // Log changes if modified
        if (modified) {
            std::ostringstream oss;
            oss << "OpenGL Swapchain Creation - ";
            oss << "Sync Interval: " << prev_sync_interval << " -> " << desc.sync_interval << ", ";
            oss << "Fullscreen: " << (prev_fullscreen_state ? "YES" : "NO") << " -> "
                << (desc.fullscreen_state ? "YES" : "NO") << ", ";
            oss << "Back Buffer Format: " << static_cast<int>(prev_format) << " -> "
                << static_cast<int>(desc.back_buffer.texture.format);
            LogInfo(oss.str().c_str());
        }

        return modified;
    } else if (api == reshade::api::device_api::vulkan) {
        // Log swapchain description
        {
            std::ostringstream oss;
            oss << "OnCreateSwapchainCapture - ";
            oss << "API: Vulkan, ";
            oss << "Fullscreen: " << (desc.fullscreen_state ? "YES" : "NO") << ", ";
            oss << "Back Buffers: " << desc.back_buffer_count << ", ";
            oss << "Present Mode: " << desc.present_mode << ", ";
            oss << "Sync Interval: " << desc.sync_interval << ", ";
            oss << "Present Flags: 0x" << std::hex << desc.present_flags << std::dec << ", ";
            oss << "Back Buffer: " << desc.back_buffer.texture.width << "x" << desc.back_buffer.texture.height << ", ";
            oss << "Back Buffer Format: " << static_cast<int>(desc.back_buffer.texture.format) << ", ";
            oss << "Back Buffer Usage: 0x" << std::hex << static_cast<uint64_t>(desc.back_buffer.usage) << std::dec
                << ", ";
            oss << "Multisample: " << desc.back_buffer.texture.samples;
            LogInfo(oss.str().c_str());
        }

        auto modified = false;
        uint32_t prev_sync_interval = desc.sync_interval;
        bool prev_fullscreen_state = desc.fullscreen_state;
        reshade::api::format prev_format = desc.back_buffer.texture.format;

        if (desc.fullscreen_state && ShouldPreventExclusiveFullscreen()) {
            LogInfo("Vulkan Swapchain: Changed fullscreen state from %s to %s", desc.fullscreen_state ? "YES" : "NO",
                    desc.fullscreen_state ? "NO" : "YES");
            desc.fullscreen_state = false;
            modified = true;
        }

        // Apply VSYNC overrides (applies to all APIs)
        if (settings::g_mainTabSettings.force_vsync_on.GetValue()) {
            desc.sync_interval = 1;  // VSYNC on
            modified = true;
        } else if (settings::g_mainTabSettings.force_vsync_off.GetValue()) {
            desc.sync_interval = 0;  // VSYNC off
            modified = true;
        }

        // Override buffer count if user selected 1–4
        const int buffer_override_vk = settings::g_mainTabSettings.buffer_count_override.GetValue();
        if (buffer_override_vk >= 1 && buffer_override_vk <= 4
            && desc.back_buffer_count != static_cast<uint32_t>(buffer_override_vk)) {
            LogInfo("Vulkan: Overriding buffer count from %u to %d", desc.back_buffer_count,
                    buffer_override_vk);
            desc.back_buffer_count = static_cast<uint32_t>(buffer_override_vk);
            modified = true;
        }

        // Log changes if modified
        if (modified) {
            std::ostringstream oss;
            oss << "Vulkan Swapchain Creation - ";
            oss << "Sync Interval: " << prev_sync_interval << " -> " << desc.sync_interval << ", ";
            oss << "Fullscreen: " << (prev_fullscreen_state ? "YES" : "NO") << " -> "
                << (desc.fullscreen_state ? "YES" : "NO") << ", ";
            oss << "Back Buffer Format: " << static_cast<int>(prev_format) << " -> "
                << static_cast<int>(desc.back_buffer.texture.format);
            LogInfo(oss.str().c_str());
        }

        return modified;
    }

    LogWarn("OnCreateSwapchainCapture: Not a supported device API - %d", static_cast<int>(api));
    return false;
}

bool OnCreateSwapchainCapture(reshade::api::device_api api, reshade::api::swapchain_desc& desc, void* hwnd) {
    CALL_GUARD_NO_TS();

    if (api == reshade::api::device_api::d3d9) {
        g_dx9_swapchain_detected.store(true);
    }
    if (desc.back_buffer.texture.width < 640) {
        return false;
    }

    // Store pre-upgrade desc for UI (e.g. DXGI subsection: show option only when original was FLIP_SEQUENTIAL)
    g_last_swapchain_desc_pre.store(std::make_shared<reshade::api::swapchain_desc>(desc));

    auto res = OnCreateSwapchainCapture2(api, desc, hwnd);

    // Store post-upgrade desc for UI display (current swapchain as created)
    g_last_swapchain_desc_post.store(std::make_shared<reshade::api::swapchain_desc>(desc));
    return res;
}

void OnDestroySwapchain(reshade::api::swapchain* swapchain, bool resize) {
    CALL_GUARD_NO_TS();
    (void)resize;
    if (swapchain == nullptr) {
        return;
    }
    const HWND hwnd = static_cast<HWND>(swapchain->get_hwnd());
    display_commander::features::auto_windows_hdr::OnSwapchainDestroyMaybeRevertAutoHdr(hwnd);
}

namespace {

// CTA-861-G / DXGI HDR10: chromaticity encoded as 0-50000 for 0.00000-0.50000 (0.00001 steps)
constexpr UINT32 kHdr10ChromaticityScale = 50000u;

bool ApplyHdr1000MetadataToDxgi(IDXGISwapChain4* swapchain4) {
    if (swapchain4 == nullptr) {
        return false;
    }
    DXGI_HDR_METADATA_HDR10 hdr10 = {};
    hdr10.RedPrimary[0] = static_cast<UINT16>(std::round(0.708 * kHdr10ChromaticityScale));    // Rec. 2020 red x
    hdr10.RedPrimary[1] = static_cast<UINT16>(std::round(0.292 * kHdr10ChromaticityScale));    // Rec. 2020 red y
    hdr10.GreenPrimary[0] = static_cast<UINT16>(std::round(0.170 * kHdr10ChromaticityScale));  // Rec. 2020 green x
    hdr10.GreenPrimary[1] = static_cast<UINT16>(std::round(0.797 * kHdr10ChromaticityScale));  // Rec. 2020 green y
    hdr10.BluePrimary[0] = static_cast<UINT16>(std::round(0.131 * kHdr10ChromaticityScale));   // Rec. 2020 blue x
    hdr10.BluePrimary[1] = static_cast<UINT16>(std::round(0.046 * kHdr10ChromaticityScale));   // Rec. 2020 blue y
    hdr10.WhitePoint[0] = static_cast<UINT16>(std::round(0.3127 * kHdr10ChromaticityScale));   // D65 white x
    hdr10.WhitePoint[1] = static_cast<UINT16>(std::round(0.3290 * kHdr10ChromaticityScale));   // D65 white y
    hdr10.MaxMasteringLuminance = 1000;
    hdr10.MinMasteringLuminance = 0;
    hdr10.MaxContentLightLevel = 1000;
    hdr10.MaxFrameAverageLightLevel = 100;
    const HRESULT hr = swapchain4->SetHDRMetaData(DXGI_HDR_METADATA_TYPE_HDR10, sizeof(hdr10), &hdr10);
    if (SUCCEEDED(hr)) {
        LogInfo("HDR metadata (MaxMDL 1000 nits, Rec. 2020) applied to swapchain");
        return true;
    }
    return false;
}

void ApplyHdr1000MetadataToSwapchain(reshade::api::swapchain* swapchain) {
    const auto api = swapchain->get_device()->get_api();
    if (api != reshade::api::device_api::d3d11 && api != reshade::api::device_api::d3d12) {
        return;
    }
    IUnknown* const iunknown = reinterpret_cast<IUnknown*>(swapchain->get_native());
    Microsoft::WRL::ComPtr<IDXGISwapChain4> swapchain4;
    if (iunknown != nullptr && SUCCEEDED(iunknown->QueryInterface(IID_PPV_ARGS(&swapchain4)))) {
        ApplyHdr1000MetadataToDxgi(swapchain4.Get());
    }
}

}  // namespace

void OnInitSwapchain(reshade::api::swapchain* swapchain, bool resize) {
    CALL_GUARD_NO_TS();
    if (swapchain == nullptr) {
        LogDebug("OnInitSwapchain: swapchain is null");
        return;
    }
    HWND hwnd = static_cast<HWND>(swapchain->get_hwnd());
    if (hwnd == nullptr) {
        return;
    }
    {
        static int log_count = 0;
        if (log_count < 3) {
            LogInfo("OnInitSwapchain: swapchain: 0x%p, resize: %s", swapchain, resize ? "true" : "false");
            log_count++;
        }
    }

    // Capture game render resolution after swapchain creation/resize - matches Special K's render_x/render_y
    // Get the current back buffer to determine the actual render resolution
    try {
        reshade::api::resource back_buffer = swapchain->get_current_back_buffer();
        if (back_buffer != 0) {
            reshade::api::resource_desc desc = swapchain->get_device()->get_resource_desc(back_buffer);
            if (desc.texture.width > 0 && desc.texture.height > 0) {
                g_game_render_width.store(desc.texture.width);
                g_game_render_height.store(desc.texture.height);
                if (resize) {
                    LogInfo("OnInitSwapchain (resize) - Game render resolution: %ux%u", desc.texture.width,
                            desc.texture.height);
                } else {
                    LogInfo("OnInitSwapchain (create) - Game render resolution: %ux%u", desc.texture.width,
                            desc.texture.height);
                }
            }
        }
    } catch (...) {
        // If getting back buffer fails, silently continue
    }
    reshade::api::effect_runtime* first_runtime = GetSelectedReShadeRuntime();
    if (first_runtime != nullptr && first_runtime->get_hwnd() != hwnd) {
        static int log_count = 0;
        if (log_count < 100) {
            LogInfo("Invalid Runtime HWND OnPresentUpdateBefore - First ReShade runtime: 0x%p, hwnd: 0x%p",
                    first_runtime, hwnd);
            log_count++;
        }
        return;
    }


    // Set game start time on first swapchain initialization (only once)
    LONGLONG expected = 0;
    LONGLONG now_ns = utils::get_now_ns();
    if (g_game_start_time_ns.compare_exchange_strong(expected, now_ns)) {
        LogInfo("Game start time recorded: %lld ns", now_ns);
    }

    // needed for quick fps limit selector to work // TODO rework this later
    CalculateWindowState(hwnd, "OnInitSwapchain");

    display_commander::features::auto_windows_hdr::OnSwapchainInitTryAutoEnableWindowsHdr(hwnd);

    // Auto-apply MaxMDL 1000 HDR metadata when enabled (inject HDR10 metadata on swapchain init)
    if (!resize && settings::g_mainTabSettings.auto_apply_maxmdl_1000_hdr_metadata.GetValue()) {
        ApplyHdr1000MetadataToSwapchain(swapchain);
    }

}

LONGLONG TimerPresentPacingDelayStart() { return utils::get_now_ns(); }

LONGLONG TimerPresentPacingDelayEnd(LONGLONG start_ns) {
    LONGLONG end_ns = utils::get_now_ns();
    fps_sleep_after_on_present_ns.store(end_ns - start_ns);
    return end_ns;
}

void OnPresentUpdateAfter(reshade::api::command_queue* queue, reshade::api::swapchain* swapchain) {
    CALL_GUARD_NO_TS();
    ChooseFpsLimiter(static_cast<uint64_t>(utils::get_now_ns()), FpsLimiterCallSite::reshade_addon_event);
    bool use_fps_limiter = GetChosenFpsLimiter(FpsLimiterCallSite::reshade_addon_event);

    if (use_fps_limiter) {
        display_commanderhooks::dxgi::HandlePresentAfter(false);
    }
    // Empty for now
}

void HandleFpsLimiterPost(bool from_present_detour, bool frame_generation_aware = false) {
    auto now = utils::get_now_ns();
    CALL_GUARD(now);
    // Skip FPS limiter for first N frames (warmup)
    if (g_global_frame_id.load(std::memory_order_relaxed) < kFpsLimiterWarmupFrames) {
        return;
    }
    g_fps_limiter_debug_post_entry_count.fetch_add(1, std::memory_order_relaxed);
    float target_fps = GetTargetFps();

    if (target_fps <= 0.0f) {
        return;
    }
    if (s_fps_limiter_mode.load() == FpsLimiterMode::kOnPresentSync) {
        CALL_GUARD(now);
        LONGLONG sleep_until_ns = g_post_sleep_ns.load();
        if (sleep_until_ns > now) {
            constexpr LONGLONG k_fps_limiter_max_wait_ns = 100 * utils::NS_TO_MS;
            if (sleep_until_ns - now > k_fps_limiter_max_wait_ns) {
                sleep_until_ns = now + k_fps_limiter_max_wait_ns;
                LogWarn("[FPS limiter] Post-sleep capped at 100 ms (requested wait was longer); timing may be off.");
            }
            utils::wait_until_ns(sleep_until_ns);
            g_onpresent_sync_post_sleep_ns.store(sleep_until_ns - now);
        } else {
            g_onpresent_sync_post_sleep_ns.store(0);
        }
    }
}

void OnPresentUpdateAfter2(bool frame_generation_aware) {
    auto start_time_ns = utils::get_now_ns();
    CALL_GUARD(start_time_ns);
    // Track render thread ID
    perf_measurement::ScopedTimer perf_timer(perf_measurement::Metric::HandlePresentAfter);
    DWORD current_thread_id = GetCurrentThreadId();
    DWORD previous_render_thread_id = g_render_thread_id.load();
    g_render_thread_id.store(current_thread_id);

    // Log render thread ID changes for debugging
    if (previous_render_thread_id != current_thread_id && previous_render_thread_id != 0) {
        static int count = 0;
        count++;
        if (count <= 10) {
            LogDebug("[TID:%d] Render thread changed from %d to %d", current_thread_id, previous_render_thread_id,
                     current_thread_id);
        }
    }

    if (s_reflex_enable_current_frame.load()) {
        if (GetReflexSendMarkers()) {
            if (g_reflexProvider->IsInitialized()) {
                g_reflexProvider->SetMarker(PRESENT_END);
            }
        }
    }

    // Sim-to-display latency measurement
    // Track that OnPresentUpdateAfter2 was called
    LONGLONG sim_start_for_measurement = g_sim_start_ns_for_measurement.load();
    if (sim_start_for_measurement > 0) {
        g_present_update_after2_called.store(true);
        g_present_update_after2_time_ns.store(start_time_ns);

        // If GPU completion callback was already finished, we're finishing second
        if (g_gpu_completion_callback_finished.load()) {
            // Calculate sim-to-display latency
            LONGLONG latency_new_ns = start_time_ns - sim_start_for_measurement;

            // Smooth the latency with exponential moving average
            LONGLONG old_latency = g_sim_to_display_latency_ns.load();
            LONGLONG smoothed_latency = UpdateRollingAverage(latency_new_ns, old_latency);

            g_sim_to_display_latency_ns.store(smoothed_latency);

            // Record frame time for Display Timing mode (Present finished second, this is actual display time)
            RecordFrameTime(FrameTimeMode::kDisplayTiming);

            // Calculate GPU late time - in this case, GPU finished first, so late time is 0
            g_gpu_late_time_ns.store(0);
        }
    }

    LONGLONG g_present_duration_new_ns =
        (start_time_ns - g_present_start_time_ns.load());  // Convert QPC ticks to seconds (QPC
                                                           // frequency is typically 10MHz)
    g_present_duration_ns.store(UpdateRollingAverage(g_present_duration_new_ns, g_present_duration_ns.load()));

    const uint64_t current_frame_id_for_slot = g_global_frame_id.load();
    const size_t present_slot = static_cast<size_t>(current_frame_id_for_slot % kFrameDataBufferSize);
    g_frame_data[present_slot].present_end_time_ns.store(start_time_ns);
    g_frame_data[present_slot].present_update_after2_time_ns.store(start_time_ns);

    // GPU completion measurement (non-blocking check)
    // GPU completion measurement is now handled by dedicated thread in gpu_completion_monitoring.cpp
    // This provides accurate completion time by waiting on the event in a blocking manner

    // Mark Present end for latent sync limiter timing
    if (dxgi::latent_sync::g_latentSyncManager) {
        auto& latent = dxgi::latent_sync::g_latentSyncManager->GetLatentLimiter();
        latent.OnPresentEnd();
    }
    auto start_ns = TimerPresentPacingDelayStart();
    g_frame_data[present_slot].sleep_post_present_start_time_ns.store(start_ns);

    // Input blocking in background is now handled by Windows message hooks
    // instead of ReShade's block_input_next_frame() for better compatibility

    // DXGI composition state computation and periodic device/colorspace refresh
    // (moved from continuous monitoring thread to present path)
    // NVIDIA Reflex: SIMULATION_END marker (minimal) and Sleep
    // Optionally delay enabling Reflex for the first N frames
    const bool delay_first_500_frames = settings::g_advancedTabSettings.reflex_delay_first_500_frames.GetValue();
    const uint64_t current_frame_id = current_frame_id_for_slot;

    // Override game Reflex when effective reflex mode (from FPS limiter + main tab reflex combo) is not "Game Defaults"
    bool override_game_reflex_settings = (GetEffectiveReflexMode() != OnPresentReflexMode::kGameDefaults);
    if (delay_first_500_frames && current_frame_id < 500) {
        override_game_reflex_settings = false;
    }
    // TODO add or Injected Reflex Enabled
    if (!(IsNativeReflexActive() || IsInjectedReflexEnabled())) {
        override_game_reflex_settings = false;
    }

    HandleFpsLimiterPost(false, frame_generation_aware);
    const LONGLONG end_ns = TimerPresentPacingDelayEnd(start_ns);
    g_frame_data[present_slot].sleep_post_present_end_time_ns.store(end_ns);
    if (g_reflexProvider->IsInitialized()) {
        if (!override_game_reflex_settings) {
            auto params = g_last_nvapi_sleep_mode_params.load();
            ReflexManager::RestoreSleepMode(g_last_nvapi_sleep_mode_dev_ptr.load(), params ? params.get() : nullptr);
            s_reflex_enable_current_frame.store(false);
        } else {
            s_reflex_enable_current_frame.store(true);
            // Apply sleep mode opportunistically each frame to reflect current
            // toggles
            float target_fps = GetTargetFps();
            if (s_fps_limiter_mode.load() != FpsLimiterMode::kReflex) {
                target_fps = 0.0f;
            }
            bool low_latency;
            bool boost;
            low_latency = GetReflexLowLatency();
            boost = GetReflexBoost();
            g_reflexProvider->ApplySleepMode(low_latency, boost,
                                             settings::g_advancedTabSettings.reflex_use_markers.GetValue(), target_fps);
            if (GetReflexSleepEnabled()) {
                perf_timer.pause();
                g_reflexProvider->Sleep();
                perf_timer.resume();
            }
        }
    }

    // Frame data cyclic buffer: finalize completed frame (set frame_id) and zero next slot for reuse
    {
        const size_t slot = static_cast<size_t>(current_frame_id % kFrameDataBufferSize);
        FrameData& fd = g_frame_data[slot];
        fd.frame_id.store(current_frame_id);

        FrameData& next_fd = g_frame_data[(current_frame_id + 1) % kFrameDataBufferSize];
        next_fd.frame_id.store(0);
        next_fd.sim_start_ns.store(0);
        next_fd.submit_start_time_ns.store(0);
        next_fd.render_submit_end_time_ns.store(0);
        next_fd.present_start_time_ns.store(0);
        next_fd.present_end_time_ns.store(0);
        next_fd.present_update_after2_time_ns.store(0);
        next_fd.gpu_completion_time_ns.store(0);
        next_fd.sleep_pre_present_start_time_ns.store(0);
        next_fd.sleep_pre_present_end_time_ns.store(0);
        next_fd.sleep_post_present_start_time_ns.store(0);
        next_fd.sleep_post_present_end_time_ns.store(0);
    }

    g_global_frame_id.fetch_add(1);
    const LONGLONG now_real_ns = utils::get_real_time_ns();
    g_global_frame_id_last_updated_ns.store(now_real_ns, std::memory_order_release);
    const LONGLONG now_ns = utils::get_now_ns();
    LONGLONG expected_overlay_deadline = 0;
    (void)g_performance_overlay_allowed_after_ns.compare_exchange_strong(
        expected_overlay_deadline, now_ns + kPerformanceOverlayPostFirstFrameDelayNs, std::memory_order_acq_rel,
        std::memory_order_relaxed);

    if (s_reflex_enable_current_frame.load()) {
        if (GetReflexSendMarkers()) {
            g_reflexProvider->SetMarker(SIMULATION_START);
            if (g_pclstats_ping_signal.exchange(false, std::memory_order_acq_rel)) {
                // Inject ping marker through the provider (which will emit both NVAPI and ETW markers)
                // g_reflexProvider->SetMarker(PC_LATENCY_PING);
            }
        }
    }

    modules::TickEnabledModules();
    HandleOnPresentEnd();

    RecordFrameTime(FrameTimeMode::kFrameBegin);
}

float GetTargetFps() {
    // Use background flag computed by monitoring thread; avoid
    // GetForegroundWindow here.
    // When background_fps_enabled is off, use fps_limit in both foreground and background.
    // When background_fps_enabled is on and in background, use fps_limit_background.
    float target_fps = 0.0f;
    bool is_background = g_app_in_background.load();
    if (is_background && settings::g_mainTabSettings.background_fps_enabled.GetValue()) {
        target_fps = settings::g_mainTabSettings.fps_limit_background.GetValue();
    } else {
        target_fps = settings::g_mainTabSettings.fps_limit.GetValue();
    }
    if (target_fps > 0.0f && target_fps < 10.0f) {
        target_fps = 0.0f;
    }
    return target_fps;
}

static OnPresentReflexMode GetEffectiveReflexMode() {
    // Use selected FPS limiter mode only (not checkbox). Reflex setting applies in all modes even when limiter off.
    switch (s_fps_limiter_mode.load()) {
        case FpsLimiterMode::kOnPresentSync:
            return static_cast<OnPresentReflexMode>(settings::g_mainTabSettings.onpresent_reflex_mode.GetValue());
        case FpsLimiterMode::kReflex:
            return static_cast<OnPresentReflexMode>(settings::g_mainTabSettings.reflex_limiter_reflex_mode.GetValue());
        case FpsLimiterMode::kLatentSync:
        default:
            return static_cast<OnPresentReflexMode>(
                settings::g_mainTabSettings.reflex_disabled_limiter_mode.GetValue());
    }
}

bool ShouldReflexBeEnabled() {
    const auto mode = GetEffectiveReflexMode();
    if (mode == OnPresentReflexMode::kGameDefaults) {
        GameReflexSleepModeParams p = {};
        GetGameReflexSleepModeParams(&p);
        return p.low_latency;
    }
    return (mode == OnPresentReflexMode::kLowLatency || mode == OnPresentReflexMode::kLowLatencyBoost);
}

bool GetReflexLowLatency() {
    const auto mode = GetEffectiveReflexMode();
    if (mode == OnPresentReflexMode::kGameDefaults) {
        GameReflexSleepModeParams p = {};
        GetGameReflexSleepModeParams(&p);
        return p.has_value ? p.low_latency : false;
    }
    return (mode == OnPresentReflexMode::kLowLatency || mode == OnPresentReflexMode::kLowLatencyBoost);
}

bool GetReflexBoost() {
    const auto mode = GetEffectiveReflexMode();
    if (mode == OnPresentReflexMode::kGameDefaults) {
        GameReflexSleepModeParams p = {};
        GetGameReflexSleepModeParams(&p);
        return p.has_value ? p.boost : false;
    }
    return (mode == OnPresentReflexMode::kLowLatencyBoost);
}

bool GetReflexSleepEnabled() {
    if (s_fps_limiter_mode.load() == FpsLimiterMode::kReflex) {
        // true if not native nvapi_d3d_sleep in last 1s
        return g_nvapi_last_sleep_timestamp_ns.load() < utils::get_now_ns() - 1 * utils::SEC_TO_NS;
    }
    if (s_fps_limiter_mode.load() == FpsLimiterMode::kOnPresentSync) {
        // true if not native nvapi_d3d_sleep in last 1s
        return g_nvapi_last_sleep_timestamp_ns.load() < utils::get_now_ns() - 1 * utils::SEC_TO_NS;
    }
    return false;
}

bool GetReflexSendMarkers() {
    //    if (settings::g_advancedTabSettings.reflex_generate_markers.GetValue()) return true;
    if (IsInjectedReflexEnabled() && g_global_frame_id.load() > 500) return true;
    return false;
}

bool ShouldReflexLowLatencyBeEnabled() { return GetReflexLowLatency(); }

bool ShouldReflexBoostBeEnabled() { return GetReflexBoost(); }

bool ShouldUseReflexAsFpsLimiter() { return s_fps_limiter_mode.load() == FpsLimiterMode::kReflex; }

// Helper function to convert low latency ratio index to delay_bias value
// Ratio index: 0 = 100% Display/0% Input, 1 = 87.5%/12.5%, 2 = 75%/25%, 3 = 62.5%/37.5%,
//              4 = 50%/50%, 5 = 37.5%/62.5%, 6 = 25%/75%, 7 = 12.5%/87.5%, 8 = 0%/100%
// Returns delay_bias: 0.0 = 100% Display, 1.0 = 100% Input
float GetDelayBiasFromRatio(int ratio_index) {
    // Clamp ratio_index to valid range [0, 8]
    ratio_index = (std::max)(0, (std::min)(8, ratio_index));
    // Map: 0→0.0, 1→0.125, 2→0.25, 3→0.375, 4→0.5, 5→0.625, 6→0.75, 7→0.875, 8→1.0
    return ratio_index * 0.125f;
}

static std::atomic<LONGLONG> g_fg2_onpresent_sync_frame_start_ns{0};

bool ShouldActivateFg2Limiter() {
    if (!GetEffectiveFpsLimiterFg2Enabled()) {
        return false;
    }
   // if (static_cast<FrameTimeMode>(settings::g_mainTabSettings.frame_time_mode.GetValue()) != FrameTimeMode::kPresent) {
  //      return false;
  //  }
  //  if (!s_fps_limiter_enabled.load(std::memory_order_relaxed)
  //      || s_fps_limiter_mode.load(std::memory_order_relaxed) != FpsLimiterMode::kOnPresentSync) {
  //      return false;
  //  }
    // Restrict FG2 to the real-frame pacing presets.
//    if (settings::g_mainTabSettings.native_reflex_fps_preset.GetValue()
 //       > static_cast<int>(FpsLimiterPreset::kDCPaceLockQ3)) {
   //     return false;
 //   }
    const DLSSGSummaryLite lite = GetDLSSGSummaryLite();
    return lite.fg_mode >= 2;
}

void HandleFpsLimiterFg2Pre() {
    if (!ShouldActivateFg2Limiter()) {
        return;
    }
    const auto start_time_ns = utils::get_now_ns();
    CALL_GUARD(start_time_ns);
    if (g_global_frame_id.load(std::memory_order_relaxed) < kFpsLimiterWarmupFrames) {
        return;
    }
    const float base_limit = GetTargetFps();
    if (base_limit <= 0.0f) {
        return;
    }
    float pct = settings::g_mainTabSettings.fps_limiter_fg2_target_boost_percent.GetValue();
   // pct = (std::max)(0.f, (std::min)(10.f, pct));
    const float target_fps = base_limit * (1.0f + pct / 100.0f);
    if (target_fps < 10.0f) {
        return;
    }
    const LONGLONG frame_time_ns = static_cast<LONGLONG>(1'000'000'000.0 / target_fps);
    const LONGLONG previous_frame_start_ns = g_fg2_onpresent_sync_frame_start_ns.load(std::memory_order_relaxed);
    const LONGLONG ideal_frame_start_ns = (std::max)(start_time_ns, previous_frame_start_ns + frame_time_ns);
    if (ideal_frame_start_ns > start_time_ns) {
        LONGLONG wait_target_ns = ideal_frame_start_ns;
        constexpr LONGLONG k_fps_limiter_max_wait_ns = 100 * utils::NS_TO_MS;
        if (wait_target_ns - start_time_ns > k_fps_limiter_max_wait_ns) {
            wait_target_ns = start_time_ns + k_fps_limiter_max_wait_ns;
        }
        utils::wait_until_ns(wait_target_ns);
    }
    g_fg2_onpresent_sync_frame_start_ns.store(ideal_frame_start_ns, std::memory_order_relaxed);
}

void HandleFpsLimiterPre(bool from_present_detour, bool frame_generation_aware = false) {
    auto start_time_ns = utils::get_now_ns();
    CALL_GUARD(start_time_ns);
    g_fps_limiter_debug_pre_entry_count.fetch_add(1, std::memory_order_relaxed);
    LONGLONG handle_fps_limiter_start_time_ns = start_time_ns;
    float target_fps = GetTargetFps();
    auto target_fps_native = target_fps;
    late_amount_ns.store(0);

    const DLSSGSummaryLite ngx_lite_snapshot = GetDLSSGSummaryLite();

    if (frame_generation_aware) {
        CALL_GUARD(start_time_ns);

        if (ngx_lite_snapshot.fg_mode >= 2) {
            target_fps /= static_cast<float>(ngx_lite_snapshot.fg_mode);
        }
        static float last_target_fps = -1.0f;  // unset

        if (last_target_fps != target_fps) {
            last_target_fps = target_fps;
            LogInfo("Target FPS: %f, Target FPS Native: %f from wrapper: %s lite.fg_mode: %d", target_fps,
                    target_fps_native, frame_generation_aware ? "true" : "false", ngx_lite_snapshot.fg_mode);
        }

        {
            static bool logged = false;
            if (!logged && ngx_lite_snapshot.fg_mode >= 2) {
                LogInfo("DLSS-G FG mode: %d", ngx_lite_snapshot.fg_mode);
                // log DLSSGSummaryLite all fields
                LogInfo(
                    "DLSSGSummaryLite: any_dlss_active: %d, dlss_active: %d, dlss_g_active: %d, "
                    "ray_reconstruction_active: %d, fg_mode: %d",
                    ngx_lite_snapshot.any_dlss_active, ngx_lite_snapshot.dlss_active, ngx_lite_snapshot.dlss_g_active,
                    ngx_lite_snapshot.ray_reconstruction_active, ngx_lite_snapshot.fg_mode);

                logged = true;
            }
        }
    } else {
        static float last_target_fps = -1.0f;  // unset
        if (last_target_fps != target_fps) {
            last_target_fps = target_fps;
            LogInfo("Target FPS: %f, Target FPS Native: %f from wrapper: %s", target_fps, target_fps_native,
                    frame_generation_aware ? "true" : "false");
        }
    }
    {
        g_fps_limiter_debug_target_fps_native.store(target_fps_native, std::memory_order_relaxed);
        g_fps_limiter_debug_target_fps_effective.store(target_fps, std::memory_order_relaxed);
        g_fps_limiter_debug_getlite_fg_mode.store(ngx_lite_snapshot.fg_mode, std::memory_order_relaxed);
        g_fps_limiter_debug_frame_generation_aware.store(frame_generation_aware ? uint8_t{1} : uint8_t{0},
                                                         std::memory_order_relaxed);
    }
    if (s_fps_limiter_enabled.load()
        && (target_fps > 0.0f || s_fps_limiter_mode.load() == FpsLimiterMode::kLatentSync)) {
        g_fps_limiter_debug_pre_active_count.fetch_add(1, std::memory_order_relaxed);
        CALL_GUARD(start_time_ns);
        // Note: Command queue flushing is now handled in OnPresentUpdateBefore using native DirectX APIs
        // No need to flush here anymore

        // Call FPS Limiter on EVERY frame (not throttled)
        switch (s_fps_limiter_mode.load()) {
            case FpsLimiterMode::kReflex: {
                if (!settings::g_advancedTabSettings.reflex_auto_configure.GetValue()) {
                    settings::g_advancedTabSettings.reflex_auto_configure.SetValue(true);
                }
                // Reflex mode - auto-configuration is handled when mode is selected
                // Reflex manages frame rate limiting internally
                break;
            }
            case FpsLimiterMode::kOnPresentSync: {
                // Get delay_bias from ratio selector
                int ratio_index = settings::g_mainTabSettings.onpresent_sync_low_latency_ratio.GetValue();
                float delay_bias = GetDelayBiasFromRatio(ratio_index);

                if (target_fps >= 1.0f) {
                    CALL_GUARD(start_time_ns);
                    // Calculate frame time
                    float adjusted_target_fps = target_fps;
                    const auto onpresent_reflex =
                        static_cast<OnPresentReflexMode>(settings::g_mainTabSettings.onpresent_reflex_mode.GetValue());
                    LONGLONG frame_time_ns = static_cast<LONGLONG>(1'000'000'000.0 / adjusted_target_fps);

                    // Store delay_bias and frame_time for post-sleep calculation
                    g_onpresent_sync_delay_bias.store(delay_bias);
                    g_onpresent_sync_frame_time_ns.store(frame_time_ns);

                    // Calculate pre-sleep time: (1 - delay_bias) * frame_time
                    // This is the time we sleep BEFORE starting frame processing
                    LONGLONG post_sleep_ns = static_cast<LONGLONG>(delay_bias * frame_time_ns);

                    // Get current time and previous frame start time
                    // KEY: Use previous frame START time, not END time, to maintain start-to-start spacin
                    LONGLONG previous_frame_start_ns = g_onpresent_sync_frame_start_ns.load();

                    // Calculate ideal frame start time
                    // Frames should be spaced by exactly frame_time_ns from start to start
                    LONGLONG ideal_frame_start_ns =
                        (std::max)(start_time_ns - post_sleep_ns, previous_frame_start_ns + frame_time_ns);

                    // Always sleep for pre_sleep_ns before starting the frame
                    // When delay_bias = 0: pre_sleep = frame_time, so we sleep for the full frame time
                    // When delay_bias = 1.0: pre_sleep = 0, so we start immediately
                    CALL_GUARD(start_time_ns);
                    bool was_late = false;
                    if (ideal_frame_start_ns - post_sleep_ns > start_time_ns) {
                        // On time - sleep until calculated time (ensures we sleep for pre_sleep_ns)
                        LONGLONG wait_target_ns = ideal_frame_start_ns - post_sleep_ns;
                        constexpr LONGLONG k_fps_limiter_max_wait_ns = 100 * utils::NS_TO_MS;
                        if (wait_target_ns - start_time_ns > k_fps_limiter_max_wait_ns) {
                            wait_target_ns = start_time_ns + k_fps_limiter_max_wait_ns;
                            LogWarn(
                                "[FPS limiter] Pre-sleep capped at 100 ms (requested wait was longer); timing may be "
                                "off.");
                        }
                        utils::wait_until_ns(wait_target_ns);
                        late_amount_ns.store(0);
                        g_onpresent_sync_pre_sleep_ns.store(ideal_frame_start_ns - start_time_ns);
                    } else {
                        // Late - but still sleep until ideal_frame_start_ns to maintain frame spacing
                        // This ensures frames are always spaced by frame_time_ns from start to start
                        // utils::wait_until_ns(ideal_frame_start_ns);
                        late_amount_ns.store(start_time_ns - ideal_frame_start_ns);
                        g_onpresent_sync_pre_sleep_ns.store(0);
                        was_late = true;
                    }
                    RecordFpsLimiterLateFrameSample(start_time_ns, was_late);
                    CALL_GUARD(start_time_ns);
                    // Record when frame processing actually started
                    g_onpresent_sync_frame_start_ns.store(ideal_frame_start_ns);
                    g_post_sleep_ns.store(ideal_frame_start_ns + post_sleep_ns);
                } else {
                    // No FPS limit - reset state
                    g_onpresent_sync_delay_bias.store(0.0f);
                    g_onpresent_sync_frame_time_ns.store(0);
                }

                break;
            }
            case FpsLimiterMode::kLatentSync: {
                // Use latent sync manager for VBlank Scanline Sync mode
                if (dxgi::latent_sync::g_latentSyncManager) {
                    auto& latent = dxgi::latent_sync::g_latentSyncManager->GetLatentLimiter();
                    if (target_fps > 0.0f) {
                        latent.LimitFrameRate();
                    }
                }
                break;
            }
        }
    }
    {
        auto end_time_ns = utils::get_now_ns();
        CALL_GUARD(end_time_ns);

        LONGLONG handle_fps_limiter_start_end_time_ns = end_time_ns;
        g_present_start_time_ns.store(handle_fps_limiter_start_end_time_ns);

        // Frame data cyclic buffer: record present start and sleep-pre-present for the frame we're starting
        const size_t slot = static_cast<size_t>(g_global_frame_id.load() % kFrameDataBufferSize);
        g_frame_data[slot].present_start_time_ns.store(handle_fps_limiter_start_end_time_ns);
        g_frame_data[slot].sleep_pre_present_start_time_ns.store(handle_fps_limiter_start_time_ns);
        g_frame_data[slot].sleep_pre_present_end_time_ns.store(handle_fps_limiter_start_end_time_ns);

        LONGLONG handle_fps_limiter_start_duration_ns =
            max(1, handle_fps_limiter_start_end_time_ns - handle_fps_limiter_start_time_ns);
        fps_sleep_before_on_present_ns.store(
            UpdateRollingAverage(handle_fps_limiter_start_duration_ns, fps_sleep_before_on_present_ns.load()));
        CALL_GUARD(end_time_ns);
    }
}

// Helper to set swap chain and ReShade runtime color space (DXGI path). Tries the requested color space
// anyway; only falls back to sRGB if SetColorSpace1 fails. Caches support result for UI.
static void SetSwapChainColorSpace(reshade::api::swapchain* swapchain, DXGI_COLOR_SPACE_TYPE color_space,
                                   reshade::api::color_space reshade_color_space) {
    auto* unknown = reinterpret_cast<IUnknown*>(swapchain->get_native());
    if (unknown == nullptr) {
        return;
    }
    Microsoft::WRL::ComPtr<IDXGISwapChain3> swapchain3;
    HRESULT hr = unknown->QueryInterface(IID_PPV_ARGS(&swapchain3));
    if (FAILED(hr)) {
        return;
    }
    LogInfo("SetSwapChainColorSpace: color_space=%d", static_cast<int>(color_space));
    UINT color_space_support = 0;
    swapchain3->CheckColorSpaceSupport(color_space, &color_space_support);
    const int supported = (color_space_support != 0) ? 1 : 0;

    hr = swapchain3->SetColorSpace1(color_space);
    if (FAILED(hr)) {
        LogError("SetSwapChainColorSpace: SetColorSpace1(ColorSpace=%d) failed 0x%08X",
                 static_cast<int>(color_space), static_cast<unsigned>(hr));
        return;
    }
    g_show_auto_colorspace_fix_in_main_tab.store(true);

    // Log only when values change to avoid repeated identical log lines.
    static std::atomic<int> s_last_color_space{-1};
    static std::atomic<int> s_last_reshade_cs{-1};
    static std::atomic<int> s_last_supported{-1};
    const int cs = static_cast<int>(color_space);
    const int rcs = static_cast<int>(reshade_color_space);
    if (s_last_color_space.load(std::memory_order_relaxed) != cs
        || s_last_reshade_cs.load(std::memory_order_relaxed) != rcs
        || s_last_supported.load(std::memory_order_relaxed) != supported) {
        s_last_color_space.store(cs, std::memory_order_relaxed);
        s_last_reshade_cs.store(rcs, std::memory_order_relaxed);
        s_last_supported.store(supported, std::memory_order_relaxed);
        LogInfo("SetSwapChainColorSpace: color_space=%d, reshade_color_space=%d, supported=%d", color_space,
                reshade_color_space, supported);
    }
    reshade::api::effect_runtime* runtime = GetSelectedReShadeRuntime();
    if (runtime != nullptr) {
        runtime->set_color_space(reshade_color_space);
    }
}

// Helper: when HDR/scRGB color fix is enabled, set DXGI + ReShade color space from back buffer format:
// 10-bit (R10G10B10A2) → HDR10 (ST2084), 16-bit FP (R16G16B16A16) → scRGB. No-op for 8-bit.
// Reads format from the DXGI swap chain (GetDesc / GetDesc1). Call at most once per swapchain
// (caller tracks via DCDxgiSwapchainData::auto_colorspace_applied).
void AutoSetColorSpace(reshade::api::swapchain* swapchain, IDXGISwapChain* dxgi_swapchain) {
    if (!settings::g_advancedTabSettings.auto_colorspace.GetValue()) {
        return;
    }
    if (g_is_renodx_loaded.load(std::memory_order_relaxed)) {
        return;
    }
    if (dxgi_swapchain == nullptr) {
        return;
    }

    DXGI_FORMAT dxgi_format = DXGI_FORMAT_UNKNOWN;
    DXGI_SWAP_CHAIN_DESC swap_desc = {};
    if (SUCCEEDED(dxgi_swapchain->GetDesc(&swap_desc))) {
        dxgi_format = swap_desc.BufferDesc.Format;
    } else {
        Microsoft::WRL::ComPtr<IDXGISwapChain1> swapchain1;
        if (SUCCEEDED(dxgi_swapchain->QueryInterface(IID_PPV_ARGS(&swapchain1)))) {
            DXGI_SWAP_CHAIN_DESC1 desc1 = {};
            if (SUCCEEDED(swapchain1->GetDesc1(&desc1))) {
                dxgi_format = desc1.Format;
            }
        }
    }
    if (dxgi_format == DXGI_FORMAT_UNKNOWN) {
        return;
    }

    DXGI_COLOR_SPACE_TYPE color_space;
    reshade::api::color_space reshade_color_space;
    if (dxgi_format == DXGI_FORMAT_R10G10B10A2_UNORM) {
        color_space = DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020;
        reshade_color_space = reshade::api::color_space::hdr10_st2084;
    } else if (dxgi_format == DXGI_FORMAT_R16G16B16A16_FLOAT) {
        color_space = DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709;
        reshade_color_space = reshade::api::color_space::extended_srgb_linear;
    } else {
        return;
    }

    LogInfo("AutoSetColorSpace: applying %d", static_cast<int>(color_space));
    SetSwapChainColorSpace(swapchain, color_space, reshade_color_space);
}
// Update composition state after presents (required for valid stats)
void OnPresentUpdateBefore(reshade::api::command_queue* command_queue, reshade::api::swapchain* swapchain,
                           const reshade::api::rect* /*source_rect*/, const reshade::api::rect* /*dest_rect*/,
                           uint32_t /*dirty_rect_count*/, const reshade::api::rect* /*dirty_rects*/) {
    auto api = swapchain->get_device()->get_api();
    command_queue->flush_immediate_command_list();
    CALL_GUARD_NO_TS();
    if (perf_measurement::IsSuppressionEnabled()
        && perf_measurement::IsMetricSuppressed(perf_measurement::Metric::OnPresentUpdateBefore)) {
        return;
    }

    perf_measurement::ScopedTimer perf_timer(perf_measurement::Metric::OnPresentUpdateBefore);

    if (swapchain == nullptr) {
        return;
    }

    HWND hwnd = static_cast<HWND>(swapchain->get_hwnd());
    if (hwnd == g_proxy_hwnd) {
        return;
    }
    reshade::api::effect_runtime* first_runtime = GetSelectedReShadeRuntime();
    if (first_runtime != nullptr && first_runtime->get_hwnd() != hwnd) {
        LogInfoThrottled(1, "Invalid Runtime HWND OnPresentUpdateBefore - First ReShade runtime: 0x%p, hwnd: 0x%p",
                         first_runtime, hwnd);
        return;
    }
    if (settings::g_mainTabSettings.present_mon_etw_enabled.GetValue()) {
        display_commander::features::presentmon::EnsurePresentMonEtwStarted();
    }

    hookToSwapChain(swapchain);

    // Per-swapchain private data: load at start, update when we have DXGI swapchain, save at end if changed
    display_commanderhooks::dxgi::DCDxgiSwapchainData private_data{};
    bool changed = false;
    IDXGISwapChain* dxgi_swapchain_for_save = nullptr;

    // Auto set color space if enabled
    bool idx_dx12 = api == reshade::api::device_api::d3d12;
    bool dx_dx11 = api == reshade::api::device_api::d3d11;
    bool dx_dx10 = api == reshade::api::device_api::d3d10;
    bool dx_d3d9 = api == reshade::api::device_api::d3d9;
    bool is_dxgi = idx_dx12 || dx_dx11 || dx_dx10;
    Microsoft::WRL::ComPtr<IDXGISwapChain> dxgi_swapchain{};

    if (is_dxgi) {
        IUnknown* swapchain_native = reinterpret_cast<IUnknown*>(swapchain->get_native());
        swapchain_native->QueryInterface(IID_PPV_ARGS(&dxgi_swapchain));
        if (dxgi_swapchain.Get() != nullptr) {
            dxgi_swapchain_for_save = dxgi_swapchain.Get();
            display_commanderhooks::dxgi::LoadDCDxgiSwapchainData(dxgi_swapchain_for_save, &private_data);

            if (private_data.dxgi_swapchain == nullptr) {
                private_data.dxgi_swapchain = dxgi_swapchain_for_save;
                private_data.swapchain = swapchain;
                private_data.command_queue = command_queue;
                private_data.device_api = api;
                changed = true;
            }

            // Auto set color space at most once per swapchain (tracked in private_data).
            if (!private_data.auto_colorspace_applied) {
                AutoSetColorSpace(swapchain, dxgi_swapchain.Get());
                private_data.auto_colorspace_applied = true;
                changed = true;
            }

            // Apply SetMaximumFrameLatency override (Main tab). Track applied value in private_data so we only set
            // when the user's choice differs from what we last applied (per swapchain).
            const int desired_latency = settings::g_mainTabSettings.max_frame_latency_override.GetValue();
            if (desired_latency >= 1 && desired_latency <= 16) {
                if (private_data.applied_max_frame_latency != static_cast<uint32_t>(desired_latency)) {
                    Microsoft::WRL::ComPtr<IDXGISwapChain2> sc2;
                    if (SUCCEEDED(dxgi_swapchain->QueryInterface(IID_PPV_ARGS(&sc2)))) {
                        const UINT clamped = static_cast<UINT>(desired_latency);
                        HRESULT hr = sc2->SetMaximumFrameLatency(clamped);
                        if (SUCCEEDED(hr)) {
                            private_data.applied_max_frame_latency = clamped;
                            changed = true;
                        }
                    }
                }
            } else {
                // No override (0): clear applied so we can re-apply when user selects a value again
                if (private_data.applied_max_frame_latency != 0) {
                    private_data.applied_max_frame_latency = 0;
                    changed = true;
                }
            }
        }
    } else if (dx_d3d9) {
        // query don't assume
        Microsoft::WRL::ComPtr<IDirect3DDevice9> d3d9_device = nullptr;
        // Save DCDxgiSwapchainData for D3D9 to global (no IDXGISwapChain) so selected runtime can be compared.
        if (private_data.d3d9_device == nullptr) {
            private_data.dxgi_swapchain = nullptr;
            private_data.swapchain = swapchain;
            private_data.command_queue = command_queue;
            private_data.device_api = api;
            private_data.d3d9_device = d3d9_device.Get();
            changed = true;
        }
    }

    HandleRenderStartAndEndTimes();

    HandleEndRenderSubmit();
    // NVIDIA Reflex: RENDERSUBMIT_END marker (minimal)
    if (s_reflex_enable_current_frame.load()) {
        if (GetReflexSendMarkers()) {
            g_reflexProvider->SetMarker(RENDERSUBMIT_END);
        }
    }

    // Update cached Reflex sleep status periodically (every ~500ms)
    static LONGLONG last_sleep_status_update_ns = 0;
    const LONGLONG sleep_status_update_interval_ns = 500 * utils::NS_TO_MS;  // 500ms
    LONGLONG now_ns = utils::get_now_ns();
    if (now_ns - last_sleep_status_update_ns >= sleep_status_update_interval_ns) {
        if (g_reflexProvider && g_reflexProvider->IsInitialized()) {
            g_reflexProvider->UpdateCachedSleepStatus();
        }
        last_sleep_status_update_ns = now_ns;
    }
    // Always flush command queue before present to reduce latency
    g_flush_before_present_time_ns.store(utils::get_now_ns());

    // Enqueue GPU completion measurement using this swapchain's private data (command_queue from
    // DCDxgiSwapchainData). Optional (default on) via Advanced tab. Skip when Smooth Motion (nvpresent) is
    // loaded - frame generation makes GPU completion timing misleading.
    const bool smooth_motion_loaded = display_commander::features::smooth_motion::IsSmoothMotionLoaded();
    if ((idx_dx12 || dx_dx11) && settings::g_advancedTabSettings.enqueue_gpu_completion.GetValue()
        && !smooth_motion_loaded) {
        perf_timer.pause();
        EnqueueGPUCompletionFromRecordedState(dxgi_swapchain_for_save, &private_data);
        perf_timer.resume();
    } else if ((idx_dx12 || dx_dx11) && settings::g_advancedTabSettings.enqueue_gpu_completion.GetValue()
               && smooth_motion_loaded) {
        static std::atomic<bool> s_smooth_motion_suppress_logged{false};
        if (!s_smooth_motion_suppress_logged.exchange(true, std::memory_order_relaxed)) {
            LogInfo("Enqueue GPU completion suppressed due to Smooth Motion (nvpresent DLL loaded).");
        }
    }

    // ReShade present-before: notify enabled modules (e.g. Controller screenshot trigger)
    modules::NotifyEnabledModulesReshadePresentBefore();

    // ReShade block_input_next_frame() is called from OnReShadePresent (addon_event::reshade_present) with the
    // matching effect_runtime* instead of GetSelectedReShadeRuntime().

    perf_timer.pause();
    // vulkan fps limiter
    ChooseFpsLimiter(static_cast<uint64_t>(utils::get_now_ns()), FpsLimiterCallSite::reshade_addon_event);
    bool use_fps_limiter = GetChosenFpsLimiter(FpsLimiterCallSite::reshade_addon_event);
    if (use_fps_limiter) {
        OnPresentFlags2(true, false);  // Called from present_detour

        RecordNativeFrameTime();
    }

    const FpsLimiterCallSite frame_loc = GetChosenFrameTimeLocation();
    if (frame_loc != FpsLimiterCallSite::dxgi_swapchain1 && frame_loc != FpsLimiterCallSite::dxgi_swapchain
        && frame_loc != FpsLimiterCallSite::dx9_present && frame_loc != FpsLimiterCallSite::dx9_presentex) {
        RecordFrameTime(FrameTimeMode::kPresent);
    }

    auto ok_to_initialize_reflex = IsNativeReflexActive() || IsInjectedReflexEnabled();
    if (ok_to_initialize_reflex) {
        if (api == reshade::api::device_api::d3d12) {
            g_reflexProvider->InitializeNative((void*)swapchain->get_device()->get_native(), DeviceTypeDC::DX12);
        } else if (api == reshade::api::device_api::d3d11) {
            g_reflexProvider->InitializeNative((void*)swapchain->get_device()->get_native(), DeviceTypeDC::DX11);
        } else if (api == reshade::api::device_api::d3d10) {
            g_reflexProvider->InitializeNative((void*)swapchain->get_device()->get_native(), DeviceTypeDC::DX10);
        }
    }

    perf_timer.resume();

    // Extract DXGI output device name from swapchain (shared via atomic; g_got_device_name tracks first
    // success)
    {
        auto api = swapchain->get_device()->get_api();
        if (api == reshade::api::device_api::d3d11 || api == reshade::api::device_api::d3d12
            || api == reshade::api::device_api::d3d10) {
            IUnknown* iunknown = reinterpret_cast<IUnknown*>(swapchain->get_native());
            if (iunknown != nullptr) {
                Microsoft::WRL::ComPtr<IDXGISwapChain> dxgi_swapchain;
                if (SUCCEEDED(iunknown->QueryInterface(IID_PPV_ARGS(&dxgi_swapchain)))) {
                    Microsoft::WRL::ComPtr<IDXGIOutput> output;
                    if (SUCCEEDED(dxgi_swapchain->GetContainingOutput(&output))) {
                        Microsoft::WRL::ComPtr<IDXGIOutput6> output6;
                        if (SUCCEEDED(output->QueryInterface(IID_PPV_ARGS(&output6)))) {
                            DXGI_OUTPUT_DESC1 desc1 = {};
                            if (SUCCEEDED(output6->GetDesc1(&desc1))) {
                                if (desc1.DeviceName[0] != L'\0') {
                                    auto device_name = std::make_shared<const std::wstring>(desc1.DeviceName);
                                    g_dxgi_output_device_name.store(device_name);
                                    g_got_device_name.store(true);
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    if (is_dxgi && dxgi_swapchain_for_save != nullptr && changed) {
        display_commanderhooks::dxgi::SaveDCDxgiSwapchainData(dxgi_swapchain_for_save, &private_data);
    }

    // DXGI composition / independent flip state is no longer queried or shown in UI.
}

bool OnBindPipeline(reshade::api::command_list* cmd_list, reshade::api::pipeline_stage stages,
                    reshade::api::pipeline pipeline) {
    CALL_GUARD_NO_TS();

    return false;  // Don't suppress pipeline binding
}

// Present flags callback to strip DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING
void OnPresentFlags2(bool from_present_detour, bool frame_generation_aware) {
    CALL_GUARD_NO_TS();
    if (perf_measurement::IsSuppressionEnabled()
        && perf_measurement::IsMetricSuppressed(perf_measurement::Metric::OnPresentFlags2)) {
        return;
    }

    {
        perf_measurement::ScopedTimer perf_timer(perf_measurement::Metric::OnPresentFlags2);
    }

    HandleFpsLimiterPre(from_present_detour, frame_generation_aware);

    if (s_reflex_enable_current_frame.load()) {
        if (GetReflexSendMarkers()) {
            if (g_reflexProvider->IsInitialized()) {
                g_reflexProvider->SetMarker(PRESENT_START);
            }
        }
    }
}

