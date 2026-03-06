// Source Code <Display Commander> // follow this order for includes in all files + add this comment at the top
#include "d3d11_device_hooks.hpp"
#include "../../settings/advanced_tab_settings.hpp"
#include "../../utils/detour_call_tracker.hpp"
#include "../../utils/general_utils.hpp"
#include "../../utils/logging.hpp"
#include "../../utils/texture_tracker.hpp"
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

// IUnknown::Release detours for texture tracking (vtable index 2). Install once per texture type when tracking enabled.
using Release_pfn = ULONG(STDMETHODCALLTYPE*)(IUnknown* This);
Release_pfn Texture1D_Release_Original = nullptr;
Release_pfn Texture2D_Release_Original = nullptr;
Release_pfn Texture3D_Release_Original = nullptr;

static std::atomic<bool> g_texture1d_release_hooked{false};
static std::atomic<bool> g_texture2d_release_hooked{false};
static std::atomic<bool> g_texture3d_release_hooked{false};

// Returns bytes per pixel for uncompressed formats, or bytes per 4x4 block for block-compressed. 0 = unknown.
static size_t GetBytesPerPixelOrBlock(DXGI_FORMAT fmt) {
    switch (fmt) {
        case DXGI_FORMAT_R32G32B32A32_TYPELESS:
        case DXGI_FORMAT_R32G32B32A32_FLOAT:
        case DXGI_FORMAT_R32G32B32A32_UINT:
        case DXGI_FORMAT_R32G32B32A32_SINT:     return 16;
        case DXGI_FORMAT_R32G32B32_TYPELESS:
        case DXGI_FORMAT_R32G32B32_FLOAT:
        case DXGI_FORMAT_R32G32B32_UINT:
        case DXGI_FORMAT_R32G32B32_SINT:        return 12;
        case DXGI_FORMAT_R16G16B16A16_TYPELESS:
        case DXGI_FORMAT_R16G16B16A16_FLOAT:
        case DXGI_FORMAT_R16G16B16A16_UNORM:
        case DXGI_FORMAT_R16G16B16A16_UINT:
        case DXGI_FORMAT_R16G16B16A16_SNORM:
        case DXGI_FORMAT_R16G16B16A16_SINT:
        case DXGI_FORMAT_R32G32_TYPELESS:
        case DXGI_FORMAT_R32G32_FLOAT:
        case DXGI_FORMAT_R32G32_UINT:
        case DXGI_FORMAT_R32G32_SINT:           return 8;
        case DXGI_FORMAT_R10G10B10A2_TYPELESS:
        case DXGI_FORMAT_R10G10B10A2_UNORM:
        case DXGI_FORMAT_R10G10B10A2_UINT:
        case DXGI_FORMAT_R11G11B10_FLOAT:
        case DXGI_FORMAT_R8G8B8A8_TYPELESS:
        case DXGI_FORMAT_R8G8B8A8_UNORM:
        case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
        case DXGI_FORMAT_R8G8B8A8_UINT:
        case DXGI_FORMAT_R8G8B8A8_SNORM:
        case DXGI_FORMAT_R8G8B8A8_SINT:
        case DXGI_FORMAT_R16G16_TYPELESS:
        case DXGI_FORMAT_R16G16_FLOAT:
        case DXGI_FORMAT_R16G16_UNORM:
        case DXGI_FORMAT_R16G16_UINT:
        case DXGI_FORMAT_R16G16_SNORM:
        case DXGI_FORMAT_R16G16_SINT:
        case DXGI_FORMAT_R32_TYPELESS:
        case DXGI_FORMAT_D32_FLOAT:
        case DXGI_FORMAT_R32_FLOAT:
        case DXGI_FORMAT_R32_UINT:
        case DXGI_FORMAT_R32_SINT:              return 4;
        case DXGI_FORMAT_R8G8_TYPELESS:
        case DXGI_FORMAT_R8G8_UNORM:
        case DXGI_FORMAT_R8G8_UINT:
        case DXGI_FORMAT_R8G8_SNORM:
        case DXGI_FORMAT_R8G8_SINT:
        case DXGI_FORMAT_R16_TYPELESS:
        case DXGI_FORMAT_R16_FLOAT:
        case DXGI_FORMAT_D16_UNORM:
        case DXGI_FORMAT_R16_UNORM:
        case DXGI_FORMAT_R16_UINT:
        case DXGI_FORMAT_R16_SNORM:
        case DXGI_FORMAT_R16_SINT:              return 2;
        case DXGI_FORMAT_R8_TYPELESS:
        case DXGI_FORMAT_R8_UNORM:
        case DXGI_FORMAT_R8_UINT:
        case DXGI_FORMAT_R8_SNORM:
        case DXGI_FORMAT_R8_SINT:
        case DXGI_FORMAT_A8_UNORM:              return 1;
        case DXGI_FORMAT_BC1_TYPELESS:
        case DXGI_FORMAT_BC1_UNORM:
        case DXGI_FORMAT_BC1_UNORM_SRGB:
        case DXGI_FORMAT_BC4_TYPELESS:
        case DXGI_FORMAT_BC4_UNORM:
        case DXGI_FORMAT_BC4_SNORM:             return 8;  // 8 bytes per 4x4 block
        case DXGI_FORMAT_BC2_TYPELESS:
        case DXGI_FORMAT_BC2_UNORM:
        case DXGI_FORMAT_BC2_UNORM_SRGB:
        case DXGI_FORMAT_BC3_TYPELESS:
        case DXGI_FORMAT_BC3_UNORM:
        case DXGI_FORMAT_BC3_UNORM_SRGB:
        case DXGI_FORMAT_BC5_TYPELESS:
        case DXGI_FORMAT_BC5_UNORM:
        case DXGI_FORMAT_BC5_SNORM:
        case DXGI_FORMAT_BC6H_TYPELESS:
        case DXGI_FORMAT_BC6H_UF16:
        case DXGI_FORMAT_BC6H_SF16:
        case DXGI_FORMAT_BC7_TYPELESS:
        case DXGI_FORMAT_BC7_UNORM:
        case DXGI_FORMAT_BC7_UNORM_SRGB:        return 16;  // 16 bytes per 4x4 block
        default:                                return 0;
    }
}

