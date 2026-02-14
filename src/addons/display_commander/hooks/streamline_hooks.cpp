#include "streamline_hooks.hpp"
#include "../config/display_commander_config.hpp"
#include "../globals.hpp"
#include "../settings/advanced_tab_settings.hpp"
#include "../settings/main_tab_settings.hpp"
#include "../utils/detour_call_tracker.hpp"
#include "../utils/general_utils.hpp"
#include "../utils/logging.hpp"
#include "../utils/timing.hpp"
#include "dxgi_factory_wrapper.hpp"
#include "hook_suppression_manager.hpp"

#include <dxgi.h>
#include <dxgi1_6.h>
#include <MinHook.h>
#include <cstdint>
#include <cstring>

// Streamline base (sl_dlss.h requires sl.h for Boolean, SL_STRUCT, etc.)
#include "sl.h"
#include "sl_consts.h"
#include "sl_core_types.h"
// Streamline DLSS types (from sl_dlss.h)
#include "sl_dlss.h"
// Streamline DLSS-G types (from sl_dlss_g.h)
#include "sl_dlss_g.h"

// Streamline function pointers
using slInit_pfn = int (*)(void* pref, uint64_t sdkVersion);
using slIsFeatureSupported_pfn = int (*)(int feature, const void* adapterInfo);
using slGetNativeInterface_pfn = int (*)(void* proxyInterface, void** baseInterface);
using slUpgradeInterface_pfn = int (*)(void** baseInterface);
using slGetFeatureFunction_pfn = int (*)(int feature, const char* functionName, void*& function);

static slInit_pfn slInit_Original = nullptr;
static slIsFeatureSupported_pfn slIsFeatureSupported_Original = nullptr;
static slGetNativeInterface_pfn slGetNativeInterface_Original = nullptr;
static slUpgradeInterface_pfn slUpgradeInterface_Original = nullptr;
static slGetFeatureFunction_pfn slGetFeatureFunction_Original = nullptr;

// slDLSSGSetOptions: Result(viewport, options) - hooked when game requests it via slGetFeatureFunction
using slDLSSGSetOptions_pfn = int (*)(const sl::ViewportHandle& viewport, const sl::DLSSGOptions& options);
static slDLSSGSetOptions_pfn slDLSSGSetOptions_Original = nullptr;
static std::atomic<bool> g_slDLSSGSetOptions_hook_installed{false};

// slDLSSGetOptimalSettings: Result(options, settings) - hooked when game requests it via slGetFeatureFunction
using slDLSSGetOptimalSettings_pfn = int (*)(const sl::DLSSOptions& options, sl::DLSSOptimalSettings& settings);
static slDLSSGetOptimalSettings_pfn slDLSSGetOptimalSettings_Original = nullptr;
static std::atomic<bool> g_slDLSSGetOptimalSettings_hook_installed{false};

// Track SDK version from slInit calls
static std::atomic<uint64_t> g_last_sdk_version{0};

// Config-driven prevent slUpgradeInterface flag
static std::atomic<bool> g_prevent_slupgrade_interface{false};

// Hook functions
int slInit_Detour(void* pref, uint64_t sdkVersion) {
    RECORD_DETOUR_CALL(utils::get_now_ns());
    // Increment counter
    g_streamline_event_counters[STREAMLINE_EVENT_SL_INIT].fetch_add(1);
    g_swapchain_event_total_count.fetch_add(1);

    // Store the SDK version
    g_last_sdk_version.store(sdkVersion);

    // Log the call
    LogInfo("slInit called (SDK Version: %llu)", sdkVersion);

    // Call original function
    if (slInit_Original != nullptr) {
        return slInit_Original(pref, sdkVersion);
    }

    return -1;  // Error if original not available
}

int slIsFeatureSupported_Detour(int feature, const void* adapterInfo) {
    RECORD_DETOUR_CALL(utils::get_now_ns());
    // Increment counter
    g_streamline_event_counters[STREAMLINE_EVENT_SL_IS_FEATURE_SUPPORTED].fetch_add(1);
    g_swapchain_event_total_count.fetch_add(1);

    static int log_count = 0;
    if (log_count < 30) {
        // Log the call
        LogInfo("slIsFeatureSupported called (Feature: %d)", feature);
        log_count++;
    }

    // Call original function
    if (slIsFeatureSupported_Original != nullptr) {
        return slIsFeatureSupported_Original(feature, adapterInfo);
    }

    return -1;  // Error if original not available
}

