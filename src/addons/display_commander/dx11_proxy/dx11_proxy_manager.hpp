/**
 * DX11 Proxy Device Manager
 *
 * Creates a separate DX11 device for presenting DX9 game content through a modern
 * DXGI swapchain. This enables HDR support, flip models, and other modern features
 * for older DX9 games.
 *
 * Approach (inspired by RenoDX):
 * 1. Game renders normally in DX9
 * 2. Create separate DX11 device + swapchain
 * 3. Use shared resources to transfer DX9 backbuffer to DX11
 * 4. Present through DX11 swapchain with modern DXGI features
 */

#pragma once

#include <d3d11.h>
#include <d3d11_4.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <atomic>
#include <mutex>
#include <cstdint>

namespace dx11_proxy {

using Microsoft::WRL::ComPtr;

/**
 * Manager for DX11 proxy device that presents DX9 content
 */
class DX11ProxyManager {
public:
    static DX11ProxyManager& GetInstance() {
        static DX11ProxyManager instance;
        return instance;
    }

    // Delete copy/move constructors
    DX11ProxyManager(const DX11ProxyManager&) = delete;
    DX11ProxyManager& operator=(const DX11ProxyManager&) = delete;
    DX11ProxyManager(DX11ProxyManager&&) = delete;
    DX11ProxyManager& operator=(DX11ProxyManager&&) = delete;

    /**
     * Initialize the DX11 proxy device
     * @param game_hwnd Window handle of the game (can be null if no swapchain)
     * @param width Desired width for swapchain (0 = no swapchain)
     * @param height Desired height for swapchain (0 = no swapchain)
     * @param create_swapchain Whether to create a swapchain (false = device only)
     * @return true if initialization succeeded
     */
    bool Initialize(HWND game_hwnd = nullptr, uint32_t width = 0, uint32_t height = 0, bool create_swapchain = false);

    /**
     * Shutdown and cleanup all DX11 proxy resources
     */
    void Shutdown();

    /**
     * Check if proxy device is initialized and ready
     */
    bool IsInitialized() const { return is_initialized_.load(); }

    /**
     * Get the DX11 device
     */
    ID3D11Device* GetDevice() const { return device_.Get(); }

    /**
     * Get the DX11 device context
     */
    ID3D11DeviceContext* GetContext() const { return context_.Get(); }

    /**
     * Get the DXGI swapchain
     */
    IDXGISwapChain* GetSwapChain() const { return swapchain_.Get(); }

    /**
     * Get current statistics
     */
    struct Stats {
        uint64_t frames_generated = 0;      // Frames processed through proxy
        uint64_t frames_presented = 0;      // Frames presented (future use)
        uint64_t initialization_count = 0;
        uint64_t shutdown_count = 0;
        bool is_initialized = false;
        uint32_t swapchain_width = 0;
        uint32_t swapchain_height = 0;
        DXGI_FORMAT swapchain_format = DXGI_FORMAT_UNKNOWN;
        bool has_swapchain = false;         // Whether proxy swapchain was created
    };
    Stats GetStats() const;

    /**
     * Increment frame generation counter
     * Called when a frame is processed through the proxy
     * This is called synchronously during Present() hook - NO separate thread needed
     */
    void IncrementFrameGenerated() {
        frames_generated_.fetch_add(1);
    }

private:
    DX11ProxyManager() = default;
    ~DX11ProxyManager() { Shutdown(); }

    // Create DX11 device
    bool CreateDevice();

    // Create DXGI swapchain
    bool CreateSwapChain(HWND hwnd, uint32_t width, uint32_t height);

    // Cleanup all resources
    void CleanupResources();

    // DX11 objects
    ComPtr<ID3D11Device> device_;
    ComPtr<ID3D11DeviceContext> context_;
    ComPtr<IDXGISwapChain> swapchain_;
    ComPtr<IDXGIFactory2> factory_;

    // State
    std::atomic<bool> is_initialized_{false};
    mutable std::mutex mutex_;

    // Configuration
    HWND game_hwnd_ = nullptr;
    uint32_t swapchain_width_ = 0;
    uint32_t swapchain_height_ = 0;
    DXGI_FORMAT swapchain_format_ = DXGI_FORMAT_R10G10B10A2_UNORM; // Default to HDR-capable format

    // Statistics
    std::atomic<uint64_t> frames_generated_{0};    // Frames processed through proxy
    std::atomic<uint64_t> frames_presented_{0};    // Frames presented (future use)
    std::atomic<uint64_t> initialization_count_{0};
    std::atomic<uint64_t> shutdown_count_{0};
};

} // namespace dx11_proxy

