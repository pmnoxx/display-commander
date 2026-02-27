#include "d3d9_device_vtable_logging.hpp"
#include "d3d9_vtable_indices.hpp"
#include "../../utils/detour_call_tracker.hpp"
#include "../../utils/general_utils.hpp"
#include "../../utils/logging.hpp"
#include "../../utils/timing.hpp"

#include <d3d9.h>
#include <MinHook.h>
#include <atomic>

namespace display_commanderhooks::d3d9 {

namespace {
std::atomic<bool> g_vtable_logging_installed{false};

// Original function pointers (one per hooked method)
using CreateTexture_pfn = HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9* This, UINT Width, UINT Height, UINT Levels,
                                                      DWORD Usage, D3DFORMAT Format, D3DPOOL Pool,
                                                      IDirect3DTexture9** ppTexture, HANDLE* pSharedHandle);
CreateTexture_pfn CreateTexture_Original = nullptr;

using CreateVolumeTexture_pfn = HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9* This, UINT Width, UINT Height, UINT Depth,
                                                            UINT Levels, DWORD Usage, D3DFORMAT Format, D3DPOOL Pool,
                                                            IDirect3DVolumeTexture9** ppVolumeTexture,
                                                            HANDLE* pSharedHandle);
CreateVolumeTexture_pfn CreateVolumeTexture_Original = nullptr;

using CreateCubeTexture_pfn = HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9* This, UINT EdgeLength, UINT Levels,
                                                          DWORD Usage, D3DFORMAT Format, D3DPOOL Pool,
                                                          IDirect3DCubeTexture9** ppCubeTexture, HANDLE* pSharedHandle);
CreateCubeTexture_pfn CreateCubeTexture_Original = nullptr;

using CreateVertexBuffer_pfn = HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9* This, UINT Length, DWORD Usage, DWORD FVF,
                                                           D3DPOOL Pool, IDirect3DVertexBuffer9** ppVertexBuffer,
                                                           HANDLE* pSharedHandle);
CreateVertexBuffer_pfn CreateVertexBuffer_Original = nullptr;

using CreateIndexBuffer_pfn = HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9* This, UINT Length, DWORD Usage,
                                                          D3DFORMAT Format, D3DPOOL Pool,
                                                          IDirect3DIndexBuffer9** ppIndexBuffer, HANDLE* pSharedHandle);
CreateIndexBuffer_pfn CreateIndexBuffer_Original = nullptr;

using CreateOffscreenPlainSurface_pfn = HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9* This, UINT Width, UINT Height,
                                                                    D3DFORMAT Format, D3DPOOL Pool,
                                                                    IDirect3DSurface9** ppSurface,
                                                                    HANDLE* pSharedHandle);
CreateOffscreenPlainSurface_pfn CreateOffscreenPlainSurface_Original = nullptr;

using CreateRenderTarget_pfn = HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9* This, UINT Width, UINT Height,
                                                           D3DFORMAT Format, D3DMULTISAMPLE_TYPE MultiSample,
                                                           DWORD MultisampleQuality, BOOL Lockable,
                                                           IDirect3DSurface9** ppSurface, HANDLE* pSharedHandle);
CreateRenderTarget_pfn CreateRenderTarget_Original = nullptr;

using CreateDepthStencilSurface_pfn = HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9* This, UINT Width, UINT Height,
                                                                  D3DFORMAT Format, D3DMULTISAMPLE_TYPE MultiSample,
                                                                  DWORD MultisampleQuality, BOOL Discard,
                                                                  IDirect3DSurface9** ppSurface, HANDLE* pSharedHandle);
CreateDepthStencilSurface_pfn CreateDepthStencilSurface_Original = nullptr;

void LogD3D9Error(const char* method, HRESULT hr) {
    LogError("[D3D9 error] %s returned 0x%08X", method, static_cast<unsigned>(hr));
}

// First-call flags (one per detour)
static std::atomic<bool> g_first_CreateTexture{true};
static std::atomic<bool> g_first_CreateVolumeTexture{true};
static std::atomic<bool> g_first_CreateCubeTexture{true};
static std::atomic<bool> g_first_CreateVertexBuffer{true};
static std::atomic<bool> g_first_CreateIndexBuffer{true};
static std::atomic<bool> g_first_CreateOffscreenPlainSurface{true};
static std::atomic<bool> g_first_CreateRenderTarget{true};
static std::atomic<bool> g_first_CreateDepthStencilSurface{true};

}  // namespace

