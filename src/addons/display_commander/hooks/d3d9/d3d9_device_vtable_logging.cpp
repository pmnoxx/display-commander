#include "d3d9_device_vtable_logging.hpp"
#include "d3d9_pool_upgrade.hpp"
#include "d3d9_vtable_indices.hpp"
#include "../../utils/detour_call_tracker.hpp"
#include "../../utils/general_utils.hpp"
#include "../../utils/logging.hpp"
#include "../../utils/timing.hpp"

#include <d3d9.h>
#include <MinHook.h>
#include <atomic>
#include <cstdint>

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

using Reset_pfn = HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9* This,
                                             D3DPRESENT_PARAMETERS* pPresentationParameters);
Reset_pfn Reset_Original = nullptr;

using BeginScene_pfn = HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9* This);
BeginScene_pfn BeginScene_Original = nullptr;

using EndScene_pfn = HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9* This);
EndScene_pfn EndScene_Original = nullptr;

using Clear_pfn = HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9* This, DWORD Count, const D3DRECT* pRects,
                                              DWORD Flags, D3DCOLOR Color, float Z, DWORD Stencil);
Clear_pfn Clear_Original = nullptr;

using CreateAdditionalSwapChain_pfn = HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9* This,
                                                                   D3DPRESENT_PARAMETERS* pPresentationParameters,
                                                                   IDirect3DSwapChain9** ppSwapChain);
CreateAdditionalSwapChain_pfn CreateAdditionalSwapChain_Original = nullptr;

using GetBackBuffer_pfn = HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9* This, UINT iSwapChain, UINT iBackBuffer,
                                                      D3DBACKBUFFER_TYPE Type, IDirect3DSurface9** ppBackBuffer);
GetBackBuffer_pfn GetBackBuffer_Original = nullptr;

using SetRenderTarget_pfn = HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9* This, DWORD RenderTargetIndex,
                                                        IDirect3DSurface9* pRenderTarget);
SetRenderTarget_pfn SetRenderTarget_Original = nullptr;

using SetDepthStencilSurface_pfn = HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9* This,
                                                               IDirect3DSurface9* pNewZStencil);
SetDepthStencilSurface_pfn SetDepthStencilSurface_Original = nullptr;

using CreateStateBlock_pfn = HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9* This, D3DSTATEBLOCKTYPE Type,
                                                         IDirect3DStateBlock9** ppSB);
CreateStateBlock_pfn CreateStateBlock_Original = nullptr;

using EndStateBlock_pfn = HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9* This, IDirect3DStateBlock9** ppSB);
EndStateBlock_pfn EndStateBlock_Original = nullptr;

using CreateVertexDeclaration_pfn = HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9* This,
                                                                 const D3DVERTEXELEMENT9* pVertexElements,
                                                                 IDirect3DVertexDeclaration9** ppDecl);
CreateVertexDeclaration_pfn CreateVertexDeclaration_Original = nullptr;

using CreateVertexShader_pfn = HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9* This, const DWORD* pFunction,
                                                            IDirect3DVertexShader9** ppShader);
CreateVertexShader_pfn CreateVertexShader_Original = nullptr;

using SetStreamSource_pfn = HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9* This, UINT StreamNumber,
                                                         IDirect3DVertexBuffer9* pStreamData, UINT OffsetInBytes,
                                                         UINT Stride);
SetStreamSource_pfn SetStreamSource_Original = nullptr;

using SetIndices_pfn = HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9* This,
                                                   IDirect3DIndexBuffer9* pIndexData);
SetIndices_pfn SetIndices_Original = nullptr;

using CreatePixelShader_pfn = HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9* This, const DWORD* pFunction,
                                                           IDirect3DPixelShader9** ppShader);
CreatePixelShader_pfn CreatePixelShader_Original = nullptr;

using TestCooperativeLevel_pfn = HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9* This);
TestCooperativeLevel_pfn TestCooperativeLevel_Original = nullptr;

using GetSwapChain_pfn = HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9* This, UINT iSwapChain,
                                                      IDirect3DSwapChain9** ppSwapChain);
GetSwapChain_pfn GetSwapChain_Original = nullptr;

using UpdateSurface_pfn = HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9* This, IDirect3DSurface9* pSourceSurface,
                                                      const RECT* pSourceRect, IDirect3DSurface9* pDestinationSurface,
                                                      const POINT* pDestPoint);
UpdateSurface_pfn UpdateSurface_Original = nullptr;

using UpdateTexture_pfn = HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9* This,
                                                       IDirect3DBaseTexture9* pSourceTexture,
                                                       IDirect3DBaseTexture9* pDestinationTexture);
UpdateTexture_pfn UpdateTexture_Original = nullptr;

using GetRenderTargetData_pfn = HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9* This,
                                                             IDirect3DSurface9* pRenderTarget,
                                                             IDirect3DSurface9* pDestSurface);
GetRenderTargetData_pfn GetRenderTargetData_Original = nullptr;

using GetFrontBufferData_pfn = HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9* This, UINT iSwapChain,
                                                            IDirect3DSurface9* pDestSurface);
GetFrontBufferData_pfn GetFrontBufferData_Original = nullptr;

using StretchRect_pfn = HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9* This, IDirect3DSurface9* pSourceSurface,
                                                    const RECT* pSourceRect, IDirect3DSurface9* pDestSurface,
                                                    const RECT* pDestRect, D3DTEXTUREFILTERTYPE Filter);
StretchRect_pfn StretchRect_Original = nullptr;

using ColorFill_pfn = HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9* This, IDirect3DSurface9* pSurface,
                                                   const RECT* pRect, D3DCOLOR color);
ColorFill_pfn ColorFill_Original = nullptr;

using BeginStateBlock_pfn = HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9* This);
BeginStateBlock_pfn BeginStateBlock_Original = nullptr;

using CreateQuery_pfn = HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9* This, D3DQUERYTYPE Type,
                                                     IDirect3DQuery9** ppQuery);
CreateQuery_pfn CreateQuery_Original = nullptr;

using DrawPrimitive_pfn = HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9* This,
                                                       D3DPRIMITIVETYPE PrimitiveType, UINT StartVertex,
                                                       UINT PrimitiveCount);
DrawPrimitive_pfn DrawPrimitive_Original = nullptr;

using DrawIndexedPrimitive_pfn = HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9* This,
                                                              D3DPRIMITIVETYPE PrimitiveType, INT BaseVertexIndex,
                                                              UINT MinVertexIndex, UINT NumVertices, UINT startIndex,
                                                              UINT primCount);
DrawIndexedPrimitive_pfn DrawIndexedPrimitive_Original = nullptr;

using DrawPrimitiveUP_pfn = HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9* This,
                                                         D3DPRIMITIVETYPE PrimitiveType, UINT PrimitiveCount,
                                                         const void* pVertexStreamZeroData,
                                                         UINT VertexStreamZeroStride);
DrawPrimitiveUP_pfn DrawPrimitiveUP_Original = nullptr;

using DrawIndexedPrimitiveUP_pfn = HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9* This,
                                                               D3DPRIMITIVETYPE PrimitiveType, UINT MinVertexIndex,
                                                               UINT NumVertices, UINT PrimitiveCount,
                                                               const void* pIndexData, D3DFORMAT IndexDataFormat,
                                                               const void* pVertexStreamZeroData,
                                                               UINT VertexStreamZeroStride);
DrawIndexedPrimitiveUP_pfn DrawIndexedPrimitiveUP_Original = nullptr;

using ProcessVertices_pfn = HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9* This, UINT SrcStartIndex, UINT DestIndex,
                                                         UINT VertexCount, IDirect3DVertexBuffer9* pDestBuffer,
                                                         IDirect3DVertexDeclaration9* pVertexDecl, DWORD Flags);
ProcessVertices_pfn ProcessVertices_Original = nullptr;

using SetVertexDeclaration_pfn = HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9* This,
                                                             IDirect3DVertexDeclaration9* pDecl);
SetVertexDeclaration_pfn SetVertexDeclaration_Original = nullptr;

using SetFVF_pfn = HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9* This, DWORD FVF);
SetFVF_pfn SetFVF_Original = nullptr;

using SetStreamSourceFreq_pfn = HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9* This, UINT StreamNumber, UINT Divider);
SetStreamSourceFreq_pfn SetStreamSourceFreq_Original = nullptr;

using GetRenderTarget_pfn = HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9* This, DWORD RenderTargetIndex,
                                                        IDirect3DSurface9** ppRenderTarget);
GetRenderTarget_pfn GetRenderTarget_Original = nullptr;

using GetDepthStencilSurface_pfn = HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9* This,
                                                                IDirect3DSurface9** ppZStencilSurface);
GetDepthStencilSurface_pfn GetDepthStencilSurface_Original = nullptr;

using SetViewport_pfn = HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9* This, const D3DVIEWPORT9* pViewport);
SetViewport_pfn SetViewport_Original = nullptr;

using SetTransform_pfn = HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9* This, D3DTRANSFORMSTATETYPE State,
                                                     const D3DMATRIX* pMatrix);
SetTransform_pfn SetTransform_Original = nullptr;

using SetRenderState_pfn = HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9* This, D3DRENDERSTATETYPE State, DWORD Value);
SetRenderState_pfn SetRenderState_Original = nullptr;

using GetTexture_pfn = HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9* This, DWORD Stage,
                                                    IDirect3DBaseTexture9** ppTexture);
GetTexture_pfn GetTexture_Original = nullptr;

using SetTexture_pfn = HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9* This, DWORD Stage,
                                                    IDirect3DBaseTexture9* pTexture);
SetTexture_pfn SetTexture_Original = nullptr;

using SetVertexShader_pfn = HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9* This,
                                                         IDirect3DVertexShader9* pShader);
SetVertexShader_pfn SetVertexShader_Original = nullptr;

using SetPixelShader_pfn = HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9* This, IDirect3DPixelShader9* pShader);
SetPixelShader_pfn SetPixelShader_Original = nullptr;

// IDirect3DDevice9Ex (vtable 119+)
using CreateRenderTargetEx_pfn = HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9* This, UINT Width, UINT Height,
                                                              D3DFORMAT Format, D3DMULTISAMPLE_TYPE MultiSample,
                                                              DWORD MultisampleQuality, BOOL Lockable,
                                                              IDirect3DSurface9** ppSurface, HANDLE* pSharedHandle,
                                                              DWORD Usage);
CreateRenderTargetEx_pfn CreateRenderTargetEx_Original = nullptr;

using CreateOffscreenPlainSurfaceEx_pfn = HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9* This, UINT Width, UINT Height,
                                                                       D3DFORMAT Format, D3DPOOL Pool,
                                                                       IDirect3DSurface9** ppSurface,
                                                                       HANDLE* pSharedHandle, DWORD Usage);
CreateOffscreenPlainSurfaceEx_pfn CreateOffscreenPlainSurfaceEx_Original = nullptr;

using CreateDepthStencilSurfaceEx_pfn = HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9* This, UINT Width, UINT Height,
                                                                     D3DFORMAT Format,
                                                                     D3DMULTISAMPLE_TYPE MultiSample,
                                                                     DWORD MultisampleQuality, BOOL Discard,
                                                                     IDirect3DSurface9** ppSurface,
                                                                     HANDLE* pSharedHandle, DWORD Usage);
