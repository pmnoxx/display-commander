/**
 * DX11 Proxy Device Manager Implementation
 */

#include "dx11_proxy_manager.hpp"
#include "dx11_proxy_shared_resources.hpp"
#include "../utils.hpp"
#include <sstream>
#include <string>

namespace dx11_proxy {

bool DX11ProxyManager::Initialize(HWND game_hwnd, uint32_t width, uint32_t height, bool create_swapchain) {
    std::lock_guard lock(mutex_);

    if (is_initialized_.load()) {
        LogInfo("DX11ProxyManager::Initialize: Already initialized, shutting down first");
        CleanupResources();
    }

    LogInfo("DX11ProxyManager::Initialize: Starting initialization");

    game_hwnd_ = game_hwnd;
    swapchain_width_ = width;
    swapchain_height_ = height;

    // Step 1: Create DX11 device
    if (!CreateDevice()) {
        LogError("DX11ProxyManager::Initialize: Failed to create DX11 device");
        CleanupResources();
        return false;
    }

    // Step 2: Create swapchain (optional)
    if (create_swapchain) {
        if (!game_hwnd) {
            LogError("DX11ProxyManager::Initialize: HWND required for swapchain creation");
            CleanupResources();
            return false;
        }

        if (!CreateSwapChain(game_hwnd, width, height)) {
            LogWarn("DX11ProxyManager::Initialize: Failed to create swapchain (window may already have one)");
            LogInfo("DX11ProxyManager::Initialize: Continuing with device-only mode");
            // Don't fail - we can still use the device without our own swapchain
        }
    } else {
        LogInfo("DX11ProxyManager::Initialize: Swapchain creation skipped (device-only mode)");
    }

    is_initialized_.store(true);
    initialization_count_.fetch_add(1);

    std::stringstream ss;
    ss << "DX11ProxyManager::Initialize: Success! Device created";
    if (swapchain_) {
        ss << ", swapchain " << width << "x" << height;
    } else {
        ss << " (device-only mode, no swapchain)";
    }
    LogInfo(ss.str().c_str());

    return true;
}

void DX11ProxyManager::Shutdown() {
    std::lock_guard lock(mutex_);

    if (!is_initialized_.load()) {
        return;
    }

    LogInfo("DX11ProxyManager::Shutdown: Cleaning up resources");

    // Clean up shared resources first
    SharedResourceManager::GetInstance().Cleanup();

    CleanupResources();

    is_initialized_.store(false);
    shutdown_count_.fetch_add(1);

    LogInfo("DX11ProxyManager::Shutdown: Complete");
}

bool DX11ProxyManager::CreateDevice() {
    LogInfo("DX11ProxyManager::CreateDevice: Creating D3D11 device");

    // Load D3D11 functions dynamically (following the pattern from directx.hpp)
    HMODULE d3d11_module = LoadLibraryW(L"d3d11.dll");
    if (!d3d11_module) {
        LogError("DX11ProxyManager::CreateDevice: Failed to load d3d11.dll");
        return false;
    }

    auto pD3D11CreateDevice = reinterpret_cast<decltype(&D3D11CreateDevice)>(
        GetProcAddress(d3d11_module, "D3D11CreateDevice"));
    if (!pD3D11CreateDevice) {
        LogError("DX11ProxyManager::CreateDevice: Failed to get D3D11CreateDevice function");
        return false;
    }

    // Create device with hardware acceleration
    D3D_FEATURE_LEVEL feature_levels[] = {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0,
    };

    UINT create_flags = 0;
#ifdef _DEBUG
    create_flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    D3D_FEATURE_LEVEL feature_level_out;
    HRESULT hr = pD3D11CreateDevice(
        nullptr,                    // Adapter (null = default)
        D3D_DRIVER_TYPE_HARDWARE,   // Driver type
        nullptr,                    // Software rasterizer module
        create_flags,               // Flags
        feature_levels,             // Feature levels to try
        ARRAYSIZE(feature_levels),  // Number of feature levels
        D3D11_SDK_VERSION,          // SDK version
        &device_,                   // Output device
        &feature_level_out,         // Output feature level
        &context_                   // Output context
    );

    if (FAILED(hr)) {
        std::stringstream ss;
        ss << "DX11ProxyManager::CreateDevice: D3D11CreateDevice failed with HRESULT 0x"
           << std::hex << hr;
        LogError(ss.str().c_str());
        return false;
    }

    std::stringstream ss;
    ss << "DX11ProxyManager::CreateDevice: Success! Feature level: 0x"
       << std::hex << feature_level_out;
    LogInfo(ss.str().c_str());

    return true;
}

bool DX11ProxyManager::CreateSwapChain(HWND hwnd, uint32_t width, uint32_t height) {
    LogInfo("DX11ProxyManager::CreateSwapChain: Creating DXGI swapchain");

    // Load DXGI functions dynamically
    HMODULE dxgi_module = LoadLibraryW(L"dxgi.dll");
    if (!dxgi_module) {
        LogError("DX11ProxyManager::CreateSwapChain: Failed to load dxgi.dll");
        return false;
    }

    auto pCreateDXGIFactory2 = reinterpret_cast<decltype(&CreateDXGIFactory2)>(
        GetProcAddress(dxgi_module, "CreateDXGIFactory2"));
    if (!pCreateDXGIFactory2) {
        LogError("DX11ProxyManager::CreateSwapChain: Failed to get CreateDXGIFactory2");
        return false;
    }

    // Create DXGI factory
    HRESULT hr = pCreateDXGIFactory2(0, IID_PPV_ARGS(&factory_));
    if (FAILED(hr)) {
        std::stringstream ss;
        ss << "DX11ProxyManager::CreateSwapChain: CreateDXGIFactory2 failed with HRESULT 0x"
           << std::hex << hr;
        LogError(ss.str().c_str());
        return false;
    }

    // Configure swapchain description for modern flip model
    DXGI_SWAP_CHAIN_DESC1 desc = {};
    desc.Width = width;
    desc.Height = height;
    desc.Format = swapchain_format_;
    desc.Stereo = FALSE;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 2; // Double buffering minimum for flip model
    desc.Scaling = DXGI_SCALING_STRETCH;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD; // Modern flip model
    desc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
    desc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING; // Enable tearing for VRR

    // Get DXGI device from D3D11 device
    ComPtr<IDXGIDevice> dxgi_device;
    hr = device_.As(&dxgi_device);
    if (FAILED(hr)) {
        LogError("DX11ProxyManager::CreateSwapChain: Failed to get IDXGIDevice");
        return false;
    }

    // Create swapchain
    ComPtr<IDXGISwapChain1> swapchain1;
    hr = factory_->CreateSwapChainForHwnd(
        dxgi_device.Get(),
        hwnd,
        &desc,
        nullptr, // Fullscreen desc (null = windowed)
        nullptr, // Restrict output (null = no restriction)
        &swapchain1
    );

    if (FAILED(hr)) {
        std::stringstream ss;
        ss << "DX11ProxyManager::CreateSwapChain: CreateSwapChainForHwnd failed with HRESULT 0x"
           << std::hex << hr;
        LogError(ss.str().c_str());
        return false;
    }

    // Get IDXGISwapChain interface
    hr = swapchain1.As(&swapchain_);
    if (FAILED(hr)) {
        LogError("DX11ProxyManager::CreateSwapChain: Failed to get IDXGISwapChain");
        return false;
    }

    // Disable Alt+Enter fullscreen switching (we'll handle this ourselves)
    factory_->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);

    std::stringstream ss;
    ss << "DX11ProxyManager::CreateSwapChain: Success! " << width << "x" << height
       << ", format: " << swapchain_format_;
    LogInfo(ss.str().c_str());

    return true;
}

void DX11ProxyManager::CleanupResources() {
    // Release in reverse order of creation
    swapchain_.Reset();
    factory_.Reset();
    context_.Reset();
    device_.Reset();

    game_hwnd_ = nullptr;
    swapchain_width_ = 0;
    swapchain_height_ = 0;
}

DX11ProxyManager::Stats DX11ProxyManager::GetStats() const {
    Stats stats;
    stats.frames_generated = frames_generated_.load();
    stats.frames_presented = frames_presented_.load();
    stats.initialization_count = initialization_count_.load();
    stats.shutdown_count = shutdown_count_.load();
    stats.is_initialized = is_initialized_.load();
    stats.swapchain_width = swapchain_width_;
    stats.swapchain_height = swapchain_height_;
    stats.swapchain_format = swapchain_format_;
    stats.has_swapchain = (swapchain_ != nullptr);
    return stats;
}

} // namespace dx11_proxy

