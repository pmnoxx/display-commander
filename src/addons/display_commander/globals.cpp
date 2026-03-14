#include "globals.hpp"
#include <psapi.h>
#include <algorithm>
#include <cctype>
#include <filesystem>
#include "../../../external/nvapi/nvapi.h"
#include "hooks/windows_hooks/api_hooks.hpp"
#include "latency/reflex_provider.hpp"
#include "nvapi/vrr_status.hpp"
#include "settings/advanced_tab_settings.hpp"
#include "settings/experimental_tab_settings.hpp"
#include "settings/hook_suppression_settings.hpp"
#include "settings/hotkeys_tab_settings.hpp"
#include "settings/main_tab_settings.hpp"
#include "settings/reshade_tab_settings.hpp"
#include "settings/streamline_tab_settings.hpp"
#include "settings/swapchain_tab_settings.hpp"
#include "utils.hpp"
#include "utils/logging.hpp"
#include "utils/srwlock_wrapper.hpp"

#include <d3d11.h>
#include <wrl/client.h>
#include <reshade.hpp>

#include <array>
#include <atomic>
#include <cmath>

// Global variables
// UI mode removed - now using new tab system

// DLL initialization state - prevents DXGI calls during DllMain
std::atomic<bool> g_dll_initialization_complete{false};

// Wine/Proton detection - result cached in function static
bool IsUsingWine() {
    static const bool cached = []() {
        HMODULE ntdll = GetModuleHandleW(L"ntdll");
        return ntdll != nullptr && GetProcAddress(ntdll, "wine_get_version") != nullptr;
    }();
    return cached;
}

// Module handle for pinning/unpinning
HMODULE g_hmodule = nullptr;

// Path of the module that caused this DLL to load (set during DLL_PROCESS_ATTACH).
std::string g_dll_load_caller_path;
std::string g_dll_load_call_stack_list;
void* g_dll_main_backtrace[kDllMainBacktraceMax] = {};
USHORT g_dll_main_backtrace_count = 0;
std::string g_dll_main_log_path;

// Our addon DLL module handle (set in AddonInit; atomic for lock-free caller checks in hooks).
std::atomic<HMODULE> g_module{nullptr};

// Track whether module was pinned (for conditional unpinning)
std::atomic<bool> g_module_pinned{false};

// DLL load timestamp in nanoseconds (for conflict resolution)
std::atomic<LONGLONG> g_dll_load_time_ns{0};

// Display Commander state for multi-proxy: HOOKED (one instance) vs PROXY_DLL_ONLY (others)
std::atomic<DisplayCommanderState> g_display_commander_state{DisplayCommanderState::DC_STATE_UNDECIDED};

// Window settings
std::atomic<AspectRatioType> s_aspect_index{AspectRatioType::k16_9};  // Default to 16:9

// Window alignment when repositioning is needed (0 = Center, 1 = Top Left, 2 = Top Right, 3 = Bottom Left, 4 = Bottom
// Right)
std::atomic<WindowAlignment> s_window_alignment{WindowAlignment::kCenter};  // default to center (slot 0)

// NVAPI Fullscreen Prevention

// Mouse position spoofing for auto-click sequences
std::atomic<bool> s_spoof_mouse_position{false};  // disabled by default
std::atomic<int> s_spoofed_mouse_x{0};
std::atomic<int> s_spoofed_mouse_y{0};

// Keyboard Shortcuts

// Auto-click enabled state (atomic, not loaded from config)
std::atomic<bool> g_auto_click_enabled{false};

// NVAPI Settings
std::atomic<bool> s_restart_needed_nvapi{false};

// Performance: background FPS cap

// VSync and tearing controls

// Monitor and display settings
// Continue rendering in background

// DirectInput hook suppression
std::atomic<bool> s_suppress_dinput_hooks{true};  // DirectInput hooks disabled by default

// Logging level: read from main tab settings
LogLevel GetMinLogLevel() { return LogLevelFromComboIndex(settings::g_mainTabSettings.log_level.GetValue()); }

// Input blocking in background (0.0f off, 1.0f on)

// Render blocking in background

// Present blocking in background

// Hide HDR capabilities from applications

// D3D9 to D3D9Ex upgrade
std::atomic<bool> s_d3d9e_upgrade_successful{false};  // Track if upgrade was successful
std::atomic<bool> g_used_flipex{false};               // Track if FLIPEX is currently being used
std::atomic<bool> g_dx9_swapchain_detected{false};    // Set when D3D9 swapchain is detected (skip DXGI swapchain hooks)

// ReShade runtimes for input blocking (multiple runtimes support)
// TODO: clear this vector OnReshadeUnload to fix F.E.A.R with DXVK
// 1. Reshade loads
// 2. Display Commander loads
// 3. Reshade Unloads - needs to clean up OnReshadeUnload
// 4. Reshade loads again without unloading Display Commander
std::vector<reshade::api::effect_runtime*> g_reshade_runtimes;
std::atomic<HMODULE> g_reshade_module{nullptr};
std::atomic<bool> g_is_renodx_loaded{false};

void RefreshReShadeModuleIfNeeded() {
    if (g_reshade_module.load() != nullptr) return;
    HMODULE modules[1024];
    DWORD num_modules_bytes = 0;
    if (K32EnumProcessModules(GetCurrentProcess(), modules, sizeof(modules), &num_modules_bytes) == 0) return;
    DWORD num_modules =
        (std::min<DWORD>)(num_modules_bytes / sizeof(HMODULE), static_cast<DWORD>(sizeof(modules) / sizeof(HMODULE)));
    for (DWORD i = 0; i < num_modules; i++) {
        if (modules[i] == nullptr) continue;
        FARPROC register_func = GetProcAddress(modules[i], "ReShadeRegisterAddon");
        if (register_func != nullptr) {
            HMODULE expected = nullptr;
            if (g_reshade_module.compare_exchange_strong(expected, modules[i])) break;
        }
    }
}

// Prevent always on top behavior

// Background feature - show black window behind game when not fullscreen

// Desktop Resolution Override
std::atomic<int> s_selected_monitor_index{0};  // Primary monitor by default

// Display Tab Enhanced Settings
std::atomic<int> s_selected_resolution_index{0};    // Default to first available resolution
std::atomic<int> s_selected_refresh_rate_index{0};  // Default to first available refresh rate

std::atomic<bool> s_initial_auto_selection_done{false};  // Track if we've done initial auto-selection

// Auto-restore resolution on game close
std::atomic<bool> s_auto_restore_resolution_on_close{true};  // Enabled by default