CreateDepthStencilSurfaceEx_pfn CreateDepthStencilSurfaceEx_Original = nullptr;

using ResetEx_pfn = HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9* This,
                                                D3DPRESENT_PARAMETERS* pPresentationParameters,
                                                D3DDISPLAYMODEEX* pFullscreenDisplayMode);
ResetEx_pfn ResetEx_Original = nullptr;

using GetDisplayModeEx_pfn = HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9* This, UINT iSwapChain,
                                                          D3DDISPLAYMODEEX* pMode,
                                                          D3DDISPLAYROTATION* pRotation);
GetDisplayModeEx_pfn GetDisplayModeEx_Original = nullptr;

using CheckDeviceState_pfn = HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9* This, HWND hDestinationWindow);
CheckDeviceState_pfn CheckDeviceState_Original = nullptr;

void LogD3D9Error(const char* method, HRESULT hr) {
    LogError("[D3D9 error] %s returned 0x%08X", method, static_cast<unsigned>(hr));
}

// Returns a known D3D9 HRESULT name or nullptr (caller can fall back to hex).
static const char* D3D9HResultName(HRESULT hr) {
    switch (static_cast<unsigned>(hr)) {
        case 0x8876017C: return "D3DERR_OUTOFVIDEOMEMORY";
        case 0x88760868: return "D3DERR_DEVICELOST";
        case 0x8876086A: return "D3DERR_NOTAVAILABLE";
        case 0x8876086C: return "D3DERR_INVALIDCALL";
        case 0x88760870: return "D3DERR_DEVICEREMOVED";
        case 0x88760874: return "D3DERR_DEVICEHUNG";
        default: return nullptr;
    }
}

// Log first failure with HRESULT name (for methods with few/no interesting args).
static void LogD3D9FirstFailure(const char* method, IDirect3DDevice9* This, HRESULT hr) {
    const char* hr_name = D3D9HResultName(hr);
    const bool has_hr_name = (hr_name != nullptr);
    LogError("[D3D9 error] %s first failure — This=%p hr=0x%08X%s%s%s", method, static_cast<void*>(This),
             static_cast<unsigned>(hr), has_hr_name ? " (" : "", has_hr_name ? hr_name : "", has_hr_name ? ")" : "");
}

// First-call flags (one per detour)
static std::atomic<bool> g_first_CreateTexture{true};
static std::atomic<bool> g_first_CreateTexture_error{true};
static std::atomic<bool> g_first_CreateVolumeTexture{true};
static std::atomic<bool> g_first_CreateVolumeTexture_error{true};
static std::atomic<bool> g_first_CreateCubeTexture{true};
static std::atomic<bool> g_first_CreateCubeTexture_error{true};
static std::atomic<bool> g_first_CreateVertexBuffer{true};
static std::atomic<bool> g_first_CreateVertexBuffer_error{true};
static std::atomic<bool> g_first_CreateIndexBuffer{true};
static std::atomic<bool> g_first_CreateIndexBuffer_error{true};
static std::atomic<bool> g_first_CreateOffscreenPlainSurface{true};
static std::atomic<bool> g_first_CreateOffscreenPlainSurface_error{true};
static std::atomic<bool> g_first_CreateRenderTarget{true};
static std::atomic<bool> g_first_CreateRenderTarget_error{true};
static std::atomic<bool> g_first_CreateDepthStencilSurface{true};
static std::atomic<bool> g_first_CreateDepthStencilSurface_error{true};
static std::atomic<bool> g_first_Reset_error{true};
static std::atomic<bool> g_first_BeginScene_error{true};
static std::atomic<bool> g_first_EndScene_error{true};
static std::atomic<bool> g_first_Clear_error{true};
static std::atomic<bool> g_first_CreateAdditionalSwapChain_error{true};
static std::atomic<bool> g_first_GetBackBuffer_error{true};
static std::atomic<bool> g_first_SetRenderTarget_error{true};
static std::atomic<bool> g_first_SetDepthStencilSurface_error{true};
static std::atomic<bool> g_first_CreateStateBlock_error{true};
static std::atomic<bool> g_first_EndStateBlock_error{true};
static std::atomic<bool> g_first_CreateVertexDeclaration_error{true};
static std::atomic<bool> g_first_CreateVertexShader_error{true};
static std::atomic<bool> g_first_SetStreamSource_error{true};
static std::atomic<bool> g_first_SetIndices_error{true};
static std::atomic<bool> g_first_CreatePixelShader_error{true};
static std::atomic<bool> g_first_TestCooperativeLevel_error{true};
static std::atomic<bool> g_first_GetSwapChain_error{true};
static std::atomic<bool> g_first_UpdateSurface_error{true};
static std::atomic<bool> g_first_UpdateTexture_error{true};
static std::atomic<bool> g_first_GetRenderTargetData_error{true};
static std::atomic<bool> g_first_GetFrontBufferData_error{true};
static std::atomic<bool> g_first_StretchRect_error{true};
static std::atomic<bool> g_first_ColorFill_error{true};
static std::atomic<bool> g_first_BeginStateBlock_error{true};
static std::atomic<bool> g_first_CreateQuery_error{true};
static std::atomic<bool> g_first_DrawPrimitive_error{true};
static std::atomic<bool> g_first_DrawIndexedPrimitive_error{true};
static std::atomic<bool> g_first_DrawPrimitiveUP_error{true};
static std::atomic<bool> g_first_DrawIndexedPrimitiveUP_error{true};
static std::atomic<bool> g_first_ProcessVertices_error{true};
static std::atomic<bool> g_first_SetVertexDeclaration_error{true};
static std::atomic<bool> g_first_SetFVF_error{true};
static std::atomic<bool> g_first_SetStreamSourceFreq_error{true};
static std::atomic<bool> g_first_GetRenderTarget_error{true};
static std::atomic<bool> g_first_GetDepthStencilSurface_error{true};
static std::atomic<bool> g_first_SetViewport_error{true};
static std::atomic<bool> g_first_SetTransform_error{true};
static std::atomic<bool> g_first_SetRenderState_error{true};
static std::atomic<bool> g_first_GetTexture_error{true};
static std::atomic<bool> g_first_SetTexture_error{true};
static std::atomic<bool> g_first_SetVertexShader_error{true};
static std::atomic<bool> g_first_SetPixelShader_error{true};
static std::atomic<bool> g_first_CreateRenderTargetEx_error{true};
static std::atomic<bool> g_first_CreateOffscreenPlainSurfaceEx_error{true};
static std::atomic<bool> g_first_CreateDepthStencilSurfaceEx_error{true};
static std::atomic<bool> g_first_ResetEx_error{true};
static std::atomic<bool> g_first_GetDisplayModeEx_error{true};
static std::atomic<bool> g_first_CheckDeviceState_error{true};

}  // namespace

// Detours: RECORD_DETOUR_CALL, log first call, call original, log on error.

