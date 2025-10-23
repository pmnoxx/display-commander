#include "addon.hpp"
#include "adhd_multi_monitor/adhd_simple_api.hpp"
#include "audio/audio_management.hpp"
#include "display_initial_state.hpp"
#include "globals.hpp"
#include "gpu_completion_monitoring.hpp"
#include "hooks/api_hooks.hpp"
#include "hooks/d3d9/d3d9_present_hooks.hpp"
#include "hooks/dxgi/dxgi_present_hooks.hpp"
#include "hooks/dxgi/dxgi_gpu_completion.hpp"
#include "hooks/window_proc_hooks.hpp"
#include "hooks/streamline_hooks.hpp"
#include "hooks/windows_hooks/windows_message_hooks.hpp"
#include "hooks/xinput_hooks.hpp"
#include "hooks/hid_suppression_hooks.hpp"
#include "hooks/hid_additional_hooks.hpp"
#include "hooks/ngx_hooks.hpp"
#include "input_remapping/input_remapping.hpp"
#include "latency/latency_manager.hpp"
#include "latent_sync/latent_sync_limiter.hpp"
#include "latent_sync/refresh_rate_monitor_integration.hpp"
#include "nvapi/nvapi_fullscreen_prevention.hpp"
#include "performance_types.hpp"
#include "settings/experimental_tab_settings.hpp"
#include "settings/main_tab_settings.hpp"
#include "settings/developer_tab_settings.hpp"

#include <dxgi.h>
#include "swapchain_events.hpp"
#include "swapchain_events_power_saving.hpp"
#include "ui/new_ui/experimental_tab.hpp"
#include "ui/new_ui/new_ui_main.hpp"
#include "utils/general_utils.hpp"
#include "utils/timing.hpp"
#include "widgets/xinput_widget/xinput_widget.hpp"
#include "widgets/dualsense_widget/dualsense_widget.hpp"

#include <d3d9.h>
#include <dxgi.h>
#include <dxgi1_4.h>
#include <minwindef.h>

#include <atomic>
#include <sstream>
#include <set>

std::atomic<int> target_width = 3840;
std::atomic<int> target_height = 2160;

bool is_target_resolution(int width, int height) {
    return width >= 1280 && width <= target_width.load() && height >= 720 && height <= target_height.load() &&
           width * 9 == height * 16;
}
std::atomic<bool> g_initialized_with_hwnd{false};


// ============================================================================
// D3D9 to D3D9Ex Upgrade Handler
// ============================================================================

bool OnCreateDevice(reshade::api::device_api api, uint32_t& api_version) {
    // Check if D3D9 upgrade is enabled
    if (!settings::g_experimentalTabSettings.d3d9_flipex_enabled.GetValue()) {
        LogInfo("D3D9 to D3D9Ex upgrade disabled");
        return false;
    }

    // Only process D3D9 API
    if (api != reshade::api::device_api::d3d9) {
        return false;
    }

    if (!settings::g_developerTabSettings.prevent_fullscreen.GetValue()) {
        LogWarn("D3D9: Fullscreen state change blocked by developer settings");
        return false;
    }

    // Check if already D3D9Ex (0x9100)
    if (api_version == 0x9100) {
        LogInfo("D3D9Ex already detected, no upgrade needed");
        s_d3d9e_upgrade_successful.store(true);
        return false;
    }

    // Upgrade D3D9 (0x9000) to D3D9Ex (0x9100)
    LogInfo("Upgrading Direct3D 9 (0x%x) to Direct3D 9Ex (0x9100)", api_version);
    api_version = 0x9100;
    s_d3d9e_upgrade_successful.store(true);

    return true;
}

void OnDestroyDevice(reshade::api::device *device) {
    if (device == nullptr) {
        return;
    }

    LogInfo("Device destroyed - performing cleanup operations device: %p", device);

    // Clean up NGX handle tracking
    CleanupNGXHooks();

    /*
    if (g_last_swapchain_ptr_unsafe.load() != nullptr) {
        reshade::api::swapchain *swapchain = reinterpret_cast<reshade::api::swapchain *>(g_last_swapchain_ptr_unsafe.load());
        if (swapchain != nullptr && swapchain->get_device() == device) {

                LogInfo("Clearing global swapchain reference swapchain: %p", swapchain);
                g_last_swapchain_ptr_unsafe.store(nullptr);
            }
        }
    }*/

    // Clean up device-specific resources
    // Note: Most cleanup is handled in DLL_PROCESS_DETACH, but this provides
    // device-specific cleanup when a device is destroyed during runtime

    // Reset any device-specific state
    // g_initialized_with_hwnd.store(false);

    // Clean up any device-specific resources that need immediate cleanup
    // (Most resources are cleaned up in DLL_PROCESS_DETACH)

    //     LogInfo("Device cleanup completed");
}

