#pragma once

// Source Code <Display Commander> // follow this order for includes in all files + add this comment at the top
#include "display/display_cache.hpp"
#include "latent_sync/latent_sync_manager.hpp"
#include "settings/advanced_tab_settings.hpp"  // IWYU pragma: export
#include "settings/hook_suppression_settings.hpp"  // IWYU pragma: export
#include "settings/hotkeys_tab_settings.hpp"  // IWYU pragma: export
#include "settings/reshade_tab_settings.hpp"  // IWYU pragma: export
#include "utils/srwlock_wrapper.hpp"
#include "utils/timing.hpp"

// ReShade / ImGui
#include <reshade_imgui.hpp>

// Standard C++
#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// Windows.h
#include <Windows.h>

// Libraries <Windows>
#include <d3d11.h>
#include <dxgi.h>
#include <winnt.h>
#include <wrl/client.h>

// Source Code <Display Commander> // follow this order for includes in all files + add this comment at the top
#include "../../../external/nvapi/nvapi.h"
#include "nvapi/vrr_status.hpp"

// Forward declarations for NGX types
struct NVSDK_NGX_Parameter;

// Experimental features flag - allows code to compile in both cases
#ifdef EXPERIMENTAL_FEATURES
constexpr bool enabled_experimental_features = true;
#else
constexpr bool enabled_experimental_features = false;
#endif

enum class DeviceTypeDC { DX9, DX10, DX11, DX12, OpenGL, Vulkan };

// Display Commander load state for multi-proxy coordination (dxgi + winmm + version.dll, etc.).
// One instance becomes HOOKED; others become PROXY_DLL_ONLY and do not install hooks.
enum DisplayCommanderState : int {
    DC_STATE_UNDECIDED = 0,  // Not yet determined
    DC_STATE_PROXY_DLL_ONLY =
        1,                    // Will not hook; register as addon only (another DC is HOOKED or we loaded another DC)
    DC_STATE_HOOKED = 2,      // This instance installs hooks (MinHook, etc.)
    DC_STATE_DO_NOTHING = 3,  // Do not hook; no other DC was loaded
    DC_STATE_DLL_LOADER = 4   // Loader instance: loads DC from Dll\X.Y.Z and does not hook; stays quiet
};
extern std::atomic<DisplayCommanderState> g_display_commander_state;

inline bool IsDisplayCommanderHookingInstance() {
    return g_display_commander_state.load(std::memory_order_acquire) == DisplayCommanderState::DC_STATE_HOOKED;
}

// Log level enum matching ReShade's log levels
enum class LogLevel {
    Error = 1,    // Only errors
    Warning = 2,  // Errors and warnings
    Info = 3,     // Errors, warnings, and info
    Debug = 4     // Everything (default)
};

// Forward declarations

class SpinLock;
class BackgroundWindowManager;
class LatentSyncManager;
class ReflexProvider;
class SwapchainTrackingManager;

// Unified parameter value that can hold multiple types
struct ParameterValue {
    enum Type { INT, UINT, FLOAT, DOUBLE, ULL };
    Type type;
    union {
        int int_val;
        unsigned int uint_val;
        float float_val;
        double double_val;
        uint64_t ull_val;
    };

    ParameterValue() : type(INT), int_val(0) {}
    ParameterValue(int val) : type(INT), int_val(val) {}
    ParameterValue(unsigned int val) : type(UINT), uint_val(val) {}
    ParameterValue(float val) : type(FLOAT), float_val(val) {}
    ParameterValue(double val) : type(DOUBLE), double_val(val) {}
    ParameterValue(uint64_t val) : type(ULL), ull_val(val) {}

    // Type conversion methods
    int get_as_int() const {
        switch (type) {
            case INT:    return int_val;
            case UINT:   return static_cast<int>(uint_val);
            case FLOAT:  return static_cast<int>(float_val);
            case DOUBLE: return static_cast<int>(double_val);
            case ULL:    return static_cast<int>(ull_val);
            default:     return 0;
        }
    }

    unsigned int get_as_uint() const {
        switch (type) {
            case INT:    return static_cast<unsigned int>(int_val);
            case UINT:   return uint_val;
            case FLOAT:  return static_cast<unsigned int>(float_val);
            case DOUBLE: return static_cast<unsigned int>(double_val);
            case ULL:    return static_cast<unsigned int>(ull_val);
            default:     return 0;
        }
    }

    float get_as_float() const {
        switch (type) {
            case INT:    return static_cast<float>(int_val);
            case UINT:   return static_cast<float>(uint_val);
            case FLOAT:  return float_val;
            case DOUBLE: return static_cast<float>(double_val);
            case ULL:    return static_cast<float>(ull_val);
            default:     return 0.0f;
        }
    }

    double get_as_double() const {
        switch (type) {
            case INT:    return static_cast<double>(int_val);
            case UINT:   return static_cast<double>(uint_val);
            case FLOAT:  return static_cast<double>(float_val);
            case DOUBLE: return double_val;
            case ULL:    return static_cast<double>(ull_val);
            default:     return 0.0;
        }
    }

    uint64_t get_as_ull() const {
        switch (type) {
            case INT:    return static_cast<uint64_t>(int_val);
            case UINT:   return static_cast<uint64_t>(uint_val);
            case FLOAT:  return static_cast<uint64_t>(float_val);
            case DOUBLE: return static_cast<uint64_t>(double_val);
            case ULL:    return ull_val;
            default:     return 0;
        }
    }
};

// NGX / UI parameter mirror: SRWLOCK shared for reads, exclusive for writes (no concurrent unordered_map access).
class UnifiedParameterMap {
   private:
    mutable SRWLOCK map_lock_ = SRWLOCK_INIT;
    std::shared_ptr<std::unordered_map<std::string, ParameterValue>> data_ =
        std::make_shared<std::unordered_map<std::string, ParameterValue>>();

   public:
    UnifiedParameterMap() = default;

    void update(const std::string& key, const ParameterValue& value) {
        utils::SRWLockExclusive lock(map_lock_);
        (*data_)[key] = value;
    }

    void update_int(const std::string& key, int value) { update(key, ParameterValue(value)); }
    void update_uint(const std::string& key, unsigned int value) { update(key, ParameterValue(value)); }
    void update_float(const std::string& key, float value) { update(key, ParameterValue(value)); }
    void update_double(const std::string& key, double value) { update(key, ParameterValue(value)); }
    void update_ull(const std::string& key, uint64_t value) { update(key, ParameterValue(value)); }

    bool get(const std::string& key, ParameterValue& value) const {
        utils::SRWLockShared lock(map_lock_);
        auto it = data_->find(key);
        if (it != data_->end()) {
            value = it->second;
            return true;
        }
        return false;
    }

    bool get_as_int(const std::string& key, int& value) const {
        ParameterValue param;
        if (get(key, param)) {
            value = param.get_as_int();
            return true;
        }
        return false;
    }

    bool get_as_uint(const std::string& key, unsigned int& value) const {
        ParameterValue param;
        if (get(key, param)) {
            value = param.get_as_uint();
            return true;
        }
        return false;
    }

    bool get_as_float(const std::string& key, float& value) const {
        ParameterValue param;
        if (get(key, param)) {
            value = param.get_as_float();
            return true;
        }
        return false;
    }

    bool get_as_double(const std::string& key, double& value) const {
        ParameterValue param;
        if (get(key, param)) {
            value = param.get_as_double();
            return true;
        }
        return false;
    }

    bool get_as_ull(const std::string& key, uint64_t& value) const {
        ParameterValue param;
        if (get(key, param)) {
            value = param.get_as_ull();
            return true;
        }
        return false;
    }

    std::shared_ptr<std::unordered_map<std::string, ParameterValue>> get_all() const {
        utils::SRWLockShared lock(map_lock_);
        return std::make_shared<std::unordered_map<std::string, ParameterValue>>(*data_);
    }

    size_t size() const {
        utils::SRWLockShared lock(map_lock_);
        return data_->size();
    }

    void remove(const std::string& key) {
        utils::SRWLockExclusive lock(map_lock_);
        data_->erase(key);
    }

    void clear() {
        utils::SRWLockExclusive lock(map_lock_);
        data_->clear();
    }
};

// DLL initialization state
extern std::atomic<bool> g_dll_initialization_complete;

// No-ReShade mode: ReShade is not loaded; hooks/UI init run without ReShade overlay.
extern std::atomic<bool> g_no_reshade_mode;

