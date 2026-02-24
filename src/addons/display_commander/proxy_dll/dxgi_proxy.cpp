/*
 * DXGI Proxy Functions
 * Forwards DXGI calls to the real system dxgi.dll
 */

#include <dxgi.h>
#include <dxgi1_6.h>
#include <Windows.h>
#include <algorithm>
#include <iterator>
#include <string>

#include "../hooks/dxgi_factory_wrapper.hpp"
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

void LoadRealDXGIFromDllMain() { (void)LoadRealDXGI(); }

extern "C" HRESULT WINAPI CreateDXGIFactory2(UINT Flags, REFIID riid, void** ppFactory) {
    if (!LoadRealDXGI()) return E_FAIL;
    if (g_pfn_CreateDXGIFactory2 == nullptr) return E_FAIL;

    LogDebug("CreateDXGIFactory2 Flags: %u", Flags);

    // ReShade dxgi_factory.cpp / dxgi.cpp iid_lookup
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
    if (upgrade_to_factory7) {
        LogInfo("Upgrading RIID to IDXGIFactory7");
    } else {
        // failure
        return E_FAIL;
    }
    REFIID request_riid = upgrade_to_factory7 ? __uuidof(IDXGIFactory7) : riid;

    HRESULT hr = g_pfn_CreateDXGIFactory2(Flags, request_riid, ppFactory);
    if (SUCCEEDED(hr) && ppFactory != nullptr && *ppFactory != nullptr && upgrade_to_factory7) {
        // if supports IDXGIFactory7 interface
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