static bool IsBlockCompressed(DXGI_FORMAT fmt) {
    switch (fmt) {
        case DXGI_FORMAT_BC1_TYPELESS:
        case DXGI_FORMAT_BC1_UNORM:
        case DXGI_FORMAT_BC1_UNORM_SRGB:
        case DXGI_FORMAT_BC2_TYPELESS:
        case DXGI_FORMAT_BC2_UNORM:
        case DXGI_FORMAT_BC2_UNORM_SRGB:
        case DXGI_FORMAT_BC3_TYPELESS:
        case DXGI_FORMAT_BC3_UNORM:
        case DXGI_FORMAT_BC3_UNORM_SRGB:
        case DXGI_FORMAT_BC4_TYPELESS:
        case DXGI_FORMAT_BC4_UNORM:
        case DXGI_FORMAT_BC4_SNORM:
        case DXGI_FORMAT_BC5_TYPELESS:
        case DXGI_FORMAT_BC5_UNORM:
        case DXGI_FORMAT_BC5_SNORM:
        case DXGI_FORMAT_BC6H_TYPELESS:
        case DXGI_FORMAT_BC6H_UF16:
        case DXGI_FORMAT_BC6H_SF16:
        case DXGI_FORMAT_BC7_TYPELESS:
        case DXGI_FORMAT_BC7_UNORM:
        case DXGI_FORMAT_BC7_UNORM_SRGB: return true;
        default:                         return false;
    }
}

static size_t GetTexture1DSizeBytes(const D3D11_TEXTURE1D_DESC* pDesc) {
    if (pDesc == nullptr) return 0;
    const size_t bpp = GetBytesPerPixelOrBlock(pDesc->Format);
    if (bpp == 0) return 0;
    const UINT mipLevels = pDesc->MipLevels ? pDesc->MipLevels : 1;
    const UINT arraySize = pDesc->ArraySize;
    size_t total = 0;
    UINT w = pDesc->Width;
    for (UINT m = 0; m < mipLevels; ++m) {
        if (w == 0) w = 1;
        total += static_cast<size_t>(w) * bpp;
        w >>= 1;
    }
    return total * arraySize;
}

static size_t GetTexture2DSizeBytes(const D3D11_TEXTURE2D_DESC* pDesc) {
    if (pDesc == nullptr) return 0;
    const size_t bpp = GetBytesPerPixelOrBlock(pDesc->Format);
    if (bpp == 0) return 0;
    const UINT mipLevels = pDesc->MipLevels ? pDesc->MipLevels : 1;
    const UINT arraySize = pDesc->ArraySize;
    const bool compressed = IsBlockCompressed(pDesc->Format);
    size_t total = 0;
    UINT w = pDesc->Width, h = pDesc->Height;
    for (UINT m = 0; m < mipLevels; ++m) {
        if (w == 0) w = 1;
        if (h == 0) h = 1;
        if (compressed) {
            total += ((w + 3) / 4) * ((h + 3) / 4) * bpp;
        } else {
            total += static_cast<size_t>(w) * static_cast<size_t>(h) * bpp;
        }
        w >>= 1;
        h >>= 1;
    }
    return total * arraySize;
}

