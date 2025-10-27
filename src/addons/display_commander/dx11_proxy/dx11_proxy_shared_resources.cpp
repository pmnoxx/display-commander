/**
 * DX11 Proxy Shared Resources Implementation
 */

#include "dx11_proxy_shared_resources.hpp"
#include "../utils.hpp"
#include "../utils/logging.hpp"
#include <sstream>

namespace dx11_proxy {

DXGI_FORMAT SharedResourceManager::D3D9FormatToDXGI(D3DFORMAT d3d9_format) {
    switch (d3d9_format) {
        case D3DFMT_A8R8G8B8:
        case D3DFMT_X8R8G8B8:
            return DXGI_FORMAT_B8G8R8A8_UNORM;

        case D3DFMT_A16B16G16R16F:
            return DXGI_FORMAT_R16G16B16A16_FLOAT;

        case D3DFMT_A32B32G32R32F:
            return DXGI_FORMAT_R32G32B32A32_FLOAT;

        case D3DFMT_A2R10G10B10:
            return DXGI_FORMAT_R10G10B10A2_UNORM;

        default:
            // Default to BGRA8
            LogWarn("SharedResourceManager: Unknown D3D9 format %d, defaulting to BGRA8", d3d9_format);
            return DXGI_FORMAT_B8G8R8A8_UNORM;
    }
}

bool SharedResourceManager::Initialize(IDirect3DDevice9* d3d9_device,
                                      ID3D11Device* d3d11_device,
                                      uint32_t width,
                                      uint32_t height,
                                      D3DFORMAT d3d9_format) {
    utils::SRWLockExclusive lock(mutex_);

    if (is_initialized_) {
        LogInfo("SharedResourceManager::Initialize: Already initialized, cleaning up first");
        Cleanup();
    }

    LogInfo("SharedResourceManager::Initialize: Creating shared resources %ux%u", width, height);

    width_ = width;
    height_ = height;
    d3d9_format_ = d3d9_format;
    dxgi_format_ = D3D9FormatToDXGI(d3d9_format);

    // Step 1: Create shared DX9 surface
    // Must use D3DPOOL_DEFAULT for sharing
    HRESULT hr = d3d9_device->CreateOffscreenPlainSurface(
        width,
        height,
        d3d9_format,
        D3DPOOL_DEFAULT,
        &d3d9_shared_surface_,
        &shared_handle_  // This creates the shared handle
    );

    if (FAILED(hr) || !shared_handle_) {
        std::stringstream ss;
        ss << "SharedResourceManager::Initialize: Failed to create shared DX9 surface, HRESULT: 0x"
           << std::hex << hr;
        LogError(ss.str().c_str());
        return false;
    }

    LogInfo("SharedResourceManager::Initialize: DX9 shared surface created with handle: 0x%p", shared_handle_);

    // Step 2: Open shared handle in DX11
    hr = d3d11_device->OpenSharedResource(
        shared_handle_,
        __uuidof(ID3D11Texture2D),
        reinterpret_cast<void**>(d3d11_shared_texture_.GetAddressOf())
    );

    if (FAILED(hr)) {
        std::stringstream ss;
        ss << "SharedResourceManager::Initialize: Failed to open shared resource in DX11, HRESULT: 0x"
           << std::hex << hr;
        LogError(ss.str().c_str());
        Cleanup();
        return false;
    }

    LogInfo("SharedResourceManager::Initialize: DX11 shared texture opened successfully");

    // Step 3: Create shader resource view for the shared texture
    D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
    srv_desc.Format = dxgi_format_;
    srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srv_desc.Texture2D.MostDetailedMip = 0;
    srv_desc.Texture2D.MipLevels = 1;

    hr = d3d11_device->CreateShaderResourceView(
        d3d11_shared_texture_.Get(),
        &srv_desc,
        &d3d11_srv_
    );

    if (FAILED(hr)) {
        std::stringstream ss;
        ss << "SharedResourceManager::Initialize: Failed to create shader resource view, HRESULT: 0x"
           << std::hex << hr;
        LogError(ss.str().c_str());
        Cleanup();
        return false;
    }

    is_initialized_ = true;
    LogInfo("SharedResourceManager::Initialize: Shared resources initialized successfully");

    return true;
}

void SharedResourceManager::Cleanup() {
    LogInfo("SharedResourceManager::Cleanup: Cleaning up shared resources");

    d3d11_srv_.Reset();
    d3d11_shared_texture_.Reset();
    d3d9_shared_surface_.Reset();

    // Note: Don't close shared_handle_, it's managed by DX9
    shared_handle_ = nullptr;

    width_ = 0;
    height_ = 0;
    d3d9_format_ = D3DFMT_UNKNOWN;
    dxgi_format_ = DXGI_FORMAT_UNKNOWN;
    is_initialized_ = false;

    LogInfo("SharedResourceManager::Cleanup: Complete");
}

bool SharedResourceManager::TransferFrame(IDirect3DDevice9* d3d9_device, IDirect3DSurface9* d3d9_source) {
    if (!is_initialized_) {
        LogError("SharedResourceManager::TransferFrame: Not initialized");
        return false;
    }

    if (!d3d9_device || !d3d9_source) {
        LogError("SharedResourceManager::TransferFrame: Invalid parameters");
        return false;
    }

    // Copy from source surface to our shared surface
    // This makes the frame available to DX11
    HRESULT hr = d3d9_device->StretchRect(
        d3d9_source,              // Source (backbuffer)
        nullptr,                  // Source rect (nullptr = entire surface)
        d3d9_shared_surface_.Get(), // Destination (shared surface)
        nullptr,                  // Dest rect (nullptr = entire surface)
        D3DTEXF_NONE              // Filter (NONE = point sampling, fastest)
    );

    if (FAILED(hr)) {
        static bool logged_once = false;
        if (!logged_once) {
            std::stringstream ss;
            ss << "SharedResourceManager::TransferFrame: StretchRect failed, HRESULT: 0x"
               << std::hex << hr;
            LogError(ss.str().c_str());
            logged_once = true;
        }
        return false;
    }

    // Frame is now in shared surface, accessible from DX11
    return true;
}

} // namespace dx11_proxy

