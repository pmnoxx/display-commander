/*
 * DXGI Proxy Functions
 * Forwards DXGI calls to the real system dxgi.dll
 */

#include <dxgi.h>
#include <dxgi1_6.h>
#include <MinHook.h>
#include <Windows.h>
#include <algorithm>
#include <iterator>
#include <string>

#include "../hooks/dxgi_factory_wrapper.hpp"
#include "../hooks/hook_suppression_manager.hpp"
#include "../utils/general_utils.hpp"
#include "../utils/logging.hpp"

// Function pointer types
typedef HRESULT(WINAPI* PFN_CreateDXGIFactory)(REFIID riid, void** ppFactory);
typedef HRESULT(WINAPI* PFN_CreateDXGIFactory1)(REFIID riid, void** ppFactory);
typedef HRESULT(WINAPI* PFN_CreateDXGIFactory2)(UINT Flags, REFIID riid, void** ppFactory);
typedef HRESULT(WINAPI* PFN_DXGIGetDebugInterface1)(UINT Flags, REFIID riid, void** pDebug);
typedef HRESULT(WINAPI* PFN_DXGIDeclareAdapterRemovalSupport)();

// Real DXGI module and function pointers - resolved once when LoadRealDXGI is first called so they cannot be overridden
// later
static HMODULE g_dxgi_module = nullptr;
static PFN_CreateDXGIFactory g_pfn_CreateDXGIFactory = nullptr;
static PFN_CreateDXGIFactory1 g_pfn_CreateDXGIFactory1 = nullptr;
static PFN_CreateDXGIFactory2 g_pfn_CreateDXGIFactory2 = nullptr;
static PFN_DXGIGetDebugInterface1 g_pfn_DXGIGetDebugInterface1 = nullptr;
static PFN_DXGIDeclareAdapterRemovalSupport g_pfn_DXGIDeclareAdapterRemovalSupport = nullptr;

// MinHook trampolines for real DXGI factory creation (set when InstallRealDXGIMinHookHooks runs)
static PFN_CreateDXGIFactory g_pfn_CreateDXGIFactory_Original = nullptr;
static PFN_CreateDXGIFactory1 g_pfn_CreateDXGIFactory1_Original = nullptr;
static PFN_CreateDXGIFactory2 g_pfn_CreateDXGIFactory2_Original = nullptr;
static bool g_real_dxgi_minhook_installed = false;

static bool LoadRealDXGI() {
    if (g_dxgi_module != nullptr) return true;

    WCHAR system_path[MAX_PATH];
    GetSystemDirectoryW(system_path, MAX_PATH);
    std::wstring dxgi_path = std::wstring(system_path) + L"\\dxgi.dll";

    g_dxgi_module = LoadLibraryW(dxgi_path.c_str());
    if (g_dxgi_module == nullptr) return false;

    // Resolve all original function pointers immediately so they cannot be overridden by other hooks
    g_pfn_CreateDXGIFactory =
        reinterpret_cast<PFN_CreateDXGIFactory>(GetProcAddress(g_dxgi_module, "CreateDXGIFactory"));
    g_pfn_CreateDXGIFactory1 =
        reinterpret_cast<PFN_CreateDXGIFactory1>(GetProcAddress(g_dxgi_module, "CreateDXGIFactory1"));
    g_pfn_CreateDXGIFactory2 =
        reinterpret_cast<PFN_CreateDXGIFactory2>(GetProcAddress(g_dxgi_module, "CreateDXGIFactory2"));
    g_pfn_DXGIGetDebugInterface1 =
        reinterpret_cast<PFN_DXGIGetDebugInterface1>(GetProcAddress(g_dxgi_module, "DXGIGetDebugInterface1"));
    g_pfn_DXGIDeclareAdapterRemovalSupport = reinterpret_cast<PFN_DXGIDeclareAdapterRemovalSupport>(
        GetProcAddress(g_dxgi_module, "DXGIDeclareAdapterRemovalSupport"));

    return (g_pfn_CreateDXGIFactory2 != nullptr);  // at least CreateDXGIFactory2 required
}