// Wine/Proton detection - cached on first call (ntdll wine_get_version present)
bool IsUsingWine();

// Module handle for pinning/unpinning
extern HMODULE g_hmodule;

// Path of the module that caused this DLL to load (set during DLL_PROCESS_ATTACH from stack walk; empty if unknown).
extern std::string g_dll_load_caller_path;

// List of module paths seen in load backtrace, outer first, consecutive duplicates merged (newline-separated).
extern std::string g_dll_load_call_stack_list;

// Path to DisplayCommander.log next to the addon DLL (boot lines from DllMain).
extern std::string g_dll_main_log_path;

// Append one line to DisplayCommander.log (same sink as DllMain boot lines). No-op if path is empty.
void AppendDisplayCommanderBootLog(const std::string& text);

// Our addon DLL module handle (set in AddonInit; atomic for lock-free caller checks in hooks).
extern std::atomic<HMODULE> g_module;

// Track whether module was pinned (for conditional unpinning)
extern std::atomic<bool> g_module_pinned;

// DLL load timestamp in nanoseconds (for conflict resolution)
extern std::atomic<LONGLONG> g_dll_load_time_ns;

// Helper: create a DXGI factory on demand (no global cache; caller owns the returned ComPtr)
Microsoft::WRL::ComPtr<IDXGIFactory1> GetSharedDXGIFactory();

// Enums
enum class WindowStyleMode : std::uint8_t { KEEP, BORDERLESS, OVERLAPPED_WINDOW };
enum class FpsLimiterMode : std::uint8_t { kOnPresentSync = 0, kReflex = 1, kLatentSync = 2 };
enum class WindowMode : std::uint8_t {
    kNoChanges = 0,                 // No changes; do not prevent exclusive fullscreen
    kFullscreen = 1,                // Borderless fullscreen (resize) + prevent exclusive fullscreen
    kAspectRatio = 2,               // Borderless windowed (aspect ratio) + prevent exclusive fullscreen
    kPreventFullscreenNoResize = 3  // Prevent exclusive fullscreen only; no window resize (new default)
};
enum class AspectRatioType : std::uint8_t {
    k3_2 = 0,     // 3:2
    k4_3 = 1,     // 4:3
    k16_10 = 2,   // 16:10
    k16_9 = 3,    // 16:9
    k19_9 = 4,    // 19:9
    k19_5_9 = 5,  // 19.5:9
    k21_9 = 6,    // 21:9
    k21_5_9 = 7,  // 21.5:9
    k32_9 = 8     // 32:9
};

enum class WindowAlignment : std::uint8_t {
    kCenter = 0,      // Center (default)
    kTopLeft = 1,     // Top Left
    kTopRight = 2,    // Top Right
    kBottomLeft = 3,  // Bottom Left
    kBottomRight = 4  // Bottom Right
};

/** Prevent display sleep / screensaver (Main tab); stored as 0..2 in config. */
enum class ScreensaverMode : std::uint8_t {
    kDefault = 0,       // UI: Default
    kInForeground = 1,  // UI: In foreground — block sleep while game window focused
    kAlways = 2         // UI: Always — block sleep for whole session
};

// Reflex mode when FPS limiter is OnPresent Sync (main tab combo)
enum class OnPresentReflexMode : std::uint8_t {
    kLowLatency = 0,       // Low latency (default)
    kLowLatencyBoost = 1,  // Low Latency + boost
    kOff = 2,              // Both low latency and boost disabled
    kGameDefaults = 3      // Do not override; use game's Reflex settings
};

/** FPS limiter preset when game has native Reflex (main tab combo). kCustom = no auto-apply. Slot 0 = default.
 *  Order: kLowLatencyNativePacing, kDCPaceLockQ1..Q3, kPaceGenerated, kPaceGeneratedSafe, kCustom. */
enum class FpsLimiterPreset : std::uint8_t {
    kLowLatencyNativePacing = 0,  // Pace real frames Low-latency (native frame pacing) — default
    kDCPaceLockQ1 = 1,  // DCPaceLock(q=1)
    kDCPaceLockQ2 = 2,  // DCPaceLock(q=2)
    kDCPaceLockQ3 = 3,  // DCPaceLock(q=3)
    kPaceGenerated = 4,           // Pace generated frames
    kPaceGeneratedSafe = 5,       // Pace generated (safe) - Reshade APIs fallback
    kCustom = 6                   // Custom (configure manually)
};

enum class InputBlockingMode : std::uint8_t {
    kDisabled = 0,                  // Disabled
    kEnabled = 1,                   // Always enabled
    kEnabledInBackground = 2,       // Only enabled when in background
    kEnabledWhenXInputDetected = 3  // Enabled when XInput gamepad is detected
};

// Why Reflex Sleep Status is not available (for UI and diagnostics)
enum class SleepStatusUnavailableReason : std::uint8_t {
    kNone = 0,                  // Status is available
    kNoReflex,                  // Reflex provider not created
    kReflexNotInitialized,      // Reflex provider exists but not initialized (no D3D device yet)
    kProviderDoesNotSupport,    // Current provider does not support sleep status
    kNoD3DDevice,               // Reflex has no D3D device (device lost or not set)
    kNvApiFunctionUnavailable,  // NvAPI_D3D_GetSleepStatus not found in nvapi64
    kNvApiError                 // NvAPI_D3D_GetSleepStatus returned an error
};

// Human-readable reason for sleep status being unavailable (for UI)
inline const char* SleepStatusUnavailableReasonToString(SleepStatusUnavailableReason r) {
    switch (r) {
        case SleepStatusUnavailableReason::kNone:                 return "Available";
        case SleepStatusUnavailableReason::kNoReflex:             return "Reflex provider not created";
        case SleepStatusUnavailableReason::kReflexNotInitialized: return "Reflex not initialized (no D3D device yet)";
        case SleepStatusUnavailableReason::kProviderDoesNotSupport:
            return "Current provider does not support sleep status";
        case SleepStatusUnavailableReason::kNoD3DDevice: return "No D3D device (device lost or not set)";
        case SleepStatusUnavailableReason::kNvApiFunctionUnavailable:
            return "NvAPI_D3D_GetSleepStatus not found in nvapi64";
        case SleepStatusUnavailableReason::kNvApiError: return "NvAPI GetSleepStatus returned an error";
        default:                                        return "Unknown";
    }
}

// Structures
struct GlobalWindowState {
    int desired_width = 0;
    int desired_height = 0;
    int target_x = 0;
    int target_y = 0;
    int target_w = 0;
    int target_h = 0;
    RECT wr_current = {0, 0, 0, 0};
    bool needs_resize = false;
    bool needs_move = false;
    bool style_changed = false;
    bool style_changed_ex = false;
    int current_style = 0;
    int current_ex_style = 0;
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

// Swapchain tracking manager for thread-safe swapchain management
class SwapchainTrackingManager {
   private:
    std::unordered_set<IDXGISwapChain*> hooked_swapchains_;
    mutable SRWLOCK lock_;

   public:
    SwapchainTrackingManager() : lock_(SRWLOCK_INIT) {}

    // Add a swapchain to the tracked set
    bool AddSwapchain(IDXGISwapChain* swapchain) {
        if (swapchain == nullptr) {
            return false;
        }

        utils::SRWLockExclusive lock(lock_);

        // Check if already tracked
        if (hooked_swapchains_.find(swapchain) != hooked_swapchains_.end()) {
            return false;  // Already tracked
        }

        hooked_swapchains_.insert(swapchain);
        return true;
    }

    // Check if a swapchain is being tracked
    bool IsSwapchainTracked(IDXGISwapChain* swapchain) const {
        if (swapchain == nullptr) {
            return false;
        }

        utils::SRWLockShared lock(lock_);
        return hooked_swapchains_.find(swapchain) != hooked_swapchains_.end();
    }