// Detours: RECORD_DETOUR_CALL, log first call, call original, log on error.

static HRESULT STDMETHODCALLTYPE CreateTexture_Detour(IDirect3DDevice9* This, UINT Width, UINT Height, UINT Levels,
                                                      DWORD Usage, D3DFORMAT Format, D3DPOOL Pool,
                                                      IDirect3DTexture9** ppTexture, HANDLE* pSharedHandle) {
    RECORD_DETOUR_CALL(utils::get_now_ns());
    if (g_first_CreateTexture.exchange(false)) {
        LogInfo("[D3D9] First call: IDirect3DDevice9::CreateTexture");
    }
    HRESULT hr = CreateTexture_Original(This, Width, Height, Levels, Usage, Format, Pool, ppTexture, pSharedHandle);
    if (FAILED(hr)) {
        LogD3D9Error("CreateTexture", hr);
    }
    return hr;
}

static HRESULT STDMETHODCALLTYPE CreateVolumeTexture_Detour(IDirect3DDevice9* This, UINT Width, UINT Height, UINT Depth,
                                                            UINT Levels, DWORD Usage, D3DFORMAT Format, D3DPOOL Pool,
                                                            IDirect3DVolumeTexture9** ppVolumeTexture,
                                                            HANDLE* pSharedHandle) {
    RECORD_DETOUR_CALL(utils::get_now_ns());
    if (g_first_CreateVolumeTexture.exchange(false)) {
        LogInfo("[D3D9] First call: IDirect3DDevice9::CreateVolumeTexture");
    }
    HRESULT hr = CreateVolumeTexture_Original(This, Width, Height, Depth, Levels, Usage, Format, Pool, ppVolumeTexture,
                                              pSharedHandle);
    if (FAILED(hr)) {
        LogD3D9Error("CreateVolumeTexture", hr);
    }
    return hr;
}

static HRESULT STDMETHODCALLTYPE CreateCubeTexture_Detour(IDirect3DDevice9* This, UINT EdgeLength, UINT Levels,
                                                          DWORD Usage, D3DFORMAT Format, D3DPOOL Pool,
                                                          IDirect3DCubeTexture9** ppCubeTexture,
                                                          HANDLE* pSharedHandle) {
    RECORD_DETOUR_CALL(utils::get_now_ns());
    if (g_first_CreateCubeTexture.exchange(false)) {
        LogInfo("[D3D9] First call: IDirect3DDevice9::CreateCubeTexture");
    }
    HRESULT hr =
        CreateCubeTexture_Original(This, EdgeLength, Levels, Usage, Format, Pool, ppCubeTexture, pSharedHandle);
    if (FAILED(hr)) {
        LogD3D9Error("CreateCubeTexture", hr);
    }
    return hr;
}

static HRESULT STDMETHODCALLTYPE CreateVertexBuffer_Detour(IDirect3DDevice9* This, UINT Length, DWORD Usage, DWORD FVF,
                                                           D3DPOOL Pool, IDirect3DVertexBuffer9** ppVertexBuffer,
                                                           HANDLE* pSharedHandle) {
    RECORD_DETOUR_CALL(utils::get_now_ns());
    if (g_first_CreateVertexBuffer.exchange(false)) {
        LogInfo("[D3D9] First call: IDirect3DDevice9::CreateVertexBuffer");
    }
    HRESULT hr = CreateVertexBuffer_Original(This, Length, Usage, FVF, Pool, ppVertexBuffer, pSharedHandle);
    if (FAILED(hr)) {
        LogD3D9Error("CreateVertexBuffer", hr);
    }
    return hr;
}

static HRESULT STDMETHODCALLTYPE CreateIndexBuffer_Detour(IDirect3DDevice9* This, UINT Length, DWORD Usage,
                                                          D3DFORMAT Format, D3DPOOL Pool,
                                                          IDirect3DIndexBuffer9** ppIndexBuffer,
                                                          HANDLE* pSharedHandle) {
    RECORD_DETOUR_CALL(utils::get_now_ns());
    if (g_first_CreateIndexBuffer.exchange(false)) {
        LogInfo("[D3D9] First call: IDirect3DDevice9::CreateIndexBuffer");
    }
    HRESULT hr = CreateIndexBuffer_Original(This, Length, Usage, Format, Pool, ppIndexBuffer, pSharedHandle);
    if (FAILED(hr)) {
        LogD3D9Error("CreateIndexBuffer", hr);
    }
    return hr;
}