// Shared body: upgrade to Factory7, call target, wrap result. Used by both proxy export and MinHook detour.
static HRESULT CreateDXGIFactory2_Body(PFN_CreateDXGIFactory2 call_target, UINT Flags, REFIID riid, void** ppFactory) {
    if (call_target == nullptr || ppFactory == nullptr) return E_FAIL;

    static constexpr IID iid_lookup[] = {
        __uuidof(IDXGIFactory),   // {7B7166EC-21C7-44AE-B21A-C9AE321AE369}
        __uuidof(IDXGIFactory1),  // {770AAE78-F26F-4DBA-A829-253C83D1B387}
        __uuidof(IDXGIFactory2),  // {50C83A1C-E072-4C48-87B0-3630FA36A6D0}
        __uuidof(IDXGIFactory3),  // {25483823-CD46-4C7D-86CA-47AA95B837BD}
        __uuidof(IDXGIFactory4),  // {1BC6EA02-EF36-464F-BF0C-21CA39E5168A}
        __uuidof(IDXGIFactory5),  // {7632E1f5-EE65-4DCA-87FD-84CD75F8838D}
        __uuidof(IDXGIFactory6),  // {C1B6694F-FF09-44A9-B03C-77900A0A1D17}
        __uuidof(IDXGIFactory7),  // {A4966EED-76DB-44DA-84C1-EE9A7AFB20A8}
    };
    bool upgrade_to_factory7 = (std::find(std::begin(iid_lookup), std::end(iid_lookup), riid) != std::end(iid_lookup));
    if (!upgrade_to_factory7) return E_FAIL;

    REFIID request_riid = __uuidof(IDXGIFactory7);
    LogDebug("CreateDXGIFactory2 Flags: %u (via %s)", Flags,
             call_target == g_pfn_CreateDXGIFactory2_Original ? "MinHook trampoline" : "direct");

    HRESULT hr = call_target(Flags, request_riid, ppFactory);
    if (SUCCEEDED(hr) && ppFactory != nullptr && *ppFactory != nullptr) {
        Microsoft::WRL::ComPtr<IDXGIFactory7> factory7;
        if (SUCCEEDED(static_cast<IUnknown*>(*ppFactory)->QueryInterface(IID_PPV_ARGS(&factory7)))) {
            IDXGIFactory7* raw_factory = static_cast<IDXGIFactory7*>(*ppFactory);
            LogInfo("Wrapping the factory");
            display_commanderhooks::DXGIFactoryWrapper* wrapper = new display_commanderhooks::DXGIFactoryWrapper(
                raw_factory, display_commanderhooks::SwapChainHook::NativeRaw);
            raw_factory->Release();
            *ppFactory = wrapper;
        } else {
            LogError("Failed to query IDXGIFactory7, skipping wrapper creation");
        }
    }
    return hr;
}

// MinHook detour for real dxgi.dll CreateDXGIFactory2 - calls trampoline then wraps.
static HRESULT WINAPI CreateDXGIFactory2_RealDetour(UINT Flags, REFIID riid, void** ppFactory) {
    LogInfo("CreateDXGIFactory2_RealDetour called");
    return CreateDXGIFactory2_Body(g_pfn_CreateDXGIFactory2_Original, Flags, riid, ppFactory);
}

