#pragma once

#include <d3d11.h>

namespace display_commanderhooks {

// VTable function pointer types for D3D11 hooks
// ID3D11Device::CreateTexture2D - vtable index 0
using ID3D11Device_CreateTexture2D_pfn = HRESULT(STDMETHODCALLTYPE*)(ID3D11Device* This, const D3D11_TEXTURE2D_DESC* pDesc, const D3D11_SUBRESOURCE_DATA* pInitialData, ID3D11Texture2D** ppTexture2D);

// ID3D11DeviceContext::UpdateSubresource - vtable index 5
using ID3D11DeviceContext_UpdateSubresource_pfn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext* This, ID3D11Resource* pDstResource, UINT DstSubresource, const D3D11_BOX* pDstBox, const void* pSrcData, UINT SrcRowPitch, UINT SrcDepthPitch);

// ID3D11DeviceContext3::UpdateSubresource1 - vtable index 11
using ID3D11DeviceContext_UpdateSubresource1_pfn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext* This, ID3D11Resource* pDstResource, UINT DstSubresource, const D3D11_BOX* pDstBox, const void* pSrcData, UINT SrcRowPitch, UINT SrcDepthPitch, UINT CopyFlags);

// Original function pointers
extern ID3D11Device_CreateTexture2D_pfn ID3D11Device_CreateTexture2D_Original;
extern ID3D11DeviceContext_UpdateSubresource_pfn ID3D11DeviceContext_UpdateSubresource_Original;
extern ID3D11DeviceContext_UpdateSubresource1_pfn ID3D11DeviceContext_UpdateSubresource1_Original;

// Hook installation functions
bool InstallD3D11Hooks();

// Hook a specific D3D11 device (vtable hooking)
bool HookD3D11Device(ID3D11Device* device);

// Hook a specific D3D11 device context (vtable hooking)
bool HookD3D11DeviceContext(ID3D11DeviceContext* context);

} // namespace display_commanderhooks

