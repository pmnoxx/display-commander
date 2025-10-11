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
#include <thread>
#include <vector>
#include <windows.h>

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
        uint64_t frames_copied = 0;         // Frames copied by copy thread
        uint64_t initialization_count = 0;
        uint64_t shutdown_count = 0;
        bool is_initialized = false;
        bool copy_thread_running = false;   // Whether copy thread is active
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

    /**
     * Start copy thread that copies from source swapchain to proxy swapchain
     * @param source_swapchain Source swapchain to copy from
     */
    void StartCopyThread(IDXGISwapChain* source_swapchain);

    /**
     * Stop the copy thread
     */
    void StopCopyThread();

    /**
     * Check if copy thread is running
     */
    bool IsCopyThreadRunning() const { return copy_thread_running_.load(); }

    /**
     * Test function: Create a test window at 4K resolution (3840x2160)
     * @return HWND of created window, or nullptr on failure
     */
    HWND CreateTestWindow4K();

    /**
     * Destroy test window created by CreateTestWindow4K
     * @param test_hwnd Window handle to destroy
     */
    void DestroyTestWindow(HWND test_hwnd);

    /**
     * Test render function: Clear backbuffer to a color and present
     * @param color_index Color index (0-7) for different colors
     * @return true if render succeeded
     */
    bool TestRender(int color_index = 0);

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
    std::atomic<uint64_t> frames_copied_{0};       // Frames copied by copy thread

    // Copy thread
    std::thread copy_thread_;
    std::atomic<bool> copy_thread_running_{false};
    ComPtr<IDXGISwapChain> source_swapchain_;      // Source swapchain to copy from

    // Test window tracking
    std::vector<HWND> test_windows_;

    // Shared resource support (for cross-device copy)
    ComPtr<ID3D11Texture2D> shared_texture_;       // Shared texture on our device
    ComPtr<ID3D11Device> source_device_;           // Source device (game's device)
    ComPtr<ID3D11DeviceContext> source_context_;   // Source context (game's context)
    bool use_shared_resources_{false};             // Whether we need shared resources
    uint32_t shared_texture_width_{0};
    uint32_t shared_texture_height_{0};
    DXGI_FORMAT shared_texture_format_{DXGI_FORMAT_UNKNOWN};

    // Copy thread implementation
    void CopyThreadLoop();

    // Copy one frame from source to destination
    bool CopyFrame();

    // Initialize shared resources for cross-device copy
    bool InitializeSharedResources(IDXGISwapChain* source_swapchain);

    // Cleanup shared resources
    void CleanupSharedResources();
};

} // namespace dx11_proxy

