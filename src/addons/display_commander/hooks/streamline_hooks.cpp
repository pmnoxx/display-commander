#include "streamline_hooks.hpp"
#include "../globals.hpp"
#include "../utils.hpp"

#include <MinHook.h>
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

// Hook functions
int slInit_Detour(void* pref, uint64_t sdkVersion) {
    // Increment counter
    g_swapchain_event_counters[SWAPCHAIN_EVENT_STREAMLINE_SL_INIT].fetch_add(1);
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
    g_swapchain_event_counters[SWAPCHAIN_EVENT_STREAMLINE_SL_IS_FEATURE_SUPPORTED].fetch_add(1);
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
    g_swapchain_event_counters[SWAPCHAIN_EVENT_STREAMLINE_SL_GET_NATIVE_INTERFACE].fetch_add(1);
    g_swapchain_event_total_count.fetch_add(1);

    // Log the call
    LogInfo("slGetNativeInterface called");

    // Call original function
    if (slGetNativeInterface_Original != nullptr) {
        return slGetNativeInterface_Original(proxyInterface, baseInterface);
    }

    return -1; // Error if original not available
}

int slUpgradeInterface_Detour(void** baseInterface) {
    // Increment counter
    g_swapchain_event_counters[SWAPCHAIN_EVENT_STREAMLINE_SL_UPGRADE_INTERFACE].fetch_add(1);
    g_swapchain_event_total_count.fetch_add(1);

    // Log the call
    LogInfo("slUpgradeInterface called");

    // Call original function
    if (slUpgradeInterface_Original != nullptr) {
        return slUpgradeInterface_Original(baseInterface);
    }

    return -1; // Error if original not available
}

bool InstallStreamlineHooks() {
    // Check if Streamline DLLs are loaded
    HMODULE sl_interposer = GetModuleHandleW(L"sl.interposer.dll");
    if (sl_interposer == nullptr) {
        LogInfo("Streamline not detected - sl.interposer.dll not loaded");
        return false;
    }

    static bool g_streamline_hooks_installed = false;
    if (g_streamline_hooks_installed) {
        LogInfo("Streamline hooks already installed");
        return true;
    }
    g_streamline_hooks_installed = true;

    LogInfo("Installing Streamline hooks...");

    // Hook slInit
    if (!CreateAndEnableHook(GetProcAddress(sl_interposer, "slInit"),
                             reinterpret_cast<LPVOID>(slInit_Detour),
                             reinterpret_cast<LPVOID*>(&slInit_Original), "slInit")) {
        LogError("Failed to create and enable slInit hook");
        return false;
    }

    // Hook slIsFeatureSupported
    if (!CreateAndEnableHook(GetProcAddress(sl_interposer, "slIsFeatureSupported"),
                             reinterpret_cast<LPVOID>(slIsFeatureSupported_Detour),
                             reinterpret_cast<LPVOID*>(&slIsFeatureSupported_Original), "slIsFeatureSupported")) {
        LogError("Failed to create and enable slIsFeatureSupported hook");
        return false;
    }

    // Hook slGetNativeInterface
    if (!CreateAndEnableHook(GetProcAddress(sl_interposer, "slGetNativeInterface"),
                             reinterpret_cast<LPVOID>(slGetNativeInterface_Detour),
                             reinterpret_cast<LPVOID*>(&slGetNativeInterface_Original), "slGetNativeInterface")) {
        LogError("Failed to create and enable slGetNativeInterface hook");
        return false;
    }

    // Hook slUpgradeInterface
    if (!CreateAndEnableHook(GetProcAddress(sl_interposer, "slUpgradeInterface"),
                             reinterpret_cast<LPVOID>(slUpgradeInterface_Detour),
                             reinterpret_cast<LPVOID*>(&slUpgradeInterface_Original), "slUpgradeInterface")) {
        LogError("Failed to create and enable slUpgradeInterface hook");
        return false;
    }

    LogInfo("Streamline hooks installed successfully");
    return true;
}


// Get last SDK version from slInit calls
uint64_t GetLastStreamlineSDKVersion() {
    return g_last_sdk_version.load();
}