    // Diagnostic: returns true if lock_ is currently held (for stuck-detection reporting)
    bool IsLockHeldForDiagnostics() const;
};

// Performance stats structure
struct PerfSample {
    // double timestamp_seconds;
    float dt;
};

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
extern std::atomic<bool> s_resolution_applied_at_least_once;

// Window management
/** Current window mode from main tab settings. */
WindowMode GetCurrentWindowMode();
/** True when window mode implies "prevent exclusive fullscreen" (all modes except kNoChanges). */
inline bool ShouldPreventExclusiveFullscreen() { return GetCurrentWindowMode() != WindowMode::kNoChanges; }
extern std::atomic<AspectRatioType> s_aspect_index;

// HDR10 / scRGB color fix setting (10-bit and 16-bit FP back buffer)

// Hide HDR capabilities from applications

// D3D9 to D3D9Ex upgrade
extern std::atomic<bool> s_d3d9e_upgrade_successful;
extern std::atomic<bool> g_used_flipex;
extern std::atomic<bool> g_dx9_swapchain_detected;

// Window Management Settings
extern std::atomic<WindowAlignment> s_window_alignment;  // Window alignment when repositioning is needed

// Render blocking in background

// Present blocking in background

// NVAPI (e.g. restart needed after fake NVAPI toggle)
extern std::atomic<bool> s_restart_needed_nvapi;

// Audio Settings

// Keyboard Shortcuts
extern std::atomic<bool> s_enable_input_blocking_shortcut;
extern std::atomic<bool> s_input_blocking_toggle;

// FPS Limiter Settings

// VSync and Tearing Controls

// ReShade Integration
extern std::vector<reshade::api::effect_runtime*> g_reshade_runtimes;
#include "utils/srwlock_registry.hpp"  // g_reshade_runtimes_lock and other global SRWLOCKs

// SRWLOCK diagnostics for stuck-detection reporting (returns true if lock is currently held)
bool IsSwapchainTrackingLockHeld();
extern std::atomic<HMODULE> g_reshade_module;

/** True when a ReShade addon whose name contains "renodx" (e.g. rennodx-silenthill2remake.addon64) has been loaded.
 *  Used to avoid conflicting DXGI color-space behavior (e.g. auto color space) and for multi-runtime UI hints. */
extern std::atomic<bool> g_is_renodx_loaded;

// If g_reshade_module is null, scan the process for ReShade (ReShadeRegisterAddon export) and set it.
// Use when ReShade may have been loaded after we attached (e.g. we started in no-ReShade mode).
void RefreshReShadeModuleIfNeeded();

// ReShade runtime management functions
void AddReShadeRuntime(reshade::api::effect_runtime* runtime);
void RemoveReShadeRuntime(reshade::api::effect_runtime* runtime);
void OnReshadeUnload();
reshade::api::effect_runtime* GetFirstReShadeRuntime();
/** Returns runtime at index, or nullptr if index >= count. */
reshade::api::effect_runtime* GetReShadeRuntimeByIndex(size_t index);
/** Returns selected runtime (from main tab setting), or first if selection is 0 or invalid. */
reshade::api::effect_runtime* GetSelectedReShadeRuntime();
size_t GetReShadeRuntimeCount();

/// Enumerate all ReShade runtimes while holding the runtimes lock. Callback receives (index, runtime, user_data).
/// Return true from callback to stop enumeration early. Do not store runtime pointers beyond the callback.
using EnumerateReShadeRuntimesCallback = bool (*)(size_t index, reshade::api::effect_runtime* runtime, void* user_data);
void EnumerateReShadeRuntimes(EnumerateReShadeRuntimesCallback callback, void* user_data);

// Monitor Management
// g_monitor_labels removed - UI now uses GetDisplayInfoForUI() directly for better reliability

// Continuous monitoring and rendering

// Atomic variables
extern std::atomic<reshade::api::device_api> g_last_reshade_device_api;
extern std::atomic<uint32_t> g_last_api_version;  // Store API version/feature level (e.g., D3D_FEATURE_LEVEL_11_1)
/** Swapchain desc before OnCreateSwapchainCapture2 modifications (game-requested). */
extern std::atomic<std::shared_ptr<reshade::api::swapchain_desc>> g_last_swapchain_desc_pre;
/** Swapchain desc after OnCreateSwapchainCapture2 modifications (actual create). */
extern std::atomic<std::shared_ptr<reshade::api::swapchain_desc>> g_last_swapchain_desc_post;
/** True when Force Flip Discard upgrade (FLIP_SEQUENTIAL → FLIP_DISCARD) was applied on last create_swapchain. */
extern std::atomic<bool> g_force_flip_discard_upgrade_done;
/** When true, show the "HDR10 / scRGB color fix" checkbox in the main tab. Default true. */
extern std::atomic<bool> g_show_auto_colorspace_fix_in_main_tab;
extern std::atomic<HWND> g_last_swapchain_hwnd;
extern std::atomic<bool> g_shutdown;
extern std::atomic<bool> g_muted_applied;

// Global instances
extern std::atomic<std::shared_ptr<GlobalWindowState>> g_window_state;
extern BackgroundWindowManager g_backgroundWindowManager;

// Latent Sync Manager
namespace dxgi::latent_sync {
extern std::unique_ptr<LatentSyncManager> g_latentSyncManager;
}

// Reflex (latency) provider
extern std::unique_ptr<ReflexProvider> g_reflexProvider;

// Global frame ID for latency management
extern std::atomic<uint64_t> g_global_frame_id;

// Same moment in QPC ns (get_real_time_ns) when g_global_frame_id was last incremented; for relative "X.Xs ago" in
// stuck log.
extern std::atomic<LONGLONG> g_global_frame_id_last_updated_ns;

/** Monotonic ns (`get_now_ns`) before the in-game performance overlay may draw. 0 until the first
 *  `g_global_frame_id` increment on the present path; then set to (that moment + kPerformanceOverlayPostFirstFrameDelayNs). */
extern std::atomic<LONGLONG> g_performance_overlay_allowed_after_ns;
inline constexpr LONGLONG kPerformanceOverlayPostFirstFrameDelayNs = 2'000'000'000LL;

/** `g_global_frame_id` when the performance overlay last requested an NVAPI GPU util sample; 0 = no active request. */
extern std::atomic<uint64_t> g_nvapi_gpu_util_request_frame_id;
/** `g_global_frame_id` when `NvAPI_GPU_GetDynamicPstatesInfoEx` was last run for overlay GPU util. */
extern std::atomic<uint64_t> g_nvapi_gpu_util_last_query_frame_id;

// QPC ns when a Windows message was last processed in the game window's WndProc; for stuck-detection log.
extern std::atomic<LONGLONG> g_last_window_message_processed_ns;

// Global frame ID for pclstats frame id
extern std::atomic<uint64_t> g_pclstats_frame_id;

// Global frame ID when XInput was last successfully detected
extern std::atomic<uint64_t> g_last_xinput_detected_frame_id;

// Global frame ID when NvAPI_D3D_SetSleepMode_Direct was last called
extern std::atomic<uint64_t> g_last_set_sleep_mode_direct_frame_id;

/** Entry points where use_fps_limiter is computed; last frame_id per site is tracked to know which paths are available.
 */
enum class FpsLimiterCallSite {
    reflex_marker,                     // NVAPI SetLatencyMarker path (D3D)
    reflex_marker_vk_nvll,             // NvLowLatencyVk.dll SetLatencyMarker path (Vulkan)
    reflex_marker_vk_loader,           // vulkan-1 / VK_NV_low_latency2 vkSetLatencyMarkerNV wrapper
    reflex_marker_pclstats_etw,        // PCLStats ETW (EventWriteTransfer) – first 6 markers only
    dxgi_swapchain1,                   // DXGI IDXGISwapChain1::Present1 detour
    dxgi_swapchain,                    // DXGI IDXGISwapChain::Present detour
    dxgi_swapchain1_streamline_proxy,  // Streamline proxy IDXGISwapChain1::Present1 (sl_proxy_dxgi_swapchain1)
    dxgi_swapchain_streamline_proxy,   // Streamline proxy IDXGISwapChain::Present (sl_proxy_dxgi_swapchain)
    dx9_present,                       // D3D9 IDirect3DDevice9::Present detour
    dx9_presentex,                     // D3D9 IDirect3DDevice9Ex::PresentEx detour
    opengl_swapbuffers,                // OpenGL wglSwapBuffers detour
    ddraw_flip,                        // DirectDraw IDirectDrawSurface::Flip detour
    reshade_addon_event,               // ReShade presentBefore/presentAfter (Vulkan/OpenGL/D3D9 or safe mode)
    vk_queue_present_khr,              // Vulkan vkQueuePresentKHR detour (FPS limiter for native Vulkan)
    dxgi_factory_wrapper,              // Rarely hit in practice (CreateSwapChain path)
    kFpsLimiterCallSiteCount           // count of call sites above (use for array size / iteration)
};

constexpr size_t kFpsLimiterCallSiteCount = static_cast<size_t>(FpsLimiterCallSite::kFpsLimiterCallSiteCount);

/** Last timestamp (ns) at which each FPS limiter call site was hit (0 = never). */
extern std::atomic<uint64_t> g_fps_limiter_last_timestamp_ns[kFpsLimiterCallSiteCount];

/** Sentinel for "no FPS limiter source chosen yet". */
constexpr uint8_t kFpsLimiterChosenUnset = 0xFF;

/** Index of the chosen FPS limiter source (0..7 = FpsLimiterCallSite, kFpsLimiterChosenUnset = unset). */
extern std::atomic<uint8_t> g_chosen_fps_limiter_site;

/** Register this call site with current timestamp and recompute chosen source. Decision is based on which sites were
 * hit within the last 1s; record is done after the decision. Call before using GetChosenFpsLimiter. */
void ChooseFpsLimiter(uint64_t timestamp_ns, FpsLimiterCallSite caller_enum);

/** Returns true iff the chosen FPS limiter source for the current decision is caller_enum. */
bool GetChosenFpsLimiter(FpsLimiterCallSite caller_enum);

/** Returns the chosen FPS limiter call site for the current frame (dxgi_swapchain1, dxgi_swapchain, or
 * reshade_addon_event). */
FpsLimiterCallSite GetChosenFrameTimeLocation();

/** Stable identifier for logs / debugging ("reflex_marker", "dxgi_swapchain", etc.). */
const char* FpsLimiterSiteName(FpsLimiterCallSite site);

/** Short user-facing description of the call site (Main tab, overlay). */
const char* FpsLimiterSiteDisplayName(FpsLimiterCallSite site);

/** Marker type constants for ProcessReflexMarkerFpsLimiter. Pass API-specific values (e.g. NVAPI vs Vulkan NVLL)
 * so the implementation can compare marker_type and index into buffers without hardcoding one API. */
struct ReflexMarkerTypes {
    int simulation_start;
    int present_start;
    int present_end;
    int sleep;
};

/** Shared Reflex latency-marker handling (NVAPI and Vulkan NVLL). Calls NotifyGameSetLatencyMarkerCall, runs FPS
 * limiter logic, and optionally forwards the marker via the callback. Returns 0 on success; callback returns 0 on
 * success. marker_types supplies the API-specific values for SIMULATION_START, PRESENT_START, PRESENT_END. */
int ProcessReflexMarkerFpsLimiter(FpsLimiterCallSite site, int marker_type, uint64_t frame_id,
                                  const ReflexMarkerTypes& marker_types,
                                  const std::function<int()>& send_present_end_to_driver);

/** User-facing label for the current chosen FPS limiter source; see FpsLimiterSiteDisplayName. "Not chosen yet" if unset. */
const char* GetChosenFpsLimiterSiteName();

/** True when native frame pacing is active and in sync (reflex_marker path hit recently, within 1s). */
bool IsNativeFramePacingInSync();

/** When native Reflex is active and a non-Custom preset is selected, returns the preset override; otherwise config. */
bool GetEffectiveLimitRealFrames();
bool GetEffectiveUseReflexMarkersAsFpsLimiter();
int GetEffectiveReflexFpsLimiterMaxQueuedFrames();
bool GetEffectiveUseStreamlineProxyFpsLimiter();
bool GetEffectiveNativePacingSimStartOnly();
bool GetEffectiveDelayPresentStartAfterSimEnabled();
bool GetEffectiveSafeModeFpsLimiter();

// Global Swapchain Tracking Manager instance
extern SwapchainTrackingManager g_swapchainTrackingManager;

// VRR Status caching (updated from OnPresentUpdateBefore with direct swapchain access)
namespace vrr_status {
extern std::atomic<bool> cached_nvapi_ok;
extern std::atomic<std::shared_ptr<nvapi::VrrStatus>> cached_nvapi_vrr;
extern std::atomic<std::shared_ptr<const std::wstring>> cached_output_device_name;
}  // namespace vrr_status

// DXGI output device name tracking (shared between swapchain_events and continuous_monitoring)
extern std::atomic<bool> g_got_device_name;
extern std::atomic<std::shared_ptr<const std::wstring>> g_dxgi_output_device_name;

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

// Game render resolution (before any modifications) - matches Special K's render_x/render_y
extern std::atomic<int> g_game_render_width;
extern std::atomic<int> g_game_render_height;

// Background/foreground state
extern std::atomic<bool> g_app_in_background;
// Returns true if the current process is not the foreground process (same logic as g_app_in_background).
bool IsAppInBackground();
// Timestamp (ns) of last foreground<->background switch; used to limit VRR/NVAPI updates to 5s after switch
extern std::atomic<LONGLONG> g_last_foreground_background_switch_ns;

// FPS limiter: enabled by checkbox (s_fps_limiter_enabled). Mode: 0 = OnPresentSync, 1 = Reflex, 2 = LatentSync
// (VBlank).
extern std::atomic<bool> s_fps_limiter_enabled;
extern std::atomic<FpsLimiterMode> s_fps_limiter_mode;

// Lock-free ring buffer for recent FPS samples (60s window at ~240 Hz -> 14400 max)
constexpr size_t kPerfRingCapacity = 65536;

// Performance stats (FPS/frametime) shared state
// Uses abstracted ring buffer structure
#include "utils/ring_buffer.hpp"
extern utils::LockFreeRingBuffer<PerfSample, kPerfRingCapacity> g_perf_ring;
extern std::atomic<double> g_perf_time_seconds;
extern std::atomic<bool> g_perf_reset_requested;
extern std::atomic<std::shared_ptr<const std::string>> g_perf_text_shared;

// Native frame time ring buffer (for frames shown to display via native swapchain Present)
// Uses abstracted ring buffer structure
extern utils::LockFreeRingBuffer<PerfSample, kPerfRingCapacity> g_native_frame_time_ring;

// Action notification system for overlay display
enum class ActionNotificationType {
    None = 0,
    Volume = 1,
    Mute = 2,
    GenericAction = 3,  // For any gamepad action
    // Add more action types here as needed
};

struct ActionNotification {
    ActionNotificationType type;
    LONGLONG timestamp_ns;
    float float_value;     // For volume percentage
    bool bool_value;       // For mute state
    char action_name[64];  // For generic actions (fixed-size array for atomic compatibility)
};

extern std::atomic<ActionNotification> g_action_notification;

// Colorspace variables - removed, now queried directly in UI
extern std::atomic<std::shared_ptr<const std::string>> g_hdr10_override_status;
extern std::atomic<std::shared_ptr<const std::string>> g_hdr10_override_timestamp;

// Config save failure tracking
extern std::atomic<std::shared_ptr<const std::string>> g_config_save_failure_path;

// DC config directory (DisplayCommander.ini location). Set at start of DLL_PROCESS_ATTACH to exe directory; can be
// changed later.
extern std::atomic<std::shared_ptr<const std::wstring>> g_dc_config_directory;

// Multiple Display Commander versions detection
extern std::atomic<std::shared_ptr<const std::string>> g_other_dc_version_detected;

// Performance optimization settings
extern std::atomic<LONGLONG> g_flush_before_present_time_ns;

// Game playtime tracking (time from game start)
extern std::atomic<LONGLONG> g_game_start_time_ns;

// Sleep delay after present as percentage of frame time - 0% to 100%
extern std::atomic<float> s_sleep_after_present_frame_time_percentage;

// Monitoring thread
extern std::atomic<bool> g_monitoring_thread_running;
extern std::thread g_monitoring_thread;
extern std::thread g_stuck_check_watchdog_thread;
// Current section of the monitoring loop (for crash/stuck reporting; set by monitoring and audio code)
extern std::atomic<const char*> g_continuous_monitoring_section;
// Current section of the rendering UI (for crash/stuck reporting; set by overlay/tab draw code)
extern std::atomic<const char*> g_rendering_ui_section;

// Render thread tracking
extern std::atomic<DWORD> g_render_thread_id;

// DirectInput hook suppression
extern std::atomic<bool> s_suppress_dinput_hooks;

// Logging level control (combo index 0=Debug, 1=Info, 2=Warning, 3=Error)
inline LogLevel LogLevelFromComboIndex(int index) {
    switch (index) {
        case 0:  return LogLevel::Debug;
        case 1:  return LogLevel::Info;
        case 2:  return LogLevel::Warning;
        case 3:  return LogLevel::Error;
        default: return LogLevel::Debug;
    }
}
/** Returns current minimum log level from main tab settings. */
LogLevel GetMinLogLevel();

// Reflex settings
extern std::atomic<bool> s_reflex_auto_configure;
extern std::atomic<bool> s_reflex_enable_current_frame;
// extern std::atomic<bool> s_reflex_supress_native;
extern std::atomic<bool> s_enable_reflex_logging;

// Shortcut settings
extern std::atomic<bool> s_enable_hotkeys;
extern std::atomic<bool> s_enable_mute_unmute_shortcut;
extern std::atomic<bool> s_enable_background_toggle_shortcut;
extern std::atomic<bool> s_enable_timeslowdown_shortcut;
extern std::atomic<bool> s_enable_adhd_toggle_shortcut;
extern std::atomic<bool> s_enable_display_commander_ui_shortcut;
extern std::atomic<bool> s_enable_performance_overlay_shortcut;

// Forward declaration for tab settings
namespace settings {
class ExperimentalTabSettings;
class MainTabSettings;
class StreamlineTabSettings;
class SwapchainTabSettings;
extern ExperimentalTabSettings g_experimentalTabSettings;
extern MainTabSettings g_mainTabSettings;
extern SwapchainTabSettings g_swapchainTabSettings;
extern StreamlineTabSettings g_streamlineTabSettings;

// Function to load all settings at startup
void LoadAllSettingsAtStartup();
}  // namespace settings

// Display settings hook counter indices
enum DisplaySettingsHookIndex {
    DISPLAY_SETTINGS_HOOK_CHANGEDISPLAYSETTINGSA,
    DISPLAY_SETTINGS_HOOK_CHANGEDISPLAYSETTINGSW,
    DISPLAY_SETTINGS_HOOK_CHANGEDISPLAYSETTINGSEXA,
    DISPLAY_SETTINGS_HOOK_CHANGEDISPLAYSETTINGSEXW,
    // Window management hooks for OpenGL fullscreen prevention
    // DISPLAY_SETTINGS_HOOK_SETWINDOWPOS moved to api_hooks.cpp
    DISPLAY_SETTINGS_HOOK_SHOWWINDOW,
    DISPLAY_SETTINGS_HOOK_SETWINDOWLONGA,
    DISPLAY_SETTINGS_HOOK_SETWINDOWLONGW,
    DISPLAY_SETTINGS_HOOK_SETWINDOWLONGPTRA,
    DISPLAY_SETTINGS_HOOK_SETWINDOWLONGPTRW,
    NUM_DISPLAY_SETTINGS_HOOKS
};

// NVAPI event counters - separate from swapchain events
enum NvapiEventIndex {
    NVAPI_EVENT_GET_HDR_CAPABILITIES,
    NVAPI_EVENT_D3D_SET_LATENCY_MARKER,
    NVAPI_EVENT_D3D_SET_SLEEP_MODE,
    NVAPI_EVENT_D3D_SLEEP,
    NVAPI_EVENT_D3D_GET_LATENCY,
    NVAPI_EVENT_D3D_GET_SLEEP_STATUS,
    NUM_NVAPI_EVENTS
};

// Create-swapchain capture path (OnCreateSwapchainCapture2); main tab reads for UI.
extern std::atomic<uint32_t> g_reshade_create_swapchain_capture_count;

// NVAPI event counters - separate from swapchain events
extern std::array<std::atomic<uint32_t>, NUM_NVAPI_EVENTS> g_nvapi_event_counters;  // Array for NVAPI events

// NVAPI sleep timestamp tracking
extern std::atomic<uint64_t> g_nvapi_last_sleep_timestamp_ns;  // Last NVAPI_D3D_Sleep call timestamp in nanoseconds
extern std::atomic<bool> g_native_reflex_detected;             // Native Reflex detected via SetLatencyMarker calls
/** First six NVAPI latency marker types (SIMULATION_START..PRESENT_END); array sizes for per-marker tracking. */
constexpr size_t kLatencyMarkerTypeCountFirstSix = 6;
/** For each of the first 6 marker types (0..5), last g_global_frame_id when we received that marker (0 = not yet). */
extern std::atomic<uint64_t> g_nvapi_d3d_last_global_frame_id_by_marker_type[kLatencyMarkerTypeCountFirstSix];
/** Last g_global_frame_id when NvAPI_D3D_Sleep was called (0 = not yet). For DXGI native Reflex status OK/FAIL. */
extern std::atomic<uint64_t> g_nvapi_d3d_last_sleep_global_frame_id;
/** Number of nvapi_QueryInterface calls with NvAPI_D3D12_SetFlipConfig ID (0xF3148C42) in this session. */
extern std::atomic<uint32_t> g_nvapi_d3d12_setflipconfig_seen;
/** Successful suppressions: NvAPI_QueryInterface returned nullptr for that ID (see Main tab allow checkbox). */
extern std::atomic<uint32_t> g_nvapi_d3d12_setflipconfig_suppressions;

// Unsorted TODO: Add in correct order above
extern std::atomic<LONGLONG> g_present_start_time_ns;

// Frame data cyclic buffer: per-frame timestamps for UpdateFrameTimelineCache and similar.
// Index: g_frame_data[g_global_frame_id % kFrameDataBufferSize]. See docs/FRAME_DATA_CYCLIC_BUFFER.md.
constexpr size_t kFrameDataBufferSize = 64;

// FPS limiter is not applied for the first N frames (warmup); frame 301+ gets limiting.
constexpr uint64_t kFpsLimiterWarmupFrames = 300;

struct FrameData {
    std::atomic<uint64_t> frame_id{0};               // Frame id this slot corresponds to (0 = not set)
    std::atomic<LONGLONG> present_start_time_ns{0};  // Start of present for this frame (after FPS limiter)
    std::atomic<LONGLONG> present_end_time_ns{0};    // End of present (when OnPresentUpdateAfter2 ran)
    std::atomic<LONGLONG> sim_start_ns{0};
    std::atomic<LONGLONG> submit_start_time_ns{0};
    std::atomic<LONGLONG> render_submit_end_time_ns{0};
    std::atomic<LONGLONG> present_update_after2_time_ns{0};
    std::atomic<LONGLONG> gpu_completion_time_ns{0};
    // Sleep timestamps (FPS limiter / pacing)
    std::atomic<LONGLONG> sleep_pre_present_start_time_ns{0};   // Start of sleep/pacing before present
    std::atomic<LONGLONG> sleep_pre_present_end_time_ns{0};     // End of sleep before present (= present_start)
    std::atomic<LONGLONG> sleep_post_present_start_time_ns{0};  // Start of sleep/pacing after present
    std::atomic<LONGLONG> sleep_post_present_end_time_ns{0};    // End of sleep after present
};

extern FrameData g_frame_data[kFrameDataBufferSize];

// Cyclic buffer: timestamp when NvAPI_D3D_SetLatencyMarker was called, keyed by (frame_id, markerType).
// Index: g_latency_marker_buffer[frame_id % kFrameDataBufferSize]. marker_time_ns[i] = time when marker type i was set.

enum class DCLatencyMarkers {
    SIMULATION_START = 0,
    SIMULATION_END = 1,
    RENDERSUBMIT_START = 2,
    RENDERSUBMIT_END = 3,
    PRESENT_START = 4,
    PRESENT_END = 5,
    REFLEX_SLEEP = 6,
    Count = 7,
};
constexpr size_t kLatencyMarkerTypeCount = static_cast<size_t>(DCLatencyMarkers::Count);
struct LatencyMarkerFrameRecord {
    std::atomic<uint64_t> frame_id{0};
    std::atomic<LONGLONG> marker_time_ns[kLatencyMarkerTypeCount];
    std::atomic<uint64_t> frame_id_by_marker_type[kLatencyMarkerTypeCount];
};

extern std::atomic<LONGLONG> g_latency_marker_buffer_per_type[kLatencyMarkerTypeCount];
extern LatencyMarkerFrameRecord g_latency_marker_buffer[kFrameDataBufferSize];

// Present pacing delay as percentage of frame time - 0% to 100%

extern std::atomic<LONGLONG> late_amount_ns;
extern std::atomic<LONGLONG> g_post_sleep_ns;

// OnPresentSync delay_bias state variables
extern std::atomic<float> g_onpresent_sync_delay_bias;            // Current delay_bias value (0.0 - 1.0)
extern std::atomic<LONGLONG> g_onpresent_sync_frame_time_ns;      // Current frame time in nanoseconds
extern std::atomic<LONGLONG> g_onpresent_sync_last_frame_end_ns;  // When last frame ended (for frame pacing)
extern std::atomic<LONGLONG> g_onpresent_sync_frame_start_ns;     // When current frame started processing
extern std::atomic<LONGLONG> g_onpresent_sync_pre_sleep_ns;       // Actual pre-sleep time applied (for debugging)
extern std::atomic<LONGLONG> g_onpresent_sync_post_sleep_ns;      // Actual post-sleep time applied (for debugging)

/** Debug tab: last HandleFpsLimiterPre snapshot (racy but sufficient for diagnostics). */
extern std::atomic<float> g_fps_limiter_debug_target_fps_native;
extern std::atomic<float> g_fps_limiter_debug_target_fps_effective;
extern std::atomic<int> g_fps_limiter_debug_getlite_fg_mode;
extern std::atomic<uint8_t> g_fps_limiter_debug_frame_generation_aware;
extern std::atomic<uint64_t> g_fps_limiter_debug_pre_entry_count;
extern std::atomic<uint64_t> g_fps_limiter_debug_pre_active_count;
extern std::atomic<uint64_t> g_fps_limiter_debug_post_entry_count;

// GPU completion measurement using EnqueueSetEvent
extern std::atomic<HANDLE> g_gpu_completion_event;  // Event handle for GPU completion measurement
extern std::atomic<LONGLONG> g_gpu_duration_ns;     // Last measured GPU duration (smoothed)

// GPU completion failure tracking
extern std::atomic<const char*>
    g_gpu_fence_failure_reason;  // Reason why GPU fence creation/usage failed (nullptr if no failure)

// Sim-start-to-display latency measurement
extern std::atomic<LONGLONG>
    g_sim_start_ns_for_measurement;                       // g_sim_start_ns captured when EnqueueGPUCompletion is called
extern std::atomic<bool> g_present_update_after2_called;  // Tracks if OnPresentUpdateAfter2 was called
extern std::atomic<bool> g_gpu_completion_callback_finished;  // Tracks if GPU completion callback finished
extern std::atomic<LONGLONG> g_sim_to_display_latency_ns;     // Measured sim-start-to-display latency (smoothed)

// GPU late time measurement (how much later GPU finishes compared to OnPresentUpdateAfter2)
extern std::atomic<LONGLONG> g_present_update_after2_time_ns;  // Time when OnPresentUpdateAfter2 was called
extern std::atomic<LONGLONG> g_gpu_late_time_ns;  // GPU late time (0 if GPU finished first, otherwise difference)

// NVIDIA Reflex minimal controls

// Set from OnModuleLoaded when nvpresent64.dll or nvpresent32.dll is loaded (NVIDIA Smooth Motion).
extern std::atomic<bool> g_smooth_motion_dll_loaded;

// DLSS-G (DLSS Frame Generation) status
extern std::atomic<bool> g_dlss_g_loaded;                                 // DLSS-G loaded status
extern std::atomic<std::shared_ptr<const std::string>> g_dlss_g_version;  // DLSS-G version string

// NGX Feature status tracking (reference count: increment on CreateFeature, decrement on ReleaseFeature)
extern std::atomic<uint32_t> g_dlss_enabled;                // DLSS Super Resolution active handle count
extern std::atomic<uint32_t> g_dlssg_enabled;               // DLSS Frame Generation active handle count
extern std::atomic<uint32_t> g_ray_reconstruction_enabled;  // Ray Reconstruction active handle count
// Set to true when corresponding feature count goes from 0 to >0; reset in CleanupNGXHandleTracking
extern std::atomic<bool> g_dlss_was_active_once;
extern std::atomic<bool> g_dlssg_was_active_once;
extern std::atomic<bool> g_ray_reconstruction_was_active_once;
// Streamline (Vulkan/sl.dlss_g): true when slDLSSGSetOptions/slDLSSGGetState reported FG mode != eOff
extern std::atomic<bool> g_streamline_dlssg_fg_enabled;
// Streamline (Vulkan/sl.dlss): true when slDLSSSetOptions/slDLSSGetOptimalSettings reported DLSS mode != eOff
extern std::atomic<bool> g_streamline_dlss_enabled;

// NGX Parameter Storage (SRWLOCK-protected mirrors for hooks and UI)
extern UnifiedParameterMap g_ngx_parameters;  // Unified NGX parameters supporting all types
extern UnifiedParameterMap
    g_ngx_parameter_overrides;  // NGX parameter overrides (user-defined values to replace game values)
extern std::atomic<NVSDK_NGX_Parameter*> g_last_ngx_parameter;  // Last NGX parameter object for direct API calls

/** Debug table row order: Parameter vtable aggregate counts, then _nvngx export order (kNGXHooks), framegen, total. */
enum class NGXCounterKind : int {
    ParameterSetF = 0,
    ParameterSetD,
    ParameterSetI,
    ParameterSetUI,
    ParameterSetULL,
    ParameterGetI,
    ParameterGetUI,
    ParameterGetULL,
    ParameterGetVoidPointer,
    D3D12_Init,
    D3D12_InitExt,
    D3D12_InitProjectId,
    D3D12_Shutdown1,
    D3D12_CreateFeature,
    D3D12_ReleaseFeature,
    D3D12_EvaluateFeature,
    D3D12_EvaluateFeature_C,
    D3D11_Init,
    D3D11_InitExt,
    D3D11_InitProjectId,
    D3D11_Shutdown1,
    D3D11_CreateFeature,
    D3D11_ReleaseFeature,
    D3D11_EvaluateFeature,
    D3D11_EvaluateFeature_C,
    Vulkan_EvaluateFeature,
    Vulkan_EvaluateFeature_C,
    Vulkan_Init,
    Vulkan_InitProjectId,
    Vulkan_Shutdown1,
    Vulkan_CreateFeature,
    Vulkan_CreateFeature1,
    Vulkan_ReleaseFeature,
    Vulkan_GetParameters,
    Vulkan_GetCapabilityParameters,
    Vulkan_AllocateParameters,
    D3D12_GetParameters,
    D3D12_GetCapabilityParameters,
    D3D12_AllocateParameters,
    D3D11_GetParameters,
    D3D11_GetCapabilityParameters,
    D3D11_AllocateParameters,
    FramegenCreateAttempt,
    Total,
    Count_
};

// NGX Counters structure for tracking NGX function calls
struct NGXCounters {
    // Parameter functions
    std::atomic<uint32_t> parameter_setf_count;
    std::atomic<uint32_t> parameter_setd_count;
    std::atomic<uint32_t> parameter_seti_count;
    std::atomic<uint32_t> parameter_setui_count;
    std::atomic<uint32_t> parameter_setull_count;
    std::atomic<uint32_t> parameter_geti_count;
    std::atomic<uint32_t> parameter_getui_count;
    std::atomic<uint32_t> parameter_getull_count;
    std::atomic<uint32_t> parameter_getvoidpointer_count;

