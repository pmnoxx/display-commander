/*
 * DXGI Proxy Functions
 * Forwards DXGI calls to the real system dxgi.dll
 */

#include <d3d11.h>
#include <dxgi.h>
#include <MinHook.h>
#include <Windows.h>
#include <string>

#include "dxgi_proxy_init.hpp"

#include "../hooks/api_hooks.hpp"
#include "../hooks/hook_suppression_manager.hpp"
#include "../utils/general_utils.hpp"
#include "../utils/logging.hpp"
#include "globals.hpp"

// Function pointer types
typedef HRESULT(WINAPI* PFN_CreateDXGIFactory)(REFIID riid, void** ppFactory);
typedef HRESULT(WINAPI* PFN_CreateDXGIFactory1)(REFIID riid, void** ppFactory);
typedef HRESULT(WINAPI* PFN_CreateDXGIFactory2)(UINT Flags, REFIID riid, void** ppFactory);
typedef HRESULT(WINAPI* PFN_DXGIGetDebugInterface1)(UINT Flags, REFIID riid, void** pDebug);
typedef HRESULT(WINAPI* PFN_DXGIDeclareAdapterRemovalSupport)();

typedef HRESULT(WINAPI* PFN_D3D11CreateDeviceAndSwapChain)(IDXGIAdapter* pAdapter, D3D_DRIVER_TYPE DriverType,
                                                           HMODULE Software, UINT Flags,
                                                           const D3D_FEATURE_LEVEL* pFeatureLevels, UINT FeatureLevels,
                                                           UINT SDKVersion, const DXGI_SWAP_CHAIN_DESC* pSwapChainDesc,
                                                           IDXGISwapChain** ppSwapChain, ID3D11Device** ppDevice,
                                                           D3D_FEATURE_LEVEL* pFeatureLevel,
                                                           ID3D11DeviceContext** ppImmediateContext);

// IDXGIFactory::CreateSwapChain (vtable index 10). Same as ReShade (d3d11.cpp) and Special K
// (SK_DXGI_HookFactory in dxgi.cpp: DXGI_VIRTUAL_HOOK index 10 = CreateSwapChain; 15 = CreateSwapChainForHwnd).
typedef HRESULT(STDMETHODCALLTYPE* PFN_IDXGIFactory_CreateSwapChain)(IDXGIFactory* pThis, IUnknown* pDevice,
                                                                     DXGI_SWAP_CHAIN_DESC* pDesc,
                                                                     IDXGISwapChain** ppSwapChain);

// Load real DXGI DLL and get function pointers
static HMODULE g_dxgi_module = nullptr;
static HMODULE g_d3d11_module = nullptr;
static PFN_D3D11CreateDeviceAndSwapChain g_D3D11CreateDeviceAndSwapChain_Original = nullptr;
static bool g_d3d11_create_device_and_swap_chain_hook_installed = false;

// Vtable+10 hook for IDXGIFactory::CreateSwapChain (testing, same idea as ReShade d3d11.cpp 109-129)
static PFN_IDXGIFactory_CreateSwapChain g_IDXGIFactory_CreateSwapChain_Original = nullptr;
static void* g_IDXGIFactory_CreateSwapChain_hooked_target = nullptr;