int slGetNativeInterface_Detour(void* proxyInterface, void** baseInterface) {
    RECORD_DETOUR_CALL(utils::get_now_ns());
    // Increment counter
    g_streamline_event_counters[STREAMLINE_EVENT_SL_GET_NATIVE_INTERFACE].fetch_add(1);
    g_swapchain_event_total_count.fetch_add(1);

    // Log the call
    LogInfo("slGetNativeInterface called");

    // Call original function
    if (slGetNativeInterface_Original != nullptr) {
        return slGetNativeInterface_Original(proxyInterface, baseInterface);
    }

    return -1;  // Error if original not available
}

// slDLSSGetOptimalSettings detour: observe calls, increment counter, optionally log
static int slDLSSGetOptimalSettings_Detour(const sl::DLSSOptions& options, sl::DLSSOptimalSettings& settings) {
    RECORD_DETOUR_CALL(utils::get_now_ns());
    g_streamline_event_counters[STREAMLINE_EVENT_SL_DLSS_GET_OPTIMAL_SETTINGS].fetch_add(1);
    g_swapchain_event_total_count.fetch_add(1);

    if (slDLSSGetOptimalSettings_Original == nullptr) {
        return static_cast<int>(sl::Result::eErrorInvalidParameter);
    }
    int result = slDLSSGetOptimalSettings_Original(options, settings);
    return result;
}

// slDLSSGSetOptions detour: when force_fg_auto is enabled, override options.mode to eAuto
static int slDLSSGSetOptions_Detour(const sl::ViewportHandle& viewport, const sl::DLSSGOptions& options) {
    if (slDLSSGSetOptions_Original == nullptr) {
        return static_cast<int>(sl::Result::eErrorInvalidParameter);
    }
    sl::DLSSGOptions modified_options = options;
    if (settings::g_mainTabSettings.force_fg_auto.GetValue()) {
        modified_options.mode = sl::DLSSGMode::eAuto;
        if (modified_options.numFramesToGenerate == 0) {
            modified_options.numFramesToGenerate = 1;
        }
    }
    return slDLSSGSetOptions_Original(viewport, modified_options);
}

// slGetFeatureFunction detour: when game requests slDLSSGSetOptions, hook it for force_fg_auto
static int slGetFeatureFunction_Detour(int feature, const char* functionName, void*& function) {
    if (slGetFeatureFunction_Original == nullptr) {
        return -1;
    }
    int result = slGetFeatureFunction_Original(feature, functionName, function);
    if (result != static_cast<int>(sl::Result::eOk) || function == nullptr) {
        return result;
    }
    // Install slDLSSGSetOptions hook on first successful lookup
    if (functionName != nullptr && std::strcmp(functionName, "slDLSSGSetOptions") == 0
        && !g_slDLSSGSetOptions_hook_installed.exchange(true)) {
        if (CreateAndEnableHook(function, reinterpret_cast<LPVOID>(slDLSSGSetOptions_Detour),
                                reinterpret_cast<LPVOID*>(&slDLSSGSetOptions_Original), "slDLSSGSetOptions")) {
            LogInfo("Installed slDLSSGSetOptions hook for force_fg_auto support");
        } else {
            g_slDLSSGSetOptions_hook_installed.store(false);
            LogError("Failed to install slDLSSGSetOptions hook");
        }
    }
    // Install slDLSSGetOptimalSettings hook on first successful lookup
    if (functionName != nullptr && std::strcmp(functionName, "slDLSSGetOptimalSettings") == 0
        && !g_slDLSSGetOptimalSettings_hook_installed.exchange(true)) {
        if (CreateAndEnableHook(function, reinterpret_cast<LPVOID>(slDLSSGetOptimalSettings_Detour),
                                reinterpret_cast<LPVOID*>(&slDLSSGetOptimalSettings_Original),
                                "slDLSSGetOptimalSettings")) {
            LogInfo("Installed slDLSSGetOptimalSettings hook");
        } else {
            g_slDLSSGetOptimalSettings_hook_installed.store(false);
            LogError("Failed to install slDLSSGetOptimalSettings hook");
        }
    }
    return result;
}

