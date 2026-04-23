#include "streamline_hooks.hpp"
#include "../../features/streamline/streamline_proxy_dxgi.hpp"
#include "../../globals.hpp"
#include "../../settings/advanced_tab_settings.hpp"
#include "../../settings/swapchain_tab_settings.hpp"
#include "../../utils/detour_call_tracker.hpp"
#include "../../utils/general_utils.hpp"
#include "../../utils/logging.hpp"
#include "../../utils/timing.hpp"
#include "../hook_suppression_manager.hpp"

// Libraries <standard C++>
#include <atomic>
#include <cstdint>
#include <cstring>

#include <MinHook.h>

#include <Windows.h>

// Libraries <Windows>
#include <d3d11.h>
#include <d3d12.h>
#include <dxgi.h>
#include <wrl/client.h>

// Streamline base (sl_dlss.h requires sl.h for Boolean, SL_STRUCT, etc.)
#include "sl.h"
#include "sl_consts.h"
#include "sl_core_types.h"
// Streamline DLSS types (from sl_dlss.h)
#include "sl_dlss.h"
// Streamline DLSS-G types (from sl_dlss_g.h)
#include "sl_dlss_g.h"

// Loader exports from sl.interposer.dll — hooked via kStreamlineLoaderHooks (sl_core_api.h PFun_*).
using slInit_pfn = sl::Result (*)(const sl::Preferences& pref, uint64_t sdkVersion);
using slUpgradeInterface_pfn = sl::Result (*)(void** baseInterface);
using slIsFeatureSupported_pfn = sl::Result (*)(sl::Feature feature, const sl::AdapterInfo& adapterInfo);
using slGetNativeInterface_pfn = sl::Result (*)(void* proxyInterface, void** baseInterface);
using slGetFeatureFunction_pfn = sl::Result (*)(sl::Feature feature, const char* functionName, void*& function);

// Feature/plugin function pointers — resolved via slGetFeatureFunction or GetProcAddress on the DLSS plugin
// (sl_dlss.h, sl_dlss_g.h, slSetData).
using slDLSSGetOptimalSettings_pfn = sl::Result (*)(const sl::DLSSOptions& options, sl::DLSSOptimalSettings& settings);
using slDLSSSetOptions_pfn = sl::Result (*)(const sl::ViewportHandle& viewport, const sl::DLSSOptions& options);
using slDLSSGSetOptions_pfn = sl::Result (*)(const sl::ViewportHandle& viewport, const sl::DLSSGOptions& options);
using slSetData_pfn = sl::Result (*)(const sl::BaseStructure* inputs, sl::CommandBuffer* cmdBuffer);
using slDLSSGGetState_pfn = sl::Result (*)(const sl::ViewportHandle& viewport, sl::DLSSGState& state, const sl::DLSSGOptions* options);

static slInit_pfn slInit_Original = nullptr;
static slUpgradeInterface_pfn slUpgradeInterface_Original = nullptr;
static slIsFeatureSupported_pfn slIsFeatureSupported_Original = nullptr;
static slGetNativeInterface_pfn slGetNativeInterface_Original = nullptr;
static slGetFeatureFunction_pfn slGetFeatureFunction_Original = nullptr;

// Forward declarations for table-driven hook install
static sl::Result slInit_Detour(const sl::Preferences& pref, uint64_t sdkVersion);
static sl::Result slUpgradeInterface_Detour(void** baseInterface);
static sl::Result slIsFeatureSupported_Detour(sl::Feature feature, const sl::AdapterInfo& adapterInfo);
static sl::Result slGetNativeInterface_Detour(void* proxyInterface, void** baseInterface);
static sl::Result slGetFeatureFunction_Detour(sl::Feature feature, const char* functionName, void*& function);

/** Table-driven install for sl.interposer.dll exports. Order matches StreamlineLoaderHook enum. */
enum class StreamlineLoaderHook : std::size_t {
    slInit = 0,
    slUpgradeInterface,
    slIsFeatureSupported,
    slGetNativeInterface,
    slGetFeatureFunction,
    Count
};
struct StreamlineLoaderHookEntry {
    const char* name;
    LPVOID detour;
    LPVOID* original;
};
static const StreamlineLoaderHookEntry kStreamlineLoaderHooks[static_cast<std::size_t>(StreamlineLoaderHook::Count)] = {
    {.name = "slInit",
     .detour = reinterpret_cast<LPVOID>(&slInit_Detour),
     .original = reinterpret_cast<LPVOID*>(&slInit_Original)},
    {.name = "slUpgradeInterface",
     .detour = reinterpret_cast<LPVOID>(&slUpgradeInterface_Detour),
     .original = reinterpret_cast<LPVOID*>(&slUpgradeInterface_Original)},
    {.name = "slIsFeatureSupported",
     .detour = reinterpret_cast<LPVOID>(&slIsFeatureSupported_Detour),
     .original = reinterpret_cast<LPVOID*>(&slIsFeatureSupported_Original)},
    {.name = "slGetNativeInterface",
     .detour = reinterpret_cast<LPVOID>(&slGetNativeInterface_Detour),
     .original = reinterpret_cast<LPVOID*>(&slGetNativeInterface_Original)},
    {.name = "slGetFeatureFunction",
     .detour = reinterpret_cast<LPVOID>(&slGetFeatureFunction_Detour),
     .original = reinterpret_cast<LPVOID*>(&slGetFeatureFunction_Original)},
};