// Auto-apply resolution and refresh rate changes
std::atomic<bool> s_auto_apply_resolution_change{false};    // Disabled by default
std::atomic<bool> s_auto_apply_refresh_rate_change{false};  // Disabled by default

// Track if resolution was successfully applied at least once
std::atomic<bool> s_resolution_applied_at_least_once{false};  // Disabled by default

// Atomic variables
std::atomic<reshade::api::device_api> g_last_reshade_device_api{static_cast<reshade::api::device_api>(0)};
std::atomic<uint32_t> g_last_api_version{0};
std::atomic<std::shared_ptr<reshade::api::swapchain_desc>> g_last_swapchain_desc_pre{nullptr};
std::atomic<std::shared_ptr<reshade::api::swapchain_desc>> g_last_swapchain_desc_post{nullptr};
std::atomic<bool> g_force_flip_discard_upgrade_done{false};
std::atomic<bool> g_show_auto_colorspace_fix_in_main_tab{true};
std::atomic<HWND> g_last_swapchain_hwnd{nullptr};
std::atomic<HWND> g_standalone_ui_hwnd{nullptr};
std::atomic<bool> g_shutdown{false};
std::atomic<bool> g_muted_applied{false};

// Continuous monitoring system
std::atomic<bool> g_monitoring_thread_running{false};
std::thread g_monitoring_thread;
std::thread g_stuck_check_watchdog_thread;

// Render thread tracking
std::atomic<DWORD> g_render_thread_id{0};

// Global window state instance
std::atomic<std::shared_ptr<GlobalWindowState>> g_window_state = std::make_shared<GlobalWindowState>();

// Global Latent Sync Manager instance
namespace dxgi::latent_sync {
std::unique_ptr<LatentSyncManager> g_latentSyncManager = std::make_unique<LatentSyncManager>();
}

// Global DXGI Device Info Manager instance
// std::unique_ptr<DXGIDeviceInfoManager> g_dxgiDeviceInfoManager = std::make_unique<DXGIDeviceInfoManager>();

// Global Latency Manager instance
std::unique_ptr<ReflexProvider> g_reflexProvider = std::make_unique<ReflexProvider>();

// Global frame ID for latency management
std::atomic<uint64_t> g_global_frame_id{1};

// When g_global_frame_id was last incremented (QPC ns)
std::atomic<LONGLONG> g_global_frame_id_last_updated_ns{0};

// When a Windows message was last processed in the game window's WndProc (QPC ns)
std::atomic<LONGLONG> g_last_window_message_processed_ns{0};

// Global frame ID for pclstats frame id
std::atomic<uint64_t> g_pclstats_frame_id{0};

// Global frame ID when XInput was last successfully detected
std::atomic<uint64_t> g_last_xinput_detected_frame_id{0};

// Global frame ID when NvAPI_D3D_SetSleepMode_Direct was last called
std::atomic<uint64_t> g_last_set_sleep_mode_direct_frame_id{0};

// Last timestamp (ns) at which each FPS limiter call site was hit (0 = never)
std::atomic<uint64_t> g_fps_limiter_last_timestamp_ns[kFpsLimiterCallSiteCount] = {};

std::atomic<uint8_t> g_chosen_fps_limiter_site{kFpsLimiterChosenUnset};

namespace {
// Priority order: reflex_marker, Vulkan reflex paths, dxgi_swapchain1, dxgi_swapchain,
// dxgi_swapchain1_streamline_proxy, dxgi_swapchain_streamline_proxy, dx9_present, ...
constexpr std::array<FpsLimiterCallSite, kFpsLimiterCallSiteCount> kFpsLimiterPriorityOrder = {
    FpsLimiterCallSite::reflex_marker,
    FpsLimiterCallSite::reflex_marker_vk_nvll,
    FpsLimiterCallSite::reflex_marker_vk_loader,
    FpsLimiterCallSite::reflex_marker_pclstats_etw,
    FpsLimiterCallSite::vk_queue_present_khr,
    FpsLimiterCallSite::dxgi_swapchain1,
    FpsLimiterCallSite::dxgi_swapchain,
    FpsLimiterCallSite::dxgi_swapchain1_streamline_proxy,
    FpsLimiterCallSite::dxgi_swapchain_streamline_proxy,
    FpsLimiterCallSite::dx9_present,
    FpsLimiterCallSite::dx9_presentex,
    FpsLimiterCallSite::opengl_swapbuffers,
    FpsLimiterCallSite::ddraw_flip,
    FpsLimiterCallSite::dxgi_factory_wrapper,
    FpsLimiterCallSite::reshade_addon_event,
};

bool IsFpsLimiterSiteEligible(FpsLimiterCallSite site, uint64_t timestamp_ns) {
    const uint64_t last = g_fps_limiter_last_timestamp_ns[static_cast<size_t>(site)].load(std::memory_order_relaxed);
    if (last == 0) {
        return false;
    }
    const uint64_t delta_ns = timestamp_ns - last;
    return delta_ns <= static_cast<uint64_t>(utils::SEC_TO_NS);
}

}  // namespace

const char* FpsLimiterSiteName(FpsLimiterCallSite site) {
    switch (site) {
        case FpsLimiterCallSite::reflex_marker:                    return "reflex_marker";
        case FpsLimiterCallSite::reflex_marker_vk_nvll:            return "reflex_marker_vk_nvll";
        case FpsLimiterCallSite::reflex_marker_vk_loader:          return "reflex_marker_vk_loader";
        case FpsLimiterCallSite::reflex_marker_pclstats_etw:       return "reflex_marker_pclstats_etw";
        case FpsLimiterCallSite::dxgi_swapchain1:                  return "dxgi_swapchain1";
        case FpsLimiterCallSite::dxgi_swapchain:                   return "dxgi_swapchain";
        case FpsLimiterCallSite::dxgi_swapchain1_streamline_proxy: return "dxgi_swapchain1_streamline_proxy";
        case FpsLimiterCallSite::dxgi_swapchain_streamline_proxy:  return "dxgi_swapchain_streamline_proxy";
        case FpsLimiterCallSite::dx9_present:                      return "dx9_present";
        case FpsLimiterCallSite::dx9_presentex:                    return "dx9_presentex";
        case FpsLimiterCallSite::opengl_swapbuffers:               return "opengl_swapbuffers";
        case FpsLimiterCallSite::ddraw_flip:                       return "ddraw_flip";
        case FpsLimiterCallSite::reshade_addon_event:              return "reshade_addon_event";
        case FpsLimiterCallSite::vk_queue_present_khr:              return "vk_queue_present_khr";
        case FpsLimiterCallSite::dxgi_factory_wrapper:             return "dxgi_factory_wrapper";
        default:                                                   return "?";
    }
}