static HRESULT STDMETHODCALLTYPE IDXGIFactory_CreateSwapChain_TestingDetour(IDXGIFactory* pThis, IUnknown* pDevice,
                                                                            DXGI_SWAP_CHAIN_DESC* pDesc,
                                                                            IDXGISwapChain** ppSwapChain) {
    LogInfo("[dxgi_proxy testing] IDXGIFactory::CreateSwapChain (vtable+10) called");
    if (g_IDXGIFactory_CreateSwapChain_Original == nullptr) {
        return E_FAIL;
    }
    static IDXGISwapChain* prev_good = nullptr;
    static DXGI_SWAP_CHAIN_DESC prev_desc = {0};
    pDesc->BufferDesc.Width *= 2;
    pDesc->BufferDesc.Height *= 2;
    auto res = g_IDXGIFactory_CreateSwapChain_Original(pThis, pDevice, pDesc, ppSwapChain);
    if (SUCCEEDED(res)) {
        prev_good = *ppSwapChain;
        prev_good->AddRef();
        prev_desc = *pDesc;
        LogInfo("[dxgi_proxy testing] IDXGIFactory::CreateSwapChain (vtable+10) succeeded: %p", ppSwapChain);
    } else {
        if (prev_good != nullptr) {
            *ppSwapChain = prev_good;
            HRESULT hr2 =
                prev_good->ResizeBuffers(prev_desc.BufferCount, pDesc->BufferDesc.Width, pDesc->BufferDesc.Height,
                                         prev_desc.BufferDesc.Format, prev_desc.Flags);
            if (FAILED(hr2)) {
                // ResizeBuffers fails (DXGI_ERROR_INVALID_CALL) because the previous swap chain's back buffers
                // still have outstanding references: the game/engine still holds RTVs and has them bound to the
                // device context. MSDN requires all references released before ResizeBuffers; we cannot do that
                // from inside the hook. Return previous swap chain at its current size so the game does not crash.
                LogWarn(
                    "[dxgi_proxy testing] IDXGIFactory::CreateSwapChain (vtable+10) resize of fallback swap chain "
                    "failed (0x%08X); returning previous swap chain at current size",
                    static_cast<unsigned>(hr2));
            }
            prev_good->AddRef();
            // resize based on pDesc
            // ..prev_good->ResizeBuffers(pDesc->BufferCount, pDesc->BufferDesc.Width * 2, pDesc->BufferDesc.Height * 2,
            //                         pDesc->BufferDesc.Format, pDesc->Flags);
            LogInfo(
                "[dxgi_proxy testing] IDXGIFactory::CreateSwapChain (vtable+10) failed, returning previous good swap "
                "chain: %p",
                prev_good);
            return S_OK;
        }
        LogError("[dxgi_proxy testing] IDXGIFactory::CreateSwapChain (vtable+10) failed: %d", res);
        return res;
    }
    return res;
}

// Get factory from device (device -> IDXGIDevice -> GetAdapter -> GetParent), then hook vtable+10 if not already
// hooked. Same path as ReShade d3d11.cpp (109-129).
static void TryHookFactoryCreateSwapChainVtable(ID3D11Device* device) {
    if (device == nullptr) {
        return;
    }
    IDXGIDevice* dxgi_device = nullptr;
    HRESULT hr = device->QueryInterface(__uuidof(IDXGIDevice), reinterpret_cast<void**>(&dxgi_device));
    if (FAILED(hr) || dxgi_device == nullptr) {
        return;
    }
    IDXGIAdapter* adapter = nullptr;
    hr = dxgi_device->GetAdapter(&adapter);
    dxgi_device->Release();
    if (FAILED(hr) || adapter == nullptr) {
        return;
    }
    IDXGIFactory* factory = nullptr;
    hr = adapter->GetParent(__uuidof(IDXGIFactory), reinterpret_cast<void**>(&factory));
    adapter->Release();
    if (FAILED(hr) || factory == nullptr) {
        return;
    }
    // Vtable layout: first pointer in object is vtable; CreateSwapChain is at index 10 (ReShade, Special K)
    void** vtable = *reinterpret_cast<void***>(factory);
    void* target = vtable[10];
    factory->Release();

    if (target == nullptr) {
        return;
    }
    if (target == g_IDXGIFactory_CreateSwapChain_hooked_target) {
        return;  // already hooked this vtable
    }
    MH_STATUS init_status = SafeInitializeMinHook(display_commanderhooks::HookType::DXGI_FACTORY);
    if (init_status != MH_OK && init_status != MH_ERROR_ALREADY_INITIALIZED) {
        LogWarn("[dxgi_proxy testing] MinHook init for vtable+10 failed: %d", init_status);
        return;
    }
    if (!CreateAndEnableHook(target, reinterpret_cast<LPVOID>(IDXGIFactory_CreateSwapChain_TestingDetour),
                             reinterpret_cast<LPVOID*>(&g_IDXGIFactory_CreateSwapChain_Original),
                             "IDXGIFactory::CreateSwapChain (testing vtable+10)")) {
        LogWarn("[dxgi_proxy testing] Failed to install IDXGIFactory::CreateSwapChain vtable+10 hook");
        return;
    }
    g_IDXGIFactory_CreateSwapChain_hooked_target = target;
    LogInfo("[dxgi_proxy testing] IDXGIFactory::CreateSwapChain vtable+10 hook installed (factory from device)");
}