// slDLSSGetOptimalSettings / slDLSSSetOptions / slDLSSGSetOptions / slSetData — originals via slGetFeatureFunction
// (or plugin for slSetData); see kSlGetFeatureResolvedHooks.
static slDLSSGetOptimalSettings_pfn slDLSSGetOptimalSettings_Original = nullptr;
static std::atomic<bool> g_slDLSSGetOptimalSettings_hook_installed{false};

static slDLSSSetOptions_pfn slDLSSSetOptions_Original = nullptr;
static std::atomic<bool> g_slDLSSSetOptions_hook_installed{false};

static slDLSSGSetOptions_pfn slDLSSGSetOptions_Original = nullptr;
static std::atomic<bool> g_slDLSSGSetOptions_hook_installed{false};

static slSetData_pfn slSetData_Original = nullptr;
static std::atomic<bool> g_slSetData_hook_installed{false};

// Track SDK version from slInit calls
static std::atomic<uint64_t> g_last_sdk_version{0};
static std::atomic<uint64_t> g_sl_upgrade_interface_call_count{0};

static std::atomic<uint64_t> g_sl_upgrade_qi_factory{0};
static std::atomic<uint64_t> g_sl_upgrade_qi_swapchain{0};
static std::atomic<uint64_t> g_sl_upgrade_qi_d3d11{0};
static std::atomic<uint64_t> g_sl_upgrade_qi_d3d12{0};
static std::atomic<uint64_t> g_sl_upgrade_qi_unknown{0};
static std::atomic<uint64_t> g_sl_upgrade_classify_non_ok{0};
static std::atomic<uint64_t> g_sl_upgrade_classify_null_iface{0};

