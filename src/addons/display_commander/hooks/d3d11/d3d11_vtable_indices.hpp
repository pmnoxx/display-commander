#pragma once

// ID3D11Device vtable indices. In the Windows SDK (d3d11.h), ID3D11Device
// inherits only from IUnknown — "ID3D11Device : public IUnknown". So slots
// 0-2 are IUnknown; slot 3 is the first ID3D11Device method (CreateBuffer),
// in declaration order.
namespace display_commanderhooks::d3d11 {

enum class VTable : unsigned int {
    // IUnknown (0-2)
    QueryInterface = 0,
    AddRef = 1,
    Release = 2,
    // ID3D11Device (3+) — SDK d3d11.h declaration order
    CreateBuffer = 3,
    CreateTexture1D = 4,
    CreateTexture2D = 5,
    CreateTexture3D = 6,
    CreateShaderResourceView = 7,
    CreateUnorderedAccessView = 8,
    CreateRenderTargetView = 9,
    CreateDepthStencilView = 10,
};

// Compile-time verification: must match Windows SDK d3d11.h ID3D11Device.
static_assert(static_cast<unsigned>(VTable::CreateBuffer) == 3,
              "ID3D11Device: CreateBuffer must be vtable index 3");
static_assert(static_cast<unsigned>(VTable::CreateTexture2D) == 5,
              "ID3D11Device: CreateTexture2D must be vtable index 5");
static_assert(static_cast<unsigned>(VTable::CreateDepthStencilView) == 10,
              "ID3D11Device: CreateDepthStencilView must be vtable index 10");

}  // namespace display_commanderhooks::d3d11