static HRESULT WINAPI D3D11CreateDeviceAndSwapChain_TestingDetour(
    IDXGIAdapter* pAdapter, D3D_DRIVER_TYPE DriverType, HMODULE Software, UINT Flags,
    const D3D_FEATURE_LEVEL* pFeatureLevels, UINT FeatureLevels, UINT SDKVersion,
    const DXGI_SWAP_CHAIN_DESC* pSwapChainDesc, IDXGISwapChain** ppSwapChain, ID3D11Device** ppDevice,
    D3D_FEATURE_LEVEL* pFeatureLevel, ID3D11DeviceContext** ppImmediateContext) {
    LogInfo("[dxgi_proxy testing] D3D11CreateDeviceAndSwapChain called (hook from LoadRealDXGI)");
    if (g_D3D11CreateDeviceAndSwapChain_Original == nullptr) {
        return E_FAIL;
    }
    HRESULT hr = g_D3D11CreateDeviceAndSwapChain_Original(pAdapter, DriverType, Software, Flags, pFeatureLevels,
                                                          FeatureLevels, SDKVersion, pSwapChainDesc, ppSwapChain,
                                                          ppDevice, pFeatureLevel, ppImmediateContext);
    // Same as ReShade d3d11.cpp (109-129): get factory from device and hook vtable+10 for CreateSwapChain if not
    // already hooked
    if (SUCCEEDED(hr) && ppDevice != nullptr && *ppDevice != nullptr) {
        TryHookFactoryCreateSwapChainVtable(*ppDevice);
    }
    return hr;
}

static void InstallD3D11CreateDeviceAndSwapChainHookTesting() {
    if (g_d3d11_module == nullptr || g_d3d11_create_device_and_swap_chain_hook_installed) {
        return;
    }
    auto* target = reinterpret_cast<LPVOID>(GetProcAddress(g_d3d11_module, "D3D11CreateDeviceAndSwapChain"));
    if (target == nullptr) {
        LogWarn("[dxgi_proxy testing] D3D11CreateDeviceAndSwapChain not found in d3d11.dll");
        return;
    }
    MH_STATUS init_status = SafeInitializeMinHook(display_commanderhooks::HookType::DXGI_FACTORY);
    if (init_status != MH_OK && init_status != MH_ERROR_ALREADY_INITIALIZED) {
        LogWarn("[dxgi_proxy testing] MinHook init failed: %d", init_status);
        return;
    }
    if (!CreateAndEnableHook(target, reinterpret_cast<LPVOID>(D3D11CreateDeviceAndSwapChain_TestingDetour),
                             reinterpret_cast<LPVOID*>(&g_D3D11CreateDeviceAndSwapChain_Original),
                             "D3D11CreateDeviceAndSwapChain (testing from LoadRealDXGI)")) {
        LogWarn("[dxgi_proxy testing] Failed to install D3D11CreateDeviceAndSwapChain hook");
        return;
    }
    g_d3d11_create_device_and_swap_chain_hook_installed = true;
    LogInfo("[dxgi_proxy testing] D3D11CreateDeviceAndSwapChain hook installed (d3d11.dll by name)");
}