// After slUpgradeInterface returns, count which DXGI/D3D interface the upgraded pointer exposes (QI order matches
// historical slUpgradeInterface_Detour: factory, swapchain, D3D11 device, D3D12 device, else unknown).
static void CountSlUpgradeInterfaceUpgradedInterface(sl::Result result, void** baseInterface) {
    if (result != sl::Result::eOk) {
        g_sl_upgrade_classify_non_ok.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    if (baseInterface == nullptr || *baseInterface == nullptr) {
        g_sl_upgrade_classify_null_iface.fetch_add(1, std::memory_order_relaxed);
        return;
    }

    IUnknown* const unk = static_cast<IUnknown*>(*baseInterface);

    Microsoft::WRL::ComPtr<IDXGIFactory> dxgi_factory;
    Microsoft::WRL::ComPtr<IDXGISwapChain> dxgi_swapchain;
    Microsoft::WRL::ComPtr<ID3D11Device> d3d11_device;
    Microsoft::WRL::ComPtr<ID3D12Device> d3d12_device;

    if (SUCCEEDED(unk->QueryInterface(IID_PPV_ARGS(&dxgi_factory))) && dxgi_factory != nullptr) {
        g_sl_upgrade_qi_factory.fetch_add(1, std::memory_order_relaxed);
    } else if (SUCCEEDED(unk->QueryInterface(IID_PPV_ARGS(&dxgi_swapchain))) && dxgi_swapchain != nullptr) {
        g_sl_upgrade_qi_swapchain.fetch_add(1, std::memory_order_relaxed);
    } else if (SUCCEEDED(unk->QueryInterface(IID_PPV_ARGS(&d3d11_device))) && d3d11_device != nullptr) {
        g_sl_upgrade_qi_d3d11.fetch_add(1, std::memory_order_relaxed);
    } else if (SUCCEEDED(unk->QueryInterface(IID_PPV_ARGS(&d3d12_device))) && d3d12_device != nullptr) {
        g_sl_upgrade_qi_d3d12.fetch_add(1, std::memory_order_relaxed);
    } else {
        g_sl_upgrade_qi_unknown.fetch_add(1, std::memory_order_relaxed);
    }
}

// Helpers to log DLSS options
static const char* DLSSModeStr(sl::DLSSMode m) {
    switch (m) {
        case sl::DLSSMode::eOff:              return "Off";
        case sl::DLSSMode::eMaxPerformance:   return "MaxPerformance";
        case sl::DLSSMode::eBalanced:         return "Balanced";
        case sl::DLSSMode::eMaxQuality:       return "MaxQuality";
        case sl::DLSSMode::eUltraPerformance: return "UltraPerformance";
        case sl::DLSSMode::eUltraQuality:     return "UltraQuality";
        case sl::DLSSMode::eDLAA:             return "DLAA";
        default:                              return "?";
    }
}
// Cached DLSS options for change detection (only log when these change)
struct CachedDLSSOptionsLog {
    sl::DLSSMode mode{};
    uint32_t outputWidth{0};
    uint32_t outputHeight{0};
    float preExposure{0.f};
    float exposureScale{0.f};
    sl::DLSSPreset dlaaPreset{};
    sl::DLSSPreset qualityPreset{};
    sl::DLSSPreset balancedPreset{};
    sl::DLSSPreset performancePreset{};
    sl::DLSSPreset ultraPerformancePreset{};
    sl::DLSSPreset ultraQualityPreset{};
    bool initialized{false};
};
static CachedDLSSOptionsLog s_lastLoggedDLSSOptions;
static constexpr uint32_t kMaxDLSSOptionsLogCount = 10u;
static std::atomic<uint32_t> s_dlssOptionsLogCount{0u};

static bool OptionsDifferFromCache(const sl::DLSSOptions& o) {
    if (!s_lastLoggedDLSSOptions.initialized) return true;
    return s_lastLoggedDLSSOptions.mode != o.mode || s_lastLoggedDLSSOptions.outputWidth != o.outputWidth
           || s_lastLoggedDLSSOptions.outputHeight != o.outputHeight
           || s_lastLoggedDLSSOptions.preExposure != o.preExposure
           || s_lastLoggedDLSSOptions.exposureScale != o.exposureScale
           || s_lastLoggedDLSSOptions.dlaaPreset != o.dlaaPreset
           || s_lastLoggedDLSSOptions.qualityPreset != o.qualityPreset
           || s_lastLoggedDLSSOptions.balancedPreset != o.balancedPreset
           || s_lastLoggedDLSSOptions.performancePreset != o.performancePreset
           || s_lastLoggedDLSSOptions.ultraPerformancePreset != o.ultraPerformancePreset
           || s_lastLoggedDLSSOptions.ultraQualityPreset != o.ultraQualityPreset;
}

static void UpdateDLSSOptionsCache(const sl::DLSSOptions& o) {
    s_lastLoggedDLSSOptions.mode = o.mode;
    s_lastLoggedDLSSOptions.outputWidth = o.outputWidth;
    s_lastLoggedDLSSOptions.outputHeight = o.outputHeight;
    s_lastLoggedDLSSOptions.preExposure = o.preExposure;
    s_lastLoggedDLSSOptions.exposureScale = o.exposureScale;
    s_lastLoggedDLSSOptions.dlaaPreset = o.dlaaPreset;
    s_lastLoggedDLSSOptions.qualityPreset = o.qualityPreset;
    s_lastLoggedDLSSOptions.balancedPreset = o.balancedPreset;
    s_lastLoggedDLSSOptions.performancePreset = o.performancePreset;
    s_lastLoggedDLSSOptions.ultraPerformancePreset = o.ultraPerformancePreset;
    s_lastLoggedDLSSOptions.ultraQualityPreset = o.ultraQualityPreset;
    s_lastLoggedDLSSOptions.initialized = true;
}

// Returns true if options were logged (i.e. they changed from last time). Logs at most kMaxDLSSOptionsLogCount times.
static bool LogDLSSOptions(const sl::DLSSOptions& o) {
    if (!OptionsDifferFromCache(o)) return false;
    uint32_t n = s_dlssOptionsLogCount.fetch_add(1, std::memory_order_relaxed);
    if (n >= kMaxDLSSOptionsLogCount) return false;
    LogInfo("  DLSSOptions: mode=%s output=%ux%u preExposure=%.2f exposureScale=%.2f", DLSSModeStr(o.mode),
            o.outputWidth, o.outputHeight, o.preExposure, o.exposureScale);
    LogInfo("  presets: dlaa=%u quality=%u balanced=%u perf=%u ultraPerf=%u ultraQual=%u",
            static_cast<unsigned>(o.dlaaPreset), static_cast<unsigned>(o.qualityPreset),
            static_cast<unsigned>(o.balancedPreset), static_cast<unsigned>(o.performancePreset),
            static_cast<unsigned>(o.ultraPerformancePreset), static_cast<unsigned>(o.ultraQualityPreset));
    UpdateDLSSOptionsCache(o);
    return true;
}

// Map main-tab DLSS quality preset (GetDLSSQualityPresetValue) to sl::DLSSMode
static sl::DLSSMode QualityPresetValueToSLMode(NVSDK_NGX_PerfQuality_Value ngxQualityValue) {
    switch (ngxQualityValue) {
        case NVSDK_NGX_PerfQuality_Value_MaxPerf:          return sl::DLSSMode::eMaxPerformance;
        case NVSDK_NGX_PerfQuality_Value_Balanced:         return sl::DLSSMode::eBalanced;
        case NVSDK_NGX_PerfQuality_Value_MaxQuality:       return sl::DLSSMode::eMaxQuality;
        case NVSDK_NGX_PerfQuality_Value_UltraPerformance: return sl::DLSSMode::eUltraPerformance;
        case NVSDK_NGX_PerfQuality_Value_UltraQuality:     return sl::DLSSMode::eUltraQuality;
        case NVSDK_NGX_PerfQuality_Value_DLAA:             return sl::DLSSMode::eDLAA;
        default:                                           return sl::DLSSMode::eMaxQuality;
    }
}

// Map sl::DLSSMode to NGX PerfQualityValue for g_ngx_parameters (DLSS Information tab). Returns -1 for eOff.
static int SLModeToPerfQualityValue(sl::DLSSMode m) {
    switch (m) {
        case sl::DLSSMode::eOff:              return -1;
        case sl::DLSSMode::eMaxPerformance:   return static_cast<int>(NVSDK_NGX_PerfQuality_Value_MaxPerf);
        case sl::DLSSMode::eBalanced:         return static_cast<int>(NVSDK_NGX_PerfQuality_Value_Balanced);
        case sl::DLSSMode::eMaxQuality:       return static_cast<int>(NVSDK_NGX_PerfQuality_Value_MaxQuality);
        case sl::DLSSMode::eUltraPerformance: return static_cast<int>(NVSDK_NGX_PerfQuality_Value_UltraPerformance);
        case sl::DLSSMode::eUltraQuality:     return static_cast<int>(NVSDK_NGX_PerfQuality_Value_UltraQuality);
        case sl::DLSSMode::eDLAA:             return static_cast<int>(NVSDK_NGX_PerfQuality_Value_DLAA);
        default:                              return -1;
    }
}

// Update g_ngx_parameters from Streamline DLSS options/settings so Vulkan (and other SL-only) titles
// show correct values in the DLSS Information tab, which reads from g_ngx_parameters (NGX path).
// Also sets g_streamline_dlss_enabled so DLSS can be shown as on elsewhere (e.g. summary, IsDLSSEnabled).
static void UpdateNGXParamsFromDLSSOptions(const sl::DLSSOptions& options) {
    const bool dlss_on = (options.mode != sl::DLSSMode::eOff);
    g_streamline_dlss_enabled.store(dlss_on);
    if (dlss_on) {
        g_dlss_was_active_once.store(true);
        // Ray Reconstruction is often part of the DLSS stack in Streamline (no separate create). Mark RR as "seen"
        // when DLSS is in use so the UI shows CreateFeature seen for DLSS-RR.
        g_ray_reconstruction_was_active_once.store(true);
    }
    if (options.outputWidth != sl::INVALID_UINT && options.outputHeight != sl::INVALID_UINT) {
        g_ngx_parameters.update_uint("Width", options.outputWidth);
        g_ngx_parameters.update_uint("Height", options.outputHeight);
    }
    const int perfVal = SLModeToPerfQualityValue(options.mode);
    if (perfVal >= 0) {
        g_ngx_parameters.update_int("PerfQualityValue", perfVal);
    }
    g_ngx_parameters.update_float("Sharpness", options.sharpness);
    g_ngx_parameters.update_float("DLSS.Pre.Exposure", options.preExposure);
    g_ngx_parameters.update_float("DLSS.Exposure.Scale", options.exposureScale);
    g_ngx_parameters.update_int("DLSSG.ColorBuffersHDR", (options.colorBuffersHDR == sl::Boolean::eTrue) ? 1 : 0);
    g_ngx_parameters.update_int("DLSS.Hint.Render.Preset.DLAA", static_cast<int>(options.dlaaPreset));
    g_ngx_parameters.update_int("DLSS.Hint.Render.Preset.Quality", static_cast<int>(options.qualityPreset));
    g_ngx_parameters.update_int("DLSS.Hint.Render.Preset.Balanced", static_cast<int>(options.balancedPreset));
    g_ngx_parameters.update_int("DLSS.Hint.Render.Preset.Performance", static_cast<int>(options.performancePreset));
    g_ngx_parameters.update_int("DLSS.Hint.Render.Preset.UltraPerformance",
                                static_cast<int>(options.ultraPerformancePreset));
    g_ngx_parameters.update_int("DLSS.Hint.Render.Preset.UltraQuality", static_cast<int>(options.ultraQualityPreset));
}

static void UpdateNGXParamsFromDLSSOptimalSettings(const sl::DLSSOptimalSettings& settings) {
    // Only write internal (subrect) dimensions when both are non-zero to avoid "width x 0" (e.g. Vulkan before plugin
    // fills)
    if (settings.optimalRenderWidth > 0 && settings.optimalRenderHeight > 0) {
        g_ngx_parameters.update_uint("DLSS.Render.Subrect.Dimensions.Width", settings.optimalRenderWidth);
        g_ngx_parameters.update_uint("DLSS.Render.Subrect.Dimensions.Height", settings.optimalRenderHeight);
    }
    g_ngx_parameters.update_float("Sharpness", settings.optimalSharpness);
}

// Update g_ngx_parameters and FG atomic from Streamline DLSS-G state (for Vulkan/Streamline DLSS Information tab).
// "Enabled" is based on returned state.status (operational), not the requested options.
// Options (if provided by the caller) are used only for mode / requested numFramesToGenerate.
static void UpdateNGXParamsFromDLSSGOptions(const sl::DLSSGState& state, const sl::DLSSGOptions* options) {
    const uint32_t status_val = static_cast<uint32_t>(state.status);
    g_ngx_parameters.update_uint("DLSSG.Status", status_val);
    g_ngx_parameters.update_uint("DLSSG.NumFramesToGenerateMax", state.numFramesToGenerateMax);
    g_ngx_parameters.update_uint("DLSSG.NumFramesActuallyPresented", state.numFramesActuallyPresented);
    g_ngx_parameters.update_uint("DLSSG.MinWidthOrHeight", state.minWidthOrHeight);

    const bool fg_operational = (state.status == sl::DLSSGStatus::eOk);
    g_streamline_dlssg_fg_enabled.store(fg_operational);
    if (fg_operational) {
        g_dlssg_was_active_once.store(true);
    }
    g_ngx_parameters.update_int("DLSSG.EnableInterp", fg_operational ? 1 : 0);

    // Keep existing key updated when caller provides options (common in real titles).
    // Streamline does not expose mode/frames-to-generate in DLSSGState.
    if (options != nullptr) {
        g_ngx_parameters.update_uint("DLSSG.MultiFrameCount", options->numFramesToGenerate);
        g_ngx_parameters.update_int("DLSSG.Mode", static_cast<int>(options->mode));
    }

    static std::atomic<uint32_t> s_last_status{UINT32_MAX};
    static std::atomic<int> s_last_mode{-1};
    static std::atomic<uint32_t> s_last_num_frames{UINT32_MAX};
    const int mode_val = (options != nullptr) ? static_cast<int>(options->mode) : -1;
    const uint32_t num_frames = (options != nullptr) ? static_cast<uint32_t>(options->numFramesToGenerate) : 0;
    if (s_last_status.load(std::memory_order_relaxed) != status_val
        || s_last_mode.load(std::memory_order_relaxed) != mode_val
        || s_last_num_frames.load(std::memory_order_relaxed) != num_frames) {
        s_last_status.store(status_val, std::memory_order_relaxed);
        s_last_mode.store(mode_val, std::memory_order_relaxed);
        s_last_num_frames.store(num_frames, std::memory_order_relaxed);
        LogInfo("UpdateNGXParamsFromDLSSGState: status=0x%X mode=%d numFramesToGenerate=%u", status_val, mode_val,
                num_frames);
    }
}

// Hook functions
sl::Result slInit_Detour(const sl::Preferences& pref, uint64_t sdkVersion) {
    (void)pref;
    CALL_GUARD_NO_TS();

    // Store the SDK version
    g_last_sdk_version.store(sdkVersion);

    // Log the call
    LogInfo("slInit called (SDK Version: %llu)", sdkVersion);

    // Call original function
    if (slInit_Original != nullptr) {
        return slInit_Original(pref, sdkVersion);
    }

    return sl::Result::eErrorNotInitialized;
}

sl::Result slUpgradeInterface_Detour(void** baseInterface) {
    CALL_GUARD_NO_TS();

    g_sl_upgrade_interface_call_count.fetch_add(1, std::memory_order_relaxed);

    if (slUpgradeInterface_Original == nullptr) {
        return sl::Result::eErrorNotInitialized;
    }

    const sl::Result result = slUpgradeInterface_Original(baseInterface);
    CountSlUpgradeInterfaceUpgradedInterface(result, baseInterface);
    /*
    if (result == sl::Result::eOk && baseInterface != nullptr && *baseInterface != nullptr) {
        IUnknown* const u = static_cast<IUnknown*>(*baseInterface);
        Microsoft::WRL::ComPtr<IDXGIFactory> dxgi_factory;
        if (SUCCEEDED(u->QueryInterface(IID_PPV_ARGS(&dxgi_factory))) && dxgi_factory != nullptr) {
            (void)display_commander::features::streamline::HookStreamlineProxyFactory(u);
        } else {
            Microsoft::WRL::ComPtr<IDXGISwapChain> dxgi_swapchain;
            if (SUCCEEDED(u->QueryInterface(IID_PPV_ARGS(&dxgi_swapchain))) && dxgi_swapchain != nullptr) {
                (void)display_commander::features::streamline::HookStreamlineProxySwapchain(dxgi_swapchain.Get());
            }
        }
    }*/
    return result;
}

sl::Result slIsFeatureSupported_Detour(sl::Feature feature, const sl::AdapterInfo& adapterInfo) {
    CALL_GUARD_NO_TS();

    static int log_count = 0;
    if (log_count < 30) {
        // Log the call
        LogInfo("slIsFeatureSupported called (Feature: %d)", static_cast<int>(feature));
        log_count++;
    }

    // Call original function
    if (slIsFeatureSupported_Original != nullptr) {
        return slIsFeatureSupported_Original(feature, adapterInfo);
    }

    return sl::Result::eErrorNotInitialized;
}

sl::Result slGetNativeInterface_Detour(void* proxyInterface, void** baseInterface) {
    CALL_GUARD_NO_TS();

    // Log the call
    LogInfo("slGetNativeInterface called");

    // Call original function
    if (slGetNativeInterface_Original != nullptr) {
        return slGetNativeInterface_Original(proxyInterface, baseInterface);
    }

    return sl::Result::eErrorNotInitialized;
}

// slDLSSGetOptimalSettings detour: observe calls, apply same quality/preset overrides as slDLSSSetOptions, then call
// original
static sl::Result slDLSSGetOptimalSettings_Detour(const sl::DLSSOptions& options, sl::DLSSOptimalSettings& settings) {
    static bool first_call = true;
    CALL_GUARD_NO_TS();

    bool optionsLogged = false;
    if (first_call) {
        optionsLogged = LogDLSSOptions(options);
    }

    if (slDLSSGetOptimalSettings_Original == nullptr) {
        return sl::Result::eErrorInvalidParameter;
    }

    sl::DLSSOptions modified_options = options;

    const NVSDK_NGX_PerfQuality_Value qualityVal =
        GetDLSSQualityPresetValue(settings::g_swapchainTabSettings.dlss_quality_preset_override.GetValue());
    if (static_cast<int>(qualityVal) >= 0) {
        modified_options.mode = QualityPresetValueToSLMode(qualityVal);
    }
    if (settings::g_swapchainTabSettings.dlss_preset_override_enabled.GetValue()) {
        const int presetVal = GetDLSSPresetValue(settings::g_swapchainTabSettings.dlss_sr_preset_override.GetValue());
        if (presetVal > 0) {
            modified_options.dlaaPreset = static_cast<sl::DLSSPreset>(presetVal);
            modified_options.qualityPreset = static_cast<sl::DLSSPreset>(presetVal);
            modified_options.balancedPreset = static_cast<sl::DLSSPreset>(presetVal);
            modified_options.performancePreset = static_cast<sl::DLSSPreset>(presetVal);
            modified_options.ultraPerformancePreset = static_cast<sl::DLSSPreset>(presetVal);
            modified_options.ultraQualityPreset = static_cast<sl::DLSSPreset>(presetVal);
        }
    }

    const sl::Result result = slDLSSGetOptimalSettings_Original(modified_options, settings);

    // Update g_ngx_parameters so DLSS Information tab shows values for Vulkan/Streamline (no NGX path)
    UpdateNGXParamsFromDLSSOptions(modified_options);
    UpdateNGXParamsFromDLSSOptimalSettings(settings);

    if (optionsLogged && first_call) {
        LogInfo(
            "slDLSSGetOptimalSettings result=%d -> optimalRender=%ux%u sharpness=%.2f renderMin=%ux%u renderMax=%ux%u",
            static_cast<int>(result), settings.optimalRenderWidth, settings.optimalRenderHeight, settings.optimalSharpness,
            settings.renderWidthMin, settings.renderHeightMin, settings.renderWidthMax, settings.renderHeightMax);
    }
    if (first_call) {
        first_call = false;
        LogInfo("Streamline: slDLSSGetOptimalSettings first call");
    }
    return result;
}

// slDLSSSetOptions detour: log arguments and apply main-tab DLSS overrides (quality preset, render preset,
// auto-exposure)
static sl::Result slDLSSSetOptions_Detour(const sl::ViewportHandle& viewport, const sl::DLSSOptions& options) {
    static bool first_call = true;
    if (slDLSSSetOptions_Original == nullptr) {
        return sl::Result::eErrorInvalidParameter;
    }
    uint32_t viewportId = static_cast<uint32_t>(viewport);
    if (LogDLSSOptions(options) && first_call) {
        LogInfo("slDLSSSetOptions called viewport=%u", viewportId);
    }

    sl::DLSSOptions modified_options = options;
    bool applied_any = false;

    // Quality preset override (Performance / Balanced / Quality / Ultra Performance / Ultra Quality / DLAA)
    const NVSDK_NGX_PerfQuality_Value qualityVal =
        GetDLSSQualityPresetValue(settings::g_swapchainTabSettings.dlss_quality_preset_override.GetValue());
    if (static_cast<int>(qualityVal) >= 0) {
        modified_options.mode = QualityPresetValueToSLMode(qualityVal);
        applied_any = true;
    }

    // Render preset override (DLSS Default / Preset A, B, C, ...) – apply to all per-mode presets
    if (settings::g_swapchainTabSettings.dlss_preset_override_enabled.GetValue()) {
        const int presetVal = GetDLSSPresetValue(settings::g_swapchainTabSettings.dlss_sr_preset_override.GetValue());
        if (presetVal > 0) {
            modified_options.dlaaPreset = static_cast<sl::DLSSPreset>(presetVal);
            modified_options.qualityPreset = static_cast<sl::DLSSPreset>(presetVal);
            modified_options.balancedPreset = static_cast<sl::DLSSPreset>(presetVal);
            modified_options.performancePreset = static_cast<sl::DLSSPreset>(presetVal);
            modified_options.ultraPerformancePreset = static_cast<sl::DLSSPreset>(presetVal);
            modified_options.ultraQualityPreset = static_cast<sl::DLSSPreset>(presetVal);
            applied_any = true;

            if (first_call) {
                LogInfo("slDLSSSetOptions: applied overrides -> mode=%s preset=%d", DLSSModeStr(modified_options.mode),
                        presetVal);
            }
        }
    }
    if (LogDLSSOptions(options) && first_call) {
        LogInfo("slDLSSSetOptions(overriden) called viewport=%u", viewportId);
    }

    // Auto-exposure override (Force Off / Force On)
    const std::string ae = settings::g_swapchainTabSettings.dlss_forced_auto_exposure.GetValue();
    if (ae == "Force Off") {
        modified_options.useAutoExposure = sl::Boolean::eFalse;
        applied_any = true;
    } else if (ae == "Force On") {
        modified_options.useAutoExposure = sl::Boolean::eTrue;
        applied_any = true;
    }

    if (applied_any) {
        if (first_call) {
            LogInfo("slDLSSSetOptions: applied overrides -> mode=%s", DLSSModeStr(modified_options.mode));
        }
    } else if (first_call && LogDLSSOptions(options)) {
        // Log why no overrides were applied (only when we're already logging this call)
        const std::string qPreset = settings::g_swapchainTabSettings.dlss_quality_preset_override.GetValue();
        const bool presetEnabled = settings::g_swapchainTabSettings.dlss_preset_override_enabled.GetValue();
        const std::string srPreset = settings::g_swapchainTabSettings.dlss_sr_preset_override.GetValue();
        LogInfo("slDLSSSetOptions: no overrides applied (quality_preset=%s preset_override_enabled=%d sr_preset=%s)",
                qPreset.c_str(), presetEnabled ? 1 : 0, srPreset.c_str());
    }
    if (first_call) {
        first_call = false;
    }

    // Update g_ngx_parameters so DLSS Information tab shows values for Vulkan/Streamline (no NGX path)
    UpdateNGXParamsFromDLSSOptions(modified_options);

    return slDLSSSetOptions_Original(viewport, modified_options);
}

// slDLSSGSetOptions detour: observe requested mode/frames-to-generate. "Enabled" is decided by slDLSSGGetState.
static sl::Result slDLSSGSetOptions_Detour(const sl::ViewportHandle& viewport, const sl::DLSSGOptions& options) {
    if (slDLSSGSetOptions_Original == nullptr) {
        return sl::Result::eErrorInvalidParameter;
    }
    const sl::Result result = slDLSSGSetOptions_Original(viewport, options);
    if (result == sl::Result::eOk) {
        g_ngx_parameters.update_uint("DLSSG.MultiFrameCount", options.numFramesToGenerate);
        g_ngx_parameters.update_int("DLSSG.Mode", static_cast<int>(options.mode));
    }
    return result;
}

// slSetData detour: log when plugin's slSetData is called (inputs chain + cmdBuffer)
static sl::Result slSetData_Detour(const sl::BaseStructure* inputs, sl::CommandBuffer* cmdBuffer) {
    if (inputs != nullptr) {
        const sl::StructType& t = inputs->structType;
        LogInfo(
            "slSetData called inputs=%p cmdBuffer=%p firstStruct: "
            "type=%08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X version=%u",
            inputs, cmdBuffer, t.data1, t.data2, t.data3, t.data4[0], t.data4[1], t.data4[2], t.data4[3], t.data4[4],
            t.data4[5], t.data4[6], t.data4[7], inputs->structVersion);
        const sl::BaseStructure* n = inputs->next;
        if (n != nullptr) {
            const sl::StructType& t2 = n->structType;
            LogInfo("  next: %p type=%08X-%04X-%04X version=%u", n, t2.data1, t2.data2, t2.data3, n->structVersion);
        }
    } else {
        LogInfo("slSetData called inputs=null cmdBuffer=%p", cmdBuffer);
    }
    if (slSetData_Original != nullptr) {
        return slSetData_Original(inputs, cmdBuffer);
    }
    return sl::Result::eErrorInvalidParameter;
}

// slDLSSGGetState — original via slGetFeatureFunction
static slDLSSGGetState_pfn slDLSSGGetState_Original = nullptr;
static std::atomic<bool> g_slDLSSGGetState_hook_installed{false};

// slDLSSGGetState detour: after original, update g_ngx_parameters and FG atomic from options when non-null
static sl::Result slDLSSGGetState_Detour(const sl::ViewportHandle& viewport, sl::DLSSGState& state,
                                         const sl::DLSSGOptions* options) {
    if (slDLSSGGetState_Original == nullptr) {
        return sl::Result::eErrorInvalidParameter;
    }
    const sl::Result result = slDLSSGGetState_Original(viewport, state, options);
    if (result == sl::Result::eOk) {
        // Disabled for now, revisit
      //  UpdateNGXParamsFromDLSSGOptions(state, options);
    }
    return result;
}

struct SlGetFeatureResolvedHookEntry {
    const char* export_name;
    sl::Feature expected_feature;
    LPVOID detour;
    LPVOID* original;
    std::atomic<bool>* installed;
    bool install_sl_set_data_from_plugin;
};

static void TryInstallSlSetDataHookFromPluginModule(void* address_in_plugin) {
    HMODULE pluginMod = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCWSTR>(address_in_plugin), &pluginMod)
        || pluginMod == nullptr) {
        return;
    }
    FARPROC slSetDataAddr = GetProcAddress(pluginMod, "slSetData");
    if (slSetDataAddr == nullptr || g_slSetData_hook_installed.exchange(true)) {
        return;
    }
    if (CreateAndEnableHook(slSetDataAddr, reinterpret_cast<LPVOID>(slSetData_Detour),
                            reinterpret_cast<LPVOID*>(&slSetData_Original), "slSetData")) {
        LogInfo("Installed slSetData hook from DLSS plugin");
    } else {
        g_slSetData_hook_installed.store(false);
        LogError("Failed to install slSetData hook");
    }
}

