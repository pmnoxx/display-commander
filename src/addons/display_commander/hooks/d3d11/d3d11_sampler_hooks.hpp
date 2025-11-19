#pragma once

#include <d3d11.h>

namespace display_commanderhooks {

// VTable function pointer types for D3D11 sampler hooks
// ID3D11Device::CreateSamplerState - vtable index 23
using ID3D11Device_CreateSamplerState_pfn = HRESULT(STDMETHODCALLTYPE*)(ID3D11Device* This, const D3D11_SAMPLER_DESC* pSamplerDesc, ID3D11SamplerState** ppSamplerState);

// Original function pointers
extern ID3D11Device_CreateSamplerState_pfn ID3D11Device_CreateSamplerState_Original;

// Hook installation functions
bool HookD3D11DeviceSampler(ID3D11Device* device);

} // namespace display_commanderhooks