    // D3D12 Feature management functions
    std::atomic<uint32_t> d3d12_init_count;
    std::atomic<uint32_t> d3d12_init_ext_count;
    std::atomic<uint32_t> d3d12_init_projectid_count;
    std::atomic<uint32_t> d3d12_shutdown1_count;
    std::atomic<uint32_t> d3d12_createfeature_count;
    std::atomic<uint32_t> d3d12_releasefeature_count;
    std::atomic<uint32_t> d3d12_evaluatefeature_count;
    std::atomic<uint32_t> d3d12_evaluatefeature_c_count;
    std::atomic<uint32_t> d3d12_getparameters_count;
    std::atomic<uint32_t> d3d12_getcapabilityparameters_count;
    std::atomic<uint32_t> d3d12_allocateparameters_count;

    // D3D11 Feature management functions
    std::atomic<uint32_t> d3d11_init_count;
    std::atomic<uint32_t> d3d11_init_ext_count;
    std::atomic<uint32_t> d3d11_init_projectid_count;
    std::atomic<uint32_t> d3d11_shutdown1_count;
    std::atomic<uint32_t> d3d11_createfeature_count;
    std::atomic<uint32_t> d3d11_releasefeature_count;
    std::atomic<uint32_t> d3d11_evaluatefeature_count;
    std::atomic<uint32_t> d3d11_evaluatefeature_c_count;
    std::atomic<uint32_t> vulkan_evaluatefeature_count;
    std::atomic<uint32_t> vulkan_evaluatefeature_c_count;
    std::atomic<uint32_t> vulkan_init_count;
    std::atomic<uint32_t> vulkan_init_projectid_count;
    std::atomic<uint32_t> vulkan_shutdown1_count;
    std::atomic<uint32_t> vulkan_createfeature_count;
    std::atomic<uint32_t> vulkan_createfeature1_count;
    std::atomic<uint32_t> vulkan_releasefeature_count;
    std::atomic<uint32_t> vulkan_getparameters_count;
    std::atomic<uint32_t> vulkan_getcapabilityparameters_count;
    std::atomic<uint32_t> vulkan_allocateparameters_count;
    std::atomic<uint32_t> d3d11_getparameters_count;
    std::atomic<uint32_t> d3d11_getcapabilityparameters_count;
    std::atomic<uint32_t> d3d11_allocateparameters_count;