static const SlGetFeatureResolvedHookEntry kSlGetFeatureResolvedHooks[] = {
    {.export_name = "slDLSSGGetState",
     .expected_feature = sl::kFeatureDLSS_G,
     .detour = reinterpret_cast<LPVOID>(slDLSSGGetState_Detour),
     .original = reinterpret_cast<LPVOID*>(&slDLSSGGetState_Original),
     .installed = &g_slDLSSGGetState_hook_installed,
     .install_sl_set_data_from_plugin = false},
    {.export_name = "slDLSSGSetOptions",
     .expected_feature = sl::kFeatureDLSS_G,
     .detour = reinterpret_cast<LPVOID>(slDLSSGSetOptions_Detour),
     .original = reinterpret_cast<LPVOID*>(&slDLSSGSetOptions_Original),
     .installed = &g_slDLSSGSetOptions_hook_installed,
     .install_sl_set_data_from_plugin = false},
    {.export_name = "slDLSSGetOptimalSettings",
     .expected_feature = sl::kFeatureDLSS,
     .detour = reinterpret_cast<LPVOID>(slDLSSGetOptimalSettings_Detour),
     .original = reinterpret_cast<LPVOID*>(&slDLSSGetOptimalSettings_Original),
     .installed = &g_slDLSSGetOptimalSettings_hook_installed,
     .install_sl_set_data_from_plugin = false},
    {.export_name = "slDLSSSetOptions",
     .expected_feature = sl::kFeatureDLSS,
     .detour = reinterpret_cast<LPVOID>(slDLSSSetOptions_Detour),
     .original = reinterpret_cast<LPVOID*>(&slDLSSSetOptions_Original),
     .installed = &g_slDLSSSetOptions_hook_installed,
     .install_sl_set_data_from_plugin = true},
};