// Shared body for CreateDXGIFactory/CreateDXGIFactory1 (same signature): upgrade to Factory7, call target, wrap.
static HRESULT CreateDXGIFactory1_Body(PFN_CreateDXGIFactory1 call_target, REFIID riid, void** ppFactory) {
    if (call_target == nullptr || ppFactory == nullptr) return E_FAIL;

    static constexpr IID iid_lookup[] = {
        __uuidof(IDXGIFactory),   // {7B7166EC-21C7-44AE-B21A-C9AE321AE369}
        __uuidof(IDXGIFactory1),  // {770AAE78-F26F-4DBA-A829-253C83D1B387}
        __uuidof(IDXGIFactory2),  // {50C83A1C-E072-4C48-87B0-3630FA36A6D0}
        __uuidof(IDXGIFactory3),  // {25483823-CD46-4C7D-86CA-47AA95B837BD}
        __uuidof(IDXGIFactory4),  // {1BC6EA02-EF36-464F-BF0C-21CA39E5168A}
        __uuidof(IDXGIFactory5),  // {7632E1f5-EE65-4DCA-87FD-84CD75F8838D}
        __uuidof(IDXGIFactory6),  // {C1B6694F-FF09-44A9-B03C-77900A0A1D17}
        __uuidof(IDXGIFactory7),  // {A4966EED-76DB-44DA-84C1-EE9A7AFB20A8}
    };
    bool upgrade_to_factory7 = (std::find(std::begin(iid_lookup), std::end(iid_lookup), riid) != std::end(iid_lookup));
    if (!upgrade_to_factory7) return E_FAIL;

    REFIID request_riid = __uuidof(IDXGIFactory7);
    LogDebug("CreateDXGIFactory/CreateDXGIFactory1 (real detour) upgrading to IDXGIFactory7");

    HRESULT hr = call_target(request_riid, ppFactory);
    if (SUCCEEDED(hr) && ppFactory != nullptr && *ppFactory != nullptr) {
        Microsoft::WRL::ComPtr<IDXGIFactory7> factory7;
        if (SUCCEEDED(static_cast<IUnknown*>(*ppFactory)->QueryInterface(IID_PPV_ARGS(&factory7)))) {
            IDXGIFactory7* raw_factory = static_cast<IDXGIFactory7*>(*ppFactory);
            LogInfo("Wrapping the factory");
            display_commanderhooks::DXGIFactoryWrapper* wrapper = new display_commanderhooks::DXGIFactoryWrapper(
                raw_factory, display_commanderhooks::SwapChainHook::NativeRaw);
            raw_factory->Release();
            *ppFactory = wrapper;
        } else {
            LogError("Failed to query IDXGIFactory7, skipping wrapper creation");
        }
    }
    return hr;
}

static HRESULT WINAPI CreateDXGIFactory_RealDetour(REFIID riid, void** ppFactory) {
    LogInfo("CreateDXGIFactory_RealDetour called");
    return CreateDXGIFactory1_Body(g_pfn_CreateDXGIFactory_Original, riid, ppFactory);
}

static HRESULT WINAPI CreateDXGIFactory1_RealDetour(REFIID riid, void** ppFactory) {
    LogInfo("CreateDXGIFactory1_RealDetour called");
    return CreateDXGIFactory1_Body(g_pfn_CreateDXGIFactory1_Original, riid, ppFactory);
}

void InstallRealDXGIMinHookHooks() {
    if (g_dxgi_module == nullptr && !LoadRealDXGI()) return;
    if (g_dxgi_module == nullptr || g_pfn_CreateDXGIFactory2 == nullptr) return;
    if (g_real_dxgi_minhook_installed) return;

    if (display_commanderhooks::HookSuppressionManager::GetInstance().ShouldSuppressHook(
            display_commanderhooks::HookType::DXGI_FACTORY)) {
        LogInfo("Real DXGI MinHook installation suppressed by user setting");
        return;
    }

    MH_STATUS init_status = SafeInitializeMinHook(display_commanderhooks::HookType::DXGI_FACTORY);
    if (init_status != MH_OK && init_status != MH_ERROR_ALREADY_INITIALIZED) {
        LogError("Failed to initialize MinHook for real DXGI hooks - Status: %d", init_status);
        return;
    }

    if (!CreateAndEnableHook(g_pfn_CreateDXGIFactory2, reinterpret_cast<LPVOID>(CreateDXGIFactory2_RealDetour),
                             reinterpret_cast<LPVOID*>(&g_pfn_CreateDXGIFactory2_Original),
                             "CreateDXGIFactory2 (real)")) {
        LogError("Failed to create and enable CreateDXGIFactory2 (real) hook");
        return;
    }

    if (g_pfn_CreateDXGIFactory != nullptr) {
        if (!CreateAndEnableHook(g_pfn_CreateDXGIFactory, reinterpret_cast<LPVOID>(CreateDXGIFactory_RealDetour),
                                 reinterpret_cast<LPVOID*>(&g_pfn_CreateDXGIFactory_Original),
                                 "CreateDXGIFactory (real)")) {
            LogError("Failed to create and enable CreateDXGIFactory (real) hook");
        } else {
            LogInfo("Real DXGI CreateDXGIFactory MinHook installed successfully");
        }
    }

    if (g_pfn_CreateDXGIFactory1 != nullptr) {
        if (!CreateAndEnableHook(g_pfn_CreateDXGIFactory1, reinterpret_cast<LPVOID>(CreateDXGIFactory1_RealDetour),
                                 reinterpret_cast<LPVOID*>(&g_pfn_CreateDXGIFactory1_Original),
                                 "CreateDXGIFactory1 (real)")) {
            LogError("Failed to create and enable CreateDXGIFactory1 (real) hook");
        } else {
            LogInfo("Real DXGI CreateDXGIFactory1 MinHook installed successfully");
        }
    }

    g_real_dxgi_minhook_installed = true;
    display_commanderhooks::HookSuppressionManager::GetInstance().MarkHookInstalled(
        display_commanderhooks::HookType::DXGI_FACTORY);
    LogInfo("Real DXGI factory MinHook hooks installed successfully");
}

