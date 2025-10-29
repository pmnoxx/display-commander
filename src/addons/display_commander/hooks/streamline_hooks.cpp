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


// Forward declaration for ReShade's DXGIFactory
// We cannot include ReShade's internal headers, but we can work with the interface

// ReShade's DXGIFactory GUID - from external/reshade/source/dxgi/dxgi_factory.hpp

struct DECLSPEC_UUID("019778D4-A03A-7AF4-B889-E92362D20238") RESHADE_DXGIFactory final : IDXGIFactory7
{
	RESHADE_DXGIFactory(IDXGIFactory  *original);
	RESHADE_DXGIFactory(IDXGIFactory2 *original);
	~RESHADE_DXGIFactory();

	RESHADE_DXGIFactory(const RESHADE_DXGIFactory &) = delete;
	RESHADE_DXGIFactory &operator=(const RESHADE_DXGIFactory &) = delete;

	#pragma region IUnknown
	HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppvObj) override;
	ULONG   STDMETHODCALLTYPE AddRef() override;
	ULONG   STDMETHODCALLTYPE Release() override;
	#pragma endregion
	#pragma region IDXGIObject
	HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID Name, UINT DataSize, const void *pData) override;
	HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID Name, const IUnknown *pUnknown) override;
	HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID Name, UINT *pDataSize, void *pData) override;
	HRESULT STDMETHODCALLTYPE GetParent(REFIID riid, void **ppParent) override;
	#pragma endregion
	#pragma region IDXGIFactory
	HRESULT STDMETHODCALLTYPE EnumAdapters(UINT Adapter, IDXGIAdapter **ppAdapter) override;
	HRESULT STDMETHODCALLTYPE MakeWindowAssociation(HWND WindowHandle, UINT Flags) override;
	HRESULT STDMETHODCALLTYPE GetWindowAssociation(HWND *pWindowHandle) override;
	HRESULT STDMETHODCALLTYPE CreateSwapChain(IUnknown *pDevice, DXGI_SWAP_CHAIN_DESC *pDesc, IDXGISwapChain **ppSwapChain) override;
	HRESULT STDMETHODCALLTYPE CreateSoftwareAdapter(HMODULE Module, IDXGIAdapter **ppAdapter) override;
	#pragma endregion
	#pragma region IDXGIFactory1
	HRESULT STDMETHODCALLTYPE EnumAdapters1(UINT Adapter, IDXGIAdapter1 **ppAdapter) override;
	BOOL    STDMETHODCALLTYPE IsCurrent() override;
	#pragma endregion
	#pragma region IDXGIFactory2
	BOOL    STDMETHODCALLTYPE IsWindowedStereoEnabled() override;
	HRESULT STDMETHODCALLTYPE CreateSwapChainForHwnd(IUnknown *pDevice, HWND hWnd, const DXGI_SWAP_CHAIN_DESC1 *pDesc, const DXGI_SWAP_CHAIN_FULLSCREEN_DESC *pFullscreenDesc, IDXGIOutput *pRestrictToOutput, IDXGISwapChain1 **ppSwapChain) override;
	HRESULT STDMETHODCALLTYPE CreateSwapChainForCoreWindow(IUnknown *pDevice, IUnknown *pWindow, const DXGI_SWAP_CHAIN_DESC1 *pDesc, IDXGIOutput *pRestrictToOutput, IDXGISwapChain1 **ppSwapChain) override;
	HRESULT STDMETHODCALLTYPE GetSharedResourceAdapterLuid(HANDLE hResource, LUID *pLuid) override;
	HRESULT STDMETHODCALLTYPE RegisterStereoStatusWindow(HWND WindowHandle, UINT wMsg, DWORD *pdwCookie) override;
	HRESULT STDMETHODCALLTYPE RegisterStereoStatusEvent(HANDLE hEvent, DWORD *pdwCookie) override;
	void    STDMETHODCALLTYPE UnregisterStereoStatus(DWORD dwCookie) override;
	HRESULT STDMETHODCALLTYPE RegisterOcclusionStatusWindow(HWND WindowHandle, UINT wMsg, DWORD *pdwCookie) override;
	HRESULT STDMETHODCALLTYPE RegisterOcclusionStatusEvent(HANDLE hEvent, DWORD *pdwCookie) override;
	void    STDMETHODCALLTYPE UnregisterOcclusionStatus(DWORD dwCookie) override;
	HRESULT STDMETHODCALLTYPE CreateSwapChainForComposition(IUnknown *pDevice, const DXGI_SWAP_CHAIN_DESC1 *pDesc, IDXGIOutput *pRestrictToOutput, IDXGISwapChain1 **ppSwapChain) override;
	#pragma endregion
	#pragma region IDXGIFactory3
	UINT    STDMETHODCALLTYPE GetCreationFlags() override;
	#pragma region
	#pragma region IDXGIFactory4
	HRESULT STDMETHODCALLTYPE EnumAdapterByLuid(LUID AdapterLuid, REFIID riid, void **ppvAdapter) override;
	HRESULT STDMETHODCALLTYPE EnumWarpAdapter(REFIID riid, void **ppvAdapter) override;
	#pragma region
	#pragma region IDXGIFactory5
	HRESULT STDMETHODCALLTYPE CheckFeatureSupport(DXGI_FEATURE Feature, void *pFeatureSupportData, UINT FeatureSupportDataSize) override;
	#pragma endregion
	#pragma region IDXGIFactory6
	HRESULT STDMETHODCALLTYPE EnumAdapterByGpuPreference(UINT Adapter, DXGI_GPU_PREFERENCE GpuPreference, REFIID riid, void **ppvAdapter) override;
	#pragma endregion
	#pragma region IDXGIFactory7
	HRESULT STDMETHODCALLTYPE RegisterAdaptersChangedEvent(HANDLE hEvent, DWORD *pdwCookie) override;
	HRESULT STDMETHODCALLTYPE UnregisterAdaptersChangedEvent(DWORD dwCookie) override;
	#pragma endregion

	bool check_and_upgrade_interface(REFIID riid);

	void check_and_proxy_adapter_interface(REFIID riid, void **original_adapter);

	IDXGIFactory *_orig;
	LONG _ref = 1;
	unsigned short _interface_version;
};