static void TryInstallSlGetFeatureResolvedHook(sl::Feature feature, const char* function_name, void* function) {
    if (function_name == nullptr || function == nullptr) {
        return;
    }

    for (const SlGetFeatureResolvedHookEntry& e : kSlGetFeatureResolvedHooks) {
        if (std::strcmp(function_name, e.export_name) != 0) {
            continue;
        }
        if (feature != e.expected_feature) {
            LogWarn("slGetFeatureFunction feature mismatch for %s (got=%d expected=%d)", function_name,
                    static_cast<int>(feature), static_cast<int>(e.expected_feature));
            return;
        }
        if (e.installed->exchange(true)) {
            return;
        }
        if (CreateAndEnableHook(function, e.detour, e.original, e.export_name)) {
            LogInfo("Installed %s hook", e.export_name);
            if (e.install_sl_set_data_from_plugin) {
                TryInstallSlSetDataHookFromPluginModule(function);
            }
        } else {
            e.installed->store(false);
            LogError("Failed to install %s hook", e.export_name);
        }
        return;
    }
}

// slGetFeatureFunction detour: install hooks when game requests DLSS-G/DLSS feature functions
static sl::Result slGetFeatureFunction_Detour(sl::Feature feature, const char* functionName, void*& function) {
    if (slGetFeatureFunction_Original == nullptr) {
        return sl::Result::eErrorNotInitialized;
    }
    const sl::Result result = slGetFeatureFunction_Original(feature, functionName, function);
    if (result != sl::Result::eOk || function == nullptr) {
        return result;
    }
    TryInstallSlGetFeatureResolvedHook(feature, functionName, function);
    return result;
}