static HRESULT STDMETHODCALLTYPE CreateOffscreenPlainSurface_Detour(IDirect3DDevice9* This, UINT Width, UINT Height,
                                                                    D3DFORMAT Format, D3DPOOL Pool,
                                                                    IDirect3DSurface9** ppSurface,
                                                                    HANDLE* pSharedHandle) {
    RECORD_DETOUR_CALL(utils::get_now_ns());
    if (g_first_CreateOffscreenPlainSurface.exchange(false)) {
        LogInfo("[D3D9] First call: IDirect3DDevice9::CreateOffscreenPlainSurface");
    }
    LogInfo("IDirect3DDevice9::CreateOffscreenPlainSurface(Width=%u, Height=%u, Format=%u, Pool=%u)", Width, Height,
            static_cast<unsigned>(Format), static_cast<unsigned>(Pool));
    HRESULT hr = CreateOffscreenPlainSurface_Original(This, Width, Height, Format, Pool, ppSurface, pSharedHandle);
    if (FAILED(hr)) {
        LogD3D9Error("CreateOffscreenPlainSurface", hr);
    }
    return hr;
}

static HRESULT STDMETHODCALLTYPE CreateRenderTarget_Detour(IDirect3DDevice9* This, UINT Width, UINT Height,
                                                           D3DFORMAT Format, D3DMULTISAMPLE_TYPE MultiSample,
                                                           DWORD MultisampleQuality, BOOL Lockable,
                                                           IDirect3DSurface9** ppSurface, HANDLE* pSharedHandle) {
    RECORD_DETOUR_CALL(utils::get_now_ns());
    if (g_first_CreateRenderTarget.exchange(false)) {
        LogInfo("[D3D9] First call: IDirect3DDevice9::CreateRenderTarget");
    }
    LogInfo("IDirect3DDevice9::CreateRenderTarget(Width=%u, Height=%u, Format=%u)", Width, Height,
            static_cast<unsigned>(Format));
    HRESULT hr = CreateRenderTarget_Original(This, Width, Height, Format, MultiSample, MultisampleQuality, Lockable,
                                             ppSurface, pSharedHandle);
    if (FAILED(hr)) {
        LogD3D9Error("CreateRenderTarget", hr);
    }
    return hr;
}

static HRESULT STDMETHODCALLTYPE CreateDepthStencilSurface_Detour(IDirect3DDevice9* This, UINT Width, UINT Height,
                                                                  D3DFORMAT Format, D3DMULTISAMPLE_TYPE MultiSample,
                                                                  DWORD MultisampleQuality, BOOL Discard,
                                                                  IDirect3DSurface9** ppSurface,
                                                                  HANDLE* pSharedHandle) {
    RECORD_DETOUR_CALL(utils::get_now_ns());
    if (g_first_CreateDepthStencilSurface.exchange(false)) {
        LogInfo("[D3D9] First call: IDirect3DDevice9::CreateDepthStencilSurface");
    }
    LogInfo("IDirect3DDevice9::CreateDepthStencilSurface(Width=%u, Height=%u, Format=%u)", Width, Height,
            static_cast<unsigned>(Format));
    HRESULT hr = CreateDepthStencilSurface_Original(This, Width, Height, Format, MultiSample, MultisampleQuality,
                                                    Discard, ppSurface, pSharedHandle);
    if (FAILED(hr)) {
        LogD3D9Error("CreateDepthStencilSurface", hr);
    }
    return hr;
}