// Reference: https://github.com/NVIDIA-RTX/Streamline/blob/b998246a3d499c08765c5681b229c9e6b4513348/source/core/sl.api/sl.cpp#L625
int slUpgradeInterface_Detour(void** baseInterface) {
    // Increment counter
    g_streamline_event_counters[STREAMLINE_EVENT_SL_UPGRADE_INTERFACE].fetch_add(1);
    g_swapchain_event_total_count.fetch_add(1);

    // Check config-driven flag
    bool prevent_slupgrade_interface = g_prevent_slupgrade_interface.load();
    LogInfo("prevent_slupgrade_interface: %d", static_cast<int>(prevent_slupgrade_interface));
    if (prevent_slupgrade_interface) {
        Microsoft::WRL::ComPtr<IDXGISwapChain> swapchain{};
        if (SUCCEEDED(swapchain->QueryInterface(IID_PPV_ARGS(&swapchain)))) {
            LogInfo("[slUpgradeInterface] Found IDXGISwapChain interface");
        }

        Microsoft::WRL::ComPtr<IDXGIFactory> dxgi_factory{};
        if (SUCCEEDED(dxgi_factory->QueryInterface(IID_PPV_ARGS(&dxgi_factory)))) {
            LogInfo("[slUpgradeInterface] Found IDXGIFactory interface");
        }
    }

    auto* unknown = static_cast<IUnknown*>(*baseInterface);

    if (slUpgradeInterface_Original == nullptr) {
        return -1; // Error if original not available
    }

    IDXGIFactory7* dxgi_factory7{};
    if (SUCCEEDED(unknown->QueryInterface(&dxgi_factory7))) {
        LogInfo("[slUpgradeInterface] Found IDXGIFactory7 interface");


        // Try to get ReShade's DXGIFactory from private data
        RESHADE_DXGIFactory* reshade_factory = nullptr;
        UINT size = sizeof(reshade_factory);

        #if 1
        auto* factory_wrapper = new display_commanderhooks::DXGIFactoryWrapper(dxgi_factory7, display_commanderhooks::SwapChainHook::Proxy); // dxgi_factory7
        factory_wrapper->SetSLGetNativeInterface(slGetNativeInterface_Original);
        factory_wrapper->SetSLUpgradeInterface(slUpgradeInterface_Original);

        slUpgradeInterface_Original(reinterpret_cast<void**>(&factory_wrapper));
        if (dxgi_factory7 == nullptr) {
          LogError("slUpgradeInterface() - dxgi_factory7 became null after slUpgradeInterface_Original call");
          HRESULT hr = slUpgradeInterface_Original(baseInterface);
          return hr;
        }

        // Create wrapper to ensure it doesn't pass active queue for swapchain creation
        auto* factory_wrapper2 = new display_commanderhooks::DXGIFactoryWrapper(factory_wrapper, display_commanderhooks::SwapChainHook::Native); // dxgi_factory7
        factory_wrapper2->SetSLGetNativeInterface(slGetNativeInterface_Original);
        factory_wrapper2->SetSLUpgradeInterface(slUpgradeInterface_Original);
        // TODO(user): Set command queue map when available

        *baseInterface = factory_wrapper2;

        #else
        // Try to retrieve ReShade's DXGIFactory using its GUID
        HRESULT private_data_hr = dxgi_factory7->GetPrivateData( __uuidof(RESHADE_DXGIFactory), &size, &reshade_factory);
        if (FAILED(private_data_hr) || reshade_factory == nullptr) {
          LogError("slUpgradeInterface() - Failed to get DXGIFactory proxy from IDXGIFactory7, hr=0x%x", private_data_hr);
          // Fallback: pass through to Streamline without wrapper
          return slUpgradeInterface_Original(baseInterface);
        }

        // For UE compatibility, we need to be very careful about interface identity
        // Instead of creating a wrapper, let's try a different approach:
        // Pass the original interface to Streamline and let it handle the upgrade
        auto original_factory = reshade_factory->_orig;

        HRESULT hr = slUpgradeInterface_Original(reinterpret_cast<void**>(&original_factory));

        reshade_factory->_orig = original_factory;

        auto* factory_wrapper2 = new display_commanderhooks::DXGIFactoryWrapper(reshade_factory, display_commanderhooks::SwapChainHook::Native); // dxgi_factory7
        factory_wrapper2->SetSLGetNativeInterface(slGetNativeInterface_Original);
        factory_wrapper2->SetSLUpgradeInterface(slUpgradeInterface_Original);
        // TODO(user): Set command queue map when available

        *baseInterface = factory_wrapper2;


        if (FAILED(hr)) {
          LogError("slUpgradeInterface() - Streamline upgrade failed, hr=0x%x", hr);
          return hr;
        }

        LogInfo("[slUpgradeInterface] Successfully upgraded interface for Streamline");
        return hr;
        #endif

        dxgi_factory7->Release();
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

bool InstallStreamlineHooks() {
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

    // Initialize prevent_slupgrade_interface from config
    InitializePreventSLUpgradeInterface();

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

    // Mark Streamline hooks as installed
    display_commanderhooks::HookSuppressionManager::GetInstance().MarkHookInstalled(display_commanderhooks::HookType::STREAMLINE);

    return true;
}


// Get last SDK version from slInit calls
uint64_t GetLastStreamlineSDKVersion() {
    return g_last_sdk_version.load();
}
