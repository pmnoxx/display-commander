/*
 * Usage Example for DirectX 11 Present Hooks
 *
 * This file demonstrates how to use the DirectX 11 Present hooks
 * in your display commander addon.
 */

#include "dx11_hooks.hpp"
#include "swapchain_detector.hpp"
#include "test_present_hooks.hpp"
#include "../../utils.hpp"

namespace renodx::hooks::directx {

// Example: Custom Present callback implementation
void ExampleOnPresent(IDXGISwapChain* swapChain, UINT SyncInterval, UINT Flags) {
    // This function is called every time Present() is called
    LogInfo("Game is presenting frame - SyncInterval: %u, Flags: 0x%x", SyncInterval, Flags);

    // You can implement your display management logic here:
    // - Monitor frame rate
    // - Detect fullscreen vs windowed mode
    // - Implement display switching logic
    // - Monitor for specific present flags

    // Example: Detect if the game is using VSync
    if (SyncInterval > 0) {
        LogInfo("Game is using VSync with interval: %u", SyncInterval);
    } else {
        LogInfo("Game is not using VSync");
    }

    // Example: Detect specific present flags
    if (Flags & DXGI_PRESENT_DO_NOT_WAIT) {
        LogInfo("Game is using DXGI_PRESENT_DO_NOT_WAIT flag");
    }

    if (Flags & DXGI_PRESENT_TEST) {
        LogInfo("Game is testing presentation (not actually presenting)");
    }
}

// Example: Custom flag modification
UINT ExampleModifyPresentFlags(UINT originalFlags) {
    UINT modifiedFlags = originalFlags;

    // Example: Force certain flags based on your display management needs
    // if (s_continue_rendering.load()) {
    //     // Force the game to not wait for VSync when continue rendering is enabled
    //     modifiedFlags |= DXGI_PRESENT_DO_NOT_WAIT;
    //     LogInfo("Modified present flags to force no-wait mode");
    // }

    // Example: Remove test flags to ensure actual presentation
    if (modifiedFlags & DXGI_PRESENT_TEST) {
        modifiedFlags &= ~DXGI_PRESENT_TEST;
        LogInfo("Removed DXGI_PRESENT_TEST flag to ensure actual presentation");
    }

    return modifiedFlags;
}

// Example: How to integrate with your main addon
void InitializeDirectXHooks() {
    LogInfo("Initializing DirectX 11 Present hooks...");

    // Install the hooks
    if (InstallDX11Hooks()) {
        LogInfo("DirectX 11 hooks installed successfully");

        // Try to detect and hook existing swap chains
        if (SwapChainDetector::HookDetectedSwapChains()) {
            LogInfo("Successfully hooked detected swap chains");
        } else {
            LogInfo("No existing swap chains detected, will hook new ones as they're created");
        }

        // Start monitoring for new swap chains
        SwapChainDetector::StartMonitoring();

    } else {
        LogError("Failed to install DirectX 11 hooks");
    }
}

void ShutdownDirectXHooks() {
    LogInfo("Shutting down DirectX 11 Present hooks...");

    // Stop monitoring
    SwapChainDetector::StopMonitoring();

    // Uninstall hooks
    UninstallDX11Hooks();

    LogInfo("DirectX 11 hooks shut down");
}

// Example: How to test the hooks
void TestDirectXHooks() {
    LogInfo("Testing DirectX 11 Present hooks...");

    // Run the test
    if (TestPresentHooks()) {
        LogInfo("DirectX 11 Present hook test completed successfully");
    } else {
        LogError("DirectX 11 Present hook test failed");
    }
}

// Example: How to verify hooks are working in a real game
void VerifyHooksInGame() {
    LogInfo("To verify DirectX 11 Present hooks are working:");
    LogInfo("1. Build and install your addon");
    LogInfo("2. Run a DirectX 11 game");
    LogInfo("3. Check the logs for messages like:");
    LogInfo("   - 'IDXGIFactory::CreateSwapChain called'");
    LogInfo("   - 'DX11 Present called - SwapChain: 0x..., SyncInterval: ..., Flags: 0x...'");
    LogInfo("   - 'DX11 OnPresent callback - SwapChain: 0x..., SyncInterval: ..., Flags: 0x...'");
    LogInfo("4. If you see these messages, the hooks are working correctly!");
}

} // namespace renodx::hooks::directx