void InstallD3D9DeviceVtableLogging(IDirect3DDevice9* device) {
    if (device == nullptr) {
        return;
    }
    if (g_vtable_logging_installed.exchange(true)) {
        LogInfo("InstallD3D9DeviceVtableLogging: already installed, skipping");
        return;
    }

    void** vtable = *reinterpret_cast<void***>(device);
    if (vtable == nullptr) {
        g_vtable_logging_installed.store(false);
        LogWarn("InstallD3D9DeviceVtableLogging: failed to get vtable");
        return;
    }

    bool ok = true;
    if (!CreateAndEnableHook(vtable[VTable::CreateTexture], reinterpret_cast<LPVOID>(&CreateTexture_Detour),
                             reinterpret_cast<LPVOID*>(&CreateTexture_Original), "IDirect3DDevice9::CreateTexture")) {
        LogWarn("InstallD3D9DeviceVtableLogging: CreateTexture hook failed");
        ok = false;
    }
    if (!CreateAndEnableHook(vtable[VTable::CreateVolumeTexture], reinterpret_cast<LPVOID>(&CreateVolumeTexture_Detour),
                             reinterpret_cast<LPVOID*>(&CreateVolumeTexture_Original),
                             "IDirect3DDevice9::CreateVolumeTexture")) {
        LogWarn("InstallD3D9DeviceVtableLogging: CreateVolumeTexture hook failed");
        ok = false;
    }
    if (!CreateAndEnableHook(vtable[VTable::CreateCubeTexture], reinterpret_cast<LPVOID>(&CreateCubeTexture_Detour),
                             reinterpret_cast<LPVOID*>(&CreateCubeTexture_Original),
                             "IDirect3DDevice9::CreateCubeTexture")) {
        LogWarn("InstallD3D9DeviceVtableLogging: CreateCubeTexture hook failed");
        ok = false;
    }
    if (!CreateAndEnableHook(vtable[VTable::CreateVertexBuffer], reinterpret_cast<LPVOID>(&CreateVertexBuffer_Detour),
                             reinterpret_cast<LPVOID*>(&CreateVertexBuffer_Original),
                             "IDirect3DDevice9::CreateVertexBuffer")) {
        LogWarn("InstallD3D9DeviceVtableLogging: CreateVertexBuffer hook failed");
        ok = false;
    }
    if (!CreateAndEnableHook(vtable[VTable::CreateIndexBuffer], reinterpret_cast<LPVOID>(&CreateIndexBuffer_Detour),
                             reinterpret_cast<LPVOID*>(&CreateIndexBuffer_Original),
                             "IDirect3DDevice9::CreateIndexBuffer")) {
        LogWarn("InstallD3D9DeviceVtableLogging: CreateIndexBuffer hook failed");
        ok = false;
    }
    if (!CreateAndEnableHook(vtable[VTable::CreateOffscreenPlainSurface],
                             reinterpret_cast<LPVOID>(&CreateOffscreenPlainSurface_Detour),
                             reinterpret_cast<LPVOID*>(&CreateOffscreenPlainSurface_Original),
                             "IDirect3DDevice9::CreateOffscreenPlainSurface")) {
        LogWarn("InstallD3D9DeviceVtableLogging: CreateOffscreenPlainSurface hook failed");
        ok = false;
    }
    if (!CreateAndEnableHook(vtable[VTable::CreateRenderTarget], reinterpret_cast<LPVOID>(&CreateRenderTarget_Detour),
                             reinterpret_cast<LPVOID*>(&CreateRenderTarget_Original),
                             "IDirect3DDevice9::CreateRenderTarget")) {
        LogWarn("InstallD3D9DeviceVtableLogging: CreateRenderTarget hook failed");
        ok = false;
    }
    if (!CreateAndEnableHook(vtable[VTable::CreateDepthStencilSurface],
                             reinterpret_cast<LPVOID>(&CreateDepthStencilSurface_Detour),
                             reinterpret_cast<LPVOID*>(&CreateDepthStencilSurface_Original),
                             "IDirect3DDevice9::CreateDepthStencilSurface")) {
        LogWarn("InstallD3D9DeviceVtableLogging: CreateDepthStencilSurface hook failed");
        ok = false;
    }

    if (ok) {
        LogInfo(
            "InstallD3D9DeviceVtableLogging: device vtable logging installed (CreateTexture, CreateVolumeTexture, "
            "CreateCubeTexture, CreateVertexBuffer, CreateIndexBuffer, CreateRenderTarget, CreateDepthStencilSurface, "
            "CreateOffscreenPlainSurface)");
    }
}

}  // namespace display_commanderhooks::d3d9