FpsLimiterCallSite GetChosenFrameTimeLocation() {
    const uint8_t chosen = g_chosen_fps_limiter_site.load(std::memory_order_relaxed);
    if (chosen == kFpsLimiterChosenUnset) {
        return FpsLimiterCallSite::reshade_addon_event;
    }
    const FpsLimiterCallSite site = static_cast<FpsLimiterCallSite>(chosen);
    if (site == FpsLimiterCallSite::dxgi_swapchain1 || site == FpsLimiterCallSite::dxgi_swapchain
        || site == FpsLimiterCallSite::dxgi_swapchain1_streamline_proxy
        || site == FpsLimiterCallSite::dxgi_swapchain_streamline_proxy || site == FpsLimiterCallSite::dx9_present
        || site == FpsLimiterCallSite::dx9_presentex || site == FpsLimiterCallSite::opengl_swapbuffers
        || site == FpsLimiterCallSite::ddraw_flip || site == FpsLimiterCallSite::reshade_addon_event
        || site == FpsLimiterCallSite::vk_queue_present_khr) {
        return site;
    }
    return FpsLimiterCallSite::reshade_addon_event;
}

void ChooseFpsLimiter(uint64_t timestamp_ns, FpsLimiterCallSite caller_enum) {
    if (g_thread_tracking_enabled.load(std::memory_order_relaxed)) {
        g_fps_limiter_site_thread_id[static_cast<size_t>(caller_enum)].store(GetCurrentThreadId(),
                                                                             std::memory_order_relaxed);
    }
    // 1. Make decision based on which sites were hit within the last 1s (before recording this call).
    FpsLimiterCallSite new_chosen = FpsLimiterCallSite::reshade_addon_event;  // default (guaranteed)
    for (FpsLimiterCallSite site : kFpsLimiterPriorityOrder) {
        if ((site == FpsLimiterCallSite::reflex_marker || site == FpsLimiterCallSite::reflex_marker_vk_nvll
             || site == FpsLimiterCallSite::reflex_marker_vk_loader
             || site == FpsLimiterCallSite::reflex_marker_pclstats_etw)
            && !settings::g_mainTabSettings.use_reflex_markers_as_fps_limiter.GetValue()) {
            continue;
        }
        if ((site == FpsLimiterCallSite::dxgi_swapchain1_streamline_proxy
             || site == FpsLimiterCallSite::dxgi_swapchain_streamline_proxy)
            && !settings::g_mainTabSettings.use_streamline_proxy_fps_limiter.GetValue()) {
            continue;
        }
        if (settings::g_mainTabSettings.safe_mode_fps_limiter.GetValue()
            && s_fps_limiter_mode.load() == FpsLimiterMode::kOnPresentSync
            && site != FpsLimiterCallSite::reshade_addon_event) {
            continue;
        }
        if (IsFpsLimiterSiteEligible(site, timestamp_ns)) {
            new_chosen = site;
            break;
        }
    }

    const uint8_t new_index = static_cast<uint8_t>(static_cast<size_t>(new_chosen));
    const uint8_t prev = g_chosen_fps_limiter_site.exchange(new_index, std::memory_order_relaxed);

    if (prev != new_index) {
        const char* old_name =
            (prev == kFpsLimiterChosenUnset) ? "unset" : FpsLimiterSiteName(static_cast<FpsLimiterCallSite>(prev));
        LogInfo("FPS limiter source: %s -> %s", old_name, FpsLimiterSiteName(new_chosen));
    }

    // 2. Record this call site with current timestamp (so future decisions see it within 1s).
    g_fps_limiter_last_timestamp_ns[static_cast<size_t>(caller_enum)].store(timestamp_ns, std::memory_order_relaxed);
}

bool GetChosenFpsLimiter(FpsLimiterCallSite caller_enum) {
    const uint8_t chosen = g_chosen_fps_limiter_site.load(std::memory_order_relaxed);
    if (chosen == kFpsLimiterChosenUnset) {
        return false;
    }
    return static_cast<size_t>(caller_enum) == static_cast<size_t>(chosen);
}

const char* GetChosenFpsLimiterSiteName() {
    const uint8_t chosen = g_chosen_fps_limiter_site.load(std::memory_order_relaxed);
    if (chosen == kFpsLimiterChosenUnset) {
        return "unset";
    }
    return FpsLimiterSiteName(static_cast<FpsLimiterCallSite>(chosen));
}

bool IsNativeFramePacingInSync() {
    const uint64_t now_ns = static_cast<uint64_t>(utils::get_now_ns());
    const uint64_t reflex_ts =
        g_fps_limiter_last_timestamp_ns[static_cast<size_t>(FpsLimiterCallSite::reflex_marker)].load();
    if (reflex_ts != 0 && (now_ns - reflex_ts) <= static_cast<uint64_t>(utils::SEC_TO_NS)) {
        return true;
    }
    const uint64_t reflex_vk_nvll_ts =
        g_fps_limiter_last_timestamp_ns[static_cast<size_t>(FpsLimiterCallSite::reflex_marker_vk_nvll)].load();
    if (reflex_vk_nvll_ts != 0 && (now_ns - reflex_vk_nvll_ts) <= static_cast<uint64_t>(utils::SEC_TO_NS)) {
        return true;
    }
    const uint64_t reflex_vk_loader_ts =
        g_fps_limiter_last_timestamp_ns[static_cast<size_t>(FpsLimiterCallSite::reflex_marker_vk_loader)].load();
    if (reflex_vk_loader_ts != 0 && (now_ns - reflex_vk_loader_ts) <= static_cast<uint64_t>(utils::SEC_TO_NS)) {
        return true;
    }
    const uint64_t reflex_pclstats_ts =
        g_fps_limiter_last_timestamp_ns[static_cast<size_t>(FpsLimiterCallSite::reflex_marker_pclstats_etw)].load();
    if (reflex_pclstats_ts != 0 && (now_ns - reflex_pclstats_ts) <= static_cast<uint64_t>(utils::SEC_TO_NS)) {
        return true;
    }
    return false;
}

// Thread tracking for frame pacing debug
std::atomic<bool> g_thread_tracking_enabled{false};
std::atomic<DWORD> g_latency_marker_thread_id[kLatencyMarkerTypeCountFirstSix] = {};
std::atomic<uint64_t> g_latency_marker_last_frame_id[kLatencyMarkerTypeCountFirstSix] = {};
std::atomic<DWORD> g_fps_limiter_site_thread_id[kFpsLimiterCallSiteCount] = {};