void LoadRealDXGIFromDllMain() { (void)LoadRealDXGI(); }

extern "C" HRESULT WINAPI CreateDXGIFactory2(UINT Flags, REFIID riid, void** ppFactory) {
    if (!LoadRealDXGI()) return E_FAIL;
    if (g_pfn_CreateDXGIFactory2 == nullptr) return E_FAIL;

    // Use MinHook trampoline when installed to avoid double detour; otherwise call real directly
    PFN_CreateDXGIFactory2 call_target =
        g_pfn_CreateDXGIFactory2_Original != nullptr ? g_pfn_CreateDXGIFactory2_Original : g_pfn_CreateDXGIFactory2;
    return CreateDXGIFactory2_Body(call_target, Flags, riid, ppFactory);
}

extern "C" HRESULT WINAPI CreateDXGIFactory(REFIID riid, void** ppFactory) {
    if (!LoadRealDXGI()) return E_FAIL;

    //	auto func = reinterpret_cast<PFN_CreateDXGIFactory>(GetProcAddress(g_dxgi_module, "CreateDXGIFactory"));
    //	if (func == nullptr)
    //		return E_FAIL;
    LogDebug("CreateDXGIFactory trying CreateDXGIFactory2");
    return CreateDXGIFactory2(0, riid, ppFactory);

    //	return func(riid, ppFactory);
}

extern "C" HRESULT WINAPI CreateDXGIFactory1(REFIID riid, void** ppFactory) {
    if (!LoadRealDXGI()) return E_FAIL;

    //	auto func = reinterpret_cast<PFN_CreateDXGIFactory1>(GetProcAddress(g_dxgi_module, "CreateDXGIFactory1"));
    //	if (func == nullptr)
    //		return E_FAIL;
    LogDebug("CreateDXGIFactory1 trying CreateDXGIFactory2");
    return CreateDXGIFactory2(0, riid, ppFactory);
    //	return func(riid, ppFactory);
}

extern "C" HRESULT WINAPI DXGIGetDebugInterface1(UINT Flags, REFIID riid, void** pDebug) {
    if (!LoadRealDXGI()) return E_FAIL;
    if (g_pfn_DXGIGetDebugInterface1 == nullptr) return E_NOINTERFACE;

    return g_pfn_DXGIGetDebugInterface1(Flags, riid, pDebug);
}

extern "C" HRESULT WINAPI DXGIDeclareAdapterRemovalSupport() {
    if (!LoadRealDXGI()) return E_FAIL;
    if (g_pfn_DXGIDeclareAdapterRemovalSupport == nullptr) return E_NOINTERFACE;

    return g_pfn_DXGIDeclareAdapterRemovalSupport();
}
