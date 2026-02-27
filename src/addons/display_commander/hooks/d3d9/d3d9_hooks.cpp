#include "d3d9_hooks.hpp"
#include "../../globals.hpp"
#include "../../settings/experimental_tab_settings.hpp"
#include "../../utils/general_utils.hpp"
#include "../../utils/logging.hpp"
#include "d3d9_device_vtable_logging.hpp"
#include "d3d9_no_reshade_device_state.hpp"
#include "d3d9_present_hooks.hpp"
#include "d3d9_present_params_upgrade.hpp"
#include "d3d9_vtable_indices.hpp"

#include <d3d9.h>
#include <MinHook.h>
#include <atomic>
#include <memory>

namespace display_commanderhooks::d3d9 {

std::atomic<bool> g_dx9_hooks_installed{false};
std::atomic<std::shared_ptr<const D3D9NoReShadeDeviceSnapshot>> g_last_d3d9_no_reshade_device_snapshot{nullptr};

namespace {

void StoreD3D9NoReShadeDeviceSnapshot(bool created_with_ex, const D3DPRESENT_PARAMETERS& pp) {
    auto s = std::make_shared<D3D9NoReShadeDeviceSnapshot>();
    s->created_with_ex = created_with_ex;
    s->back_buffer_count = pp.BackBufferCount;
    s->swap_effect = pp.SwapEffect;
    s->presentation_interval = pp.PresentationInterval;
    s->windowed = (pp.Windowed != FALSE) ? 1 : 0;
    g_last_d3d9_no_reshade_device_snapshot.store(s);
}

}  // namespace

namespace {

// Direct3DCreate9 / Direct3DCreate9Ex export types
using Direct3DCreate9_pfn = IDirect3D9*(WINAPI*)(UINT SDKVersion);
using Direct3DCreate9Ex_pfn = HRESULT(WINAPI*)(UINT SDKVersion, IDirect3D9Ex** ppD3D);

// IDirect3D9::CreateDevice and IDirect3D9Ex::CreateDeviceEx (STDMETHODCALLTYPE = __stdcall)
using CreateDevice_pfn = HRESULT(STDMETHODCALLTYPE*)(IDirect3D9* This, UINT Adapter, D3DDEVTYPE DeviceType,
                                                     HWND hFocusWindow, DWORD BehaviorFlags,
                                                     D3DPRESENT_PARAMETERS* pPresentationParameters,
                                                     IDirect3DDevice9** ppReturnedDeviceInterface);
using CreateDeviceEx_pfn = HRESULT(STDMETHODCALLTYPE*)(IDirect3D9Ex* This, UINT Adapter, D3DDEVTYPE DeviceType,
                                                       HWND hFocusWindow, DWORD BehaviorFlags,
                                                       D3DPRESENT_PARAMETERS* pPresentationParameters,
                                                       D3DDISPLAYMODEEX* pFullscreenDisplayMode,
                                                       IDirect3DDevice9Ex** ppReturnedDeviceInterface);

Direct3DCreate9_pfn Direct3DCreate9_Original = nullptr;
Direct3DCreate9Ex_pfn Direct3DCreate9Ex_Original = nullptr;
CreateDevice_pfn CreateDevice_Original = nullptr;
CreateDeviceEx_pfn CreateDeviceEx_Original = nullptr;

std::atomic<bool> g_d3d9_factory_create_device_hooked{false};
std::atomic<bool> g_d3d9_factory_create_device_ex_hooked{false};

void OnD3D9DeviceCreated(IDirect3DDevice9* device) {
    if (device == nullptr) {
        return;
    }
    RecordPresentUpdateDevice(device);
    if (HookD3D9Present(device)) {
        LogInfo("InstallDX9Hooks: D3D9 Present hooks installed for device 0x%p", static_cast<void*>(device));
    }
    InstallD3D9DeviceVtableLogging(device);
}

// Detour for IDirect3D9::CreateDevice. When d3d9_flipex_enabled_no_reshade: upgrade to D3D9Ex (like ReShade
// OnCreateDevice api_version 0x9100) by calling Direct3DCreate9Ex + CreateDeviceEx. Otherwise apply
// present-param upgrades (no FLIPEX) and call original CreateDevice.
HRESULT STDMETHODCALLTYPE CreateDevice_Detour(IDirect3D9* This, UINT Adapter, D3DDEVTYPE DeviceType, HWND hFocusWindow,
                                              DWORD BehaviorFlags, D3DPRESENT_PARAMETERS* pPresentationParameters,
                                              IDirect3DDevice9** ppReturnedDeviceInterface) {
    if (CreateDevice_Original == nullptr) {
        return D3DERR_INVALIDCALL;
    }
    if (pPresentationParameters == nullptr) {
        return D3DERR_INVALIDCALL;
    }
    if ((BehaviorFlags & D3DCREATE_ADAPTERGROUP_DEVICE) != 0) {
        LogInfo("D3D9 CreateDevice: adapter group devices unsupported");
        return D3DERR_NOTAVAILABLE;
    }

    // Upgrade CreateDevice -> CreateDeviceEx when FLIPEX is enabled (mirrors ReShade create_device 0x9100)
    if (settings::g_experimentalTabSettings.d3d9_flipex_enabled_no_reshade.GetValue()
        && Direct3DCreate9Ex_Original != nullptr) {
        IDirect3D9Ex* d3dex = nullptr;
        HRESULT hrEx = Direct3DCreate9Ex_Original(D3D_SDK_VERSION, &d3dex);
        if (SUCCEEDED(hrEx) && d3dex != nullptr) {
            ApplyD3D9PresentParameterUpgrades(pPresentationParameters, true);
            D3DPRESENT_PARAMETERS pp = *pPresentationParameters;
            // ReShade's upgrade path passes nullptr for pFullscreenDisplayMode
            HRESULT hr = d3dex->CreateDeviceEx(Adapter, DeviceType, hFocusWindow, BehaviorFlags, &pp, nullptr,
                                               reinterpret_cast<IDirect3DDevice9Ex**>(ppReturnedDeviceInterface));
            d3dex->Release();

            if (SUCCEEDED(hr) && ppReturnedDeviceInterface != nullptr && *ppReturnedDeviceInterface != nullptr) {
                pPresentationParameters->BackBufferWidth = pp.BackBufferWidth;
                pPresentationParameters->BackBufferHeight = pp.BackBufferHeight;
                pPresentationParameters->BackBufferFormat = pp.BackBufferFormat;
                pPresentationParameters->BackBufferCount = pp.BackBufferCount;
                s_d3d9e_upgrade_successful.store(true, std::memory_order_relaxed);
                StoreD3D9NoReShadeDeviceSnapshot(true, pp);
                OnD3D9DeviceCreated(*ppReturnedDeviceInterface);
                LogInfo("D3D9 CreateDevice -> CreateDeviceEx upgrade (FLIPEX) succeeded");
            }
            return hr;
        }
        if (d3dex != nullptr) {
            d3dex->Release();
        }
        LogInfo("D3D9 CreateDevice -> CreateDeviceEx upgrade skipped (Direct3DCreate9Ex failed), using CreateDevice");
    }

    if (pPresentationParameters != nullptr) {
        ApplyD3D9PresentParameterUpgrades(pPresentationParameters, false);  // no FLIPEX for non-Ex
    }
    HRESULT hr = CreateDevice_Original(This, Adapter, DeviceType, hFocusWindow, BehaviorFlags, pPresentationParameters,
                                       ppReturnedDeviceInterface);
    if (SUCCEEDED(hr) && ppReturnedDeviceInterface != nullptr && *ppReturnedDeviceInterface != nullptr) {
        StoreD3D9NoReShadeDeviceSnapshot(false, *pPresentationParameters);
        OnD3D9DeviceCreated(*ppReturnedDeviceInterface);
    }
    return hr;
}

// Detour for IDirect3D9Ex::CreateDeviceEx - mark D3D9Ex in use, apply FLIPEX/VSync upgrades, then hook the device on
// success
HRESULT STDMETHODCALLTYPE CreateDeviceEx_Detour(IDirect3D9Ex* This, UINT Adapter, D3DDEVTYPE DeviceType,
                                                HWND hFocusWindow, DWORD BehaviorFlags,
                                                D3DPRESENT_PARAMETERS* pPresentationParameters,
                                                D3DDISPLAYMODEEX* pFullscreenDisplayMode,
                                                IDirect3DDevice9Ex** ppReturnedDeviceInterface) {
    if (CreateDeviceEx_Original == nullptr) {
        return D3DERR_INVALIDCALL;
    }
    s_d3d9e_upgrade_successful.store(true, std::memory_order_relaxed);
    if (pPresentationParameters != nullptr) {
        ApplyD3D9PresentParameterUpgrades(pPresentationParameters, true);  // full FLIPEX/VSync upgrades for Ex
    }
    HRESULT hr = CreateDeviceEx_Original(This, Adapter, DeviceType, hFocusWindow, BehaviorFlags,
                                         pPresentationParameters, pFullscreenDisplayMode, ppReturnedDeviceInterface);
    if (SUCCEEDED(hr) && ppReturnedDeviceInterface != nullptr && *ppReturnedDeviceInterface != nullptr) {
        if (pPresentationParameters != nullptr) {
            StoreD3D9NoReShadeDeviceSnapshot(true, *pPresentationParameters);
        }
        OnD3D9DeviceCreated(*ppReturnedDeviceInterface);
    }
    return hr;
}

// Hook the factory's CreateDevice (and optionally CreateDeviceEx) vtable slots. Idempotent per vtable.
void HookD3D9FactoryVtable(IDirect3D9* d3d9, bool isEx) {
    if (d3d9 == nullptr) {
        return;
    }
    void** vtable = *reinterpret_cast<void***>(d3d9);

    if (!g_d3d9_factory_create_device_hooked.load(std::memory_order_relaxed)) {
        void* createDeviceTarget = vtable[D3D9FactoryVTable::CreateDevice];
        if (createDeviceTarget != nullptr
            && CreateAndEnableHook(createDeviceTarget, reinterpret_cast<LPVOID>(&CreateDevice_Detour),
                                   reinterpret_cast<LPVOID*>(&CreateDevice_Original), "IDirect3D9::CreateDevice")) {
            g_d3d9_factory_create_device_hooked.store(true, std::memory_order_relaxed);
            LogInfo("InstallDX9Hooks: IDirect3D9::CreateDevice hooked");
        }
    }

    if (isEx && !g_d3d9_factory_create_device_ex_hooked.load(std::memory_order_relaxed)) {
        void* createDeviceExTarget = vtable[D3D9FactoryVTable::CreateDeviceEx];
        if (createDeviceExTarget != nullptr
            && CreateAndEnableHook(createDeviceExTarget, reinterpret_cast<LPVOID>(&CreateDeviceEx_Detour),
                                   reinterpret_cast<LPVOID*>(&CreateDeviceEx_Original),
                                   "IDirect3D9Ex::CreateDeviceEx")) {
            g_d3d9_factory_create_device_ex_hooked.store(true, std::memory_order_relaxed);
            LogInfo("InstallDX9Hooks: IDirect3D9Ex::CreateDeviceEx hooked");
        }
    }
}

IDirect3D9* WINAPI Direct3DCreate9_Detour(UINT SDKVersion) {
    if (Direct3DCreate9_Original == nullptr) {
        return nullptr;
    }
    IDirect3D9* d3d9 = Direct3DCreate9_Original(SDKVersion);
    if (d3d9 != nullptr) {
        HookD3D9FactoryVtable(d3d9, false);
    }
    return d3d9;
}

HRESULT WINAPI Direct3DCreate9Ex_Detour(UINT SDKVersion, IDirect3D9Ex** ppD3D) {
    if (Direct3DCreate9Ex_Original == nullptr || ppD3D == nullptr) {
        return E_FAIL;
    }
    HRESULT hr = Direct3DCreate9Ex_Original(SDKVersion, ppD3D);
    if (SUCCEEDED(hr) && *ppD3D != nullptr) {
        HookD3D9FactoryVtable(*ppD3D, true);
    }
    return hr;
}

}  // namespace

bool InstallDX9Hooks(HMODULE hModule) {
    if (g_dx9_hooks_installed.load(std::memory_order_relaxed)) {
        LogInfo("InstallDX9Hooks: D3D9 hooks already installed");
        return true;
    }

    if (g_shutdown.load(std::memory_order_relaxed)) {
        LogInfo("InstallDX9Hooks: shutdown in progress, skipping");
        return false;
    }

    if (hModule == nullptr) {
        LogWarn("InstallDX9Hooks: null module handle, skipping");
        return false;
    }

    auto* pDirect3DCreate9 = reinterpret_cast<LPVOID>(GetProcAddress(hModule, "Direct3DCreate9"));
    if (pDirect3DCreate9 == nullptr) {
        LogWarn("InstallDX9Hooks: Direct3DCreate9 not found in d3d9.dll");
        return false;
    }

    if (!CreateAndEnableHook(pDirect3DCreate9, reinterpret_cast<LPVOID>(&Direct3DCreate9_Detour),
                             reinterpret_cast<LPVOID*>(&Direct3DCreate9_Original), "Direct3DCreate9")) {
        LogWarn("InstallDX9Hooks: Direct3DCreate9 hook failed");
        return false;
    }

    auto* pDirect3DCreate9Ex = reinterpret_cast<LPVOID>(GetProcAddress(hModule, "Direct3DCreate9Ex"));
    if (pDirect3DCreate9Ex != nullptr) {
        if (CreateAndEnableHook(pDirect3DCreate9Ex, reinterpret_cast<LPVOID>(&Direct3DCreate9Ex_Detour),
                                reinterpret_cast<LPVOID*>(&Direct3DCreate9Ex_Original), "Direct3DCreate9Ex")) {
            LogInfo("InstallDX9Hooks: Direct3DCreate9Ex hooked");
        }
    } else {
        LogInfo("InstallDX9Hooks: Direct3DCreate9Ex not present (optional, Vista+)");
    }

    LogInfo("InstallDX9Hooks: d3d9.dll hooked (CreateDevice/CreateDeviceEx will hook on first factory)");
    g_dx9_hooks_installed.store(true, std::memory_order_relaxed);
    return true;
}

void UninstallDX9Hooks() {
    if (!g_dx9_hooks_installed.load(std::memory_order_relaxed)) {
        return;
    }
    g_dx9_hooks_installed.store(false, std::memory_order_relaxed);
    g_d3d9_factory_create_device_hooked.store(false, std::memory_order_relaxed);
    g_d3d9_factory_create_device_ex_hooked.store(false, std::memory_order_relaxed);
    Direct3DCreate9_Original = nullptr;
    Direct3DCreate9Ex_Original = nullptr;
    CreateDevice_Original = nullptr;
    CreateDeviceEx_Original = nullptr;
    LogInfo("UninstallDX9Hooks: D3D9 hook state cleared");
}

bool AreDX9HooksInstalled() { return g_dx9_hooks_installed.load(std::memory_order_relaxed); }

}  // namespace display_commanderhooks::d3d9