// Global Swapchain Tracking Manager instance
SwapchainTrackingManager g_swapchainTrackingManager;

// VRR Status caching (updated from OnPresentUpdateBefore with direct swapchain access)
namespace vrr_status {
std::atomic<bool> cached_nvapi_ok{false};
std::atomic<std::shared_ptr<nvapi::VrrStatus>> cached_nvapi_vrr{std::make_shared<nvapi::VrrStatus>()};
std::atomic<std::shared_ptr<const std::wstring>> cached_output_device_name{nullptr};
}  // namespace vrr_status

// DXGI output device name tracking (shared between swapchain_events and continuous_monitoring)
std::atomic<bool> g_got_device_name{false};
std::atomic<std::shared_ptr<const std::wstring>> g_dxgi_output_device_name{nullptr};

// Backbuffer dimensions
std::atomic<int> g_last_backbuffer_width{0};
std::atomic<int> g_last_backbuffer_height{0};

// Game render resolution (before any modifications) - matches Special K's render_x/render_y
std::atomic<int> g_game_render_width{0};
std::atomic<int> g_game_render_height{0};

// Background/foreground state (updated by monitoring thread)
std::atomic<bool> g_app_in_background{false};
std::atomic<LONGLONG> g_last_foreground_background_switch_ns{0};

// FPS limiter: enabled by checkbox; mode 0 = OnPresentSync, 1 = Reflex, 2 = LatentSync (VBlank)
std::atomic<bool> s_fps_limiter_enabled{true};
std::atomic<FpsLimiterMode> s_fps_limiter_mode{FpsLimiterMode::kOnPresentSync};

// FPS limiter injection timing: 0 = Default (Direct DX9/10/11/12), 1 = Fallback(1) (Through ReShade), 2 = Fallback(2)
// (Through ReShade)

// Scanline offset

// VBlank Sync Divisor (like VSync /2 /3 /4) - 0 to 8, default 1 (0 = off)

// Performance stats (FPS/frametime) shared state
// Uses abstracted ring buffer structure
utils::LockFreeRingBuffer<PerfSample, kPerfRingCapacity> g_perf_ring;
std::atomic<double> g_perf_time_seconds{0.0};
std::atomic<bool> g_perf_reset_requested{false};
std::atomic<std::shared_ptr<const std::string>> g_perf_text_shared{std::make_shared<const std::string>("")};

// Native frame time ring buffer (for frames shown to display via native swapchain Present)
utils::LockFreeRingBuffer<PerfSample, kPerfRingCapacity> g_native_frame_time_ring;

// Action notification system for overlay display
std::atomic<ActionNotification> g_action_notification{
    ActionNotification{ActionNotificationType::None, 0, 0.0f, false, {0}}};

// Colorspace variables - removed, now queried directly in UI

// HDR10 override status (thread-safe; read by Swapchain tab UI)
std::atomic<std::shared_ptr<const std::string>> g_hdr10_override_status{std::make_shared<std::string>("Not applied")};

// HDR10 override timestamp (thread-safe; read by Swapchain tab UI)
std::atomic<std::shared_ptr<const std::string>> g_hdr10_override_timestamp{std::make_shared<std::string>("Never")};

// Config save failure path (thread-safe, updated by config manager, read by UI thread)
std::atomic<std::shared_ptr<const std::string>> g_config_save_failure_path{nullptr};

// Multiple Display Commander versions detection
std::atomic<std::shared_ptr<const std::string>> g_other_dc_version_detected{nullptr};

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