static bool LoadRealDXGI() {
    if (g_dxgi_module != nullptr) return true;

    WCHAR system_path[MAX_PATH];
    GetSystemDirectoryW(system_path, MAX_PATH);
    std::wstring dxgi_path = std::wstring(system_path) + L"\\dxgi.dll";

    g_dxgi_module = LoadLibraryW(dxgi_path.c_str());
    if (g_dxgi_module == nullptr) {
        LogError("[dxgi_proxy] Failed to load dxgi.dll from %s", dxgi_path.c_str());
        return false;
    }

    // TODO: needs rewrite
    if (!enabled_experimental_features) return true;

    LogInfo("[dxgi_proxy] Loaded dxgi.dll from %s", dxgi_path.c_str());

    // Testing: load d3d11.dll by name (no system path) and hook D3D11CreateDeviceAndSwapChain
    g_d3d11_module = LoadLibraryW(L"d3d11.dll");
    if (g_d3d11_module != nullptr) {
        InstallD3D11CreateDeviceAndSwapChainHookTesting();
    } else {
        LogWarn("[dxgi_proxy testing] LoadLibraryW(d3d11.dll) failed");
    }

    return true;
}

// Preload real system dxgi.dll only. Safe to call from DllMain (no hooks, no d3d11).
void LoadRealDXGIFromDllMain() { LoadRealDXGI(); }

// Install MinHook on real dxgi.dll CreateDXGIFactory/CreateDXGIFactory1. No-op if real DXGI not loaded.
void InstallRealDXGIMinHookHooks() {
    if (g_dxgi_module == nullptr) {
        return;
    }
    display_commanderhooks::InstallDxgiFactoryHooks(g_dxgi_module);
}

extern "C" HRESULT WINAPI CreateDXGIFactory(REFIID riid, void** ppFactory) {
    if (!LoadRealDXGI()) return E_FAIL;

    auto func = reinterpret_cast<PFN_CreateDXGIFactory>(GetProcAddress(g_dxgi_module, "CreateDXGIFactory"));
    if (func == nullptr) return E_FAIL;

    return func(riid, ppFactory);
}

extern "C" HRESULT WINAPI CreateDXGIFactory1(REFIID riid, void** ppFactory) {
    if (!LoadRealDXGI()) return E_FAIL;

    auto func = reinterpret_cast<PFN_CreateDXGIFactory1>(GetProcAddress(g_dxgi_module, "CreateDXGIFactory1"));
    if (func == nullptr) return E_FAIL;

    return func(riid, ppFactory);
}

extern "C" HRESULT WINAPI CreateDXGIFactory2(UINT Flags, REFIID riid, void** ppFactory) {
    if (!LoadRealDXGI()) return E_FAIL;

    auto func = reinterpret_cast<PFN_CreateDXGIFactory2>(GetProcAddress(g_dxgi_module, "CreateDXGIFactory2"));
    if (func == nullptr) return E_FAIL;

    return func(Flags, riid, ppFactory);
}

extern "C" HRESULT WINAPI DXGIGetDebugInterface1(UINT Flags, REFIID riid, void** pDebug) {
    if (!LoadRealDXGI()) return E_FAIL;

    auto func = reinterpret_cast<PFN_DXGIGetDebugInterface1>(GetProcAddress(g_dxgi_module, "DXGIGetDebugInterface1"));
    if (func == nullptr) return E_FAIL;

    return func(Flags, riid, pDebug);
}

extern "C" HRESULT WINAPI DXGIDeclareAdapterRemovalSupport() {
    if (!LoadRealDXGI()) return E_FAIL;

    auto func = reinterpret_cast<PFN_DXGIDeclareAdapterRemovalSupport>(
        GetProcAddress(g_dxgi_module, "DXGIDeclareAdapterRemovalSupport"));
    if (func == nullptr) return E_FAIL;

    return func();
}