static HRESULT STDMETHODCALLTYPE CreateTexture_Detour(IDirect3DDevice9* This, UINT Width, UINT Height, UINT Levels,
                                                      DWORD Usage, D3DFORMAT Format, D3DPOOL Pool,
                                                      IDirect3DTexture9** ppTexture, HANDLE* pSharedHandle) {
    RECORD_DETOUR_CALL(utils::get_now_ns());
    if (g_first_CreateTexture.exchange(false)) {
        LogInfo("[D3D9] First call: IDirect3DDevice9::CreateTexture");
    }
    const D3DPOOL poolToUse = UpgradePoolForDevice9Ex(This, Pool);
    HRESULT hr = CreateTexture_Original(This, Width, Height, Levels, Usage, Format, poolToUse, ppTexture, pSharedHandle);
    if (FAILED(hr)) {
        LogD3D9Error("CreateTexture", hr);
        if (g_first_CreateTexture_error.exchange(false)) {
            const char* hr_name = D3D9HResultName(hr);
            const bool has_hr_name = (hr_name != nullptr);
            LogError("[D3D9 error] CreateTexture first failure — full arguments: This=%p Width=%u Height=%u Levels=%u "
                     "Usage=0x%X Format=%u Pool=%u ppTexture=%p pSharedHandle=%p hr=0x%08X%s%s%s",
                     static_cast<void*>(This), Width, Height, Levels, static_cast<unsigned>(Usage),
                     static_cast<unsigned>(Format), static_cast<unsigned>(Pool), static_cast<void*>(ppTexture),
                     static_cast<void*>(pSharedHandle), static_cast<unsigned>(hr),
                     has_hr_name ? " (" : "", has_hr_name ? hr_name : "", has_hr_name ? ")" : "");
        }
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
    const D3DPOOL poolToUse = UpgradePoolForDevice9Ex(This, Pool);
    HRESULT hr = CreateVolumeTexture_Original(This, Width, Height, Depth, Levels, Usage, Format, poolToUse,
                                              ppVolumeTexture, pSharedHandle);
    if (FAILED(hr)) {
        LogD3D9Error("CreateVolumeTexture", hr);
        if (g_first_CreateVolumeTexture_error.exchange(false)) {
            const char* hr_name = D3D9HResultName(hr);
            const bool has_hr_name = (hr_name != nullptr);
            LogError("[D3D9 error] CreateVolumeTexture first failure — This=%p Width=%u Height=%u Depth=%u Levels=%u "
                     "Usage=0x%X Format=%u Pool=%u ppVolumeTexture=%p pSharedHandle=%p hr=0x%08X%s%s%s",
                     static_cast<void*>(This), Width, Height, Depth, Levels, static_cast<unsigned>(Usage),
                     static_cast<unsigned>(Format), static_cast<unsigned>(Pool), static_cast<void*>(ppVolumeTexture),
                     static_cast<void*>(pSharedHandle), static_cast<unsigned>(hr),
                     has_hr_name ? " (" : "", has_hr_name ? hr_name : "", has_hr_name ? ")" : "");
        }
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
    const D3DPOOL poolToUse = UpgradePoolForDevice9Ex(This, Pool);
    HRESULT hr =
        CreateCubeTexture_Original(This, EdgeLength, Levels, Usage, Format, poolToUse, ppCubeTexture, pSharedHandle);
    if (FAILED(hr)) {
        LogD3D9Error("CreateCubeTexture", hr);
        if (g_first_CreateCubeTexture_error.exchange(false)) {
            const char* hr_name = D3D9HResultName(hr);
            const bool has_hr_name = (hr_name != nullptr);
            LogError("[D3D9 error] CreateCubeTexture first failure — This=%p EdgeLength=%u Levels=%u Usage=0x%X "
                     "Format=%u Pool=%u ppCubeTexture=%p pSharedHandle=%p hr=0x%08X%s%s%s",
                     static_cast<void*>(This), EdgeLength, Levels, static_cast<unsigned>(Usage),
                     static_cast<unsigned>(Format), static_cast<unsigned>(Pool), static_cast<void*>(ppCubeTexture),
                     static_cast<void*>(pSharedHandle), static_cast<unsigned>(hr),
                     has_hr_name ? " (" : "", has_hr_name ? hr_name : "", has_hr_name ? ")" : "");
        }
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
    const D3DPOOL poolToUse = UpgradePoolForDevice9Ex(This, Pool);
    HRESULT hr = CreateVertexBuffer_Original(This, Length, Usage, FVF, poolToUse, ppVertexBuffer, pSharedHandle);
    if (FAILED(hr)) {
        LogD3D9Error("CreateVertexBuffer", hr);
        if (g_first_CreateVertexBuffer_error.exchange(false)) {
            const char* hr_name = D3D9HResultName(hr);
            const bool has_hr_name = (hr_name != nullptr);
            LogError("[D3D9 error] CreateVertexBuffer first failure — This=%p Length=%u Usage=0x%X FVF=0x%lX Pool=%u "
                     "ppVertexBuffer=%p pSharedHandle=%p hr=0x%08X%s%s%s",
                     static_cast<void*>(This), Length, static_cast<unsigned>(Usage), static_cast<unsigned long>(FVF),
                     static_cast<unsigned>(Pool), static_cast<void*>(ppVertexBuffer), static_cast<void*>(pSharedHandle),
                     static_cast<unsigned>(hr), has_hr_name ? " (" : "", has_hr_name ? hr_name : "",
                     has_hr_name ? ")" : "");
        }
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
    const D3DPOOL poolToUse = UpgradePoolForDevice9Ex(This, Pool);
    HRESULT hr = CreateIndexBuffer_Original(This, Length, Usage, Format, poolToUse, ppIndexBuffer, pSharedHandle);
    if (FAILED(hr)) {
        LogD3D9Error("CreateIndexBuffer", hr);
        if (g_first_CreateIndexBuffer_error.exchange(false)) {
            const char* hr_name = D3D9HResultName(hr);
            const bool has_hr_name = (hr_name != nullptr);
            LogError("[D3D9 error] CreateIndexBuffer first failure — This=%p Length=%u Usage=0x%X Format=%u Pool=%u "
                     "ppIndexBuffer=%p pSharedHandle=%p hr=0x%08X%s%s%s",
                     static_cast<void*>(This), Length, static_cast<unsigned>(Usage), static_cast<unsigned>(Format),
                     static_cast<unsigned>(Pool), static_cast<void*>(ppIndexBuffer), static_cast<void*>(pSharedHandle),
                     static_cast<unsigned>(hr), has_hr_name ? " (" : "", has_hr_name ? hr_name : "",
                     has_hr_name ? ")" : "");
        }
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
    const D3DPOOL poolToUse = UpgradePoolForDevice9Ex(This, Pool);
    HRESULT hr = CreateOffscreenPlainSurface_Original(This, Width, Height, Format, poolToUse, ppSurface, pSharedHandle);
    if (FAILED(hr)) {
        LogD3D9Error("CreateOffscreenPlainSurface", hr);
        if (g_first_CreateOffscreenPlainSurface_error.exchange(false)) {
            const char* hr_name = D3D9HResultName(hr);
            const bool has_hr_name = (hr_name != nullptr);
            LogError("[D3D9 error] CreateOffscreenPlainSurface first failure — This=%p Width=%u Height=%u Format=%u "
                     "Pool=%u ppSurface=%p pSharedHandle=%p hr=0x%08X%s%s%s",
                     static_cast<void*>(This), Width, Height, static_cast<unsigned>(Format),
                     static_cast<unsigned>(Pool), static_cast<void*>(ppSurface), static_cast<void*>(pSharedHandle),
                     static_cast<unsigned>(hr), has_hr_name ? " (" : "", has_hr_name ? hr_name : "",
                     has_hr_name ? ")" : "");
        }
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
        if (g_first_CreateRenderTarget_error.exchange(false)) {
            const char* hr_name = D3D9HResultName(hr);
            const bool has_hr_name = (hr_name != nullptr);
            LogError("[D3D9 error] CreateRenderTarget first failure — This=%p Width=%u Height=%u Format=%u "
                     "MultiSample=%u MultisampleQuality=%u Lockable=%d ppSurface=%p pSharedHandle=%p hr=0x%08X%s%s%s",
                     static_cast<void*>(This), Width, Height, static_cast<unsigned>(Format),
                     static_cast<unsigned>(MultiSample), MultisampleQuality, static_cast<int>(Lockable),
                     static_cast<void*>(ppSurface), static_cast<void*>(pSharedHandle), static_cast<unsigned>(hr),
                     has_hr_name ? " (" : "", has_hr_name ? hr_name : "", has_hr_name ? ")" : "");
        }
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
        if (g_first_CreateDepthStencilSurface_error.exchange(false)) {
            const char* hr_name = D3D9HResultName(hr);
            const bool has_hr_name = (hr_name != nullptr);
            LogError("[D3D9 error] CreateDepthStencilSurface first failure — This=%p Width=%u Height=%u Format=%u "
                     "MultiSample=%u MultisampleQuality=%u Discard=%d ppSurface=%p pSharedHandle=%p hr=0x%08X%s%s%s",
                     static_cast<void*>(This), Width, Height, static_cast<unsigned>(Format),
                     static_cast<unsigned>(MultiSample), MultisampleQuality, static_cast<int>(Discard),
                     static_cast<void*>(ppSurface), static_cast<void*>(pSharedHandle), static_cast<unsigned>(hr),
                     has_hr_name ? " (" : "", has_hr_name ? hr_name : "", has_hr_name ? ")" : "");
        }
    }
    return hr;
}

static HRESULT STDMETHODCALLTYPE Reset_Detour(IDirect3DDevice9* This,
                                             D3DPRESENT_PARAMETERS* pPresentationParameters) {
    RECORD_DETOUR_CALL(utils::get_now_ns());
    HRESULT hr = Reset_Original(This, pPresentationParameters);
    if (FAILED(hr)) {
        LogD3D9Error("Reset", hr);
        if (g_first_Reset_error.exchange(false)) {
            LogD3D9FirstFailure("Reset", This, hr);
            LogError("[D3D9 error] Reset first failure — pPresentationParameters=%p",
                     static_cast<void*>(pPresentationParameters));
        }
    }
    return hr;
}

static HRESULT STDMETHODCALLTYPE BeginScene_Detour(IDirect3DDevice9* This) {
    RECORD_DETOUR_CALL(utils::get_now_ns());
    HRESULT hr = BeginScene_Original(This);
    if (FAILED(hr)) {
        LogD3D9Error("BeginScene", hr);
        if (g_first_BeginScene_error.exchange(false)) {
            LogD3D9FirstFailure("BeginScene", This, hr);
        }
    }
    return hr;
}

static HRESULT STDMETHODCALLTYPE EndScene_Detour(IDirect3DDevice9* This) {
    RECORD_DETOUR_CALL(utils::get_now_ns());
    HRESULT hr = EndScene_Original(This);
    if (FAILED(hr)) {
        LogD3D9Error("EndScene", hr);
        if (g_first_EndScene_error.exchange(false)) {
            LogD3D9FirstFailure("EndScene", This, hr);
        }
    }
    return hr;
}

static HRESULT STDMETHODCALLTYPE Clear_Detour(IDirect3DDevice9* This, DWORD Count, const D3DRECT* pRects,
                                              DWORD Flags, D3DCOLOR Color, float Z, DWORD Stencil) {
    RECORD_DETOUR_CALL(utils::get_now_ns());
    HRESULT hr = Clear_Original(This, Count, pRects, Flags, Color, Z, Stencil);
    if (FAILED(hr)) {
        LogD3D9Error("Clear", hr);
        if (g_first_Clear_error.exchange(false)) {
            LogD3D9FirstFailure("Clear", This, hr);
            LogError("[D3D9 error] Clear first failure — Count=%lu Flags=0x%lX", static_cast<unsigned long>(Count),
                     static_cast<unsigned long>(Flags));
        }
    }
    return hr;
}

static HRESULT STDMETHODCALLTYPE CreateAdditionalSwapChain_Detour(
    IDirect3DDevice9* This, D3DPRESENT_PARAMETERS* pPresentationParameters, IDirect3DSwapChain9** ppSwapChain) {
    RECORD_DETOUR_CALL(utils::get_now_ns());
    HRESULT hr = CreateAdditionalSwapChain_Original(This, pPresentationParameters, ppSwapChain);
    if (FAILED(hr)) {
        LogD3D9Error("CreateAdditionalSwapChain", hr);
        if (g_first_CreateAdditionalSwapChain_error.exchange(false)) {
            LogD3D9FirstFailure("CreateAdditionalSwapChain", This, hr);
            LogError("[D3D9 error] CreateAdditionalSwapChain first failure — pPresentationParameters=%p ppSwapChain=%p",
                     static_cast<void*>(pPresentationParameters), static_cast<void*>(ppSwapChain));
        }
    }
    return hr;
}

static HRESULT STDMETHODCALLTYPE GetBackBuffer_Detour(IDirect3DDevice9* This, UINT iSwapChain, UINT iBackBuffer,
                                                      D3DBACKBUFFER_TYPE Type, IDirect3DSurface9** ppBackBuffer) {
    RECORD_DETOUR_CALL(utils::get_now_ns());
    HRESULT hr = GetBackBuffer_Original(This, iSwapChain, iBackBuffer, Type, ppBackBuffer);
    if (FAILED(hr)) {
        LogD3D9Error("GetBackBuffer", hr);
        if (g_first_GetBackBuffer_error.exchange(false)) {
            LogD3D9FirstFailure("GetBackBuffer", This, hr);
            LogError("[D3D9 error] GetBackBuffer first failure — iSwapChain=%u iBackBuffer=%u Type=%u",
                     iSwapChain, iBackBuffer, static_cast<unsigned>(Type));
        }
    }
    return hr;
}

static HRESULT STDMETHODCALLTYPE SetRenderTarget_Detour(IDirect3DDevice9* This, DWORD RenderTargetIndex,
                                                        IDirect3DSurface9* pRenderTarget) {
    RECORD_DETOUR_CALL(utils::get_now_ns());
    HRESULT hr = SetRenderTarget_Original(This, RenderTargetIndex, pRenderTarget);
    if (FAILED(hr)) {
        LogD3D9Error("SetRenderTarget", hr);
        if (g_first_SetRenderTarget_error.exchange(false)) {
            LogD3D9FirstFailure("SetRenderTarget", This, hr);
            LogError("[D3D9 error] SetRenderTarget first failure — RenderTargetIndex=%lu pRenderTarget=%p",
                     static_cast<unsigned long>(RenderTargetIndex), static_cast<void*>(pRenderTarget));
        }
    }
    return hr;
}

static HRESULT STDMETHODCALLTYPE SetDepthStencilSurface_Detour(IDirect3DDevice9* This,
                                                              IDirect3DSurface9* pNewZStencil) {
    RECORD_DETOUR_CALL(utils::get_now_ns());
    HRESULT hr = SetDepthStencilSurface_Original(This, pNewZStencil);
    if (FAILED(hr)) {
        LogD3D9Error("SetDepthStencilSurface", hr);
        if (g_first_SetDepthStencilSurface_error.exchange(false)) {
            LogD3D9FirstFailure("SetDepthStencilSurface", This, hr);
            LogError("[D3D9 error] SetDepthStencilSurface first failure — pNewZStencil=%p",
                     static_cast<void*>(pNewZStencil));
        }
    }
    return hr;
}

static HRESULT STDMETHODCALLTYPE CreateStateBlock_Detour(IDirect3DDevice9* This, D3DSTATEBLOCKTYPE Type,
                                                         IDirect3DStateBlock9** ppSB) {
    RECORD_DETOUR_CALL(utils::get_now_ns());
    HRESULT hr = CreateStateBlock_Original(This, Type, ppSB);
    if (FAILED(hr)) {
        LogD3D9Error("CreateStateBlock", hr);
        if (g_first_CreateStateBlock_error.exchange(false)) {
            LogD3D9FirstFailure("CreateStateBlock", This, hr);
            LogError("[D3D9 error] CreateStateBlock first failure — Type=%u ppSB=%p",
                     static_cast<unsigned>(Type), static_cast<void*>(ppSB));
        }
    }
    return hr;
}

static HRESULT STDMETHODCALLTYPE EndStateBlock_Detour(IDirect3DDevice9* This, IDirect3DStateBlock9** ppSB) {
    RECORD_DETOUR_CALL(utils::get_now_ns());
    HRESULT hr = EndStateBlock_Original(This, ppSB);
    if (FAILED(hr)) {
        LogD3D9Error("EndStateBlock", hr);
        if (g_first_EndStateBlock_error.exchange(false)) {
            LogD3D9FirstFailure("EndStateBlock", This, hr);
            LogError("[D3D9 error] EndStateBlock first failure — ppSB=%p", static_cast<void*>(ppSB));
        }
    }
    return hr;
}

static HRESULT STDMETHODCALLTYPE CreateVertexDeclaration_Detour(IDirect3DDevice9* This,
                                                                const D3DVERTEXELEMENT9* pVertexElements,
                                                                IDirect3DVertexDeclaration9** ppDecl) {
    RECORD_DETOUR_CALL(utils::get_now_ns());
    HRESULT hr = CreateVertexDeclaration_Original(This, pVertexElements, ppDecl);
    if (FAILED(hr)) {
        LogD3D9Error("CreateVertexDeclaration", hr);
        if (g_first_CreateVertexDeclaration_error.exchange(false)) {
            LogD3D9FirstFailure("CreateVertexDeclaration", This, hr);
            LogError("[D3D9 error] CreateVertexDeclaration first failure — pVertexElements=%p ppDecl=%p",
                     reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(pVertexElements)),
                     static_cast<void*>(ppDecl));
        }
    }
    return hr;
}

static HRESULT STDMETHODCALLTYPE CreateVertexShader_Detour(IDirect3DDevice9* This, const DWORD* pFunction,
                                                            IDirect3DVertexShader9** ppShader) {
    RECORD_DETOUR_CALL(utils::get_now_ns());
    HRESULT hr = CreateVertexShader_Original(This, pFunction, ppShader);
    if (FAILED(hr)) {
        LogD3D9Error("CreateVertexShader", hr);
        if (g_first_CreateVertexShader_error.exchange(false)) {
            LogD3D9FirstFailure("CreateVertexShader", This, hr);
            LogError("[D3D9 error] CreateVertexShader first failure — pFunction=%p ppShader=%p",
                     reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(pFunction)),
                     static_cast<void*>(ppShader));
        }
    }
    return hr;
}

static HRESULT STDMETHODCALLTYPE SetStreamSource_Detour(IDirect3DDevice9* This, UINT StreamNumber,
                                                        IDirect3DVertexBuffer9* pStreamData, UINT OffsetInBytes,
                                                        UINT Stride) {
    RECORD_DETOUR_CALL(utils::get_now_ns());
    HRESULT hr = SetStreamSource_Original(This, StreamNumber, pStreamData, OffsetInBytes, Stride);
    if (FAILED(hr)) {
        LogD3D9Error("SetStreamSource", hr);
        if (g_first_SetStreamSource_error.exchange(false)) {
            LogD3D9FirstFailure("SetStreamSource", This, hr);
            LogError("[D3D9 error] SetStreamSource first failure — StreamNumber=%u OffsetInBytes=%u Stride=%u",
                     StreamNumber, OffsetInBytes, Stride);
        }
    }
    return hr;
}

static HRESULT STDMETHODCALLTYPE SetIndices_Detour(IDirect3DDevice9* This, IDirect3DIndexBuffer9* pIndexData) {
    RECORD_DETOUR_CALL(utils::get_now_ns());
    HRESULT hr = SetIndices_Original(This, pIndexData);
    if (FAILED(hr)) {
        LogD3D9Error("SetIndices", hr);
        if (g_first_SetIndices_error.exchange(false)) {
            LogD3D9FirstFailure("SetIndices", This, hr);
            LogError("[D3D9 error] SetIndices first failure — pIndexData=%p", static_cast<void*>(pIndexData));
        }
    }
    return hr;
}

static HRESULT STDMETHODCALLTYPE CreatePixelShader_Detour(IDirect3DDevice9* This, const DWORD* pFunction,
                                                          IDirect3DPixelShader9** ppShader) {
    RECORD_DETOUR_CALL(utils::get_now_ns());
    HRESULT hr = CreatePixelShader_Original(This, pFunction, ppShader);
    if (FAILED(hr)) {
        LogD3D9Error("CreatePixelShader", hr);
        if (g_first_CreatePixelShader_error.exchange(false)) {
            LogD3D9FirstFailure("CreatePixelShader", This, hr);
            LogError("[D3D9 error] CreatePixelShader first failure — pFunction=%p ppShader=%p",
                     reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(pFunction)),
                     static_cast<void*>(ppShader));
        }
    }
    return hr;
}

static HRESULT STDMETHODCALLTYPE TestCooperativeLevel_Detour(IDirect3DDevice9* This) {
    RECORD_DETOUR_CALL(utils::get_now_ns());
    HRESULT hr = TestCooperativeLevel_Original(This);
    if (FAILED(hr)) {
        LogD3D9Error("TestCooperativeLevel", hr);
        if (g_first_TestCooperativeLevel_error.exchange(false)) {
            LogD3D9FirstFailure("TestCooperativeLevel", This, hr);
        }
    }
    return hr;
}

static HRESULT STDMETHODCALLTYPE GetSwapChain_Detour(IDirect3DDevice9* This, UINT iSwapChain,
                                                      IDirect3DSwapChain9** ppSwapChain) {
    RECORD_DETOUR_CALL(utils::get_now_ns());
    HRESULT hr = GetSwapChain_Original(This, iSwapChain, ppSwapChain);
    if (FAILED(hr)) {
        LogD3D9Error("GetSwapChain", hr);
        if (g_first_GetSwapChain_error.exchange(false)) {
            LogD3D9FirstFailure("GetSwapChain", This, hr);
            LogError("[D3D9 error] GetSwapChain first failure — iSwapChain=%u ppSwapChain=%p", iSwapChain,
                     static_cast<void*>(ppSwapChain));
        }
    }
    return hr;
}

static HRESULT STDMETHODCALLTYPE UpdateSurface_Detour(IDirect3DDevice9* This, IDirect3DSurface9* pSourceSurface,
                                                      const RECT* pSourceRect,
                                                      IDirect3DSurface9* pDestinationSurface,
                                                      const POINT* pDestPoint) {
    RECORD_DETOUR_CALL(utils::get_now_ns());
    HRESULT hr = UpdateSurface_Original(This, pSourceSurface, pSourceRect, pDestinationSurface, pDestPoint);
    if (FAILED(hr)) {
        LogD3D9Error("UpdateSurface", hr);
        if (g_first_UpdateSurface_error.exchange(false)) {
            LogD3D9FirstFailure("UpdateSurface", This, hr);
            LogError("[D3D9 error] UpdateSurface first failure — pSourceSurface=%p pDestinationSurface=%p",
                     static_cast<void*>(pSourceSurface), static_cast<void*>(pDestinationSurface));
        }
    }
    return hr;
}

static HRESULT STDMETHODCALLTYPE UpdateTexture_Detour(IDirect3DDevice9* This,
                                                       IDirect3DBaseTexture9* pSourceTexture,
                                                       IDirect3DBaseTexture9* pDestinationTexture) {
    RECORD_DETOUR_CALL(utils::get_now_ns());
    HRESULT hr = UpdateTexture_Original(This, pSourceTexture, pDestinationTexture);
    if (FAILED(hr)) {
        LogD3D9Error("UpdateTexture", hr);
        if (g_first_UpdateTexture_error.exchange(false)) {
            LogD3D9FirstFailure("UpdateTexture", This, hr);
            LogError("[D3D9 error] UpdateTexture first failure — pSourceTexture=%p pDestinationTexture=%p",
                     static_cast<void*>(pSourceTexture), static_cast<void*>(pDestinationTexture));
        }
    }
    return hr;
}

static HRESULT STDMETHODCALLTYPE GetRenderTargetData_Detour(IDirect3DDevice9* This,
                                                             IDirect3DSurface9* pRenderTarget,
                                                             IDirect3DSurface9* pDestSurface) {
    RECORD_DETOUR_CALL(utils::get_now_ns());
    HRESULT hr = GetRenderTargetData_Original(This, pRenderTarget, pDestSurface);
    if (FAILED(hr)) {
        LogD3D9Error("GetRenderTargetData", hr);
        if (g_first_GetRenderTargetData_error.exchange(false)) {
            LogD3D9FirstFailure("GetRenderTargetData", This, hr);
            LogError("[D3D9 error] GetRenderTargetData first failure — pRenderTarget=%p pDestSurface=%p",
                     static_cast<void*>(pRenderTarget), static_cast<void*>(pDestSurface));
        }
    }
    return hr;
}

static HRESULT STDMETHODCALLTYPE GetFrontBufferData_Detour(IDirect3DDevice9* This, UINT iSwapChain,
                                                            IDirect3DSurface9* pDestSurface) {
    RECORD_DETOUR_CALL(utils::get_now_ns());
    HRESULT hr = GetFrontBufferData_Original(This, iSwapChain, pDestSurface);
    if (FAILED(hr)) {
        LogD3D9Error("GetFrontBufferData", hr);
        if (g_first_GetFrontBufferData_error.exchange(false)) {
            LogD3D9FirstFailure("GetFrontBufferData", This, hr);
            LogError("[D3D9 error] GetFrontBufferData first failure — iSwapChain=%u pDestSurface=%p", iSwapChain,
                     static_cast<void*>(pDestSurface));
        }
    }
    return hr;
}

static HRESULT STDMETHODCALLTYPE StretchRect_Detour(IDirect3DDevice9* This, IDirect3DSurface9* pSourceSurface,
                                                    const RECT* pSourceRect, IDirect3DSurface9* pDestSurface,
                                                    const RECT* pDestRect, D3DTEXTUREFILTERTYPE Filter) {
    RECORD_DETOUR_CALL(utils::get_now_ns());
    HRESULT hr = StretchRect_Original(This, pSourceSurface, pSourceRect, pDestSurface, pDestRect, Filter);
    if (FAILED(hr)) {
        LogD3D9Error("StretchRect", hr);
        if (g_first_StretchRect_error.exchange(false)) {
            LogD3D9FirstFailure("StretchRect", This, hr);
            LogError("[D3D9 error] StretchRect first failure — pSourceSurface=%p pDestSurface=%p Filter=%u",
                     static_cast<void*>(pSourceSurface), static_cast<void*>(pDestSurface),
                     static_cast<unsigned>(Filter));
        }
    }
    return hr;
}

static HRESULT STDMETHODCALLTYPE ColorFill_Detour(IDirect3DDevice9* This, IDirect3DSurface9* pSurface,
                                                   const RECT* pRect, D3DCOLOR color) {
    RECORD_DETOUR_CALL(utils::get_now_ns());
    HRESULT hr = ColorFill_Original(This, pSurface, pRect, color);
    if (FAILED(hr)) {
        LogD3D9Error("ColorFill", hr);
        if (g_first_ColorFill_error.exchange(false)) {
            LogD3D9FirstFailure("ColorFill", This, hr);
            LogError("[D3D9 error] ColorFill first failure — pSurface=%p color=0x%08X",
                     static_cast<void*>(pSurface), static_cast<unsigned>(color));
        }
    }
    return hr;
}

static HRESULT STDMETHODCALLTYPE BeginStateBlock_Detour(IDirect3DDevice9* This) {
    RECORD_DETOUR_CALL(utils::get_now_ns());
    HRESULT hr = BeginStateBlock_Original(This);
    if (FAILED(hr)) {
        LogD3D9Error("BeginStateBlock", hr);
        if (g_first_BeginStateBlock_error.exchange(false)) {
            LogD3D9FirstFailure("BeginStateBlock", This, hr);
        }
    }
    return hr;
}

static HRESULT STDMETHODCALLTYPE CreateQuery_Detour(IDirect3DDevice9* This, D3DQUERYTYPE Type,
                                                     IDirect3DQuery9** ppQuery) {
    RECORD_DETOUR_CALL(utils::get_now_ns());
    HRESULT hr = CreateQuery_Original(This, Type, ppQuery);
    if (FAILED(hr)) {
        LogD3D9Error("CreateQuery", hr);
        if (g_first_CreateQuery_error.exchange(false)) {
            LogD3D9FirstFailure("CreateQuery", This, hr);
            LogError("[D3D9 error] CreateQuery first failure — Type=%u ppQuery=%p", static_cast<unsigned>(Type),
                     static_cast<void*>(ppQuery));
        }
    }
    return hr;
}

static HRESULT STDMETHODCALLTYPE DrawPrimitive_Detour(IDirect3DDevice9* This,
                                                       D3DPRIMITIVETYPE PrimitiveType, UINT StartVertex,
                                                       UINT PrimitiveCount) {
    RECORD_DETOUR_CALL(utils::get_now_ns());
    HRESULT hr = DrawPrimitive_Original(This, PrimitiveType, StartVertex, PrimitiveCount);
    if (FAILED(hr)) {
        LogD3D9Error("DrawPrimitive", hr);
        if (g_first_DrawPrimitive_error.exchange(false)) {
            LogD3D9FirstFailure("DrawPrimitive", This, hr);
            LogError("[D3D9 error] DrawPrimitive first failure — PrimitiveType=%u StartVertex=%u PrimitiveCount=%u",
                     static_cast<unsigned>(PrimitiveType), StartVertex, PrimitiveCount);
        }
    }
    return hr;
}

static HRESULT STDMETHODCALLTYPE DrawIndexedPrimitive_Detour(IDirect3DDevice9* This,
                                                              D3DPRIMITIVETYPE PrimitiveType, INT BaseVertexIndex,
                                                              UINT MinVertexIndex, UINT NumVertices, UINT startIndex,
                                                              UINT primCount) {
    RECORD_DETOUR_CALL(utils::get_now_ns());
    HRESULT hr = DrawIndexedPrimitive_Original(This, PrimitiveType, BaseVertexIndex, MinVertexIndex, NumVertices,
                                               startIndex, primCount);
    if (FAILED(hr)) {
        LogD3D9Error("DrawIndexedPrimitive", hr);
        if (g_first_DrawIndexedPrimitive_error.exchange(false)) {
            LogD3D9FirstFailure("DrawIndexedPrimitive", This, hr);
            LogError("[D3D9 error] DrawIndexedPrimitive first failure — PrimitiveType=%u startIndex=%u primCount=%u",
                     static_cast<unsigned>(PrimitiveType), startIndex, primCount);
        }
    }
    return hr;
}

static HRESULT STDMETHODCALLTYPE DrawPrimitiveUP_Detour(IDirect3DDevice9* This,
                                                         D3DPRIMITIVETYPE PrimitiveType, UINT PrimitiveCount,
                                                         const void* pVertexStreamZeroData,
                                                         UINT VertexStreamZeroStride) {
    RECORD_DETOUR_CALL(utils::get_now_ns());
    HRESULT hr = DrawPrimitiveUP_Original(This, PrimitiveType, PrimitiveCount, pVertexStreamZeroData,
                                          VertexStreamZeroStride);
    if (FAILED(hr)) {
        LogD3D9Error("DrawPrimitiveUP", hr);
        if (g_first_DrawPrimitiveUP_error.exchange(false)) {
            LogD3D9FirstFailure("DrawPrimitiveUP", This, hr);
            LogError("[D3D9 error] DrawPrimitiveUP first failure — PrimitiveType=%u PrimitiveCount=%u Stride=%u",
                     static_cast<unsigned>(PrimitiveType), PrimitiveCount, VertexStreamZeroStride);
        }
    }
    return hr;
}

static HRESULT STDMETHODCALLTYPE DrawIndexedPrimitiveUP_Detour(
    IDirect3DDevice9* This, D3DPRIMITIVETYPE PrimitiveType, UINT MinVertexIndex, UINT NumVertices,
    UINT PrimitiveCount, const void* pIndexData, D3DFORMAT IndexDataFormat, const void* pVertexStreamZeroData,
    UINT VertexStreamZeroStride) {
    RECORD_DETOUR_CALL(utils::get_now_ns());
    HRESULT hr = DrawIndexedPrimitiveUP_Original(This, PrimitiveType, MinVertexIndex, NumVertices, PrimitiveCount,
                                                 pIndexData, IndexDataFormat, pVertexStreamZeroData,
                                                 VertexStreamZeroStride);
    if (FAILED(hr)) {
        LogD3D9Error("DrawIndexedPrimitiveUP", hr);
        if (g_first_DrawIndexedPrimitiveUP_error.exchange(false)) {
            LogD3D9FirstFailure("DrawIndexedPrimitiveUP", This, hr);
            LogError("[D3D9 error] DrawIndexedPrimitiveUP first failure — PrimitiveType=%u PrimitiveCount=%u",
                     static_cast<unsigned>(PrimitiveType), PrimitiveCount);
        }
    }
    return hr;
}

static HRESULT STDMETHODCALLTYPE ProcessVertices_Detour(IDirect3DDevice9* This, UINT SrcStartIndex, UINT DestIndex,
                                                         UINT VertexCount, IDirect3DVertexBuffer9* pDestBuffer,
                                                         IDirect3DVertexDeclaration9* pVertexDecl, DWORD Flags) {
    RECORD_DETOUR_CALL(utils::get_now_ns());
    HRESULT hr = ProcessVertices_Original(This, SrcStartIndex, DestIndex, VertexCount, pDestBuffer, pVertexDecl, Flags);
    if (FAILED(hr)) {
        LogD3D9Error("ProcessVertices", hr);
        if (g_first_ProcessVertices_error.exchange(false)) {
            LogD3D9FirstFailure("ProcessVertices", This, hr);
            LogError("[D3D9 error] ProcessVertices first failure — VertexCount=%u pDestBuffer=%p", VertexCount,
                     static_cast<void*>(pDestBuffer));
        }
    }
    return hr;
}

static HRESULT STDMETHODCALLTYPE SetVertexDeclaration_Detour(IDirect3DDevice9* This,
                                                             IDirect3DVertexDeclaration9* pDecl) {
    RECORD_DETOUR_CALL(utils::get_now_ns());
    HRESULT hr = SetVertexDeclaration_Original(This, pDecl);
    if (FAILED(hr)) {
        LogD3D9Error("SetVertexDeclaration", hr);
        if (g_first_SetVertexDeclaration_error.exchange(false)) {
            LogD3D9FirstFailure("SetVertexDeclaration", This, hr);
            LogError("[D3D9 error] SetVertexDeclaration first failure — pDecl=%p", static_cast<void*>(pDecl));
        }
    }
    return hr;
}

static HRESULT STDMETHODCALLTYPE SetFVF_Detour(IDirect3DDevice9* This, DWORD FVF) {
    RECORD_DETOUR_CALL(utils::get_now_ns());
    HRESULT hr = SetFVF_Original(This, FVF);
    if (FAILED(hr)) {
        LogD3D9Error("SetFVF", hr);
        if (g_first_SetFVF_error.exchange(false)) {
            LogD3D9FirstFailure("SetFVF", This, hr);
            LogError("[D3D9 error] SetFVF first failure — FVF=0x%lX", static_cast<unsigned long>(FVF));
        }
    }
    return hr;
}

static HRESULT STDMETHODCALLTYPE SetStreamSourceFreq_Detour(IDirect3DDevice9* This, UINT StreamNumber, UINT Divider) {
    RECORD_DETOUR_CALL(utils::get_now_ns());
    HRESULT hr = SetStreamSourceFreq_Original(This, StreamNumber, Divider);
    if (FAILED(hr)) {
        LogD3D9Error("SetStreamSourceFreq", hr);
        if (g_first_SetStreamSourceFreq_error.exchange(false)) {
            LogD3D9FirstFailure("SetStreamSourceFreq", This, hr);
            LogError("[D3D9 error] SetStreamSourceFreq first failure — StreamNumber=%u Divider=%u", StreamNumber,
                     Divider);
        }
    }
    return hr;
}

static HRESULT STDMETHODCALLTYPE GetRenderTarget_Detour(IDirect3DDevice9* This, DWORD RenderTargetIndex,
                                                        IDirect3DSurface9** ppRenderTarget) {
    RECORD_DETOUR_CALL(utils::get_now_ns());
    HRESULT hr = GetRenderTarget_Original(This, RenderTargetIndex, ppRenderTarget);
    if (FAILED(hr)) {
        LogD3D9Error("GetRenderTarget", hr);
        if (g_first_GetRenderTarget_error.exchange(false)) {
            LogD3D9FirstFailure("GetRenderTarget", This, hr);
            LogError("[D3D9 error] GetRenderTarget first failure — RenderTargetIndex=%lu", static_cast<unsigned long>(RenderTargetIndex));
        }
    }
    return hr;
}

static HRESULT STDMETHODCALLTYPE GetDepthStencilSurface_Detour(IDirect3DDevice9* This,
                                                                IDirect3DSurface9** ppZStencilSurface) {
    RECORD_DETOUR_CALL(utils::get_now_ns());
    HRESULT hr = GetDepthStencilSurface_Original(This, ppZStencilSurface);
    if (FAILED(hr)) {
        LogD3D9Error("GetDepthStencilSurface", hr);
        if (g_first_GetDepthStencilSurface_error.exchange(false)) {
            LogD3D9FirstFailure("GetDepthStencilSurface", This, hr);
        }
    }
    return hr;
}

static HRESULT STDMETHODCALLTYPE SetViewport_Detour(IDirect3DDevice9* This, const D3DVIEWPORT9* pViewport) {
    RECORD_DETOUR_CALL(utils::get_now_ns());
    HRESULT hr = SetViewport_Original(This, pViewport);
    if (FAILED(hr)) {
        LogD3D9Error("SetViewport", hr);
        if (g_first_SetViewport_error.exchange(false)) {
            LogD3D9FirstFailure("SetViewport", This, hr);
            LogError("[D3D9 error] SetViewport first failure — pViewport=%p", reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(pViewport)));
        }
    }
    return hr;
}

static HRESULT STDMETHODCALLTYPE SetTransform_Detour(IDirect3DDevice9* This, D3DTRANSFORMSTATETYPE State,
                                                     const D3DMATRIX* pMatrix) {
    RECORD_DETOUR_CALL(utils::get_now_ns());
    HRESULT hr = SetTransform_Original(This, State, pMatrix);
    if (FAILED(hr)) {
        LogD3D9Error("SetTransform", hr);
        if (g_first_SetTransform_error.exchange(false)) {
            LogD3D9FirstFailure("SetTransform", This, hr);
            LogError("[D3D9 error] SetTransform first failure — State=%u", static_cast<unsigned>(State));
        }
    }
    return hr;
}

static HRESULT STDMETHODCALLTYPE SetRenderState_Detour(IDirect3DDevice9* This, D3DRENDERSTATETYPE State, DWORD Value) {
    RECORD_DETOUR_CALL(utils::get_now_ns());
    HRESULT hr = SetRenderState_Original(This, State, Value);
    if (FAILED(hr)) {
        LogD3D9Error("SetRenderState", hr);
        if (g_first_SetRenderState_error.exchange(false)) {
            LogD3D9FirstFailure("SetRenderState", This, hr);
            LogError("[D3D9 error] SetRenderState first failure — State=%u Value=%lu", static_cast<unsigned>(State), static_cast<unsigned long>(Value));
        }
    }
    return hr;
}

static HRESULT STDMETHODCALLTYPE GetTexture_Detour(IDirect3DDevice9* This, DWORD Stage,
                                                    IDirect3DBaseTexture9** ppTexture) {
    RECORD_DETOUR_CALL(utils::get_now_ns());
    HRESULT hr = GetTexture_Original(This, Stage, ppTexture);
    if (FAILED(hr)) {
        LogD3D9Error("GetTexture", hr);
        if (g_first_GetTexture_error.exchange(false)) {
            LogD3D9FirstFailure("GetTexture", This, hr);
            LogError("[D3D9 error] GetTexture first failure — Stage=%lu", static_cast<unsigned long>(Stage));
        }
    }
    return hr;
}

static HRESULT STDMETHODCALLTYPE SetTexture_Detour(IDirect3DDevice9* This, DWORD Stage,
                                                    IDirect3DBaseTexture9* pTexture) {
    RECORD_DETOUR_CALL(utils::get_now_ns());
    HRESULT hr = SetTexture_Original(This, Stage, pTexture);
    if (FAILED(hr)) {
        LogD3D9Error("SetTexture", hr);
        if (g_first_SetTexture_error.exchange(false)) {
            LogD3D9FirstFailure("SetTexture", This, hr);
            LogError("[D3D9 error] SetTexture first failure — Stage=%lu pTexture=%p", static_cast<unsigned long>(Stage), static_cast<void*>(pTexture));
        }
    }
    return hr;
}

static HRESULT STDMETHODCALLTYPE SetVertexShader_Detour(IDirect3DDevice9* This, IDirect3DVertexShader9* pShader) {
    RECORD_DETOUR_CALL(utils::get_now_ns());
    HRESULT hr = SetVertexShader_Original(This, pShader);
    if (FAILED(hr)) {
        LogD3D9Error("SetVertexShader", hr);
        if (g_first_SetVertexShader_error.exchange(false)) {
            LogD3D9FirstFailure("SetVertexShader", This, hr);
            LogError("[D3D9 error] SetVertexShader first failure — pShader=%p", static_cast<void*>(pShader));
        }
    }
    return hr;
}

static HRESULT STDMETHODCALLTYPE SetPixelShader_Detour(IDirect3DDevice9* This, IDirect3DPixelShader9* pShader) {
    RECORD_DETOUR_CALL(utils::get_now_ns());
    HRESULT hr = SetPixelShader_Original(This, pShader);
    if (FAILED(hr)) {
        LogD3D9Error("SetPixelShader", hr);
        if (g_first_SetPixelShader_error.exchange(false)) {
            LogD3D9FirstFailure("SetPixelShader", This, hr);
            LogError("[D3D9 error] SetPixelShader first failure — pShader=%p", static_cast<void*>(pShader));
        }
    }
    return hr;
}

static HRESULT STDMETHODCALLTYPE CreateRenderTargetEx_Detour(
    IDirect3DDevice9* This, UINT Width, UINT Height, D3DFORMAT Format, D3DMULTISAMPLE_TYPE MultiSample,
    DWORD MultisampleQuality, BOOL Lockable, IDirect3DSurface9** ppSurface, HANDLE* pSharedHandle, DWORD Usage) {
    RECORD_DETOUR_CALL(utils::get_now_ns());
    HRESULT hr = CreateRenderTargetEx_Original(This, Width, Height, Format, MultiSample, MultisampleQuality, Lockable,
                                               ppSurface, pSharedHandle, Usage);
    if (FAILED(hr)) {
        LogD3D9Error("CreateRenderTargetEx", hr);
        if (g_first_CreateRenderTargetEx_error.exchange(false)) {
            LogD3D9FirstFailure("CreateRenderTargetEx", This, hr);
            LogError("[D3D9 error] CreateRenderTargetEx first failure — Width=%u Height=%u Format=%u Usage=0x%lX",
                     Width, Height, static_cast<unsigned>(Format), static_cast<unsigned long>(Usage));
        }
    }
    return hr;
}

static HRESULT STDMETHODCALLTYPE CreateOffscreenPlainSurfaceEx_Detour(
    IDirect3DDevice9* This, UINT Width, UINT Height, D3DFORMAT Format, D3DPOOL Pool, IDirect3DSurface9** ppSurface,
    HANDLE* pSharedHandle, DWORD Usage) {
    RECORD_DETOUR_CALL(utils::get_now_ns());
    HRESULT hr = CreateOffscreenPlainSurfaceEx_Original(This, Width, Height, Format, Pool, ppSurface, pSharedHandle,
                                                         Usage);
    if (FAILED(hr)) {
        LogD3D9Error("CreateOffscreenPlainSurfaceEx", hr);
        if (g_first_CreateOffscreenPlainSurfaceEx_error.exchange(false)) {
            LogD3D9FirstFailure("CreateOffscreenPlainSurfaceEx", This, hr);
            LogError("[D3D9 error] CreateOffscreenPlainSurfaceEx first failure — Width=%u Height=%u Format=%u", Width,
                     Height, static_cast<unsigned>(Format));
        }
    }
    return hr;
}

static HRESULT STDMETHODCALLTYPE CreateDepthStencilSurfaceEx_Detour(
    IDirect3DDevice9* This, UINT Width, UINT Height, D3DFORMAT Format, D3DMULTISAMPLE_TYPE MultiSample,
    DWORD MultisampleQuality, BOOL Discard, IDirect3DSurface9** ppSurface, HANDLE* pSharedHandle, DWORD Usage) {
    RECORD_DETOUR_CALL(utils::get_now_ns());
    HRESULT hr = CreateDepthStencilSurfaceEx_Original(This, Width, Height, Format, MultiSample, MultisampleQuality,
                                                       Discard, ppSurface, pSharedHandle, Usage);
    if (FAILED(hr)) {
        LogD3D9Error("CreateDepthStencilSurfaceEx", hr);
        if (g_first_CreateDepthStencilSurfaceEx_error.exchange(false)) {
            LogD3D9FirstFailure("CreateDepthStencilSurfaceEx", This, hr);
            LogError("[D3D9 error] CreateDepthStencilSurfaceEx first failure — Width=%u Height=%u", Width, Height);
        }
    }
    return hr;
}

static HRESULT STDMETHODCALLTYPE ResetEx_Detour(IDirect3DDevice9* This,
                                                D3DPRESENT_PARAMETERS* pPresentationParameters,
                                                D3DDISPLAYMODEEX* pFullscreenDisplayMode) {
    RECORD_DETOUR_CALL(utils::get_now_ns());
    HRESULT hr = ResetEx_Original(This, pPresentationParameters, pFullscreenDisplayMode);
    if (FAILED(hr)) {
        LogD3D9Error("ResetEx", hr);
        if (g_first_ResetEx_error.exchange(false)) {
            LogD3D9FirstFailure("ResetEx", This, hr);
            LogError("[D3D9 error] ResetEx first failure — pPresentationParameters=%p pFullscreenDisplayMode=%p",
                     static_cast<void*>(pPresentationParameters),
                     static_cast<void*>(pFullscreenDisplayMode));
        }
    }
    return hr;
}

static HRESULT STDMETHODCALLTYPE GetDisplayModeEx_Detour(IDirect3DDevice9* This, UINT iSwapChain,
                                                          D3DDISPLAYMODEEX* pMode,
                                                          D3DDISPLAYROTATION* pRotation) {
    RECORD_DETOUR_CALL(utils::get_now_ns());
    HRESULT hr = GetDisplayModeEx_Original(This, iSwapChain, pMode, pRotation);
    if (FAILED(hr)) {
        LogD3D9Error("GetDisplayModeEx", hr);
        if (g_first_GetDisplayModeEx_error.exchange(false)) {
            LogD3D9FirstFailure("GetDisplayModeEx", This, hr);
            LogError("[D3D9 error] GetDisplayModeEx first failure — iSwapChain=%u", iSwapChain);
        }
    }
    return hr;
}

static HRESULT STDMETHODCALLTYPE CheckDeviceState_Detour(IDirect3DDevice9* This, HWND hDestinationWindow) {
    RECORD_DETOUR_CALL(utils::get_now_ns());
    HRESULT hr = CheckDeviceState_Original(This, hDestinationWindow);
    if (FAILED(hr)) {
        LogD3D9Error("CheckDeviceState", hr);
        if (g_first_CheckDeviceState_error.exchange(false)) {
            LogD3D9FirstFailure("CheckDeviceState", This, hr);
            LogError("[D3D9 error] CheckDeviceState first failure — hDestinationWindow=%p",
                     static_cast<void*>(hDestinationWindow));
        }
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

    LogInfo("InstallD3D9DeviceVtableLogging: installing for device=%p", static_cast<void*>(device));

    void** vtable = *reinterpret_cast<void***>(device);
    if (vtable == nullptr) {
        g_vtable_logging_installed.store(false);
        LogWarn("InstallD3D9DeviceVtableLogging: failed to get vtable");
        return;
    }

    bool ok = true;
    if (!CreateAndEnableHook(vtable[VTable::Reset], reinterpret_cast<LPVOID>(&Reset_Detour),
                             reinterpret_cast<LPVOID*>(&Reset_Original), "IDirect3DDevice9::Reset")) {
        LogWarn("InstallD3D9DeviceVtableLogging: Reset hook failed");
        ok = false;
    }
    if (!CreateAndEnableHook(vtable[VTable::BeginScene], reinterpret_cast<LPVOID>(&BeginScene_Detour),
                             reinterpret_cast<LPVOID*>(&BeginScene_Original), "IDirect3DDevice9::BeginScene")) {
        LogWarn("InstallD3D9DeviceVtableLogging: BeginScene hook failed");
        ok = false;
    }
    if (!CreateAndEnableHook(vtable[VTable::EndScene], reinterpret_cast<LPVOID>(&EndScene_Detour),
                             reinterpret_cast<LPVOID*>(&EndScene_Original), "IDirect3DDevice9::EndScene")) {
        LogWarn("InstallD3D9DeviceVtableLogging: EndScene hook failed");
        ok = false;
    }
    if (!CreateAndEnableHook(vtable[VTable::Clear], reinterpret_cast<LPVOID>(&Clear_Detour),
                             reinterpret_cast<LPVOID*>(&Clear_Original), "IDirect3DDevice9::Clear")) {
        LogWarn("InstallD3D9DeviceVtableLogging: Clear hook failed");
        ok = false;
    }
    if (!CreateAndEnableHook(vtable[VTable::TestCooperativeLevel],
                             reinterpret_cast<LPVOID>(&TestCooperativeLevel_Detour),
                             reinterpret_cast<LPVOID*>(&TestCooperativeLevel_Original),
                             "IDirect3DDevice9::TestCooperativeLevel")) {
        LogWarn("InstallD3D9DeviceVtableLogging: TestCooperativeLevel hook failed");
        ok = false;
    }
    if (!CreateAndEnableHook(vtable[VTable::CreateAdditionalSwapChain],
                             reinterpret_cast<LPVOID>(&CreateAdditionalSwapChain_Detour),
                             reinterpret_cast<LPVOID*>(&CreateAdditionalSwapChain_Original),
                             "IDirect3DDevice9::CreateAdditionalSwapChain")) {
        LogWarn("InstallD3D9DeviceVtableLogging: CreateAdditionalSwapChain hook failed");
        ok = false;
    }
    if (!CreateAndEnableHook(vtable[VTable::GetBackBuffer], reinterpret_cast<LPVOID>(&GetBackBuffer_Detour),
                             reinterpret_cast<LPVOID*>(&GetBackBuffer_Original), "IDirect3DDevice9::GetBackBuffer")) {
        LogWarn("InstallD3D9DeviceVtableLogging: GetBackBuffer hook failed");
        ok = false;
    }
    if (!CreateAndEnableHook(vtable[VTable::GetSwapChain], reinterpret_cast<LPVOID>(&GetSwapChain_Detour),
                             reinterpret_cast<LPVOID*>(&GetSwapChain_Original),
                             "IDirect3DDevice9::GetSwapChain")) {
        LogWarn("InstallD3D9DeviceVtableLogging: GetSwapChain hook failed");
        ok = false;
    }
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
    if (!CreateAndEnableHook(vtable[VTable::UpdateSurface], reinterpret_cast<LPVOID>(&UpdateSurface_Detour),
                             reinterpret_cast<LPVOID*>(&UpdateSurface_Original),
                             "IDirect3DDevice9::UpdateSurface")) {
        LogWarn("InstallD3D9DeviceVtableLogging: UpdateSurface hook failed");
        ok = false;
    }
    if (!CreateAndEnableHook(vtable[VTable::UpdateTexture], reinterpret_cast<LPVOID>(&UpdateTexture_Detour),
                             reinterpret_cast<LPVOID*>(&UpdateTexture_Original),
                             "IDirect3DDevice9::UpdateTexture")) {
        LogWarn("InstallD3D9DeviceVtableLogging: UpdateTexture hook failed");
        ok = false;
    }
    if (!CreateAndEnableHook(vtable[VTable::GetRenderTargetData],
                             reinterpret_cast<LPVOID>(&GetRenderTargetData_Detour),
                             reinterpret_cast<LPVOID*>(&GetRenderTargetData_Original),
                             "IDirect3DDevice9::GetRenderTargetData")) {
        LogWarn("InstallD3D9DeviceVtableLogging: GetRenderTargetData hook failed");
        ok = false;
    }
    if (!CreateAndEnableHook(vtable[VTable::GetFrontBufferData],
                             reinterpret_cast<LPVOID>(&GetFrontBufferData_Detour),
                             reinterpret_cast<LPVOID*>(&GetFrontBufferData_Original),
                             "IDirect3DDevice9::GetFrontBufferData")) {
        LogWarn("InstallD3D9DeviceVtableLogging: GetFrontBufferData hook failed");
        ok = false;
    }
    if (!CreateAndEnableHook(vtable[VTable::StretchRect], reinterpret_cast<LPVOID>(&StretchRect_Detour),
                             reinterpret_cast<LPVOID*>(&StretchRect_Original), "IDirect3DDevice9::StretchRect")) {
        LogWarn("InstallD3D9DeviceVtableLogging: StretchRect hook failed");
        ok = false;
    }
    if (!CreateAndEnableHook(vtable[VTable::ColorFill], reinterpret_cast<LPVOID>(&ColorFill_Detour),
                             reinterpret_cast<LPVOID*>(&ColorFill_Original), "IDirect3DDevice9::ColorFill")) {
        LogWarn("InstallD3D9DeviceVtableLogging: ColorFill hook failed");
        ok = false;
    }
    if (!CreateAndEnableHook(vtable[VTable::SetRenderTarget], reinterpret_cast<LPVOID>(&SetRenderTarget_Detour),
                             reinterpret_cast<LPVOID*>(&SetRenderTarget_Original),
                             "IDirect3DDevice9::SetRenderTarget")) {
        LogWarn("InstallD3D9DeviceVtableLogging: SetRenderTarget hook failed");
        ok = false;
    }
    if (!CreateAndEnableHook(vtable[VTable::SetDepthStencilSurface],
                             reinterpret_cast<LPVOID>(&SetDepthStencilSurface_Detour),
                             reinterpret_cast<LPVOID*>(&SetDepthStencilSurface_Original),
                             "IDirect3DDevice9::SetDepthStencilSurface")) {
        LogWarn("InstallD3D9DeviceVtableLogging: SetDepthStencilSurface hook failed");
        ok = false;
    }
    if (!CreateAndEnableHook(vtable[VTable::CreateStateBlock], reinterpret_cast<LPVOID>(&CreateStateBlock_Detour),
                             reinterpret_cast<LPVOID*>(&CreateStateBlock_Original),
                             "IDirect3DDevice9::CreateStateBlock")) {
        LogWarn("InstallD3D9DeviceVtableLogging: CreateStateBlock hook failed");
        ok = false;
    }
    if (!CreateAndEnableHook(vtable[VTable::EndStateBlock], reinterpret_cast<LPVOID>(&EndStateBlock_Detour),
                             reinterpret_cast<LPVOID*>(&EndStateBlock_Original),
                             "IDirect3DDevice9::EndStateBlock")) {
        LogWarn("InstallD3D9DeviceVtableLogging: EndStateBlock hook failed");
        ok = false;
    }
    if (!CreateAndEnableHook(vtable[VTable::BeginStateBlock], reinterpret_cast<LPVOID>(&BeginStateBlock_Detour),
                             reinterpret_cast<LPVOID*>(&BeginStateBlock_Original),
                             "IDirect3DDevice9::BeginStateBlock")) {
        LogWarn("InstallD3D9DeviceVtableLogging: BeginStateBlock hook failed");
        ok = false;
    }
    if (!CreateAndEnableHook(vtable[VTable::CreateVertexDeclaration],
                             reinterpret_cast<LPVOID>(&CreateVertexDeclaration_Detour),
                             reinterpret_cast<LPVOID*>(&CreateVertexDeclaration_Original),
                             "IDirect3DDevice9::CreateVertexDeclaration")) {
        LogWarn("InstallD3D9DeviceVtableLogging: CreateVertexDeclaration hook failed");
        ok = false;
    }
    if (!CreateAndEnableHook(vtable[VTable::CreateVertexShader], reinterpret_cast<LPVOID>(&CreateVertexShader_Detour),
                             reinterpret_cast<LPVOID*>(&CreateVertexShader_Original),
                             "IDirect3DDevice9::CreateVertexShader")) {
        LogWarn("InstallD3D9DeviceVtableLogging: CreateVertexShader hook failed");
        ok = false;
    }
    if (!CreateAndEnableHook(vtable[VTable::SetStreamSource], reinterpret_cast<LPVOID>(&SetStreamSource_Detour),
                             reinterpret_cast<LPVOID*>(&SetStreamSource_Original),
                             "IDirect3DDevice9::SetStreamSource")) {
        LogWarn("InstallD3D9DeviceVtableLogging: SetStreamSource hook failed");
        ok = false;
    }
    if (!CreateAndEnableHook(vtable[VTable::SetIndices], reinterpret_cast<LPVOID>(&SetIndices_Detour),
                             reinterpret_cast<LPVOID*>(&SetIndices_Original), "IDirect3DDevice9::SetIndices")) {
        LogWarn("InstallD3D9DeviceVtableLogging: SetIndices hook failed");
        ok = false;
    }
    if (!CreateAndEnableHook(vtable[VTable::CreatePixelShader], reinterpret_cast<LPVOID>(&CreatePixelShader_Detour),
                             reinterpret_cast<LPVOID*>(&CreatePixelShader_Original),
                             "IDirect3DDevice9::CreatePixelShader")) {
        LogWarn("InstallD3D9DeviceVtableLogging: CreatePixelShader hook failed");
        ok = false;
    }
    if (!CreateAndEnableHook(vtable[VTable::CreateQuery], reinterpret_cast<LPVOID>(&CreateQuery_Detour),
                             reinterpret_cast<LPVOID*>(&CreateQuery_Original),
                             "IDirect3DDevice9::CreateQuery")) {
        LogWarn("InstallD3D9DeviceVtableLogging: CreateQuery hook failed");
        ok = false;
    }
    if (!CreateAndEnableHook(vtable[VTable::DrawPrimitive], reinterpret_cast<LPVOID>(&DrawPrimitive_Detour),
                             reinterpret_cast<LPVOID*>(&DrawPrimitive_Original),
                             "IDirect3DDevice9::DrawPrimitive")) {
        LogWarn("InstallD3D9DeviceVtableLogging: DrawPrimitive hook failed");
        ok = false;
    }
    if (!CreateAndEnableHook(vtable[VTable::DrawIndexedPrimitive],
                             reinterpret_cast<LPVOID>(&DrawIndexedPrimitive_Detour),
                             reinterpret_cast<LPVOID*>(&DrawIndexedPrimitive_Original),
                             "IDirect3DDevice9::DrawIndexedPrimitive")) {
        LogWarn("InstallD3D9DeviceVtableLogging: DrawIndexedPrimitive hook failed");
        ok = false;
    }
    if (!CreateAndEnableHook(vtable[VTable::DrawPrimitiveUP], reinterpret_cast<LPVOID>(&DrawPrimitiveUP_Detour),
                             reinterpret_cast<LPVOID*>(&DrawPrimitiveUP_Original),
                             "IDirect3DDevice9::DrawPrimitiveUP")) {
        LogWarn("InstallD3D9DeviceVtableLogging: DrawPrimitiveUP hook failed");
        ok = false;
    }
    if (!CreateAndEnableHook(vtable[VTable::DrawIndexedPrimitiveUP],
                             reinterpret_cast<LPVOID>(&DrawIndexedPrimitiveUP_Detour),
                             reinterpret_cast<LPVOID*>(&DrawIndexedPrimitiveUP_Original),
                             "IDirect3DDevice9::DrawIndexedPrimitiveUP")) {
        LogWarn("InstallD3D9DeviceVtableLogging: DrawIndexedPrimitiveUP hook failed");
        ok = false;
    }
    if (!CreateAndEnableHook(vtable[VTable::ProcessVertices], reinterpret_cast<LPVOID>(&ProcessVertices_Detour),
                             reinterpret_cast<LPVOID*>(&ProcessVertices_Original),
                             "IDirect3DDevice9::ProcessVertices")) {
        LogWarn("InstallD3D9DeviceVtableLogging: ProcessVertices hook failed");
        ok = false;
    }
    if (!CreateAndEnableHook(vtable[VTable::SetVertexDeclaration],
                             reinterpret_cast<LPVOID>(&SetVertexDeclaration_Detour),
                             reinterpret_cast<LPVOID*>(&SetVertexDeclaration_Original),
                             "IDirect3DDevice9::SetVertexDeclaration")) {
        LogWarn("InstallD3D9DeviceVtableLogging: SetVertexDeclaration hook failed");
        ok = false;
    }
    if (!CreateAndEnableHook(vtable[VTable::SetFVF], reinterpret_cast<LPVOID>(&SetFVF_Detour),
                             reinterpret_cast<LPVOID*>(&SetFVF_Original), "IDirect3DDevice9::SetFVF")) {
        LogWarn("InstallD3D9DeviceVtableLogging: SetFVF hook failed");
        ok = false;
    }
    if (!CreateAndEnableHook(vtable[VTable::SetStreamSourceFreq],
                             reinterpret_cast<LPVOID>(&SetStreamSourceFreq_Detour),
                             reinterpret_cast<LPVOID*>(&SetStreamSourceFreq_Original),
                             "IDirect3DDevice9::SetStreamSourceFreq")) {
        LogWarn("InstallD3D9DeviceVtableLogging: SetStreamSourceFreq hook failed");
        ok = false;
    }
    if (!CreateAndEnableHook(vtable[VTable::GetRenderTarget], reinterpret_cast<LPVOID>(&GetRenderTarget_Detour),
                             reinterpret_cast<LPVOID*>(&GetRenderTarget_Original),
                             "IDirect3DDevice9::GetRenderTarget")) {
        LogWarn("InstallD3D9DeviceVtableLogging: GetRenderTarget hook failed");
        ok = false;
    }
    if (!CreateAndEnableHook(vtable[VTable::GetDepthStencilSurface],
                             reinterpret_cast<LPVOID>(&GetDepthStencilSurface_Detour),
                             reinterpret_cast<LPVOID*>(&GetDepthStencilSurface_Original),
                             "IDirect3DDevice9::GetDepthStencilSurface")) {
        LogWarn("InstallD3D9DeviceVtableLogging: GetDepthStencilSurface hook failed");
        ok = false;
    }
    if (!CreateAndEnableHook(vtable[VTable::SetViewport], reinterpret_cast<LPVOID>(&SetViewport_Detour),
                             reinterpret_cast<LPVOID*>(&SetViewport_Original),
                             "IDirect3DDevice9::SetViewport")) {
        LogWarn("InstallD3D9DeviceVtableLogging: SetViewport hook failed");
        ok = false;
    }
    if (!CreateAndEnableHook(vtable[VTable::SetTransform], reinterpret_cast<LPVOID>(&SetTransform_Detour),
                             reinterpret_cast<LPVOID*>(&SetTransform_Original),
                             "IDirect3DDevice9::SetTransform")) {
        LogWarn("InstallD3D9DeviceVtableLogging: SetTransform hook failed");
        ok = false;
    }
    if (!CreateAndEnableHook(vtable[VTable::SetRenderState], reinterpret_cast<LPVOID>(&SetRenderState_Detour),
                             reinterpret_cast<LPVOID*>(&SetRenderState_Original),
                             "IDirect3DDevice9::SetRenderState")) {
        LogWarn("InstallD3D9DeviceVtableLogging: SetRenderState hook failed");
        ok = false;
    }
    if (!CreateAndEnableHook(vtable[VTable::GetTexture], reinterpret_cast<LPVOID>(&GetTexture_Detour),
                             reinterpret_cast<LPVOID*>(&GetTexture_Original),
                             "IDirect3DDevice9::GetTexture")) {
        LogWarn("InstallD3D9DeviceVtableLogging: GetTexture hook failed");
        ok = false;
    }
    if (!CreateAndEnableHook(vtable[VTable::SetTexture], reinterpret_cast<LPVOID>(&SetTexture_Detour),
                             reinterpret_cast<LPVOID*>(&SetTexture_Original), "IDirect3DDevice9::SetTexture")) {
        LogWarn("InstallD3D9DeviceVtableLogging: SetTexture hook failed");
        ok = false;
    }
    if (!CreateAndEnableHook(vtable[VTable::SetVertexShader], reinterpret_cast<LPVOID>(&SetVertexShader_Detour),
                             reinterpret_cast<LPVOID*>(&SetVertexShader_Original),
                             "IDirect3DDevice9::SetVertexShader")) {
        LogWarn("InstallD3D9DeviceVtableLogging: SetVertexShader hook failed");
        ok = false;
    }
    if (!CreateAndEnableHook(vtable[VTable::SetPixelShader], reinterpret_cast<LPVOID>(&SetPixelShader_Detour),
                             reinterpret_cast<LPVOID*>(&SetPixelShader_Original),
                             "IDirect3DDevice9::SetPixelShader")) {
        LogWarn("InstallD3D9DeviceVtableLogging: SetPixelShader hook failed");
        ok = false;
    }
    if (!CreateAndEnableHook(vtable[VTable::CheckDeviceState], reinterpret_cast<LPVOID>(&CheckDeviceState_Detour),
                             reinterpret_cast<LPVOID*>(&CheckDeviceState_Original),
                             "IDirect3DDevice9Ex::CheckDeviceState")) {
        LogWarn("InstallD3D9DeviceVtableLogging: CheckDeviceState hook failed");
        ok = false;
    }
    if (!CreateAndEnableHook(vtable[VTable::CreateRenderTargetEx],
                             reinterpret_cast<LPVOID>(&CreateRenderTargetEx_Detour),
                             reinterpret_cast<LPVOID*>(&CreateRenderTargetEx_Original),
                             "IDirect3DDevice9Ex::CreateRenderTargetEx")) {
        LogWarn("InstallD3D9DeviceVtableLogging: CreateRenderTargetEx hook failed");
        ok = false;
    }
    if (!CreateAndEnableHook(vtable[VTable::CreateOffscreenPlainSurfaceEx],
                             reinterpret_cast<LPVOID>(&CreateOffscreenPlainSurfaceEx_Detour),
                             reinterpret_cast<LPVOID*>(&CreateOffscreenPlainSurfaceEx_Original),
                             "IDirect3DDevice9Ex::CreateOffscreenPlainSurfaceEx")) {
        LogWarn("InstallD3D9DeviceVtableLogging: CreateOffscreenPlainSurfaceEx hook failed");
        ok = false;
    }
    if (!CreateAndEnableHook(vtable[VTable::CreateDepthStencilSurfaceEx],
                             reinterpret_cast<LPVOID>(&CreateDepthStencilSurfaceEx_Detour),
                             reinterpret_cast<LPVOID*>(&CreateDepthStencilSurfaceEx_Original),
                             "IDirect3DDevice9Ex::CreateDepthStencilSurfaceEx")) {
        LogWarn("InstallD3D9DeviceVtableLogging: CreateDepthStencilSurfaceEx hook failed");
        ok = false;
    }
    if (!CreateAndEnableHook(vtable[VTable::ResetEx], reinterpret_cast<LPVOID>(&ResetEx_Detour),
                             reinterpret_cast<LPVOID*>(&ResetEx_Original), "IDirect3DDevice9Ex::ResetEx")) {
        LogWarn("InstallD3D9DeviceVtableLogging: ResetEx hook failed");
        ok = false;
    }
    if (!CreateAndEnableHook(vtable[VTable::GetDisplayModeEx], reinterpret_cast<LPVOID>(&GetDisplayModeEx_Detour),
                             reinterpret_cast<LPVOID*>(&GetDisplayModeEx_Original),
                             "IDirect3DDevice9Ex::GetDisplayModeEx")) {
        LogWarn("InstallD3D9DeviceVtableLogging: GetDisplayModeEx hook failed");
        ok = false;
    }

    if (ok) {
        LogInfo(
            "InstallD3D9DeviceVtableLogging: device vtable logging installed (59 methods: Reset, BeginScene, EndScene, "
            "Clear, TestCooperativeLevel, CreateAdditionalSwapChain, GetBackBuffer, GetSwapChain, CreateTexture, "
            "CreateVolumeTexture, CreateCubeTexture, CreateVertexBuffer, CreateIndexBuffer, CreateRenderTarget, "
            "CreateDepthStencilSurface, CreateOffscreenPlainSurface, UpdateSurface, UpdateTexture, GetRenderTargetData, "
            "GetFrontBufferData, StretchRect, ColorFill, SetRenderTarget, GetRenderTarget, SetDepthStencilSurface, "
            "GetDepthStencilSurface, CreateStateBlock, BeginStateBlock, EndStateBlock, CreateVertexDeclaration, "
            "CreateVertexShader, SetStreamSource, SetIndices, CreatePixelShader, CreateQuery, DrawPrimitive, "
            "DrawIndexedPrimitive, DrawPrimitiveUP, DrawIndexedPrimitiveUP, ProcessVertices, SetVertexDeclaration, "
            "SetFVF, SetStreamSourceFreq, SetViewport, SetTransform, SetRenderState, GetTexture, SetTexture, "
            "SetVertexShader, SetPixelShader, CheckDeviceState, CreateRenderTargetEx, CreateOffscreenPlainSurfaceEx, "
            "CreateDepthStencilSurfaceEx, ResetEx, GetDisplayModeEx)");
    }
}

}  // namespace display_commanderhooks::d3d9