// Reference:
// https://github.com/NVIDIA-RTX/Streamline/blob/b998246a3d499c08765c5681b229c9e6b4513348/source/core/sl.api/sl.cpp#L625
int slUpgradeInterface_Detour(void** baseInterface) {
    RECORD_DETOUR_CALL(utils::get_now_ns());
    // Increment counter
    g_streamline_event_counters[STREAMLINE_EVENT_SL_UPGRADE_INTERFACE].fetch_add(1);
    g_swapchain_event_total_count.fetch_add(1);

    // Check config-driven flag
    bool prevent_slupgrade_interface = g_prevent_slupgrade_interface.load();
    LogInfo("prevent_slupgrade_interface: %d", static_cast<int>(prevent_slupgrade_interface));

    if (slUpgradeInterface_Original == nullptr) {
        return -1;  // Error if original not available
    }
    auto result = slUpgradeInterface_Original(baseInterface);
    auto* unknown = static_cast<IUnknown*>(*baseInterface);

    Microsoft::WRL::ComPtr<IDXGIFactory> dxgi_factory;
    Microsoft::WRL::ComPtr<IDXGIFactory7> dxgi_factory7;
    Microsoft::WRL::ComPtr<IDXGISwapChain> dxgi_swapchain;
    if (SUCCEEDED(unknown->QueryInterface(IID_PPV_ARGS(&dxgi_factory7))) && dxgi_factory7 != nullptr) {
        LogInfo("[slUpgradeInterface] Found IDXGIFactory7 interface");

        // auto* factory_wrapper = dxgi_factory7.Get();
        // new display_commanderhooks::DXGIFactoryWrapper(dxgi_factory7.Get(),
        // display_commanderhooks::SwapChainHook::Proxy);
        //..factory_wrapper->SetSLGetNativeInterface(slGetNativeInterface_Original);
        //..factory_wrapper->SetSLUpgradeInterface(slUpgradeInterface_Original);

        // AddRef the factory so wrapper can take ownership
        dxgi_factory7->AddRef();

        LogInfo("[slUpgradeInterface] Found IDXGIFactory7 interface");
        // Create wrapper to ensure it doesn't pass active queue for swapchain creation
        auto* factory_wrapper2 = new display_commanderhooks::DXGIFactoryWrapper(
            dxgi_factory7.Get(), display_commanderhooks::SwapChainHook::Native);

        // Release the original factory reference
        unknown->Release();

        factory_wrapper2->SetSLGetNativeInterface(slGetNativeInterface_Original);
        factory_wrapper2->SetSLUpgradeInterface(slUpgradeInterface_Original);
        // TODO(user): Set command queue map when available

        *baseInterface = factory_wrapper2;

        // ComPtr will automatically release when it goes out of scope
    } else if (SUCCEEDED(unknown->QueryInterface(IID_PPV_ARGS(&dxgi_factory))) && dxgi_factory != nullptr) {
        LogError("[slUpgradeInterface] Found IDXGIFactory interface not hooked TODO");
    } else if (SUCCEEDED(unknown->QueryInterface(IID_PPV_ARGS(&dxgi_swapchain))) && dxgi_swapchain != nullptr) {
        LogError("[slUpgradeInterface] IDXGISwapChain interface not hooked TODO");
    } else {
        LogError("[slUpgradeInterface] Unknown interface not hooked TODO");
    }
    return result;
}