    // Frame Generation (DLSS-G) create attempt count
    std::atomic<uint32_t> framegen_create_attempt_count;

    // Total counter
    std::atomic<uint32_t> total_count;

    // Constructor to initialize all counters to 0
    NGXCounters()
        : parameter_setf_count(0),
          parameter_setd_count(0),
          parameter_seti_count(0),
          parameter_setui_count(0),
          parameter_setull_count(0),
          parameter_geti_count(0),
          parameter_getui_count(0),
          parameter_getull_count(0),
          parameter_getvoidpointer_count(0),
          d3d12_init_count(0),
          d3d12_init_ext_count(0),
          d3d12_init_projectid_count(0),
          d3d12_shutdown1_count(0),
          d3d12_createfeature_count(0),
          d3d12_releasefeature_count(0),
          d3d12_evaluatefeature_count(0),
          d3d12_evaluatefeature_c_count(0),
          d3d12_getparameters_count(0),
          d3d12_getcapabilityparameters_count(0),
          d3d12_allocateparameters_count(0),
          d3d11_init_count(0),
          d3d11_init_ext_count(0),
          d3d11_init_projectid_count(0),
          d3d11_shutdown1_count(0),
          d3d11_createfeature_count(0),
          d3d11_releasefeature_count(0),
          d3d11_evaluatefeature_count(0),
          d3d11_evaluatefeature_c_count(0),
          vulkan_evaluatefeature_count(0),
          vulkan_evaluatefeature_c_count(0),
          vulkan_init_count(0),
          vulkan_init_projectid_count(0),
          vulkan_shutdown1_count(0),
          vulkan_createfeature_count(0),
          vulkan_createfeature1_count(0),
          vulkan_releasefeature_count(0),
          vulkan_getparameters_count(0),
          vulkan_getcapabilityparameters_count(0),
          vulkan_allocateparameters_count(0),
          d3d11_getparameters_count(0),
          d3d11_getcapabilityparameters_count(0),
          d3d11_allocateparameters_count(0),
          framegen_create_attempt_count(0),
          total_count(0) {}

