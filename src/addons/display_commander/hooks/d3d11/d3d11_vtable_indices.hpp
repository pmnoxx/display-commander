#pragma once

// ID3D11Device vtable indices. Order matches Windows SDK d3d11.h declaration order:
//   IUnknown (0-2), ID3D11Object (3-6), ID3D11DeviceChild (7), ID3D11Device (8+).
// ID3D11Device methods 8..19 in SDK order: GetFeatureLevel, GetCreationFlags,
//   GetDeviceRemovedReason, GetImmediateContext, CreateBuffer, CreateTexture1D,
//   CreateTexture2D, CreateTexture3D, CreateShaderResourceView,
//   CreateUnorderedAccessView, CreateRenderTargetView, CreateDepthStencilView.
// Verify key indices at compile time (see e.g. Windows SDK d3d11.h MIDL interface).
namespace display_commanderhooks::d3d11 {

enum class VTable : unsigned int {
    // IUnknown (0-2)
    QueryInterface = 0,
    AddRef = 1,
    Release = 2,
    // ID3D11Object
    GetDevice = 3,
    SetPrivateData = 4,
    SetPrivateDataInterface = 5,
    GetPrivateData = 6,
    // ID3D11DeviceChild (one slot)
    // GetDevice = 7
    // ID3D11Device (Windows d3d11.h order)
    GetFeatureLevel = 8,
    GetCreationFlags = 9,
    GetDeviceRemovedReason = 10,
    GetImmediateContext = 11,
    CreateBuffer = 12,
    CreateTexture1D = 13,
    CreateTexture2D = 14,
    CreateTexture3D = 15,
    CreateShaderResourceView = 16,
    CreateUnorderedAccessView = 17,
    CreateRenderTargetView = 18,
    CreateDepthStencilView = 19,
};

// Compile-time verification of critical vtable indices (must match SDK d3d11.h).
static_assert(static_cast<unsigned>(VTable::CreateBuffer) == 12,
              "ID3D11Device: CreateBuffer must be vtable index 12");
static_assert(static_cast<unsigned>(VTable::CreateTexture2D) == 14,
              "ID3D11Device: CreateTexture2D must be vtable index 14");
static_assert(static_cast<unsigned>(VTable::CreateDepthStencilView) == 19,
              "ID3D11Device: CreateDepthStencilView must be vtable index 19");

}  // namespace display_commanderhooks::d3d11
