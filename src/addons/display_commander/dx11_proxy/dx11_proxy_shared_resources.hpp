/**
 * DX11 Proxy Shared Resources
 *
 * Manages shared resources between DX9 and DX11 for frame transfer
 */

#pragma once

#include <d3d9.h>
#include <d3d11.h>
#include <wrl/client.h>
#include <cstdint>
#include "../utils/srwlock_wrapper.hpp"

namespace dx11_proxy {

using Microsoft::WRL::ComPtr;

/**
 * Manages shared resources for transferring frames between DX9 and DX11
 */
class SharedResourceManager {
public:
    static SharedResourceManager& GetInstance() {
        static SharedResourceManager instance;
        return instance;
    }

    // Delete copy/move constructors
    SharedResourceManager(const SharedResourceManager&) = delete;
    SharedResourceManager& operator=(const SharedResourceManager&) = delete;
    SharedResourceManager(SharedResourceManager&&) = delete;
    SharedResourceManager& operator=(SharedResourceManager&&) = delete;

    /**
     * Initialize shared resources
     * @param d3d9_device DX9 device to create shared surface
     * @param d3d11_device DX11 device to open shared texture
     * @param width Width of the shared resource
     * @param height Height of the shared resource
     * @param format DX9 format (will be converted to compatible DXGI format)
     * @return true if initialization succeeded
     */
    bool Initialize(IDirect3DDevice9* d3d9_device,
                   ID3D11Device* d3d11_device,
                   uint32_t width,
                   uint32_t height,
                   D3DFORMAT d3d9_format);

    /**
     * Cleanup shared resources
     */
    void Cleanup();

    /**
     * Check if shared resources are initialized
     */
    bool IsInitialized() const { return is_initialized_; }

    /**
     * Transfer frame from DX9 to DX11
     * @param d3d9_device DX9 device
     * @param d3d9_source Source DX9 surface (usually backbuffer)
     * @return true if transfer succeeded
     */
    bool TransferFrame(IDirect3DDevice9* d3d9_device, IDirect3DSurface9* d3d9_source);

    /**
     * Get the DX11 shared texture (for rendering/processing)
     */
    ID3D11Texture2D* GetDX11SharedTexture() const { return d3d11_shared_texture_.Get(); }

    /**
     * Get the DX11 shader resource view (for sampling the texture)
     */
    ID3D11ShaderResourceView* GetDX11ShaderResourceView() const { return d3d11_srv_.Get(); }

    /**
     * Get current dimensions
     */
    uint32_t GetWidth() const { return width_; }
    uint32_t GetHeight() const { return height_; }

private:
    SharedResourceManager() = default;
    ~SharedResourceManager() { Cleanup(); }

    // Convert D3D9 format to DXGI format
    DXGI_FORMAT D3D9FormatToDXGI(D3DFORMAT d3d9_format);

    // DX9 shared resources
    ComPtr<IDirect3DSurface9> d3d9_shared_surface_;
    HANDLE shared_handle_ = nullptr;

    // DX11 shared resources
    ComPtr<ID3D11Texture2D> d3d11_shared_texture_;
    ComPtr<ID3D11ShaderResourceView> d3d11_srv_;

    // State
    bool is_initialized_ = false;
    mutable SRWLOCK mutex_ = SRWLOCK_INIT;

    // Resource info
    uint32_t width_ = 0;
    uint32_t height_ = 0;
    D3DFORMAT d3d9_format_ = D3DFMT_UNKNOWN;
    DXGI_FORMAT dxgi_format_ = DXGI_FORMAT_UNKNOWN;
};

} // namespace dx11_proxy