// Initialize config-driven prevent_slupgrade_interface flag
void InitializePreventSLUpgradeInterface() {
    bool prevent_slupgrade_interface = false;

    if (display_commander::config::get_config_value("DisplayCommander.Safemode", "PreventSLUpgradeInterface",
                                                    prevent_slupgrade_interface)) {
        g_prevent_slupgrade_interface.store(prevent_slupgrade_interface);
        LogInfo("Loaded PreventSLUpgradeInterface from config: %s",
                prevent_slupgrade_interface ? "enabled" : "disabled");
    } else {
        // Default to false if not found in config
        g_prevent_slupgrade_interface.store(false);
        display_commander::config::set_config_value("DisplayCommander.Safemode", "PreventSLUpgradeInterface",
                                                    prevent_slupgrade_interface);
        LogInfo("PreventSLUpgradeInterface not found in config, using default: disabled");
    }
}

bool InstallStreamlineHooks(HMODULE streamline_module) {
    // Check if Streamline hooks should be suppressed
    if (display_commanderhooks::HookSuppressionManager::GetInstance().ShouldSuppressHook(
            display_commanderhooks::HookType::STREAMLINE)) {
        LogInfo("Streamline hooks installation suppressed by user setting");
        return false;
    }

    // Check if Streamline DLLs are loaded
    HMODULE sl_interposer = streamline_module;
    if (sl_interposer == nullptr) {
        sl_interposer = GetModuleHandleW(L"sl.interposer.dll");
        if (sl_interposer == nullptr) {
            LogInfo("Streamline not detected - sl.interposer.dll not loaded");
            return false;
        }
    }

    static bool g_streamline_hooks_installed = false;
    if (g_streamline_hooks_installed) {
        LogInfo("Streamline hooks already installed");
        return true;
    }
    g_streamline_hooks_installed = true;

    // Initialize prevent_slupgrade_interface from config
    InitializePreventSLUpgradeInterface();

    LogInfo("Installing Streamline hooks...");

    // Hook slInit
    if (!CreateAndEnableHook(GetProcAddress(sl_interposer, "slInit"), reinterpret_cast<LPVOID>(slInit_Detour),
                             reinterpret_cast<LPVOID*>(&slInit_Original), "slInit")) {
        LogError("Failed to create and enable slInit hook");
        //  return false;
    }

    /*
    // Hook slUpgradeInterface
    if (!CreateAndEnableHook(GetProcAddress(sl_interposer, "slUpgradeInterface"),
                             reinterpret_cast<LPVOID>(slUpgradeInterface_Detour),
                             reinterpret_cast<LPVOID*>(&slUpgradeInterface_Original), "slUpgradeInterface")) {
        LogError("Failed to create and enable slUpgradeInterface hook");
     //   return false;
    }*/

    // Hook slIsFeatureSupported
    if (!CreateAndEnableHook(GetProcAddress(sl_interposer, "slIsFeatureSupported"),
                             reinterpret_cast<LPVOID>(slIsFeatureSupported_Detour),
                             reinterpret_cast<LPVOID*>(&slIsFeatureSupported_Original), "slIsFeatureSupported")) {
        LogError("Failed to create and enable slIsFeatureSupported hook");
        //     return false;
    }

    // Hook slGetNativeInterface
    if (!CreateAndEnableHook(GetProcAddress(sl_interposer, "slGetNativeInterface"),
                             reinterpret_cast<LPVOID>(slGetNativeInterface_Detour),
                             reinterpret_cast<LPVOID*>(&slGetNativeInterface_Original), "slGetNativeInterface")) {
        LogError("Failed to create and enable slGetNativeInterface hook");
    }

    // Hook slGetFeatureFunction to intercept slDLSSGSetOptions for force_fg_auto
    if (!CreateAndEnableHook(GetProcAddress(sl_interposer, "slGetFeatureFunction"),
                             reinterpret_cast<LPVOID>(slGetFeatureFunction_Detour),
                             reinterpret_cast<LPVOID*>(&slGetFeatureFunction_Original), "slGetFeatureFunction")) {
        LogError("Failed to create and enable slGetFeatureFunction hook");
    }

    LogInfo("Streamline hooks installed successfully");

    // Mark Streamline hooks as installed
    display_commanderhooks::HookSuppressionManager::GetInstance().MarkHookInstalled(
        display_commanderhooks::HookType::STREAMLINE);

    return true;
}

// Get last SDK version from slInit calls
uint64_t GetLastStreamlineSDKVersion() { return g_last_sdk_version.load(); }
