#include "streamline_hooks.hpp"
#include "hook_suppression_manager.hpp"
#include "dxgi_factory_wrapper.hpp"
#include "../settings/developer_tab_settings.hpp"
#include "../globals.hpp"
#include "../utils/general_utils.hpp"
#include "../utils/logging.hpp"
#include "../config/display_commander_config.hpp"

#include <MinHook.h>
#include <dxgi.h>
#include <dxgi1_6.h>
#include <cstdint>

// Streamline function pointers
using slInit_pfn = int (*)(void* pref, uint64_t sdkVersion);
using slIsFeatureSupported_pfn = int (*)(int feature, const void* adapterInfo);
using slGetNativeInterface_pfn = int (*)(void* proxyInterface, void** baseInterface);
using slUpgradeInterface_pfn = int (*)(void** baseInterface);

static slInit_pfn slInit_Original = nullptr;
static slIsFeatureSupported_pfn slIsFeatureSupported_Original = nullptr;
static slGetNativeInterface_pfn slGetNativeInterface_Original = nullptr;
static slUpgradeInterface_pfn slUpgradeInterface_Original = nullptr;

// Track SDK version from slInit calls
static std::atomic<uint64_t> g_last_sdk_version{0};

// Config-driven prevent slUpgradeInterface flag
static std::atomic<bool> g_prevent_slupgrade_interface{false};

// Hook functions
int slInit_Detour(void* pref, uint64_t sdkVersion) {
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

    return -1; // Error if original not available
}

int slIsFeatureSupported_Detour(int feature, const void* adapterInfo) {
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

    return -1; // Error if original not available
}

int slGetNativeInterface_Detour(void* proxyInterface, void** baseInterface) {
    // Increment counter
    g_streamline_event_counters[STREAMLINE_EVENT_SL_GET_NATIVE_INTERFACE].fetch_add(1);
    g_swapchain_event_total_count.fetch_add(1);

    // Log the call
    LogInfo("slGetNativeInterface called");

    // Call original function
    if (slGetNativeInterface_Original != nullptr) {
        return slGetNativeInterface_Original(proxyInterface, baseInterface);
    }

    return -1; // Error if original not available
}


// Reference: https://github.com/NVIDIA-RTX/Streamline/blob/b998246a3d499c08765c5681b229c9e6b4513348/source/core/sl.api/sl.cpp#L625
int slUpgradeInterface_Detour(void** baseInterface) {
    // Increment counter
    g_streamline_event_counters[STREAMLINE_EVENT_SL_UPGRADE_INTERFACE].fetch_add(1);
    g_swapchain_event_total_count.fetch_add(1);

    // Check config-driven flag
    bool prevent_slupgrade_interface = g_prevent_slupgrade_interface.load();
    LogInfo("prevent_slupgrade_interface: %d", static_cast<int>(prevent_slupgrade_interface));

    auto* unknown = static_cast<IUnknown*>(*baseInterface);

    if (slUpgradeInterface_Original == nullptr) {
        return -1; // Error if original not available
    }

    Microsoft::WRL::ComPtr<IDXGIFactory7> dxgi_factory7;
    if (SUCCEEDED(unknown->QueryInterface(IID_PPV_ARGS(&dxgi_factory7))) && dxgi_factory7 != nullptr) {
        LogInfo("[slUpgradeInterface] Found IDXGIFactory7 interface");

        auto* factory_wrapper = new display_commanderhooks::DXGIFactoryWrapper(dxgi_factory7.Get(), display_commanderhooks::SwapChainHook::Proxy);
        factory_wrapper->SetSLGetNativeInterface(slGetNativeInterface_Original);
        factory_wrapper->SetSLUpgradeInterface(slUpgradeInterface_Original);

        slUpgradeInterface_Original(reinterpret_cast<void**>(&factory_wrapper));
        if (dxgi_factory7 == nullptr) {
          LogError("slUpgradeInterface() - dxgi_factory7 became null after slUpgradeInterface_Original call");
          HRESULT hr = slUpgradeInterface_Original(baseInterface);
          return hr;
        }

        // Create wrapper to ensure it doesn't pass active queue for swapchain creation
        auto* factory_wrapper2 = new display_commanderhooks::DXGIFactoryWrapper(factory_wrapper, display_commanderhooks::SwapChainHook::Native);
        factory_wrapper2->SetSLGetNativeInterface(slGetNativeInterface_Original);
        factory_wrapper2->SetSLUpgradeInterface(slUpgradeInterface_Original);
        // TODO(user): Set command queue map when available

        *baseInterface = factory_wrapper2;

        // ComPtr will automatically release when it goes out of scope
        return 0;
    }

    return slUpgradeInterface_Original(baseInterface);
}

// Initialize config-driven prevent_slupgrade_interface flag
void InitializePreventSLUpgradeInterface() {
    bool prevent_slupgrade_interface = false;

    if (display_commander::config::get_config_value("DisplayCommander.Safemode", "PreventSLUpgradeInterface", prevent_slupgrade_interface)) {
        g_prevent_slupgrade_interface.store(prevent_slupgrade_interface);
        LogInfo("Loaded PreventSLUpgradeInterface from config: %s", prevent_slupgrade_interface ? "enabled" : "disabled");
    } else {
        // Default to false if not found in config
        g_prevent_slupgrade_interface.store(false);
        display_commander::config::set_config_value("DisplayCommander.Safemode", "PreventSLUpgradeInterface", prevent_slupgrade_interface);
        LogInfo("PreventSLUpgradeInterface not found in config, using default: disabled");
    }
}

bool InstallStreamlineHooks(HMODULE streamline_module) {
    if (!settings::g_developerTabSettings.load_streamline.GetValue()) {
        LogInfo("Streamline hooks not installed - load_streamline is disabled");
        return false;
    }

    // Check if Streamline hooks should be suppressed
    if (display_commanderhooks::HookSuppressionManager::GetInstance().ShouldSuppressHook(display_commanderhooks::HookType::STREAMLINE)) {
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
    if (!CreateAndEnableHook(GetProcAddress(sl_interposer, "slInit"),
                             reinterpret_cast<LPVOID>(slInit_Detour),
                             reinterpret_cast<LPVOID*>(&slInit_Original), "slInit")) {
        LogError("Failed to create and enable slInit hook");
      //  return false;
    }

    // Hook slUpgradeInterface
    if (!CreateAndEnableHook(GetProcAddress(sl_interposer, "slUpgradeInterface"),
                             reinterpret_cast<LPVOID>(slUpgradeInterface_Detour),
                             reinterpret_cast<LPVOID*>(&slUpgradeInterface_Original), "slUpgradeInterface")) {
        LogError("Failed to create and enable slUpgradeInterface hook");
     //   return false;
    }

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
    //    return false;
    }


    LogInfo("Streamline hooks installed successfully");

    // Mark Streamline hooks as installed
    display_commanderhooks::HookSuppressionManager::GetInstance().MarkHookInstalled(display_commanderhooks::HookType::STREAMLINE);

    return true;
}


// Get last SDK version from slInit calls
uint64_t GetLastStreamlineSDKVersion() {
    return g_last_sdk_version.load();
}