// Helper: create a DXGI factory on demand (no global cache; caller owns the returned ComPtr)
Microsoft::WRL::ComPtr<IDXGIFactory1> GetSharedDXGIFactory() {
    if (!g_dll_initialization_complete.load()) {
        return nullptr;
    }
    Microsoft::WRL::ComPtr<IDXGIFactory1> factory;
    HRESULT hr = display_commanderhooks::CreateDXGIFactory1_Direct(IID_PPV_ARGS(factory.GetAddressOf()));
    if (FAILED(hr)) {
        LogWarn("[GetSharedDXGIFactory] CreateDXGIFactory1_Direct failed hr=0x%x", static_cast<unsigned>(hr));
        return nullptr;
    }
    return factory;
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
std::array<std::atomic<uint32_t>, NUM_NVAPI_EVENTS> g_nvapi_event_counters = {};  // Array for NVAPI events

// NVAPI sleep timestamp tracking
std::atomic<uint64_t> g_nvapi_last_sleep_timestamp_ns{0};  // Last NVAPI_D3D_Sleep call timestamp in nanoseconds
std::atomic<bool> g_native_reflex_detected{false};         // Native Reflex detected via SetLatencyMarker calls

// OpenGL hook counters
std::array<std::atomic<uint64_t>, NUM_OPENGL_HOOKS> g_opengl_hook_counters = {};  // Array for all OpenGL hook events

// Present pacing delay as percentage of frame time - 0% to 100%
// This adds a delay after present to improve frame pacing and reduce CPU usage
// Higher values create more consistent frame timing but may increase latency
// 0% = no delay, 100% = full frame time delay between simulation start and present

std::atomic<LONGLONG> late_amount_ns{0};
std::atomic<LONGLONG> g_post_sleep_ns{0};

// OnPresentSync delay_bias state variables
std::atomic<float> g_onpresent_sync_delay_bias{0.0f};
std::atomic<LONGLONG> g_onpresent_sync_frame_time_ns{0};
std::atomic<LONGLONG> g_onpresent_sync_last_frame_end_ns{0};
std::atomic<LONGLONG> g_onpresent_sync_frame_start_ns{0};
std::atomic<LONGLONG> g_onpresent_sync_pre_sleep_ns{0};
std::atomic<LONGLONG> g_onpresent_sync_post_sleep_ns{0};

// GPU completion measurement using EnqueueSetEvent
std::atomic<HANDLE> g_gpu_completion_event{nullptr};  // Event handle for GPU completion measurement
std::atomic<LONGLONG> g_gpu_duration_ns{0};           // Last measured GPU duration (smoothed)

// GPU completion failure tracking
std::atomic<const char*> g_gpu_fence_failure_reason{
    nullptr};  // Reason why GPU fence creation/usage failed (nullptr if no failure)

// Sim-start-to-display latency measurement
std::atomic<LONGLONG> g_sim_start_ns_for_measurement{0};  // g_sim_start_ns captured when EnqueueGPUCompletion is called
std::atomic<bool> g_present_update_after2_called{false};  // Tracks if OnPresentUpdateAfter2 was called
std::atomic<bool> g_gpu_completion_callback_finished{false};  // Tracks if GPU completion callback finished
std::atomic<LONGLONG> g_sim_to_display_latency_ns{0};         // Measured sim-start-to-display latency (smoothed)

// GPU late time measurement (how much later GPU finishes compared to OnPresentUpdateAfter2)
std::atomic<LONGLONG> g_present_update_after2_time_ns{0};  // Time when OnPresentUpdateAfter2 was called
std::atomic<LONGLONG> g_gpu_late_time_ns{0};  // GPU late time (0 if GPU finished first, otherwise difference)

// Frame data cyclic buffer (see docs/FRAME_DATA_CYCLIC_BUFFER.md). Not populated yet.
FrameData g_frame_data[kFrameDataBufferSize] = {};

// Latency marker timestamps per (frame_id, markerType); recorded in NvAPI_D3D_SetLatencyMarker_Detour.
LatencyMarkerFrameRecord g_latency_marker_buffer[kFrameDataBufferSize] = {};

// NVIDIA Reflex minimal controls (disabled by default)

// Smooth Motion (nvpresent64/nvpresent32.dll) - set from OnModuleLoaded
std::atomic<bool> g_smooth_motion_dll_loaded{false};

// DLSS-G (DLSS Frame Generation) status
std::atomic<bool> g_dlss_g_loaded{false};
std::atomic<std::shared_ptr<const std::string>> g_dlss_g_version{std::make_shared<const std::string>("Unknown")};

// NGX Feature status tracking (set in CreateFeature detours)
std::atomic<uint32_t> g_dlss_enabled{0};                // DLSS Super Resolution active handle count
std::atomic<uint32_t> g_dlssg_enabled{0};               // DLSS Frame Generation active handle count
std::atomic<uint32_t> g_ray_reconstruction_enabled{0};  // Ray Reconstruction active handle count
std::atomic<bool> g_dlss_was_active_once{false};
std::atomic<bool> g_dlssg_was_active_once{false};
std::atomic<bool> g_ray_reconstruction_was_active_once{false};
std::atomic<bool> g_streamline_dlssg_fg_enabled{false};
std::atomic<bool> g_streamline_dlss_enabled{false};

// Unified game Reflex sleep mode params (D3D and Vulkan both write; read for Game Defaults)
static GameReflexSleepModeParams g_game_reflex_sleep_mode_params = {};

void SetGameReflexSleepModeParams(bool low_latency, bool boost, uint32_t minimum_interval_us) {
    utils::SRWLockExclusive lock(utils::g_game_reflex_sleep_mode_params_lock);
    g_game_reflex_sleep_mode_params.low_latency = low_latency;
    g_game_reflex_sleep_mode_params.boost = boost;
    g_game_reflex_sleep_mode_params.minimum_interval_us = minimum_interval_us;
    g_game_reflex_sleep_mode_params.has_value = true;
}

void GetGameReflexSleepModeParams(GameReflexSleepModeParams* out) {
    if (out == nullptr) {
        return;
    }
    utils::SRWLockShared lock(utils::g_game_reflex_sleep_mode_params_lock);
    *out = g_game_reflex_sleep_mode_params;
}

// NVAPI SetSleepMode tracking
std::atomic<std::shared_ptr<NV_SET_SLEEP_MODE_PARAMS>> g_last_nvapi_sleep_mode_params{nullptr};
std::atomic<IUnknown*> g_last_nvapi_sleep_mode_dev_ptr{nullptr};
std::atomic<std::shared_ptr<NV_SET_SLEEP_MODE_PARAMS>> g_last_reflex_params_set_by_addon{nullptr};

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

// PCLStats ping signal (edge-triggered, cleared after injection on SIMULATION_START)
std::atomic<bool> g_pclstats_ping_signal{false};

// DX11 Proxy HWND for filtering
HWND g_proxy_hwnd = nullptr;

// Experimental tab settings global instance
namespace settings {
ExperimentalTabSettings g_experimentalTabSettings;
AdvancedTabSettings g_advancedTabSettings;
MainTabSettings g_mainTabSettings;
SwapchainTabSettings g_swapchainTabSettings;
StreamlineTabSettings g_streamlineTabSettings;
HotkeysTabSettings g_hotkeysTabSettings;
HookSuppressionSettings g_hook_suppression_settings;
ReShadeTabSettings g_reshadeTabSettings;
// Function to load all settings at startup
void LoadAllSettingsAtStartup() {
    g_advancedTabSettings.LoadAll();
    g_experimentalTabSettings.LoadAll();
    g_mainTabSettings.LoadSettings();
    g_swapchainTabSettings.LoadAll();
    g_streamlineTabSettings.LoadAll();
    g_hotkeysTabSettings.LoadAll();
    g_hook_suppression_settings.LoadAll();
    g_reshadeTabSettings.LoadAll();
    LogInfo("All settings loaded at startup");
}

}  // namespace settings

// NGX Parameter Storage global instance
UnifiedParameterMap g_ngx_parameters;
UnifiedParameterMap g_ngx_parameter_overrides;
std::atomic<NVSDK_NGX_Parameter*> g_last_ngx_parameter{nullptr};

// NGX Counters global instance
NGXCounters g_ngx_counters;

// Tracked DLSS modules/paths from OnModuleLoaded (shared_ptr only; null = not set)
static std::shared_ptr<DlssTrackedInfo> g_dlss_tracked;
static std::shared_ptr<DlssTrackedInfo> g_dlssg_tracked;
static std::shared_ptr<DlssTrackedInfo> g_dlssd_tracked;

std::optional<HMODULE> GetDlssTrackedModule(DlssTrackedKind kind) {
    utils::SRWLockShared lock(utils::g_dlss_tracked_srwlock);
    const std::shared_ptr<DlssTrackedInfo>* p = nullptr;
    switch (kind) {
        case DlssTrackedKind::DLSS:  p = &g_dlss_tracked; break;
        case DlssTrackedKind::DLSSG: p = &g_dlssg_tracked; break;
        case DlssTrackedKind::DLSSD: p = &g_dlssd_tracked; break;
    }
    if (p && *p && (*p)->module != nullptr) {
        return (*p)->module;
    }
    return std::nullopt;
}

std::optional<std::string> GetDlssTrackedPath(DlssTrackedKind kind) {
    utils::SRWLockShared lock(utils::g_dlss_tracked_srwlock);
    const std::shared_ptr<DlssTrackedInfo>* p = nullptr;
    switch (kind) {
        case DlssTrackedKind::DLSS:  p = &g_dlss_tracked; break;
        case DlssTrackedKind::DLSSG: p = &g_dlssg_tracked; break;
        case DlssTrackedKind::DLSSD: p = &g_dlssd_tracked; break;
    }
    if (p && *p) {
        return (*p)->path;
    }
    return std::nullopt;
}

void SetDlssTracked(DlssTrackedKind kind, HMODULE hMod, bool force) {
    if (!hMod) return;
    wchar_t pathW[MAX_PATH];
    if (GetModuleFileNameW(hMod, pathW, MAX_PATH) == 0) return;
    int size = WideCharToMultiByte(CP_UTF8, 0, pathW, -1, nullptr, 0, nullptr, nullptr);
    if (size <= 0) return;
    std::string path(static_cast<size_t>(size - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, pathW, -1, path.data(), size, nullptr, nullptr);

    utils::SRWLockExclusive lock(utils::g_dlss_tracked_srwlock);
    auto info = std::make_shared<DlssTrackedInfo>();
    info->module = hMod;
    info->path = path;
    switch (kind) {
        case DlssTrackedKind::DLSS:
            if (!g_dlss_tracked || force) {
                g_dlss_tracked = std::move(info);
            }
            break;
        case DlssTrackedKind::DLSSG:
            if (!g_dlssg_tracked || force) {
                g_dlssg_tracked = std::move(info);
            }
            break;
        case DlssTrackedKind::DLSSD:
            if (!g_dlssd_tracked || force) {
                g_dlssd_tracked = std::move(info);
            }
            break;
    }
}

std::atomic<bool> g_dlss_from_nvidia_app_bin{false};

// Get DLSS/DLSS-G summary from NGX parameters
DLSSGSummary GetDLSSGSummary() {
    DLSSGSummary summary;

    // Use the new global tracking variables for more accurate status
    summary.dlss_active = (g_dlss_enabled.load() != 0) || g_streamline_dlss_enabled.load();
    summary.dlss_g_active = (g_dlssg_enabled.load() != 0) || g_streamline_dlssg_fg_enabled.load();
    summary.ray_reconstruction_active = g_ray_reconstruction_enabled.load() != 0;
    summary.any_dlss_was_active_once =
        g_dlss_was_active_once.load() || g_dlssg_was_active_once.load() || g_ray_reconstruction_was_active_once.load();

    // Get resolutions - using correct parameter names
    unsigned int internal_width, internal_height, output_width, output_height;
    bool has_internal_width = g_ngx_parameters.get_as_uint("DLSS.Render.Subrect.Dimensions.Width", internal_width);
    bool has_internal_height = g_ngx_parameters.get_as_uint("DLSS.Render.Subrect.Dimensions.Height", internal_height);
    bool has_output_width = g_ngx_parameters.get_as_uint("Width", output_width);
    bool has_output_height = g_ngx_parameters.get_as_uint("Height", output_height);

    if (has_internal_width && has_internal_height && internal_width > 0 && internal_height > 0) {
        summary.internal_resolution = std::to_string(internal_width) + "x" + std::to_string(internal_height);
    }
    if (has_output_width && has_output_height) {
        summary.output_resolution = std::to_string(output_width) + "x" + std::to_string(output_height);
    }

    // Calculate scaling ratio
    if (has_internal_width && has_internal_height && has_output_width && has_output_height && internal_width > 0
        && internal_height > 0) {
        float scale_x = static_cast<float>(output_width) / internal_width;
        char buffer[32];
        snprintf(buffer, sizeof(buffer), "%.2fx", scale_x);
        summary.scaling_ratio = std::string(buffer);
    }

    // Get quality preset based on PerfQualityValue
    unsigned int perf_quality;
    if (g_ngx_parameters.get_as_uint("PerfQualityValue", perf_quality)) {
        switch (static_cast<NVSDK_NGX_PerfQuality_Value>(perf_quality)) {
            case NVSDK_NGX_PerfQuality_Value_MaxPerf:          summary.quality_preset = "Performance"; break;
            case NVSDK_NGX_PerfQuality_Value_Balanced:         summary.quality_preset = "Balanced"; break;
            case NVSDK_NGX_PerfQuality_Value_MaxQuality:       summary.quality_preset = "Quality"; break;
            case NVSDK_NGX_PerfQuality_Value_UltraPerformance: summary.quality_preset = "Ultra Performance"; break;
            case NVSDK_NGX_PerfQuality_Value_UltraQuality:     summary.quality_preset = "Ultra Quality"; break;
            case NVSDK_NGX_PerfQuality_Value_DLAA:             summary.quality_preset = "DLAA"; break;
            default:                                           summary.quality_preset = "Unknown"; break;
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

    // Get DLL versions from tracked modules (OnModuleLoaded) when available, else GetModuleHandleW (e.g.
    // nvngx_dlss.dll)
    std::string loaded_dlss_path, loaded_dlssg_path, loaded_dlssd_path;
    HMODULE dlss_handle = nullptr;
    {
        auto tracked = GetDlssTrackedModule(DlssTrackedKind::DLSS);
        if (tracked.has_value()) {
            dlss_handle = *tracked;
        }
    }
    if (dlss_handle != nullptr) {
        auto path_opt = GetDlssTrackedPath(DlssTrackedKind::DLSS);
        if (path_opt.has_value()) {
            loaded_dlss_path = *path_opt;
            std::wstring wpath;
            int wlen = MultiByteToWideChar(CP_UTF8, 0, loaded_dlss_path.c_str(), -1, nullptr, 0);
            if (wlen > 0) {
                wpath.resize(static_cast<size_t>(wlen - 1));
                MultiByteToWideChar(CP_UTF8, 0, loaded_dlss_path.c_str(), -1, &wpath[0], wlen);
            }
            summary.dlss_dll_version = wpath.empty() ? "Loaded (path unknown)" : GetDLLVersionString(wpath);
        } else {
            wchar_t dlss_path[MAX_PATH];
            DWORD path_length = GetModuleFileNameW(dlss_handle, dlss_path, MAX_PATH);
            if (path_length > 0) {
                loaded_dlss_path = std::filesystem::path(dlss_path).string();
                summary.dlss_dll_version = GetDLLVersionString(std::wstring(dlss_path));
            } else {
                summary.dlss_dll_version = "Loaded (path unknown)";
            }
        }
    } else {
        summary.dlss_dll_version = "Not loaded";
    }

    HMODULE dlssg_handle = nullptr;
    {
        auto tracked = GetDlssTrackedModule(DlssTrackedKind::DLSSG);
        if (tracked.has_value()) {
            dlssg_handle = *tracked;
        }
    }
    if (dlssg_handle != nullptr) {
        auto path_opt = GetDlssTrackedPath(DlssTrackedKind::DLSSG);
        if (path_opt.has_value()) {
            loaded_dlssg_path = *path_opt;
            std::wstring wpath;
            int wlen = MultiByteToWideChar(CP_UTF8, 0, loaded_dlssg_path.c_str(), -1, nullptr, 0);
            if (wlen > 0) {
                wpath.resize(static_cast<size_t>(wlen - 1));
                MultiByteToWideChar(CP_UTF8, 0, loaded_dlssg_path.c_str(), -1, &wpath[0], wlen);
            }
            summary.dlssg_dll_version = wpath.empty() ? "Loaded (path unknown)" : GetDLLVersionString(wpath);
        } else {
            wchar_t dlssg_path[MAX_PATH];
            DWORD path_length = GetModuleFileNameW(dlssg_handle, dlssg_path, MAX_PATH);
            if (path_length > 0) {
                loaded_dlssg_path = std::filesystem::path(dlssg_path).string();
                summary.dlssg_dll_version = GetDLLVersionString(std::wstring(dlssg_path));
            } else {
                summary.dlssg_dll_version = "Loaded (path unknown)";
            }
        }
    } else {
        summary.dlssg_dll_version = "Not loaded";
    }

    HMODULE dlssd_handle = nullptr;
    {
        auto tracked = GetDlssTrackedModule(DlssTrackedKind::DLSSD);
        if (tracked.has_value()) {
            dlssd_handle = *tracked;
        }
    }
    if (dlssd_handle != nullptr) {
        auto path_opt = GetDlssTrackedPath(DlssTrackedKind::DLSSD);
        if (path_opt.has_value()) {
            loaded_dlssd_path = *path_opt;
            std::wstring wpath;
            int wlen = MultiByteToWideChar(CP_UTF8, 0, loaded_dlssd_path.c_str(), -1, nullptr, 0);
            if (wlen > 0) {
                wpath.resize(static_cast<size_t>(wlen - 1));
                MultiByteToWideChar(CP_UTF8, 0, loaded_dlssd_path.c_str(), -1, &wpath[0], wlen);
            }
            summary.dlssd_dll_version = wpath.empty() ? "Loaded (path unknown)" : GetDLLVersionString(wpath);
        } else {
            wchar_t dlssd_path[MAX_PATH];
            DWORD path_length = GetModuleFileNameW(dlssd_handle, dlssd_path, MAX_PATH);
            if (path_length > 0) {
                loaded_dlssd_path = std::filesystem::path(dlssd_path).string();
                summary.dlssd_dll_version = GetDLLVersionString(std::wstring(dlssd_path));
            } else {
                summary.dlssd_dll_version = "Loaded (path unknown)";
            }
        }
    } else {
        summary.dlssd_dll_version = "Not loaded";
    }

    summary.any_dlss_dll_loaded = (summary.dlss_dll_version != "Not loaded")
                                  || (summary.dlssg_dll_version != "Not loaded")
                                  || (summary.dlssd_dll_version != "Not loaded");

    // Per-DLL override: set _override_applied and prefer override folder version for display
    const bool master_override = settings::g_streamlineTabSettings.dlss_override_enabled.GetValue();
    auto path_under_folder = [](const std::string& p, const std::string& folder) {
        if (p.empty() || folder.empty()) return false;
        std::string fn = folder;
        std::replace(fn.begin(), fn.end(), '/', '\\');
        for (char& c : fn) c = static_cast<char>(::tolower(static_cast<unsigned char>(c)));
        std::string n = p;
        std::replace(n.begin(), n.end(), '/', '\\');
        for (char& c : n) c = static_cast<char>(::tolower(static_cast<unsigned char>(c)));
        return n.size() >= fn.size() && n.compare(0, fn.size(), fn) == 0;
    };

    // Per-DLL override: set _override_applied only; version strings stay from loaded modules above
    // nvngx_dlss.dll (empty subfolder = root folder dlss_override)
    if (master_override && settings::g_streamlineTabSettings.dlss_override_dlss.GetValue()) {
        std::string sub = settings::g_streamlineTabSettings.dlss_override_subfolder.GetValue();
        std::string folder = GetEffectiveDefaultDlssOverrideFolder(sub).string();
        if (!folder.empty()) {
            summary.dlss_override_applied = loaded_dlss_path.empty() || path_under_folder(loaded_dlss_path, folder);
        } else {
            summary.dlss_override_applied = true;
        }
    } else {
        summary.dlss_override_applied = true;
    }

    // nvngx_dlssd.dll (D = denoiser / RR)
    if (master_override && settings::g_streamlineTabSettings.dlss_override_dlss_rr.GetValue()) {
        std::string sub = settings::g_streamlineTabSettings.dlss_override_subfolder_dlssd.GetValue();
        std::string folder = GetEffectiveDefaultDlssOverrideFolder(sub).string();
        if (!folder.empty()) {
            summary.dlssd_override_applied = loaded_dlssd_path.empty() || path_under_folder(loaded_dlssd_path, folder);
        } else {
            summary.dlssd_override_applied = true;
        }
    } else {
        summary.dlssd_override_applied = true;
    }

    // nvngx_dlssg.dll (G = generation / FG)
    if (master_override && settings::g_streamlineTabSettings.dlss_override_dlss_fg.GetValue()) {
        std::string sub = settings::g_streamlineTabSettings.dlss_override_subfolder_dlssg.GetValue();
        std::string folder = GetEffectiveDefaultDlssOverrideFolder(sub).string();
        if (!folder.empty()) {
            summary.dlssg_override_applied = loaded_dlssg_path.empty() || path_under_folder(loaded_dlssg_path, folder);
        } else {
            summary.dlssg_override_applied = true;
        }
    } else {
        summary.dlssg_override_applied = true;
    }

    // Determine supported DLSS SR/RR presets: when DLSS override is enabled, use the version of the
    // DLL we want to load (override path) so preset selection matches the library that will be loaded.
    std::string version_for_presets = summary.dlss_dll_version;
    if (master_override && settings::g_streamlineTabSettings.dlss_override_dlss.GetValue()) {
        std::string sub = settings::g_streamlineTabSettings.dlss_override_subfolder.GetValue();
        std::string folder = GetEffectiveDefaultDlssOverrideFolder(sub).string();
        if (!folder.empty()) {
            std::filesystem::path override_dlss_path = std::filesystem::path(folder) / "nvngx_dlss.dll";
            std::string override_version = GetDLLVersionString(override_dlss_path.wstring());
            if (!override_version.empty() && override_version != "Unknown") {
                version_for_presets = override_version;
            }
        }
    }
    summary.supported_dlss_presets = GetSupportedDLSSSRPresetsFromVersionString(version_for_presets);
    summary.supported_dlss_rr_presets = GetSupportedDLSSRRPresetsFromVersionString(version_for_presets);

    // Auto-exposure from DLSS Feature Create Flags (NVSDK_NGX_DLSS_Feature_Flags_AutoExposure = 1 << 6)
    int create_flags = 0;
    if (g_ngx_parameters.get_as_int("DLSS.Feature.Create.Flags", create_flags)) {
        constexpr unsigned int k_auto_exposure_flag = 1u << 6;
        summary.auto_exposure = (static_cast<unsigned int>(create_flags) & k_auto_exposure_flag) ? "On" : "Off";
    }

    return summary;
}

// Lite version: any_dlss_active, dlss_active, dlss_g_active, ray_reconstruction_active, fg_mode (call every frame from
// FPS limiter / overlay)
DLSSGSummaryLite GetDLSSGSummaryLite() {
    DLSSGSummaryLite summary;
    summary.dlss_active = (g_dlss_enabled.load() != 0) || g_streamline_dlss_enabled.load();
    summary.dlss_g_active = (g_dlssg_enabled.load() != 0) || g_streamline_dlssg_fg_enabled.load();
    summary.ray_reconstruction_active = g_ray_reconstruction_enabled.load() != 0;
    summary.any_dlss_active = summary.dlss_active || summary.dlss_g_active || summary.ray_reconstruction_active;

    int enable_interp;
    if (g_ngx_parameters.get_as_int("DLSSG.EnableInterp", enable_interp)) {
        if (enable_interp == 1) {
            unsigned int multi_frame_count;
            if (g_ngx_parameters.get_as_uint("DLSSG.MultiFrameCount", multi_frame_count)) {
                switch (multi_frame_count) {
                    case 1:  summary.fg_mode = DLSSGFgMode::k2x; break;
                    case 2:  summary.fg_mode = DLSSGFgMode::k3x; break;
                    case 3:  summary.fg_mode = DLSSGFgMode::k4x; break;
                    default: summary.fg_mode = DLSSGFgMode::Other; break;
                }
            } else {
                summary.fg_mode = DLSSGFgMode::ActiveUnknown;
            }
        } else {
            summary.fg_mode = DLSSGFgMode::Off;
        }
    } else {
        summary.fg_mode = DLSSGFgMode::Unknown;
    }
    return summary;
}

// Helper functions for ReShade runtime management
void AddReShadeRuntime(reshade::api::effect_runtime* runtime) {
    if (runtime == nullptr) {
        return;
    }

    utils::SRWLockExclusive lock(utils::g_reshade_runtimes_lock);

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

    utils::SRWLockExclusive lock(utils::g_reshade_runtimes_lock);

    auto it = std::find(g_reshade_runtimes.begin(), g_reshade_runtimes.end(), runtime);
    if (it != g_reshade_runtimes.end()) {
        g_reshade_runtimes.erase(it);
        LogInfo("Removed ReShade runtime from vector - Total runtimes: %zu", g_reshade_runtimes.size());
    }
}

reshade::api::effect_runtime* GetFirstReShadeRuntime() {
    utils::SRWLockShared lock(utils::g_reshade_runtimes_lock);

    if (g_reshade_runtimes.empty()) {
        return nullptr;
    }

    return g_reshade_runtimes.front();
}

reshade::api::effect_runtime* GetReShadeRuntimeByIndex(size_t index) {
    utils::SRWLockShared lock(utils::g_reshade_runtimes_lock);
    if (index >= g_reshade_runtimes.size()) {
        return nullptr;
    }
    return g_reshade_runtimes[index];
}

reshade::api::effect_runtime* GetSelectedReShadeRuntime() {
    const size_t count = GetReShadeRuntimeCount();
    if (count == 0) {
        return nullptr;
    }
    const int selected = settings::g_mainTabSettings.selected_reshade_runtime_index.GetValue();
    if (selected <= 0) {
        return GetFirstReShadeRuntime();
    }
    const size_t index = static_cast<size_t>(selected);
    reshade::api::effect_runtime* rt = GetReShadeRuntimeByIndex(index);
    return rt != nullptr ? rt : GetFirstReShadeRuntime();
}

void EnumerateReShadeRuntimes(EnumerateReShadeRuntimesCallback callback, void* user_data) {
    if (callback == nullptr) return;
    utils::SRWLockShared lock(utils::g_reshade_runtimes_lock);
    for (size_t i = 0; i < g_reshade_runtimes.size(); ++i) {
        if (callback(i, g_reshade_runtimes[i], user_data)) break;
    }
}

size_t GetReShadeRuntimeCount() {
    utils::SRWLockShared lock(utils::g_reshade_runtimes_lock);
    return g_reshade_runtimes.size();
}

void OnReshadeUnload() {
    utils::SRWLockExclusive lock(utils::g_reshade_runtimes_lock);
    g_reshade_runtimes.clear();
    // g_reshade_module = nullptr;  // module unloaded; avoid using stale handle
    LogInfo("OnReshadeUnload: Cleared all ReShade runtimes and g_reshade_module");
}

bool SwapchainTrackingManager::IsLockHeldForDiagnostics() const {
    return utils::TryIsSRWLockHeld(const_cast<SRWLOCK&>(lock_));
}

bool IsSwapchainTrackingLockHeld() { return g_swapchainTrackingManager.IsLockHeldForDiagnostics(); }

// NGX preset initialization tracking
std::atomic<bool> g_ngx_presets_initialized{false};

// Swapchain wrapper statistics
SwapChainWrapperStats g_swapchain_wrapper_stats_proxy;
SwapChainWrapperStats g_swapchain_wrapper_stats_native;

// Track if DXGISwapChain4Wrapper::Present or Present1 has been called at least once
std::atomic<bool> g_swapchain_wrapper_present_called{false};
std::atomic<bool> g_swapchain_wrapper_present1_called{false};

// Cached frame statistics (updated in present detour, read by monitoring thread)
std::atomic<std::shared_ptr<DXGI_FRAME_STATISTICS>> g_cached_frame_stats{nullptr};

// Cached refresh rate statistics (updated in continuous monitoring thread, read by render/UI threads)
std::atomic<std::shared_ptr<const dxgi::fps_limiter::RefreshRateStats>> g_cached_refresh_rate_stats{nullptr};

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