void OnDestroyEffectRuntime(reshade::api::effect_runtime *runtime) {
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

void hookToSwapChain(reshade::api::swapchain *swapchain) {
    HWND hwnd = static_cast<HWND>(swapchain->get_hwnd());
    if (hwnd == g_proxy_hwnd) {
        return;
    }
    static std::set<reshade::api::swapchain *> hooked_swapchains;

    static reshade::api::swapchain *last_swapchain = nullptr;
    if (last_swapchain == swapchain || swapchain == nullptr || swapchain->get_hwnd() == nullptr) {
        return;
    }
    if (hooked_swapchains.find(swapchain) != hooked_swapchains.end()) {
        return;
    }
    hooked_swapchains.insert(swapchain);
    last_swapchain = swapchain;

    //..static bool initialized = false;
    //if (initialized || swapchain == nullptr || swapchain->get_hwnd() == nullptr) {
    //    return;
    //
    LogInfo("onInitSwapChain: swapchain: 0x%p", swapchain);

    // Store the current swapchain for UI access
    g_last_reshade_device_api.store(static_cast<int>(swapchain->get_device()->get_api()));
    g_last_swapchain_ptr_unsafe.store(static_cast<void*>(swapchain));

    // Query and store API version/feature level
    uint32_t api_version = 0;
    if (swapchain->get_device()->get_property(reshade::api::device_properties::api_version, &api_version)) {
        g_last_api_version.store(api_version);
        LogInfo("Device API version/feature level: 0x%x", api_version);
    }

    // Schedule auto-apply even on resizes (generation counter ensures only latest
    // runs)
    if (hwnd != nullptr) {
        g_last_swapchain_hwnd.store(hwnd);

        // Initialize if not already done
        DoInitializationWithHwnd(hwnd);

        // Hook DXGI Present calls for this swapchain
        // Get the underlying DXGI swapchain from the ReShade swapchain
        if (swapchain->get_device()->get_api() == reshade::api::device_api::d3d12 || swapchain->get_device()->get_api() == reshade::api::device_api::d3d11 || swapchain->get_device()->get_api() == reshade::api::device_api::d3d10) {
            if (auto *dxgi_swapchain = reinterpret_cast<IDXGISwapChain *>(swapchain->get_native())) {
                if (display_commanderhooks::dxgi::HookSwapchain(dxgi_swapchain)) {
                    LogInfo("Successfully hooked DXGI Present calls for swapchain: 0x%p", dxgi_swapchain);
                } else {
                    LogWarn("Failed to hook DXGI Present calls for swapchain: 0x%p", dxgi_swapchain);
                }
            } else {
                LogWarn("Could not get DXGI swapchain from ReShade swapchain for Present "
                        "hooking");
            }
        }
        // Try to hook DX9 Present calls if this is a DX9 device
        // Get the underlying DX9 device from the ReShade device
        if (swapchain->get_device()->get_api() == reshade::api::device_api::d3d9) {
            if (auto *device = swapchain->get_device()) {
                if (auto *d3d9_device = reinterpret_cast<IDirect3DDevice9 *>(device->get_native())) {
                    if (display_commanderhooks::d3d9::HookD3D9Present(d3d9_device)) {
                        LogInfo("Successfully hooked DX9 Present calls for device: 0x%p", d3d9_device);
                    } else {
                        LogInfo("DX9 Present hooking not available for device: 0x%p (may not be DX9)", d3d9_device);
                    }
                } else {
                    LogInfo("Could not get DX9 device from ReShade device for Present hooking");
                }
            }
        }
    }
}

// Centralized initialization method
void DoInitializationWithHwnd(HWND hwnd) {
    bool expected = false;
    if (!g_initialized_with_hwnd.compare_exchange_strong(expected, true)) {
        return; // Already initialized
    }

    // Install XInput hooks if enabled
    if (settings::g_developerTabSettings.load_xinput.GetValue()) {
        display_commanderhooks::InstallXInputHooks();
    } else {
        LogInfo("XInput hooks installation skipped - Load XInput setting is disabled");
    }

    LogInfo("DoInitialization: Starting initialization with HWND: 0x%p", hwnd);

    // Initialize display cache
    display_cache::g_displayCache.Initialize();

    // Capture initial display state for restoration
    display_initial_state::g_initialDisplayState.CaptureInitialState();

    // Initialize input remapping system
    display_commander::input_remapping::initialize_input_remapping();

    // Initialize UI system
    ui::new_ui::InitializeNewUISystem();
    StartContinuousMonitoring();
    StartGPUCompletionMonitoring();

    // Initialize refresh rate monitoring
    dxgi::fps_limiter::StartRefreshRateMonitoring();

    // Initialize experimental tab
    std::thread(RunBackgroundAudioMonitor).detach();

    // Check for auto-enable NVAPI features for specific games
    g_nvapiFullscreenPrevention.CheckAndAutoEnable();

    // Initialize fake NVAPI if enabled
    nvapi::g_fakeNvapiManager.Initialize();

    ui::new_ui::InitExperimentalTab();

    // Initialize DualSense support
    display_commander::widgets::dualsense_widget::InitializeDualSenseWidget();

    // Install HID suppression hooks if enabled
    if (settings::g_experimentalTabSettings.hid_suppression_enabled.GetValue()) {
        renodx::hooks::InstallHIDSuppressionHooks();
    }

    // Install additional HID hooks for statistics tracking
    display_commanderhooks::InstallAdditionalHIDHooks();

    // Set up window hooks if we have a valid HWND
    if (hwnd != nullptr && IsWindow(hwnd)) {
        LogInfo("DoInitialization: Setting up window hooks for HWND: 0x%p", hwnd);

        // Set the game window for API hooks
        display_commanderhooks::SetGameWindow(hwnd);

        // Save the display device ID for the game window
        settings::SaveGameWindowDisplayDeviceId(hwnd);
    }

    LogInfo("DoInitialization: Initialization completed");
    // Set the game window for API hooks
    display_commanderhooks::SetGameWindow(hwnd);

    // Install window procedure hooks
    if (display_commanderhooks::InstallWindowProcHooks(hwnd)) {
        LogInfo("Window procedure hooks installed successfully");
    } else {
        LogError("Failed to install window procedure hooks");
    }

    // Install Streamline hooks
    if (InstallStreamlineHooks()) {
        LogInfo("Streamline hooks installed successfully");
    } else {
        LogInfo("Streamline hooks not installed (Streamline not detected)");
    }

    // Initialize keyboard tracking system
    display_commanderhooks::keyboard_tracker::Initialize();
    LogInfo("Keyboard tracking system initialized");
}

std::atomic<LONGLONG> g_present_start_time_ns{0};
std::atomic<LONGLONG> g_present_duration_ns{0};

// Render start time tracking
std::atomic<LONGLONG> g_submit_start_time_ns{0};

// Present after end time tracking
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

void HandleRenderStartAndEndTimes() {
    LONGLONG expected = 0;
    if (g_submit_start_time_ns.load() == 0) {
        LONGLONG now_ns = utils::get_now_ns();
        LONGLONG present_after_end_time_ns = g_sim_start_ns.load();
        if (present_after_end_time_ns > 0 && g_submit_start_time_ns.compare_exchange_strong(expected, now_ns)) {
            // Compare to g_present_after_end_time
            LONGLONG g_simulation_duration_ns_new = (now_ns - present_after_end_time_ns);
            g_simulation_duration_ns.store(
                UpdateRollingAverage(g_simulation_duration_ns_new, g_simulation_duration_ns.load()));

            if (s_reflex_enable_current_frame.load()) {
                if (s_reflex_generate_markers.load()) {
                    if (g_latencyManager->IsInitialized()) {
                        g_latencyManager->SetMarker(LatencyMarkerType::SIMULATION_END);
                        g_latencyManager->SetMarker(LatencyMarkerType::RENDERSUBMIT_START);
                    }
                }
            }
        }
    }
}

void HandleEndRenderSubmit() {
    LONGLONG now_ns = utils::get_now_ns();
    g_render_submit_end_time_ns.store(now_ns);
    if (g_submit_start_time_ns.load() > 0) {
        LONGLONG g_render_submit_duration_ns_new = (now_ns - g_submit_start_time_ns.load());
        g_render_submit_duration_ns.store(
            UpdateRollingAverage(g_render_submit_duration_ns_new, g_render_submit_duration_ns.load()));
    }
}

void HandleOnPresentEnd() {
    LONGLONG now_ns = utils::get_now_ns();

    g_sim_start_ns.store(now_ns);
    g_submit_start_time_ns.store(0);

    if (g_render_submit_end_time_ns.load() > 0) {
        LONGLONG g_reshade_overhead_duration_ns_new = (now_ns - g_render_submit_end_time_ns.load());
        g_reshade_overhead_duration_ns.store(
            UpdateRollingAverage(g_reshade_overhead_duration_ns_new, g_reshade_overhead_duration_ns.load()));
    }
}

// Query DXGI composition state - should only be called from DXGI present hooks
void QueryDxgiCompositionState(IDXGISwapChain *dxgi_swapchain) {
    if (dxgi_swapchain == nullptr) {
        return;
    }

    // Periodically refresh colorspace and enumerate devices (approx every 4
    // seconds at 60fps = 240 frames)
    static int present_after_counter = 0;
    if (present_after_counter % 256 == 1) {
        // Compute DXGI composition state and log on change
        DxgiBypassMode mode = GetIndependentFlipState(dxgi_swapchain);

        // Update shared state for fast reads on present
        s_dxgi_composition_state.store(mode);
    }
    present_after_counter++;
}

void RecordFrameTime(FrameTimeMode reason) {
    // Filter calls based on the selected frame time mode
    FrameTimeMode frame_time_mode = static_cast<FrameTimeMode>(settings::g_mainTabSettings.frame_time_mode.GetValue());

    // Only record if the call reason matches the selected mode
    if (reason != frame_time_mode) {
        return; // Skip recording for this call reason
    }

    static LONGLONG start_time_ns = utils::get_now_ns();
    LONGLONG now_ns = utils::get_now_ns();
    const double elapsed = static_cast<double>(now_ns - start_time_ns) / static_cast<double>(utils::SEC_TO_NS);
    g_perf_time_seconds.store(elapsed, std::memory_order_release);
    static double last_tp = 0.0;
    const double dt = elapsed - last_tp;
    if (dt > 0.0) {
        const float fps = static_cast<float>(1.0 / dt);
        uint32_t idx = g_perf_ring_head.fetch_add(1, std::memory_order_acq_rel);
        g_perf_ring[idx & (kPerfRingCapacity - 1)] = PerfSample{elapsed, fps};
        last_tp = elapsed;
    }
}

// Get the sync interval coefficient for FPS calculation
float GetSyncIntervalCoefficient(float sync_interval_value) {
    // Map sync interval values to their corresponding coefficients
    // 3 = V-Sync (1x), 4 = V-Sync 2x, 5 = V-Sync 3x, 6 = V-Sync 4x
    switch (static_cast<int>(sync_interval_value)) {
    case 0:
        return 0.0f; // App Controlled
    case 1:
        return 0.0f; // No-VSync
    case 2:
        return 1.0f; // V-Sync
    case 3:
        return 2.0f; // V-Sync 2x
    case 4:
        return 3.0f; // V-Sync 3x
    case 5:
        return 4.0f; // V-Sync 4x
    default:
        return 1.0f; // Fallback
    }
}

// Convert ComboSetting value to reshade::api::format
static reshade::api::format GetFormatFromComboValue(int combo_value) {
    switch (combo_value) {
    case 0:
        return reshade::api::format::r8g8b8a8_unorm; // R8G8B8A8_UNORM
    case 1:
        return reshade::api::format::r10g10b10a2_unorm; // R10G10B10A2_UNORM
    case 2:
        return reshade::api::format::r16g16b16a16_float; // R16G16B16A16_FLOAT
    default:
        return reshade::api::format::r8g8b8a8_unorm; // Default fallback
    }
}

// Capture sync interval during create_swapchain
bool OnCreateSwapchainCapture2(reshade::api::device_api api, reshade::api::swapchain_desc &desc, void *hwnd) {
    // Don't reset counters on swapchain creation - let them accumulate throughout the session

    // Increment event counter
    g_reshade_event_counters[RESHADE_EVENT_CREATE_SWAPCHAIN_CAPTURE].fetch_add(1);
    g_swapchain_event_total_count.fetch_add(1);

    if (hwnd == nullptr)
        return false;

    // Initialize if not already done
    DoInitializationWithHwnd(static_cast<HWND>(hwnd));

    // Check if this is a supported API (D3D9, D3D10, D3D11, D3D12)
    const bool is_d3d9 = (api == reshade::api::device_api::d3d9);
    const bool is_dxgi = (api == reshade::api::device_api::d3d12 || api == reshade::api::device_api::d3d11 || api == reshade::api::device_api::d3d10);

    // D3D9 FLIPEX upgrade logic (only for D3D9)
    if (is_d3d9) {
        // log desc
        {
            std::ostringstream oss;
            oss << "OnCreateSwapchainCapture - ";
            oss << "API: " << static_cast<int>(api) << ", ";
            oss << "Fullscreen: " << (desc.fullscreen_state ? "YES" : "NO") << ", ";
            oss << "Back Buffers: " << desc.back_buffer_count << ", ";
            oss << "Present Mode: " << D3DSwapEffectToString(desc.present_mode) << ", ";
            oss << "Sync Interval: " << desc.sync_interval << ", ";
            oss << "Present Flags: " << D3DPresentFlagsToString(desc.present_flags) << ", ";
            oss << "Back Buffer: " << desc.back_buffer.texture.width << "x" << desc.back_buffer.texture.height << ", ";
            oss << "Back Buffer Format: " << (long long)desc.back_buffer.texture.format << ", ";
            oss << "Back Buffer Usage: " << (long long)desc.back_buffer.usage;
            oss << "Multisample: " << desc.back_buffer.texture.samples << ", ";
            LogInfo(oss.str().c_str());
        }


        bool modified = false;
        if (desc.fullscreen_state && settings::g_developerTabSettings.prevent_fullscreen.GetValue()) {
            if (!settings::g_developerTabSettings.prevent_fullscreen.GetValue()) {
                LogWarn("D3D9: Fullscreen state change blocked by developer settings");
                return false;
            }
            LogInfo("D3D9: Changed fullscreen state from %s to %s", desc.fullscreen_state ? "YES" : "NO", desc.fullscreen_state ? "NO" : "YES");
            desc.fullscreen_state = false;
            modified = true;
        }

        // Apply FLIPEX if all requirements are met
        if (settings::g_experimentalTabSettings.d3d9_flipex_enabled.GetValue() && desc.present_mode != D3DSWAPEFFECT_FLIPEX) {

            if (desc.back_buffer_count < 3) {
                LogInfo("D3D9 FLIPEX: Increasing back buffer count from %u to 2 (required for FLIPEX)",
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
            LogInfo("D3D9 FLIPEX: Full-screen: %s, Back buffers: %u",
                   desc.fullscreen_state ? "YES" : "NO", desc.back_buffer_count);

            desc.present_mode = D3DSWAPEFFECT_FLIPEX;
            if (desc.sync_interval != D3DPRESENT_INTERVAL_IMMEDIATE) {
                LogInfo("D3D9 FLIPEX: Setting sync interval to immediate");
                desc.sync_interval = D3DPRESENT_INTERVAL_IMMEDIATE;
                modified = true;
            }
            if ((desc.present_flags & D3DPRESENT_DONOTFLIP) != 0) {
                LogInfo("D3D9 FLIPEX: Stripping D3DPRESENT_DONOTFLIP flag");
                desc.present_flags &= ~D3DPRESENT_DONOTFLIP; // only fullscreen mode is supported
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
            LogInfo("D3D9 FLIPEX: FLIPEX cannot be applied. Present mode is %u", desc.present_mode);
            // FLIPEX cannot be applied, set to false
            g_used_flipex.store(false);
        }
        return modified;
    }
    else if (is_dxgi) {


        // Apply sync interval setting if enabled
        bool modified = false;

        uint32_t prev_sync_interval = UINT32_MAX;
        uint32_t prev_present_flags = desc.present_flags;
        uint32_t prev_back_buffer_count = desc.back_buffer_count;
        uint32_t prev_present_mode = desc.present_mode;
        const bool is_flip =
            (desc.present_mode == DXGI_SWAP_EFFECT_FLIP_DISCARD || desc.present_mode == DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL);


        // Explicit VSYNC overrides take precedence over generic sync-interval
        // dropdown (applies to all APIs)
        if (s_force_vsync_on.load()) {
            desc.sync_interval = 1; // VSYNC on
            modified = true;
        } else if (s_force_vsync_off.load()) {
            desc.sync_interval = 0; // VSYNC off
            modified = true;
        }

        // DXGI-specific settings (only for D3D10/11/12)
        if (s_prevent_tearing.load() && (desc.present_flags & DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING) != 0) {
            desc.present_flags &= ~DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;
            modified = true;
        }


        // Enable flip chain if enabled (experimental feature) - forces flip model
        if (settings::g_experimentalTabSettings.enable_flip_chain_enabled.GetValue()
        || s_enable_flip_chain.load()) {
            if (desc.back_buffer_count < 2) {
                desc.back_buffer_count = 2;
                modified = true;

                LogInfo("DXGI: Increasing back buffer count from %u to 2", desc.back_buffer_count);
            }
            // Check if current present mode is NOT a flip model
            const bool is_flip_discard = (desc.present_mode == DXGI_SWAP_EFFECT_FLIP_DISCARD);
            const bool is_flip_sequential = (desc.present_mode == DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL);
            const bool is_traditional = (desc.present_mode == DXGI_SWAP_EFFECT_DISCARD || desc.present_mode == DXGI_SWAP_EFFECT_SEQUENTIAL);

            if (is_traditional) {
                // Store original mode for logging
                uint32_t original_mode = desc.present_mode;

                // Force flip model swap chain (FLIP_DISCARD is more performant than FLIP_SEQUENTIAL)
                desc.present_mode = DXGI_SWAP_EFFECT_FLIP_DISCARD;
                modified = true;

                // Log the change
                std::ostringstream flip_oss;
                flip_oss << "Enable Flip Chain: Changed present mode from ";
                if (original_mode == DXGI_SWAP_EFFECT_DISCARD) {
                    flip_oss << "DISCARD";
                } else {
                    flip_oss << "SEQUENTIAL";
                }
                flip_oss << " to FLIP_DISCARD (flip model swap chain)";
                LogInfo("%s", flip_oss.str().c_str());
            }
        }
        // Apply backbuffer format override if enabled (all APIs)
        if (settings::g_experimentalTabSettings.backbuffer_format_override_enabled.GetValue()) {
            reshade::api::format original_format = desc.back_buffer.texture.format;
            reshade::api::format target_format =
                GetFormatFromComboValue(settings::g_experimentalTabSettings.backbuffer_format_override.GetValue());

            if (original_format != target_format) {
                desc.back_buffer.texture.format = target_format;
                modified = true;

                // Log the format change
                std::ostringstream format_oss;
                format_oss << "Backbuffer format override: " << static_cast<int>(original_format) << " -> "
                           << static_cast<int>(target_format);
                LogInfo("%s", format_oss.str().c_str());
            }
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
                oss << ", Present Flags: 0x" << std::hex << prev_present_flags << " -> 0x" << desc.present_flags;
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
            return modified; // return true if we modified the desc
        }
    }

    LogWarn("OnCreateSwapchainCapture: Not a supported device API - %d", static_cast<int>(api));
    return false;
}

bool OnCreateSwapchainCapture(reshade::api::device_api api, reshade::api::swapchain_desc &desc, void *hwnd) {

    auto res = OnCreateSwapchainCapture2(api, desc, hwnd);

    // Store swapchain description for UI display
    auto initial_desc_copy = std::make_shared<reshade::api::swapchain_desc>(desc);
    if (desc.back_buffer.texture.width < 640) {
        return false;
    }
    g_last_swapchain_desc.store(initial_desc_copy);
    return res;
}


void OnInitSwapchain(reshade::api::swapchain *swapchain, bool resize) {
    if (swapchain == nullptr) {
        LogDebug("OnInitSwapchain: swapchain is null");
        return;
    }

    // Increment event counter
    g_reshade_event_counters[RESHADE_EVENT_INIT_SWAPCHAIN].fetch_add(1);
    g_swapchain_event_total_count.fetch_add(1);

    // backbuffer desc
    HWND hwnd = static_cast<HWND>(swapchain->get_hwnd());
    if (!hwnd) {
        return;
    }
    reshade::api::effect_runtime* first_runtime = GetFirstReShadeRuntime();
    if (first_runtime != nullptr && first_runtime->get_hwnd() != hwnd) {
        static int log_count = 0;
        if (log_count < 100) {
            LogInfo("Invalid Runtime HWND OnPresentUpdateBefore - First ReShade runtime: 0x%p, hwnd: 0x%p", first_runtime, hwnd);
            log_count++;
        }
        return;
    }
    // how to check
    hookToSwapChain(swapchain);
}

HANDLE g_timer_handle = nullptr;
LONGLONG TimerPresentPacingDelayStart() {
    LONGLONG start_ns = utils::get_now_ns();
    float delay_percentage = s_present_pacing_delay_percentage.load();
    if (delay_percentage > 0.0f) {
        // Calculate frame time from the most recent performance sample
        const uint32_t head = g_perf_ring_head.load(std::memory_order_acquire);
        if (head > 0) {
            const uint32_t last_idx = (head - 1) & (kPerfRingCapacity - 1);
            const PerfSample &last_sample = g_perf_ring[last_idx];
            if (last_sample.fps > 0.0f) {
                // Convert FPS to frame time in milliseconds, then to nanoseconds
                float frame_time_ms = 1000.0f / last_sample.fps;
                float delay_ms = frame_time_ms * (delay_percentage / 100.0f);
                LONGLONG delta_ns = static_cast<LONGLONG>(delay_ms * utils::NS_TO_MS);
                delta_ns -= late_amount_ns.load();
                if (delta_ns > 0) {
                    utils::wait_until_ns(utils::get_now_ns() + delta_ns, g_timer_handle);
                }
            }
        }
    }
    return start_ns;
}

LONGLONG TimerPresentPacingDelayEnd(LONGLONG start_ns) {
    LONGLONG end_ns = utils::get_now_ns();
    fps_sleep_after_on_present_ns.store(end_ns - start_ns);
    return end_ns;
}

void OnPresentUpdateAfter2(void* native_device, DeviceTypeDC device_type) {
    // Track render thread ID
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

    // Increment event counter
    g_reshade_event_counters[RESHADE_EVENT_PRESENT_UPDATE_AFTER].fetch_add(1);
    g_swapchain_event_total_count.fetch_add(1);

    if (s_reflex_enable_current_frame.load()) {
        if (s_reflex_generate_markers.load()) {
            if (g_latencyManager->IsInitialized()) {
                g_latencyManager->SetMarker(LatencyMarkerType::PRESENT_END);
            }
        }
    }

    // g_present_duration
    LONGLONG now_ns = utils::get_now_ns();

    // Sim-to-display latency measurement
    // Track that OnPresentUpdateAfter2 was called
    LONGLONG sim_start_for_measurement = g_sim_start_ns_for_measurement.load();
    if (sim_start_for_measurement > 0) {
        g_present_update_after2_called.store(true);
        g_present_update_after2_time_ns.store(now_ns);

        // If GPU completion callback was already finished, we're finishing second
        if (g_gpu_completion_callback_finished.load()) {
            // Calculate sim-to-display latency
            LONGLONG latency_new_ns = now_ns - sim_start_for_measurement;

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

    LONGLONG g_present_duration_new_ns = (now_ns - g_present_start_time_ns.load()); // Convert QPC ticks to seconds (QPC
                                                                                     // frequency is typically 10MHz)
    g_present_duration_ns.store(UpdateRollingAverage(g_present_duration_new_ns, g_present_duration_ns.load()));

    // GPU completion measurement (non-blocking check)
    // GPU completion measurement is now handled by dedicated thread in gpu_completion_monitoring.cpp
    // This provides accurate completion time by waiting on the event in a blocking manner

    // Mark Present end for latent sync limiter timing
    if (dxgi::latent_sync::g_latentSyncManager) {
        auto &latent = dxgi::latent_sync::g_latentSyncManager->GetLatentLimiter();
        latent.OnPresentEnd();
    }
    auto start_ns = TimerPresentPacingDelayStart();

    // Input blocking in background is now handled by Windows message hooks
    // instead of ReShade's block_input_next_frame() for better compatibility

    // DXGI composition state computation and periodic device/colorspace refresh
    // (moved from continuous monitoring thread to avoid accessing
    // g_last_swapchain_ptr_unsafe from background thread)
    // NVIDIA Reflex: SIMULATION_END marker (minimal) and Sleep
    if (s_reflex_enable.load()) {
        s_reflex_enable_current_frame.store(true);
        if (native_device && g_latencyManager->Initialize(native_device, device_type)) {
            g_latencyManager->IncreaseFrameId();
            // Apply sleep mode opportunistically each frame to reflect current
            // toggles
            g_latencyManager->ApplySleepMode(s_reflex_low_latency.load(), s_reflex_boost.load(),
                                             s_reflex_use_markers.load());
            if (s_reflex_enable_sleep.load()) {
                g_latencyManager->Sleep();
            }
            if (s_reflex_generate_markers.load()) {
                g_latencyManager->SetMarker(LatencyMarkerType::SIMULATION_START);
            }
        }
    } else {
        s_reflex_enable_current_frame.store(false);
        if (g_latencyManager->IsInitialized()) {
            g_latencyManager->Shutdown();
        }
    }
    auto end_ns = TimerPresentPacingDelayEnd(start_ns);
    HandleOnPresentEnd();

    RecordFrameTime(FrameTimeMode::kFrameBegin);
}

void flush_command_queue_with_command_queue(reshade::api::command_queue *command_queue) {
    if (ShouldBackgroundSuppressOperation())
        return;

    command_queue->flush_immediate_command_list();
}

void flush_command_queue() {
    if (ShouldBackgroundSuppressOperation())
        return;

    reshade::api::effect_runtime* runtime = GetFirstReShadeRuntime();

    if (runtime != nullptr) {
        runtime->get_command_queue()->flush_immediate_command_list();
    } else {
        LogError("flush_command_queue failed: no ReShade runtime available");
    }
}

void HandleFpsLimiter() {
    LONGLONG handle_fps_limiter_start_time_ns = utils::get_now_ns();
    // Use background flag computed by monitoring thread; avoid
    // GetForegroundWindow here

    float target_fps = 0.0f;
    if (g_app_in_background.load()) {
        target_fps = s_fps_limit_background.load();
    } else {
        target_fps = s_fps_limit.load();
    }
    if (target_fps > 0.0f && target_fps < 10.0f) {
        target_fps = 0.0f;
    }
    late_amount_ns.store(0);
    if (target_fps > 0.0f || s_fps_limiter_mode.load() == FpsLimiterMode::kLatentSync) {
        flush_command_queue();

        // Call FPS Limiter on EVERY frame (not throttled)
        switch (s_fps_limiter_mode.load()) {
        case FpsLimiterMode::kDisabled: {
            // No FPS limiting - do nothing
            break;
        }
        case FpsLimiterMode::kReflex: {
            if (!s_reflex_auto_configure.load()) {
                s_reflex_auto_configure.store(true);
            }
            // Reflex mode - auto-configuration is handled when mode is selected
            // Reflex manages frame rate limiting internally
            break;
        }
        case FpsLimiterMode::kOnPresentSync: {
            // Use FPS limiter manager for OnPresent Frame Synchronizer mode
            if (dxgi::fps_limiter::g_customFpsLimiter) {
                auto &limiter = dxgi::fps_limiter::g_customFpsLimiter;
                if (target_fps > 0.0f) {
                    limiter->LimitFrameRate(target_fps);
                }
            }
            break;
        }
        case FpsLimiterMode::kLatentSync: {
            // Use latent sync manager for VBlank Scanline Sync mode
            if (dxgi::latent_sync::g_latentSyncManager) {
                auto &latent = dxgi::latent_sync::g_latentSyncManager->GetLatentLimiter();
                if (target_fps > 0.0f) {
                    latent.LimitFrameRate();
                }
            }
            break;
        }
        case FpsLimiterMode::kNonReflexLowLatency: {
            // Non-Reflex Low Latency Mode not implemented yet - treat as disabled
            LogInfo("FPS Limiter: Non-Reflex Low Latency Mode - Not implemented yet");
            break;
        }
        }
    }

    LONGLONG handle_fps_limiter_start_end_time_ns = utils::get_now_ns();
    g_present_start_time_ns.store(handle_fps_limiter_start_end_time_ns);

    LONGLONG handle_fps_limiter_start_duration_ns =
        handle_fps_limiter_start_end_time_ns - handle_fps_limiter_start_time_ns;
    fps_sleep_before_on_present_ns.store(
        UpdateRollingAverage(handle_fps_limiter_start_duration_ns, fps_sleep_before_on_present_ns.load()));
}

// Helper function to automatically set color space based on format
void AutoSetColorSpace(reshade::api::swapchain *swapchain) {
    if (!s_auto_colorspace.load()) {
        return;
    }

    // Get current swapchain description
    auto desc_ptr = g_last_swapchain_desc.load();
    if (!desc_ptr) {
        return;
    }

    const auto& desc = *desc_ptr;
    auto format = desc.back_buffer.texture.format;

    // Determine appropriate color space based on format
    DXGI_COLOR_SPACE_TYPE color_space;
    reshade::api::color_space reshade_color_space;
    std::string color_space_name;

    if (format == reshade::api::format::r10g10b10a2_unorm) {
        color_space = DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020; // HDR10
        reshade_color_space = reshade::api::color_space::hdr10_st2084;
        color_space_name = "HDR10 (ST2084)";
    } else if (format == reshade::api::format::r16g16b16a16_float) {
        color_space = DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709; // scRGB
        reshade_color_space = reshade::api::color_space::extended_srgb_linear;
        color_space_name = "scRGB (Linear)";
    } else if (format == reshade::api::format::r8g8b8a8_unorm) {
        color_space = DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709; // sRGB
        reshade_color_space = reshade::api::color_space::srgb_nonlinear;
        color_space_name = "sRGB (Non-linear)";
    } else {
        LogError("AutoSetColorSpace: Unsupported format %d", static_cast<int>(format));
        return; // Unsupported format
    }

    auto* unknown = reinterpret_cast<IUnknown*>(swapchain->get_native());
    if (unknown == nullptr) {
        return;
    }

    Microsoft::WRL::ComPtr<IDXGISwapChain3> swapchain3;
    HRESULT hr = unknown->QueryInterface(IID_PPV_ARGS(&swapchain3));
    if (FAILED(hr)) {
        return;
    }

    // Check if the color space is supported before trying to set it
    UINT color_space_support = 0;
    hr = swapchain3->CheckColorSpaceSupport(color_space, &color_space_support);
    if (FAILED(hr) || color_space_support == 0) {
        // Try fallback to basic sRGB color space
        DXGI_COLOR_SPACE_TYPE fallback_color_space = DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;
        UINT fallback_support = 0;
        hr = swapchain3->CheckColorSpaceSupport(fallback_color_space, &fallback_support);
        if (SUCCEEDED(hr) && fallback_support > 0) {
            swapchain3->SetColorSpace1(fallback_color_space);

            // Set ReShade runtime color space to sRGB fallback
            reshade::api::effect_runtime* runtime = GetFirstReShadeRuntime();
            if (runtime != nullptr) {
                runtime->set_color_space(reshade::api::color_space::srgb_nonlinear);
            }
        }
        return;
    }

    // Set the appropriate color space
    swapchain3->SetColorSpace1(color_space);

    // Set ReShade runtime color space
    reshade::api::effect_runtime* runtime = GetFirstReShadeRuntime();
    if (runtime != nullptr) {
        runtime->set_color_space(reshade_color_space);
    }
}
// Update composition state after presents (required for valid stats)
void OnPresentUpdateBefore(reshade::api::command_queue * command_queue, reshade::api::swapchain *swapchain,
                           const reshade::api::rect * /*source_rect*/, const reshade::api::rect * /*dest_rect*/,
                           uint32_t /*dirty_rect_count*/, const reshade::api::rect * /*dirty_rects*/) {
    if (swapchain == nullptr) {
        return;
    }

    HWND hwnd = static_cast<HWND>(swapchain->get_hwnd());
    if (hwnd == g_proxy_hwnd) {
        return;
    }

    reshade::api::effect_runtime* first_runtime = GetFirstReShadeRuntime();
    if (first_runtime != nullptr && first_runtime->get_hwnd() != hwnd) {
        static int log_count = 0;
        if (log_count < 100) {
            LogInfo("Invalid Runtime HWND OnPresentUpdateBefore - First ReShade runtime: 0x%p, hwnd: 0x%p", first_runtime, hwnd);
            log_count++;
        }
        return;
    }

    hookToSwapChain(swapchain);

    // Auto set color space if enabled
    AutoSetColorSpace(swapchain);

    // Record the native DXGI swapchain for Present detour filtering
    if (swapchain->get_device()->get_api() == reshade::api::device_api::d3d12 ||
        swapchain->get_device()->get_api() == reshade::api::device_api::d3d11 ||
        swapchain->get_device()->get_api() == reshade::api::device_api::d3d10) {
        IDXGISwapChain* dxgi_swapchain = reinterpret_cast<IDXGISwapChain*>(swapchain->get_native());
        if (dxgi_swapchain != nullptr) {
            display_commanderhooks::dxgi::RecordPresentUpdateSwapchain(dxgi_swapchain);
        }
    }

    // Record the native D3D9 device for Present detour filtering
    if (swapchain->get_device()->get_api() == reshade::api::device_api::d3d9) {
        IDirect3DDevice9* d3d9_device = reinterpret_cast<IDirect3DDevice9*>(swapchain->get_device()->get_native());
        if (d3d9_device != nullptr) {
            display_commanderhooks::d3d9::RecordPresentUpdateDevice(d3d9_device);
        }
    }

    HandleRenderStartAndEndTimes();

    HandleEndRenderSubmit();
    // NVIDIA Reflex: RENDERSUBMIT_END marker (minimal)
    if (s_reflex_enable_current_frame.load()) {
        if (s_reflex_generate_markers.load()) {
            g_latencyManager->SetMarker(LatencyMarkerType::RENDERSUBMIT_END);
        }
    }
    // Always flush command queue before present to reduce latency
    g_flush_before_present_time_ns.store(utils::get_now_ns());

    // Enqueue GPU completion measurement BEFORE flush for accurate timing
    // This captures the full GPU workload including the flush operation
    if (swapchain->get_device()->get_api() == reshade::api::device_api::d3d11 ||
        swapchain->get_device()->get_api() == reshade::api::device_api::d3d12) {
        EnqueueGPUCompletion(swapchain, command_queue);
    } else {
        g_sim_start_ns_for_measurement.store(g_sim_start_ns.load());
        g_present_update_after2_called.store(false);
        g_gpu_completion_callback_finished.store(false);
    }

    flush_command_queue_with_command_queue(command_queue); // Flush command queue before addons start processing
                           // to reduce rendering latency caused by reshade

    // Increment event counter
    g_reshade_event_counters[RESHADE_EVENT_PRESENT_UPDATE_BEFORE].fetch_add(1);
    g_swapchain_event_total_count.fetch_add(1);

    // Check for XInput chord screenshot trigger
    display_commander::widgets::xinput_widget::CheckAndHandleScreenshot();


    // Note: DXGI composition state query moved to QueryDxgiCompositionState()
    // and is now called only from DXGI present hooks

}

bool OnBindPipeline(reshade::api::command_list *cmd_list, reshade::api::pipeline_stage stages,
                    reshade::api::pipeline pipeline) {
    // Increment event counter
    g_reshade_event_counters[RESHADE_EVENT_BIND_PIPELINE].fetch_add(1);
    g_swapchain_event_total_count.fetch_add(1);

    // Power saving: skip pipeline binding in background if enabled
    if (s_suppress_binding_in_background.load() && ShouldBackgroundSuppressOperation()) {
        return true; // Skip the pipeline binding
    }

    return false; // Don't skip the pipeline binding
}

// Present flags callback to strip DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING
void OnPresentFlags2(uint32_t *present_flags, DeviceTypeDC api_type) {
    // Increment event counter
    g_reshade_event_counters[RESHADE_EVENT_PRESENT_FLAGS].fetch_add(1);
    g_swapchain_event_total_count.fetch_add(1);

    if (api_type == DeviceTypeDC::DX11 || api_type == DeviceTypeDC::DX12 || api_type == DeviceTypeDC::DX10)
    {
        // Always strip DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING flag
        if (s_prevent_tearing.load() && *present_flags & DXGI_PRESENT_ALLOW_TEARING) {
            *present_flags &= ~DXGI_PRESENT_ALLOW_TEARING;

            // Log the flag removal for debugging
            std::ostringstream oss;
            oss << "Present flags callback: Stripped "
                "DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING, new flags: 0x"
                << std::hex << *present_flags;
            LogInfo(oss.str().c_str());
        }
        // Don't block presents if continue rendering is enabled
        if (s_no_present_in_background.load() && g_app_in_background.load(std::memory_order_acquire) &&
            !s_continue_rendering.load()) {
            *present_flags = DXGI_PRESENT_DO_NOT_SEQUENCE;
        }
    }

    HandleFpsLimiter();

    if (s_reflex_enable_current_frame.load()) {
        if (s_reflex_generate_markers.load()) {
            if (g_latencyManager->IsInitialized()) {
                g_latencyManager->SetMarker(LatencyMarkerType::PRESENT_START);
            }
        }
    }
}


// Resource creation event handler to upgrade buffer resolutions and texture
// formats
bool OnCreateResource(reshade::api::device *device, reshade::api::resource_desc &desc,
                      reshade::api::subresource_data * /*initial_data*/, reshade::api::resource_usage /*usage*/) {
    bool modified = false;

    // Only handle 2D textures
    if (desc.type != reshade::api::resource_type::texture_2d) {
        return false; // No modification needed
    }

    if (!is_target_resolution(desc.texture.width, desc.texture.height)) {
        return false; // No modification needed
    }

    // Handle buffer resolution upgrade if enabled
    if (settings::g_experimentalTabSettings.buffer_resolution_upgrade_enabled.GetValue()) {
        uint32_t original_width = desc.texture.width;
        uint32_t original_height = desc.texture.height;

        if (original_width != target_width || original_height != target_height) {
            desc.texture.width = target_width;
            desc.texture.height = target_height;

            LogInfo("ZZZ Buffer resolution upgrade: %d,%d %dx%d -> %d,%d %dx%d", original_width, original_height,
                    original_width, original_height, target_width.load(), target_height.load(), target_width.load(),
                    target_height.load());

            modified = true;
        }
    }

    if (settings::g_experimentalTabSettings.texture_format_upgrade_enabled.GetValue()) {
        reshade::api::format original_format = desc.texture.format;
        reshade::api::format target_format = reshade::api::format::r16g16b16a16_float; // RGB16A16

        // Only upgrade certain formats to RGB16A16
        bool should_upgrade_format = false;
        switch (original_format) {
        case reshade::api::format::r8g8b8a8_typeless:
        case reshade::api::format::r8g8b8a8_unorm_srgb:
        case reshade::api::format::r8g8b8a8_unorm:
        case reshade::api::format::b8g8r8a8_unorm:
        case reshade::api::format::r8g8b8a8_snorm:
        case reshade::api::format::b8g8r8a8_typeless:
        case reshade::api::format::r8g8b8a8_uint:
        case reshade::api::format::r8g8b8a8_sint:
            should_upgrade_format = true;
            break;
        default:
            // Don't upgrade formats that are already high precision or special
            // formats
            break;
        }

        if (should_upgrade_format && original_format != target_format) {
            desc.texture.format = target_format;

            LogInfo("ZZZ Texture format upgrade: %d -> %d (RGB16A16) at %d,%d", static_cast<int>(original_format),
                    static_cast<int>(target_format), desc.texture.width, desc.texture.height);
            modified = true;
        }
    }

    return modified;
}

// Resource view creation event handler to upgrade render target views for
// buffer resolution and texture format upgrades
bool OnCreateResourceView(reshade::api::device *device, reshade::api::resource resource,
                          reshade::api::resource_usage usage_type, reshade::api::resource_view_desc &desc) {
    bool modified = false;

    if (!device)
        return false;

    reshade::api::resource_desc resource_desc = device->get_resource_desc(resource);

    if (resource_desc.type != reshade::api::resource_type::texture_2d) {
        return false; // No modification needed
    }

    if (!is_target_resolution(resource_desc.texture.width, resource_desc.texture.height)) {
        return false; // No modification needed
    }

    if (settings::g_experimentalTabSettings.texture_format_upgrade_enabled.GetValue()) {
        reshade::api::format resource_format = resource_desc.texture.format;
        reshade::api::format target_format = reshade::api::format::r16g16b16a16_float; // RGB16A16

        if (resource_format == target_format) {
            reshade::api::format original_view_format = desc.format;

            switch (original_view_format) {
            case reshade::api::format::r8g8b8a8_typeless:
            case reshade::api::format::r8g8b8a8_unorm_srgb:
            case reshade::api::format::r8g8b8a8_unorm:
            case reshade::api::format::b8g8r8a8_unorm:
            case reshade::api::format::r8g8b8a8_snorm:
            case reshade::api::format::r8g8b8a8_uint:
            case reshade::api::format::r8g8b8a8_sint:
                desc.format = target_format;

                LogInfo("ZZZ Resource view format upgrade: %d -> %d (RGB16A16)", static_cast<int>(original_view_format),
                        static_cast<int>(target_format));

                return true;
            default:
                // Don't upgrade view formats that are already high precision or special
                // formats
                break;
            }
        }
    }

    return modified;
}

// Viewport event handler to scale viewports for buffer resolution upgrade
void OnSetViewport(reshade::api::command_list *cmd_list, uint32_t first, uint32_t count,
                   const reshade::api::viewport *viewports) {
    // Only handle viewport scaling if buffer resolution upgrade is enabled
    if (!settings::g_experimentalTabSettings.buffer_resolution_upgrade_enabled.GetValue()) {
        return; // No modification needed
    }

    // Get the current backbuffer to determine if we need to scale
    auto *device = cmd_list->get_device();
    if (!device)
        return;

    // Create scaled viewports only for matching dimensions
    std::vector<reshade::api::viewport> scaled_viewports(viewports, viewports + count);
    for (auto &viewport : scaled_viewports) {

        // Only scale viewports that match the source resolution
        if (is_target_resolution(viewport.width, viewport.height)) {
            double scale_width = target_width.load() / viewport.width;
            double scale_height = target_height.load() / viewport.height;
            viewport = {
                static_cast<float>(viewport.x * scale_width),       // x
                static_cast<float>(viewport.y * scale_height),      // y
                static_cast<float>(viewport.width * scale_width),   // width (1280 -> 1280*scale)
                static_cast<float>(viewport.height * scale_height), // height (720 -> 720*scale)
                viewport.min_depth,                                 // min_depth
                viewport.max_depth                                  // max_depth
            };
            LogInfo("ZZZ Viewport scaling: %d,%d %dx%d -> %d,%d %dx%d", viewport.x, viewport.y, viewport.width,
                    viewport.height, viewport.x, viewport.y, viewport.width, viewport.height);
        }
    }

    // Set the scaled viewports - this will override the original viewport setting
    cmd_list->bind_viewports(first, count, scaled_viewports.data());
}

// Scissor rectangle event handler to scale scissor rectangles for buffer
// resolution upgrade
void OnSetScissorRects(reshade::api::command_list *cmd_list, uint32_t first, uint32_t count,
                       const reshade::api::rect *rects) {
    // Only handle scissor scaling if buffer resolution upgrade is enabled
    if (!settings::g_experimentalTabSettings.buffer_resolution_upgrade_enabled.GetValue()) {
        return; // No modification needed
    }

    int mode = settings::g_experimentalTabSettings.buffer_resolution_upgrade_mode.GetValue();
    int scale_factor = settings::g_experimentalTabSettings.buffer_resolution_upgrade_scale_factor.GetValue();

    // Create scaled scissor rectangles only for matching dimensions
    std::vector<reshade::api::rect> scaled_rects(rects, rects + count);

    for (auto &rect : scaled_rects) {
        // Only scale scissor rectangles that match the source resolution
        if (is_target_resolution(rect.right - rect.left, rect.bottom - rect.top)) {
            double scale_width = target_width.load() / (rect.right - rect.left);
            double scale_height = target_height.load() / (rect.bottom - rect.top);
            rect = {
                static_cast<int32_t>(round(rect.left * scale_width)),   // left
                static_cast<int32_t>(round(rect.top * scale_height)),   // top
                static_cast<int32_t>(round(rect.right * scale_width)),  // right
                static_cast<int32_t>(round(rect.bottom * scale_height)) // bottom
            };

            LogInfo("ZZZ Scissor scaling: %d,%d %dx%d -> %d,%d %dx%d", rect.left, rect.top, rect.right - rect.left,
                    rect.bottom - rect.top, rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top);
        }
    }

    // Set the scaled scissor rectangles
    cmd_list->bind_scissor_rects(first, count, scaled_rects.data());
}

// OnSetFullscreenState function removed - fullscreen prevention now handled directly in IDXGISwapChain_SetFullscreenState_Detour