bool InstallStreamlineHooks(HMODULE streamline_module) { // sl.interposer.dll
    if (streamline_module == nullptr) {
        LogError("Streamline not detected - sl.interposer.dll not loaded");
        return false;
    }
    // Check if Streamline hooks should be suppressed
    if (display_commanderhooks::HookSuppressionManager::GetInstance().ShouldSuppressHook(
            display_commanderhooks::HookType::STREAMLINE)) {
        LogInfo("Streamline hooks installation suppressed by user setting");
        return false;
    }


    static bool g_streamline_hooks_installed = false;
    if (g_streamline_hooks_installed) {
        LogInfo("Streamline hooks already installed");
        return true;
    }
    display_commanderhooks::HookSuppressionManager::GetInstance().MarkHookInstalled(
        display_commanderhooks::HookType::STREAMLINE);

    LogInfo("Installing Streamline hooks...");

    for (const auto& entry : kStreamlineLoaderHooks) {
        FARPROC target = GetProcAddress(streamline_module, entry.name);
        if (target == nullptr) {
            LogError("Streamline: %s not exported by sl.interposer.dll", entry.name);
           // RollbackStreamlineLoaderHooks();
           // return false;
        }
        if (!CreateAndEnableHook(reinterpret_cast<LPVOID>(target), entry.detour, entry.original, entry.name)) {
            LogError("Failed to create and enable %s hook", entry.name);
           // RollbackStreamlineLoaderHooks();
           // return false;
        }
    }

    if (slGetFeatureFunction_Original != nullptr) {
        for (const SlGetFeatureResolvedHookEntry& e : kSlGetFeatureResolvedHooks) {
            void* resolved_function = nullptr;
            const sl::Result resolve_result =
                slGetFeatureFunction_Original(e.expected_feature, e.export_name, resolved_function);
            if (resolve_result != sl::Result::eOk || resolved_function == nullptr) {
                continue;
            }
            TryInstallSlGetFeatureResolvedHook(e.expected_feature, e.export_name, resolved_function);
        }
    }

    g_streamline_hooks_installed = true;
    LogInfo("Streamline hooks installed successfully");

    // Mark Streamline hooks as installed

    return true;
}