    // Reset all counters to 0
    void reset() {
        parameter_setf_count.store(0);
        parameter_setd_count.store(0);
        parameter_seti_count.store(0);
        parameter_setui_count.store(0);
        parameter_setull_count.store(0);
        parameter_geti_count.store(0);
        parameter_getui_count.store(0);
        parameter_getull_count.store(0);
        parameter_getvoidpointer_count.store(0);
        d3d12_init_count.store(0);
        d3d12_init_ext_count.store(0);
        d3d12_init_projectid_count.store(0);
        d3d12_shutdown1_count.store(0);
        d3d12_createfeature_count.store(0);
        d3d12_releasefeature_count.store(0);
        d3d12_evaluatefeature_count.store(0);
        d3d12_evaluatefeature_c_count.store(0);
        d3d12_getparameters_count.store(0);
        d3d12_getcapabilityparameters_count.store(0);
        d3d12_allocateparameters_count.store(0);
        d3d11_init_count.store(0);
        d3d11_init_ext_count.store(0);
        d3d11_init_projectid_count.store(0);
        d3d11_shutdown1_count.store(0);
        d3d11_createfeature_count.store(0);
        d3d11_releasefeature_count.store(0);
        d3d11_evaluatefeature_count.store(0);
        d3d11_evaluatefeature_c_count.store(0);
        vulkan_evaluatefeature_count.store(0);
        vulkan_evaluatefeature_c_count.store(0);
        vulkan_init_count.store(0);
        vulkan_init_projectid_count.store(0);
        vulkan_shutdown1_count.store(0);
        vulkan_createfeature_count.store(0);
        vulkan_createfeature1_count.store(0);
        vulkan_releasefeature_count.store(0);
        vulkan_getparameters_count.store(0);
        vulkan_getcapabilityparameters_count.store(0);
        vulkan_allocateparameters_count.store(0);
        d3d11_getparameters_count.store(0);
        d3d11_getcapabilityparameters_count.store(0);
        d3d11_allocateparameters_count.store(0);
        framegen_create_attempt_count.store(0);
        total_count.store(0);
    }
};

// NGX Counters global instance
extern NGXCounters g_ngx_counters;

// DLSS/DLSS-G Summary structure
struct DLSSGSummary {
    bool dlss_active = false;
    bool dlss_g_active = false;
    bool ray_reconstruction_active = false;
    bool any_dlss_was_active_once =
        false;  // true if any feature was active at least once this session (for UI section visibility)
    bool any_dlss_dll_loaded = false;  // true if any of nvngx_dlss/dlssd/dlssg.dll is loaded in process
    std::string internal_resolution = "N/A";
    std::string output_resolution = "N/A";
    std::string scaling_ratio = "N/A";
    std::string quality_preset = "N/A";
    std::string aspect_ratio = "N/A";
    std::string fov = "N/A";
    std::string jitter_offset = "N/A";
    std::string exposure = "N/A";
    std::string depth_inverted = "N/A";
    std::string hdr_enabled = "N/A";
    std::string motion_vectors_included = "N/A";
    std::string frame_time_delta = "N/A";
    std::string sharpness = "N/A";
    std::string tonemapper_type = "N/A";
    std::string fg_mode = "N/A";
    std::string ofa_enabled = "N/A";
    std::string dlss_dll_version = "N/A";
    std::string dlssg_dll_version = "N/A";
    std::string dlssd_dll_version = "N/A";
    std::string supported_dlss_presets = "N/A";
    std::string supported_dlss_rr_presets = "N/A";
    std::string runtime_sr_preset = "N/A";  // Observed NGX value (captured from hooks)
    std::string runtime_rr_preset = "N/A";  // Observed NGX value (captured from hooks)
    std::string auto_exposure = "N/A";  // "On", "Off", or "N/A" (from DLSS Feature Create Flags)
    // When DLSS override is enabled for a DLL: true if that loaded DLL is from its override folder
    bool dlss_override_applied = true;   // nvngx_dlss.dll
    bool dlssd_override_applied = true;  // nvngx_dlssd.dll (D = denoiser / RR)
    bool dlssg_override_applied = true;  // nvngx_dlssg.dll (G = generation / FG)
};

// Tracked DLSS module/path from OnModuleLoaded (DLL name or .bin identified as nvngx_dlss/dlssg/dlssd).
// Used instead of GetModuleHandleW when we saw the load (works for .bin files that don't have DLL name).
enum class DlssTrackedKind : int { DLSS = 0, DLSSG = 1, DLSSD = 2 };

struct DlssTrackedInfo {
    HMODULE module = nullptr;
    std::string path;
};

// Thread-safe getters (read under g_dlss_tracked_srwlock). Returns nullopt if not yet set.
std::optional<HMODULE> GetDlssTrackedModule(DlssTrackedKind kind);
std::optional<std::string> GetDlssTrackedPath(DlssTrackedKind kind);

// Set from OnModuleLoaded (write under g_dlss_tracked_srwlock). Path is obtained from hMod via GetModuleFileNameW.
void SetDlssTracked(DlssTrackedKind kind, HMODULE hMod, bool force = false);

// True when a .bin was identified as DLSS/DLSS-G/DLSS-D (NVIDIA App override). Set from loadlibrary_hooks.
extern std::atomic<bool> g_dlss_from_nvidia_app_bin;

// DLSS Model Profile structure
struct DLSSModelProfile {
    bool is_valid = false;
    int sr_quality_preset = 0;
    int sr_balanced_preset = 0;
    int sr_performance_preset = 0;
    int sr_ultra_performance_preset = 0;
    int sr_ultra_quality_preset = 0;
    int sr_dlaa_preset = 0;
    int rr_quality_preset = 0;
    int rr_balanced_preset = 0;
    int rr_performance_preset = 0;
    int rr_ultra_performance_preset = 0;
    int rr_ultra_quality_preset = 0;
};

// Function to get DLSS/DLSS-G summary from NGX parameters
DLSSGSummary GetDLSSGSummary();

// Main tab optional \"DLSS Control\" panel: cheap gate before calling GetDLSSGSummary (DLSS DLL loaded / feature seen).
bool ShouldShowDlssInformationSection();

// DLSSGSummaryLite::fg_mode — frame-generation multiplier (call GetDLSSGSummaryLite every frame)
inline constexpr int kDlssGFgModeOff = 0;              // FG disabled
inline constexpr int kDlssGFgModeActiveUnknown = -1;     // FG on but NGX MultiFrameCount unavailable

// Lite summary for FPS limiter / overlay: any_dlss_active, dlss_active, dlss_g_active, ray_reconstruction_active,
// fg_mode (call every frame)
struct DLSSGSummaryLite {
    bool any_dlss_active = false;  // dlss_active || dlss_g_active || ray_reconstruction_active
    bool dlss_active = false;
    bool dlss_g_active = false;
    bool ray_reconstruction_active = false;
    // 0 = off, -1 = active unknown, >= 2 => Nx (divide capped FPS by this integer)
    int fg_mode = kDlssGFgModeOff;
};
DLSSGSummaryLite GetDLSSGSummaryLite();

// Function to get DLSS Model Profile
DLSSModelProfile GetDLSSModelProfile();

// Unified game Reflex sleep mode params (D3D and Vulkan both write here; used for Game Defaults mode)
struct GameReflexSleepModeParams {
    bool low_latency = false;
    bool boost = false;
    uint32_t minimum_interval_us = 0;
    bool has_value = false;
};
void SetGameReflexSleepModeParams(bool low_latency, bool boost, uint32_t minimum_interval_us);
void GetGameReflexSleepModeParams(GameReflexSleepModeParams* out);

// NVAPI SetSleepMode tracking
extern std::atomic<std::shared_ptr<NV_SET_SLEEP_MODE_PARAMS>>
    g_last_nvapi_sleep_mode_params;                             // Last SetSleepMode parameters
extern std::atomic<IUnknown*> g_last_nvapi_sleep_mode_dev_ptr;  // Last device pointer for SetSleepMode
// Last Reflex params that we (addon) set via ApplySleepMode / NvAPI_D3D_SetSleepMode_Direct (for UI tooltip)
extern std::atomic<std::shared_ptr<NV_SET_SLEEP_MODE_PARAMS>> g_last_reflex_params_set_by_addon;

// NVAPI Reflex timing tracking
extern std::atomic<LONGLONG> g_sleep_reflex_injected_ns;  // Time between injected Reflex sleep calls
extern std::atomic<LONGLONG> g_sleep_reflex_native_ns;    // Time between native Reflex sleep calls
// Smoothed (rolling average) time between native Reflex sleep calls
extern std::atomic<LONGLONG> g_sleep_reflex_native_ns_smooth;
// Smoothed (rolling average) time between injected Reflex sleep calls
extern std::atomic<LONGLONG> g_sleep_reflex_injected_ns_smooth;

// g_nvapi_last_sleep_timestamp_ns

// Helper function to check if native Reflex is active
// Now detects native Reflex only via SetLatencyMarker calls (following Special-K approach)
inline bool IsNativeReflexActive() {
    return g_native_reflex_detected.load();
    // && !settings::g_advancedTabSettings.reflex_supress_native.GetValue();
}

// Reflex debug counters
extern std::atomic<uint32_t> g_reflex_sleep_count;             // Total Sleep calls
extern std::atomic<uint32_t> g_reflex_apply_sleep_mode_count;  // Total ApplySleepMode calls
extern std::atomic<LONGLONG> g_reflex_sleep_duration_ns;       // Rolling average sleep duration in nanoseconds

// Individual marker type counters
extern std::atomic<uint32_t> g_reflex_marker_simulation_start_count;
extern std::atomic<uint32_t> g_reflex_marker_simulation_end_count;
extern std::atomic<uint32_t> g_reflex_marker_rendersubmit_start_count;
extern std::atomic<uint32_t> g_reflex_marker_rendersubmit_end_count;
extern std::atomic<uint32_t> g_reflex_marker_present_start_count;
extern std::atomic<uint32_t> g_reflex_marker_present_end_count;
extern std::atomic<uint32_t> g_reflex_marker_input_sample_count;

// Injected Reflex status: last timestamp (ns) when each of the first 6 markers was sent, and when sleep was called (0 =
// never)
extern std::atomic<LONGLONG> g_injected_reflex_last_marker_time_ns[6];  // SIMULATION_START..PRESENT_END
extern std::atomic<LONGLONG> g_injected_reflex_last_sleep_time_ns;

// PCLStats ping signal (edge-triggered, cleared after injection on SIMULATION_START)
extern std::atomic<bool> g_pclstats_ping_signal;

// PCLStats ETW diagnostics (slots index by PCLSTATS_LATENCY_MARKER_TYPE value, clamped)
constexpr std::size_t kPclStatsEtwMarkerSlotCount = 32;
extern std::atomic<uint64_t> g_pclstats_init_success_count;
extern std::atomic<LONGLONG> g_pclstats_last_init_time_ns;
extern std::atomic<uint64_t> g_pclstats_etw_total_count;
extern std::atomic<uint64_t> g_pclstats_etw_by_marker[kPclStatsEtwMarkerSlotCount];

// DX11 Proxy HWND for filtering
extern HWND g_proxy_hwnd;

// NGX preset initialization tracking
extern std::atomic<bool> g_ngx_presets_initialized;

// Cached frame statistics (updated in present detour, read by monitoring thread)
extern std::atomic<std::shared_ptr<DXGI_FRAME_STATISTICS>> g_cached_frame_stats;

// Forward declaration for refresh rate stats (full type needed for shared_ptr)
namespace dxgi::fps_limiter {
struct RefreshRateStats;
}

// Cached refresh rate statistics (updated in continuous monitoring thread, read by render/UI threads)
// Note: Using forward declaration works here because shared_ptr only needs the type for storage
extern std::atomic<std::shared_ptr<const dxgi::fps_limiter::RefreshRateStats>> g_cached_refresh_rate_stats;

// Swapchain wrapper statistics
// Frame time ring buffer capacity (must be power of 2 for efficient modulo)
constexpr size_t kSwapchainFrameTimeCapacity = 256;

struct SwapChainWrapperStats {
    std::atomic<uint64_t> total_present_calls{0};
    std::atomic<uint64_t> total_present1_calls{0};
    std::atomic<uint64_t> last_present_time_ns{0};   // Last Present call time in nanoseconds
    std::atomic<uint64_t> last_present1_time_ns{0};  // Last Present1 call time in nanoseconds
    std::atomic<double> smoothed_present_fps{0.0};   // Smoothed FPS for Present (calls per second)
    std::atomic<double> smoothed_present1_fps{0.0};  // Smoothed FPS for Present1 (calls per second)

    // Frame time ring buffer (stores frame times in milliseconds)
    std::atomic<uint32_t> frame_time_head{0};                // Ring buffer head index
    float frame_times[kSwapchainFrameTimeCapacity];          // Frame times in ms (array of floats)
    std::atomic<uint64_t> last_present_combined_time_ns{0};  // Last call time for any Present/Present1
};

extern SwapChainWrapperStats g_swapchain_wrapper_stats_proxy;
extern SwapChainWrapperStats g_swapchain_wrapper_stats_native;

// Track if DXGISwapChain4Wrapper::Present or Present1 has been called at least once
extern std::atomic<bool> g_swapchain_wrapper_present_called;
extern std::atomic<bool> g_swapchain_wrapper_present1_called;

// Continuous monitoring functions
void StartContinuousMonitoring();
void StopContinuousMonitoring();
void HandleReflexAutoConfigure();
