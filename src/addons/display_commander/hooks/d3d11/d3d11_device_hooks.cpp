// Source Code <Display Commander> // follow this order for includes in all files + add this comment at the top
#include "d3d11_device_hooks.hpp"
#include "../../utils/detour_call_tracker.hpp"
#include "../../utils/general_utils.hpp"
#include "../../utils/logging.hpp"
#include "../../utils/timing.hpp"
#include "../hook_suppression_manager.hpp"
#include "d3d11_vtable_indices.hpp"

// Libraries <ReShade> / <imgui>
// (none)

// Libraries <standard C++>
#include <atomic>
#include <set>

// Libraries <Windows.h> — before other Windows headers
#include <Windows.h>

// Libraries <Windows>
#include <d3d11.h>
#include <MinHook.h>

namespace display_commanderhooks::d3d11 {

namespace {
std::set<ID3D11Device*> g_hooked_d3d11_devices;
std::atomic<bool> g_vtable_logging_installed{false};

using VTable = display_commanderhooks::d3d11::VTable;

// Original function pointers
using CreateBuffer_pfn = HRESULT(STDMETHODCALLTYPE*)(ID3D11Device* This, const D3D11_BUFFER_DESC* pDesc,
                                                     const D3D11_SUBRESOURCE_DATA* pInitialData,
                                                     ID3D11Buffer** ppBuffer);
using CreateTexture1D_pfn = HRESULT(STDMETHODCALLTYPE*)(ID3D11Device* This, const D3D11_TEXTURE1D_DESC* pDesc,
                                                        const D3D11_SUBRESOURCE_DATA* pInitialData,
                                                        ID3D11Texture1D** ppTexture1D);
using CreateTexture2D_pfn = HRESULT(STDMETHODCALLTYPE*)(ID3D11Device* This, const D3D11_TEXTURE2D_DESC* pDesc,
                                                        const D3D11_SUBRESOURCE_DATA* pInitialData,
                                                        ID3D11Texture2D** ppTexture2D);
using CreateTexture3D_pfn = HRESULT(STDMETHODCALLTYPE*)(ID3D11Device* This, const D3D11_TEXTURE3D_DESC* pDesc,
                                                        const D3D11_SUBRESOURCE_DATA* pInitialData,
                                                        ID3D11Texture3D** ppTexture3D);
using CreateShaderResourceView_pfn = HRESULT(STDMETHODCALLTYPE*)(ID3D11Device* This, ID3D11Resource* pResource,
                                                                 const D3D11_SHADER_RESOURCE_VIEW_DESC* pDesc,
                                                                 ID3D11ShaderResourceView** ppSRView);
using CreateRenderTargetView_pfn = HRESULT(STDMETHODCALLTYPE*)(ID3D11Device* This, ID3D11Resource* pResource,
                                                               const D3D11_RENDER_TARGET_VIEW_DESC* pDesc,
                                                               ID3D11RenderTargetView** ppRTView);
using CreateDepthStencilView_pfn = HRESULT(STDMETHODCALLTYPE*)(ID3D11Device* This, ID3D11Resource* pResource,
                                                               const D3D11_DEPTH_STENCIL_VIEW_DESC* pDesc,
                                                               ID3D11DepthStencilView** ppDepthStencilView);

CreateBuffer_pfn CreateBuffer_Original = nullptr;
CreateTexture1D_pfn CreateTexture1D_Original = nullptr;
CreateTexture2D_pfn CreateTexture2D_Original = nullptr;
CreateTexture3D_pfn CreateTexture3D_Original = nullptr;
CreateShaderResourceView_pfn CreateShaderResourceView_Original = nullptr;
CreateRenderTargetView_pfn CreateRenderTargetView_Original = nullptr;
CreateDepthStencilView_pfn CreateDepthStencilView_Original = nullptr;

void LogD3D11FirstFailure(const char* method, ID3D11Device* This, HRESULT hr) {
    LogError("[D3D11 error] %s first failure — This=%p hr=0x%08X", method, static_cast<void*>(This),
             static_cast<unsigned>(hr));
}

// Detours: CALL_GUARD, log first call, call original, on FAILED log throttled + first failure.
// All detours guard on *_Original != nullptr before calling (avoid crash if hook install partially failed).
HRESULT STDMETHODCALLTYPE CreateBuffer_Detour(ID3D11Device* This, const D3D11_BUFFER_DESC* pDesc,
                                              const D3D11_SUBRESOURCE_DATA* pInitialData, ID3D11Buffer** ppBuffer) {
    CALL_GUARD(utils::get_now_ns());
    if (CreateBuffer_Original == nullptr) {
        LogError("[D3D11] CreateBuffer_Detour: Original is null, skipping");
        return E_FAIL;
    }
    static std::atomic<bool> first_call{true};
    if (first_call.exchange(false)) {
        LogInfo("[D3D11] First call: ID3D11Device::CreateBuffer");
    }
    HRESULT hr = CreateBuffer_Original(This, pDesc, pInitialData, ppBuffer);
    if (FAILED(hr)) {
        LogErrorThrottled(10, "[D3D11 error] CreateBuffer returned 0x%08X", static_cast<unsigned>(hr));
        static std::atomic<bool> first_error{true};
        if (first_error.exchange(false)) {
            LogD3D11FirstFailure("CreateBuffer", This, hr);
            if (pDesc != nullptr) {
                LogError("[D3D11 error] CreateBuffer first failure — ByteWidth=%u Usage=%u BindFlags=0x%X",
                         pDesc->ByteWidth, static_cast<unsigned>(pDesc->Usage),
                         static_cast<unsigned>(pDesc->BindFlags));
            }
        }
    }
    return hr;
}

HRESULT STDMETHODCALLTYPE CreateTexture1D_Detour(ID3D11Device* This, const D3D11_TEXTURE1D_DESC* pDesc,
                                                 const D3D11_SUBRESOURCE_DATA* pInitialData,
                                                 ID3D11Texture1D** ppTexture1D) {
    CALL_GUARD(utils::get_now_ns());
    if (CreateTexture1D_Original == nullptr) {
        LogError("[D3D11] CreateTexture1D_Detour: Original is null, skipping");
        return E_FAIL;
    }
    static std::atomic<bool> first_call{true};
    if (first_call.exchange(false)) {
        LogInfo("[D3D11] First call: ID3D11Device::CreateTexture1D");
    }
    HRESULT hr = CreateTexture1D_Original(This, pDesc, pInitialData, ppTexture1D);
    if (FAILED(hr)) {
        LogErrorThrottled(10, "[D3D11 error] CreateTexture1D returned 0x%08X", static_cast<unsigned>(hr));
        static std::atomic<bool> first_error{true};
        if (first_error.exchange(false)) {
            LogD3D11FirstFailure("CreateTexture1D", This, hr);
            if (pDesc != nullptr) {
                LogError("[D3D11 error] CreateTexture1D first failure — Width=%u MipLevels=%u Format=%u", pDesc->Width,
                         pDesc->MipLevels, static_cast<unsigned>(pDesc->Format));
            }
        }
    }
    return hr;
}

HRESULT STDMETHODCALLTYPE CreateTexture2D_Detour(ID3D11Device* This, const D3D11_TEXTURE2D_DESC* pDesc,
                                                 const D3D11_SUBRESOURCE_DATA* pInitialData,
                                                 ID3D11Texture2D** ppTexture2D) {
    CALL_GUARD(utils::get_now_ns());
    if (CreateTexture2D_Original == nullptr) {
        LogError("[D3D11] CreateTexture2D_Detour: Original is null, skipping");
        return E_FAIL;
    }
    static std::atomic<bool> first_call{true};
    if (first_call.exchange(false)) {
        LogInfo("[D3D11] First call: ID3D11Device::CreateTexture2D");
    }
    HRESULT hr = CreateTexture2D_Original(This, pDesc, pInitialData, ppTexture2D);
    if (FAILED(hr)) {
        LogErrorThrottled(10, "[D3D11 error] CreateTexture2D returned 0x%08X", static_cast<unsigned>(hr));
        static std::atomic<bool> first_error{true};
        if (first_error.exchange(false)) {
            LogD3D11FirstFailure("CreateTexture2D", This, hr);
            if (pDesc != nullptr) {
                LogError("[D3D11 error] CreateTexture2D first failure — Width=%u Height=%u MipLevels=%u Format=%u",
                         pDesc->Width, pDesc->Height, pDesc->MipLevels, static_cast<unsigned>(pDesc->Format));
            }
        }
    }
    return hr;
}

HRESULT STDMETHODCALLTYPE CreateTexture3D_Detour(ID3D11Device* This, const D3D11_TEXTURE3D_DESC* pDesc,
                                                 const D3D11_SUBRESOURCE_DATA* pInitialData,
                                                 ID3D11Texture3D** ppTexture3D) {
    CALL_GUARD(utils::get_now_ns());
    if (CreateTexture3D_Original == nullptr) {
        LogError("[D3D11] CreateTexture3D_Detour: Original is null, skipping");
        return E_FAIL;
    }
    static std::atomic<bool> first_call{true};
    if (first_call.exchange(false)) {
        LogInfo("[D3D11] First call: ID3D11Device::CreateTexture3D");
    }
    HRESULT hr = CreateTexture3D_Original(This, pDesc, pInitialData, ppTexture3D);
    if (FAILED(hr)) {
        LogErrorThrottled(10, "[D3D11 error] CreateTexture3D returned 0x%08X", static_cast<unsigned>(hr));
        static std::atomic<bool> first_error{true};
        if (first_error.exchange(false)) {
            LogD3D11FirstFailure("CreateTexture3D", This, hr);
            if (pDesc != nullptr) {
                LogError("[D3D11 error] CreateTexture3D first failure — Width=%u Height=%u Depth=%u Format=%u",
                         pDesc->Width, pDesc->Height, pDesc->Depth, static_cast<unsigned>(pDesc->Format));
            }
        }
    }
    return hr;
}

HRESULT STDMETHODCALLTYPE CreateShaderResourceView_Detour(ID3D11Device* This, ID3D11Resource* pResource,
                                                          const D3D11_SHADER_RESOURCE_VIEW_DESC* pDesc,
                                                          ID3D11ShaderResourceView** ppSRView) {
    CALL_GUARD(utils::get_now_ns());
    if (CreateShaderResourceView_Original == nullptr) {
        LogError("[D3D11] CreateShaderResourceView_Detour: Original is null, skipping");
        return E_FAIL;
    }
    HRESULT hr = CreateShaderResourceView_Original(This, pResource, pDesc, ppSRView);
    if (FAILED(hr)) {
        LogErrorThrottled(10, "[D3D11 error] CreateShaderResourceView returned 0x%08X", static_cast<unsigned>(hr));
        static std::atomic<bool> first_error{true};
        if (first_error.exchange(false)) {
            LogD3D11FirstFailure("CreateShaderResourceView", This, hr);
            LogError("[D3D11 error] CreateShaderResourceView first failure — pResource=%p",
                     static_cast<void*>(pResource));
        }
    }
    return hr;
}

HRESULT STDMETHODCALLTYPE CreateRenderTargetView_Detour(ID3D11Device* This, ID3D11Resource* pResource,
                                                        const D3D11_RENDER_TARGET_VIEW_DESC* pDesc,
                                                        ID3D11RenderTargetView** ppRTView) {
    CALL_GUARD(utils::get_now_ns());
    if (CreateRenderTargetView_Original == nullptr) {
        LogError("[D3D11] CreateRenderTargetView_Detour: Original is null, skipping");
        return E_FAIL;
    }
    HRESULT hr = CreateRenderTargetView_Original(This, pResource, pDesc, ppRTView);
    if (FAILED(hr)) {
        LogErrorThrottled(10, "[D3D11 error] CreateRenderTargetView returned 0x%08X", static_cast<unsigned>(hr));
        static std::atomic<bool> first_error{true};
        if (first_error.exchange(false)) {
            LogD3D11FirstFailure("CreateRenderTargetView", This, hr);
            LogError("[D3D11 error] CreateRenderTargetView first failure — pResource=%p",
                     static_cast<void*>(pResource));
        }
    }
    return hr;
}

HRESULT STDMETHODCALLTYPE CreateDepthStencilView_Detour(ID3D11Device* This, ID3D11Resource* pResource,
                                                        const D3D11_DEPTH_STENCIL_VIEW_DESC* pDesc,
                                                        ID3D11DepthStencilView** ppDepthStencilView) {
    CALL_GUARD(utils::get_now_ns());
    if (CreateDepthStencilView_Original == nullptr) {
        LogError("[D3D11] CreateDepthStencilView_Detour: Original is null, skipping");
        return E_FAIL;
    }
    HRESULT hr = CreateDepthStencilView_Original(This, pResource, pDesc, ppDepthStencilView);
    if (FAILED(hr)) {
        LogErrorThrottled(10, "[D3D11 error] CreateDepthStencilView returned 0x%08X", static_cast<unsigned>(hr));
        static std::atomic<bool> first_error{true};
        if (first_error.exchange(false)) {
            LogD3D11FirstFailure("CreateDepthStencilView", This, hr);
            LogError("[D3D11 error] CreateDepthStencilView first failure — pResource=%p",
                     static_cast<void*>(pResource));
        }
    }
    return hr;
}

void InstallD3D11DeviceVtableLogging(ID3D11Device* device) {
    if (device == nullptr) {
        return;
    }
    if (HookSuppressionManager::GetInstance().ShouldSuppressHook(HookType::D3D11_DEVICE)) {
        LogInfo("D3D11 device vtable logging suppressed by user setting");
        return;
    }
    // Install once per process on the first device's vtable. MinHook patches the target function (e.g. in
    // ReShade64.dll or d3d11.dll), so all devices sharing that implementation are hooked. D3D11on12 or
    // multiple devices with different vtables are not hooked; the crash at D3D11On12CreateDevice is unrelated.
    if (g_vtable_logging_installed.exchange(true)) {
        LogInfo("InstallD3D11DeviceVtableLogging: already installed, skipping");
        return;
    }

    MH_STATUS init_status = SafeInitializeMinHook(HookType::D3D11_DEVICE);
    if (init_status != MH_OK && init_status != MH_ERROR_ALREADY_INITIALIZED) {
        LogError("InstallD3D11DeviceVtableLogging: MinHook init failed: %d", init_status);
        g_vtable_logging_installed.store(false);
        return;
    }

    LogInfo("InstallD3D11DeviceVtableLogging: installing for device=%p", static_cast<void*>(device));

    void** vtable = *reinterpret_cast<void***>(device);
    if (vtable == nullptr) {
        g_vtable_logging_installed.store(false);
        LogWarn("InstallD3D11DeviceVtableLogging: failed to get vtable");
        return;
    }
    bool ok = true;
    if (!CreateAndEnableHook(vtable[static_cast<unsigned>(VTable::CreateBuffer)], CreateBuffer_Detour,
                             reinterpret_cast<LPVOID*>(&CreateBuffer_Original), "ID3D11Device::CreateBuffer")) {
        LogWarn("InstallD3D11DeviceVtableLogging: ID3D11Device::CreateBuffer hook failed");
        ok = false;
    }
    if (!CreateAndEnableHook(vtable[static_cast<unsigned>(VTable::CreateTexture1D)], CreateTexture1D_Detour,
                             reinterpret_cast<LPVOID*>(&CreateTexture1D_Original), "ID3D11Device::CreateTexture1D")) {
        LogWarn("InstallD3D11DeviceVtableLogging: ID3D11Device::CreateTexture1D       hook failed");
        ok = false;
    }
    if (!CreateAndEnableHook(vtable[static_cast<unsigned>(VTable::CreateTexture2D)], CreateTexture2D_Detour,
                             reinterpret_cast<LPVOID*>(&CreateTexture2D_Original), "ID3D11Device::CreateTexture2D")) {
        LogWarn("InstallD3D11DeviceVtableLogging: ID3D11Device::CreateTexture2D       hook failed");
        ok = false;
    }
    if (!CreateAndEnableHook(vtable[static_cast<unsigned>(VTable::CreateTexture3D)], CreateTexture3D_Detour,
                             reinterpret_cast<LPVOID*>(&CreateTexture3D_Original), "ID3D11Device::CreateTexture3D")) {
        LogWarn("InstallD3D11DeviceVtableLogging: ID3D11Device::CreateTexture3D       hook failed");
        ok = false;
    }
    if (!CreateAndEnableHook(
            vtable[static_cast<unsigned>(VTable::CreateShaderResourceView)], CreateShaderResourceView_Detour,
            reinterpret_cast<LPVOID*>(&CreateShaderResourceView_Original), "ID3D11Device::CreateShaderResourceView")) {
        LogWarn("InstallD3D11DeviceVtableLogging: ID3D11Device::CreateShaderResourceView hook failed");
        ok = false;
    }
    if (!CreateAndEnableHook(vtable[static_cast<unsigned>(VTable::CreateRenderTargetView)],
                             CreateRenderTargetView_Detour, reinterpret_cast<LPVOID*>(&CreateRenderTargetView_Original),
                             "ID3D11Device::CreateRenderTargetView")) {
        LogWarn("InstallD3D11DeviceVtableLogging: ID3D11Device::CreateRenderTargetView hook failed");
        ok = false;
    }
    if (!CreateAndEnableHook(vtable[static_cast<unsigned>(VTable::CreateDepthStencilView)],
                             CreateDepthStencilView_Detour, reinterpret_cast<LPVOID*>(&CreateDepthStencilView_Original),
                             "ID3D11Device::CreateDepthStencilView")) {
        LogWarn("InstallD3D11DeviceVtableLogging: ID3D11Device::CreateDepthStencilView hook failed");
        ok = false;
    }

    if (ok) {
        LogInfo(
            "InstallD3D11DeviceVtableLogging: device vtable logging installed (CreateBuffer, "
            "CreateTexture1D/2D/3D, "
            "CreateShaderResourceView, CreateRenderTargetView, CreateDepthStencilView)");
        HookSuppressionManager::GetInstance().MarkHookInstalled(HookType::D3D11_DEVICE);
    }
}

}  // namespace

bool HookD3D11Device(ID3D11Device* device) {
    if (device == nullptr) {
        return false;
    }
    static bool hooked = false;
    if (hooked) {
        return true;  // Already hooked
    }
    hooked = true;
    LogInfo("D3D11 device hooked: 0x%p (from ReShade swapchain->get_device()->get_native())", device);
    InstallD3D11DeviceVtableLogging(device);
    return hooked;
}

}  // namespace display_commanderhooks::d3d11