// Get last SDK version from slInit calls
uint64_t GetLastStreamlineSDKVersion() { return g_last_sdk_version.load(); }

uint64_t GetSlUpgradeInterfaceCallCount() {
    return g_sl_upgrade_interface_call_count.load(std::memory_order_relaxed);
}

uint64_t GetSlUpgradeInterfaceClassCountFactory() {
    return g_sl_upgrade_qi_factory.load(std::memory_order_relaxed);
}

uint64_t GetSlUpgradeInterfaceClassCountSwapChain() {
    return g_sl_upgrade_qi_swapchain.load(std::memory_order_relaxed);
}

uint64_t GetSlUpgradeInterfaceClassCountD3D11Device() {
    return g_sl_upgrade_qi_d3d11.load(std::memory_order_relaxed);
}

uint64_t GetSlUpgradeInterfaceClassCountD3D12Device() {
    return g_sl_upgrade_qi_d3d12.load(std::memory_order_relaxed);
}

uint64_t GetSlUpgradeInterfaceClassCountUnknown() {
    return g_sl_upgrade_qi_unknown.load(std::memory_order_relaxed);
}

uint64_t GetSlUpgradeInterfaceClassifyNonOkCount() {
    return g_sl_upgrade_classify_non_ok.load(std::memory_order_relaxed);
}

uint64_t GetSlUpgradeInterfaceClassifyNullIfaceCount() {
    return g_sl_upgrade_classify_null_iface.load(std::memory_order_relaxed);
}