static size_t GetTexture3DSizeBytes(const D3D11_TEXTURE3D_DESC* pDesc) {
    if (pDesc == nullptr) return 0;
    const size_t bpp = GetBytesPerPixelOrBlock(pDesc->Format);
    if (bpp == 0) return 0;
    const UINT mipLevels = pDesc->MipLevels ? pDesc->MipLevels : 1;
    const bool compressed = IsBlockCompressed(pDesc->Format);
    size_t total = 0;
    UINT w = pDesc->Width, h = pDesc->Height, d = pDesc->Depth;
    for (UINT m = 0; m < mipLevels; ++m) {
        if (w == 0) w = 1;
        if (h == 0) h = 1;
        if (d == 0) d = 1;
        if (compressed) {
            total += ((w + 3) / 4) * ((h + 3) / 4) * ((d + 3) / 4) * bpp;
        } else {
            total += static_cast<size_t>(w) * static_cast<size_t>(h) * static_cast<size_t>(d) * bpp;
        }
        w >>= 1;
        h >>= 1;
        d >>= 1;
    }
    return total;
}

static void TryInstallTextureReleaseHook(void* texture_vtable, Release_pfn* pOriginal, std::atomic<bool>* pHooked,
                                         ULONG(STDMETHODCALLTYPE* detour)(IUnknown*), const char* name) {
    if (texture_vtable == nullptr || pOriginal == nullptr || pHooked == nullptr) return;
    if (pHooked->exchange(true)) return;  // Already hooked
    void** vtable = static_cast<void**>(texture_vtable);
    void* releaseSlot = vtable[2];  // IUnknown::Release
    if (releaseSlot == nullptr) {
        pHooked->store(false);
        return;
    }
    if (!CreateAndEnableHook(releaseSlot, reinterpret_cast<LPVOID>(detour), reinterpret_cast<LPVOID*>(pOriginal),
                             name)) {
        LogWarn("Texture tracking: failed to hook %s", name);
        pHooked->store(false);
    }
}

ULONG STDMETHODCALLTYPE Texture1D_Release_Detour(IUnknown* This) {
    const ULONG ref = Texture1D_Release_Original != nullptr ? Texture1D_Release_Original(This) : 0;
    if (ref == 0 && This != nullptr) {
        utils::TextureTrackerRemove(static_cast<void*>(This));
    }
    return ref;
}

ULONG STDMETHODCALLTYPE Texture2D_Release_Detour(IUnknown* This) {
    const ULONG ref = Texture2D_Release_Original != nullptr ? Texture2D_Release_Original(This) : 0;
    if (ref == 0 && This != nullptr) {
        utils::TextureTrackerRemove(static_cast<void*>(This));
    }
    return ref;
}

ULONG STDMETHODCALLTYPE Texture3D_Release_Detour(IUnknown* This) {
    const ULONG ref = Texture3D_Release_Original != nullptr ? Texture3D_Release_Original(This) : 0;
    if (ref == 0 && This != nullptr) {
        utils::TextureTrackerRemove(static_cast<void*>(This));
    }
    return ref;
}

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
    if (SUCCEEDED(hr) && ppTexture1D != nullptr && *ppTexture1D != nullptr
        && settings::g_advancedTabSettings.texture_tracking_enabled.GetValue()) {
        const size_t size_bytes = GetTexture1DSizeBytes(pDesc);
        if (size_bytes > 0) {
            utils::TextureTrackerAdd(static_cast<void*>(*ppTexture1D), size_bytes);
            TryInstallTextureReleaseHook(*reinterpret_cast<void**>(*ppTexture1D), &Texture1D_Release_Original,
                                         &g_texture1d_release_hooked, Texture1D_Release_Detour,
                                         "ID3D11Texture1D::Release");
        }
    }
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
    if (SUCCEEDED(hr) && ppTexture2D != nullptr && *ppTexture2D != nullptr
        && settings::g_advancedTabSettings.texture_tracking_enabled.GetValue()) {
        const size_t size_bytes = GetTexture2DSizeBytes(pDesc);
        if (size_bytes > 0) {
            utils::TextureTrackerAdd(static_cast<void*>(*ppTexture2D), size_bytes);
            TryInstallTextureReleaseHook(*reinterpret_cast<void**>(*ppTexture2D), &Texture2D_Release_Original,
                                         &g_texture2d_release_hooked, Texture2D_Release_Detour,
                                         "ID3D11Texture2D::Release");
        }
    }
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
    if (SUCCEEDED(hr) && ppTexture3D != nullptr && *ppTexture3D != nullptr
        && settings::g_advancedTabSettings.texture_tracking_enabled.GetValue()) {
        const size_t size_bytes = GetTexture3DSizeBytes(pDesc);
        if (size_bytes > 0) {
            utils::TextureTrackerAdd(static_cast<void*>(*ppTexture3D), size_bytes);
            TryInstallTextureReleaseHook(*reinterpret_cast<void**>(*ppTexture3D), &Texture3D_Release_Original,
                                         &g_texture3d_release_hooked, Texture3D_Release_Detour,
                                         "ID3D11Texture3D::Release");
        }
    }
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
