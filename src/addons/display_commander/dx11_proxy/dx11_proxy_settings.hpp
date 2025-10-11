/**
 * DX11 Proxy Settings
 *
 * Configuration for the DX11 proxy device feature
 */

#pragma once

#include <cstdint>
#include <atomic>

namespace dx11_proxy {

/**
 * Settings for DX11 proxy functionality
 */
struct DX11ProxySettings {
    // Enable/disable the DX11 proxy device
    std::atomic<bool> enabled{false};

    // Auto-initialize on DX9 device detection
    std::atomic<bool> auto_initialize{false};

    // Create own swapchain (false = device-only mode, use game's swapchain)
    std::atomic<bool> create_swapchain{false};

    // Swapchain format (default: R10G10B10A2_UNORM for HDR capability)
    // 0 = R10G10B10A2_UNORM (HDR 10-bit)
    // 1 = R16G16B16A16_FLOAT (HDR 16-bit float)
    // 2 = R8G8B8A8_UNORM (SDR)
    std::atomic<int> swapchain_format{0};

    // Use tearing mode (VRR support)
    std::atomic<bool> allow_tearing{true};

    // Buffer count (2-4)
    std::atomic<int> buffer_count{2};

    // Debug mode
    std::atomic<bool> debug_mode{false};

    // Show statistics in UI
    std::atomic<bool> show_stats{true};
};

// Global instance
extern DX11ProxySettings g_dx11ProxySettings;

} // namespace dx11_proxy

