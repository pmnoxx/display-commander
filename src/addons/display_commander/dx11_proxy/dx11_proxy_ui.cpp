/**
 * DX11 Proxy UI Implementation
 */

#include "dx11_proxy_ui.hpp"
#include "dx11_proxy_manager.hpp"
#include "dx11_proxy_settings.hpp"
#include "../utils.hpp"
#include "../globals.hpp"
#include "../res/ui_colors.hpp"
#include "../res/forkawesome.h"

#include <deps/imgui/imgui.h>
#include <reshade.hpp>
#include <sstream>
#include <string>

namespace dx11_proxy {

void InitUI() {
    LogInfo("DX11ProxyUI::InitUI - Initializing UI");
    // TODO: Load settings from file when implemented
}

void DrawDX11ProxyControls() {
    ImGui::TextColored(ui::colors::TEXT_LABEL, "DX11 Proxy Device");
    ImGui::Separator();
    ImGui::Spacing();

    // Warning banner
    ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 200, 0, 255));
    ImGui::TextWrapped("EXPERIMENTAL: This feature creates a separate DX11 device to present DX9 game content through a modern DXGI swapchain.");
    ImGui::PopStyleColor();
    ImGui::Spacing();

    // Description
    ImGui::TextWrapped("Benefits:");
    ImGui::BulletText("Enable HDR for DX9 games");
    ImGui::BulletText("Modern flip model presentation");
    ImGui::BulletText("Better VRR/G-Sync support");
    ImGui::BulletText("Tearing support for lower latency");
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Main enable toggle
    {
        bool enabled = g_dx11ProxySettings.enabled.load();
        if (ImGui::Checkbox("Enable DX11 Proxy Device", &enabled)) {
            g_dx11ProxySettings.enabled.store(enabled);

            if (enabled) {
                LogInfo("DX11ProxyUI: User enabled DX11 proxy");
                // TODO: Trigger initialization when connected to DX9 device
            } else {
                LogInfo("DX11ProxyUI: User disabled DX11 proxy");
                // Shutdown if active
                if (DX11ProxyManager::GetInstance().IsInitialized()) {
                    DX11ProxyManager::GetInstance().Shutdown();
                }
            }
        }
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered()) {
            ImGui::BeginTooltip();
            ImGui::Text("Create a separate DX11 device for presenting game content");
            ImGui::EndTooltip();
        }
    }

    ImGui::Spacing();

    // Auto-initialize option
    {
        bool auto_init = g_dx11ProxySettings.auto_initialize.load();
        if (ImGui::Checkbox("Auto-Initialize on DX9 Detection", &auto_init)) {
            g_dx11ProxySettings.auto_initialize.store(auto_init);
        }
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered()) {
            ImGui::BeginTooltip();
            ImGui::Text("Automatically create proxy device when DX9 game is detected");
            ImGui::EndTooltip();
        }
    }

    ImGui::Spacing();

    // Create swapchain option
    {
        bool create_swapchain = g_dx11ProxySettings.create_swapchain.load();
        if (ImGui::Checkbox("Create Own Swapchain", &create_swapchain)) {
            g_dx11ProxySettings.create_swapchain.store(create_swapchain);
        }
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered()) {
            ImGui::BeginTooltip();
            ImGui::Text("Create a separate swapchain for the proxy device");
            ImGui::Text("Usually not needed - device-only mode is recommended");
            ImGui::Text("Disable this to avoid 'Access Denied' errors");
            ImGui::EndTooltip();
        }
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Configuration section
    ImGui::TextColored(ui::colors::TEXT_LABEL, "Configuration");
    ImGui::Spacing();

    // Swapchain format
    {
        const char* format_names[] = {
            "R10G10B10A2 (HDR 10-bit)",
            "R16G16B16A16 Float (HDR 16-bit)",
            "R8G8B8A8 (SDR)"
        };
        int format_idx = g_dx11ProxySettings.swapchain_format.load();
        if (ImGui::Combo("Swapchain Format", &format_idx, format_names, IM_ARRAYSIZE(format_names))) {
            g_dx11ProxySettings.swapchain_format.store(format_idx);
            std::string msg = "DX11ProxyUI: Swapchain format changed to " + std::to_string(format_idx);
            LogInfo(msg.c_str());
        }
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered()) {
            ImGui::BeginTooltip();
            ImGui::Text("Output format for the proxy swapchain");
            ImGui::Text("10-bit: Best for HDR displays");
            ImGui::Text("16-bit: Maximum quality HDR");
            ImGui::Text("8-bit: Standard SDR output");
            ImGui::EndTooltip();
        }
    }

    ImGui::Spacing();

    // Buffer count
    {
        int buffer_count = g_dx11ProxySettings.buffer_count.load();
        if (ImGui::SliderInt("Buffer Count", &buffer_count, 2, 4)) {
            g_dx11ProxySettings.buffer_count.store(buffer_count);
        }
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered()) {
            ImGui::BeginTooltip();
            ImGui::Text("Number of back buffers for the swapchain");
            ImGui::Text("2: Lower latency, may stutter");
            ImGui::Text("3-4: Smoother, slightly higher latency");
            ImGui::EndTooltip();
        }
    }

    ImGui::Spacing();

    // Tearing support
    {
        bool allow_tearing = g_dx11ProxySettings.allow_tearing.load();
        if (ImGui::Checkbox("Allow Tearing (VRR)", &allow_tearing)) {
            g_dx11ProxySettings.allow_tearing.store(allow_tearing);
        }
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered()) {
            ImGui::BeginTooltip();
            ImGui::Text("Enable tearing for Variable Refresh Rate displays");
            ImGui::Text("Enables G-Sync/FreeSync support");
            ImGui::EndTooltip();
        }
    }

    ImGui::Spacing();

    // Debug mode
    {
        bool debug_mode = g_dx11ProxySettings.debug_mode.load();
        if (ImGui::Checkbox("Debug Mode", &debug_mode)) {
            g_dx11ProxySettings.debug_mode.store(debug_mode);
        }
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered()) {
            ImGui::BeginTooltip();
            ImGui::Text("Enable D3D11 debug layer for validation");
            ImGui::EndTooltip();
        }
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Status and statistics
    ImGui::TextColored(ui::colors::TEXT_LABEL, "Status");
    ImGui::Spacing();

    auto& manager = DX11ProxyManager::GetInstance();
    auto stats = manager.GetStats();

    // Status indicator
    if (stats.is_initialized) {
        ImGui::TextColored(ui::colors::STATUS_ACTIVE, ICON_FK_OK " Initialized");
    } else {
        ImGui::TextColored(ui::colors::STATUS_INACTIVE, "Not Initialized");
    }

    ImGui::Spacing();

    // Show statistics if enabled
    bool show_stats = g_dx11ProxySettings.show_stats.load();
    if (ImGui::Checkbox("Show Statistics", &show_stats)) {
        g_dx11ProxySettings.show_stats.store(show_stats);
    }

    if (show_stats && stats.is_initialized) {
        ImGui::Spacing();
        ImGui::BeginChild("DX11ProxyStats", ImVec2(0, 200), true);

        ImGui::TextColored(ui::colors::TEXT_INFO, "Statistics:");
        ImGui::Separator();
        ImGui::Spacing();

        // Device info
        ImGui::TextColored(ui::colors::TEXT_LABEL, "Device:");
        ImGui::Text("  Mode: %s", stats.has_swapchain ? "Device + Swapchain" : "Device-Only");
        if (stats.has_swapchain) {
            ImGui::Text("  Swapchain: %ux%u", stats.swapchain_width, stats.swapchain_height);
            ImGui::Text("  Format: %d", static_cast<int>(stats.swapchain_format));
        }

        ImGui::Spacing();

        // Frame counters
        ImGui::TextColored(ui::colors::TEXT_LABEL, "Frames:");
        ImGui::Text("  Generated: %llu", stats.frames_generated);
        if (stats.has_swapchain) {
            ImGui::Text("  Presented: %llu", stats.frames_presented);
        }
        ImGui::Text("  Copied: %llu", stats.frames_copied);

        ImGui::Spacing();

        // Copy thread status
        ImGui::TextColored(ui::colors::TEXT_LABEL, "Copy Thread:");
        ImGui::Text("  Status: %s", stats.copy_thread_running ? "Running" : "Stopped");
        if (stats.copy_thread_running) {
            ImGui::TextColored(ui::colors::STATUS_ACTIVE, "  " ICON_FK_OK " Active (1 fps)");
        }

        ImGui::Spacing();

        // Lifecycle counters
        ImGui::TextColored(ui::colors::TEXT_LABEL, "Lifecycle:");
        ImGui::Text("  Initializations: %llu", stats.initialization_count);
        ImGui::Text("  Shutdowns: %llu", stats.shutdown_count);

        ImGui::EndChild();
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Manual controls
    ImGui::TextColored(ui::colors::TEXT_LABEL, "Manual Controls");
    ImGui::Spacing();

    ImGui::BeginDisabled(!g_dx11ProxySettings.enabled.load());

    // Test initialize button
    if (ImGui::Button("Test Initialize")) {
        LogInfo("DX11ProxyUI: User requested test initialization");

        bool create_swapchain = g_dx11ProxySettings.create_swapchain.load();
        HWND hwnd = nullptr;
        uint32_t width = 0;
        uint32_t height = 0;

        if (create_swapchain) {
            // Get current window handle and size for swapchain
            hwnd = g_last_swapchain_hwnd.load();
            if (hwnd && IsWindow(hwnd)) {
                RECT rect;
                GetClientRect(hwnd, &rect);
                width = rect.right - rect.left;
                height = rect.bottom - rect.top;

                if (width == 0 || height == 0) {
                    LogError("DX11ProxyUI: Invalid window dimensions");
                    create_swapchain = false;  // Fall back to device-only
                }
            } else {
                LogWarn("DX11ProxyUI: No valid window handle, creating device-only");
                create_swapchain = false;  // Fall back to device-only
            }
        }

        bool success = manager.Initialize(hwnd, width, height, create_swapchain);
        if (success) {
            LogInfo("DX11ProxyUI: Test initialization succeeded");
        } else {
            LogError("DX11ProxyUI: Test initialization failed");
        }
    }
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::Text("Manually initialize the DX11 proxy device");
        ImGui::Text("Uses current game window and dimensions");
        ImGui::EndTooltip();
    }

    ImGui::SameLine();

    // Shutdown button
    ImGui::BeginDisabled(!manager.IsInitialized());
    if (ImGui::Button("Shutdown")) {
        LogInfo("DX11ProxyUI: User requested shutdown");
        manager.Shutdown();
    }
    ImGui::EndDisabled();

    ImGui::Spacing();

    // Quick test button: Enable + Create 4K Window + Initialize
    if (ImGui::Button("Quick Test: Enable + Create 4K Window")) {
        LogInfo("DX11ProxyUI: User requested quick test - Enable + Create 4K Window");

        // Step 1: Enable DX11 proxy
        g_dx11ProxySettings.enabled.store(true);
        g_dx11ProxySettings.create_swapchain.store(true);
        LogInfo("DX11ProxyUI: Enabled DX11 proxy and swapchain creation");

        // Step 2: Create 4K test window
        HWND test_window = manager.CreateTestWindow4K();
        if (test_window) {
            LogInfo("DX11ProxyUI: Created 4K test window successfully");

            // Step 3: Initialize with 4K dimensions
            bool success = manager.Initialize(test_window, 3840, 2160, true);
            if (success) {
                LogInfo("DX11ProxyUI: Quick test initialization succeeded!");
                // Window will be black until you start copying game content
            } else {
                LogError("DX11ProxyUI: Quick test initialization failed");
                manager.DestroyTestWindow(test_window);
            }
        } else {
            LogError("DX11ProxyUI: Failed to create 4K test window");
        }
    }
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::Text("One-click test:");
        ImGui::Text("1. Enables DX11 proxy");
        ImGui::Text("2. Creates a 3840x2160 test window");
        ImGui::Text("3. Initializes the proxy device");
        ImGui::Text("Use this for quick testing!");
        ImGui::Text("Window will be black until you start copying");
        ImGui::EndTooltip();
    }

    ImGui::Spacing();

    // Display game content - copy from game swapchain
    ImGui::TextColored(ui::colors::TEXT_LABEL, "Game Content Display:");
    ImGui::Spacing();

    // Check all conditions
    bool proxy_initialized = manager.IsInitialized();
    bool has_swapchain = stats.has_swapchain;
    void* game_swapchain_ptr = g_last_swapchain_ptr.load();
    int game_api = g_last_swapchain_api.load();
    bool has_game_swapchain = (game_swapchain_ptr != nullptr);

    // Check if it's a compatible API (DX11 or DX12)
    // ReShade uses hex enum values: d3d11=0xb000, d3d12=0xc000
    bool is_dx11 = (game_api == 0xb000); // reshade::api::device_api::d3d11
    bool is_dx12 = (game_api == 0xc000); // reshade::api::device_api::d3d12
    bool compatible_api = is_dx11 || is_dx12;

    // Show status
    ImGui::Text("Status:");
    ImGui::Indent();

    // Proxy status
    if (proxy_initialized) {
        ImGui::TextColored(ui::colors::STATUS_ACTIVE, ICON_FK_OK " Proxy Initialized");
    } else {
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "X Proxy Not Initialized");
    }

    // Swapchain status
    if (has_swapchain) {
        ImGui::TextColored(ui::colors::STATUS_ACTIVE, ICON_FK_OK " Test Window Swapchain Ready");
    } else {
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "X No Test Window Swapchain");
    }

    // Game swapchain status
    if (has_game_swapchain) {
        ImGui::TextColored(ui::colors::STATUS_ACTIVE, ICON_FK_OK " Game Swapchain Detected");
    } else {
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "X No Game Swapchain (start a game first)");
    }

    // API status
    if (has_game_swapchain) {
        if (compatible_api) {
            const char* api_name = is_dx11 ? "DX11" : "DX12";
            ImGui::TextColored(ui::colors::STATUS_ACTIVE, ICON_FK_OK " Compatible API: %s", api_name);
        } else {
            // Use GetDeviceApiString to get proper API name
            const char* api_name = GetDeviceApiString(static_cast<reshade::api::device_api>(game_api));
            ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "X Incompatible API: %s (0x%x)", api_name, game_api);
        }
    }

    ImGui::Unindent();
    ImGui::Spacing();

    // Enable button only if all conditions are met
    bool can_copy = proxy_initialized && has_swapchain && has_game_swapchain && compatible_api;

    ImGui::BeginDisabled(!can_copy);
    if (ImGui::Button("Display Game Content (Start Copying)")) {
        LogInfo("DX11ProxyUI: User requested to display game content");

        if (game_swapchain_ptr && compatible_api) {
            // The pointer is actually a reshade::api::swapchain*, need to get native
            auto* reshade_swapchain = static_cast<reshade::api::swapchain*>(game_swapchain_ptr);

            // Get the native DXGI swapchain from ReShade (returns uint64_t handle)
            uint64_t native_handle = reshade_swapchain->get_native();
            IDXGISwapChain* native_swapchain = reinterpret_cast<IDXGISwapChain*>(native_handle);

            if (native_swapchain) {
                manager.StartCopyThread(native_swapchain);
                LogInfo("DX11ProxyUI: Started copying from game swapchain (native: 0x%p) to test window", native_swapchain);
            } else {
                LogError("DX11ProxyUI: Failed to get native swapchain from ReShade");
            }
        } else {
            if (!compatible_api) {
                LogError("DX11ProxyUI: Game is not DX11/DX12 (API: %d) - cannot copy", game_api);
            } else {
                LogError("DX11ProxyUI: No game swapchain available");
            }
        }
    }
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::BeginTooltip();
        ImGui::Text("Copy game's rendered frames to test window");
        ImGui::Text("Copies once per second (1 fps)");
        ImGui::Text("Uses shared resources for cross-device copy");
        ImGui::Separator();
        ImGui::Text("Requirements:");
        ImGui::BulletText("Proxy initialized with test window");
        ImGui::BulletText("Game running with DX11/DX12");
        ImGui::BulletText("Game swapchain detected");
        if (!can_copy) {
            ImGui::Separator();
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "See status above for missing requirements");
        }
        ImGui::EndTooltip();
    }
    ImGui::EndDisabled();

    ImGui::SameLine();

    // Stop copy thread button
    ImGui::BeginDisabled(!manager.IsCopyThreadRunning());
    if (ImGui::Button("Stop Copying")) {
        LogInfo("DX11ProxyUI: User stopped game content copying");
        manager.StopCopyThread();
    }
    ImGui::EndDisabled();

    if (manager.IsCopyThreadRunning()) {
        ImGui::SameLine();
        ImGui::TextColored(ui::colors::STATUS_ACTIVE, ICON_FK_OK " Copying");
    }

    ImGui::Spacing();

    // Test frame generation button (for debugging)
    ImGui::BeginDisabled(!manager.IsInitialized());
    if (ImGui::Button("Test Frame Generation (+1)")) {
        manager.IncrementFrameGenerated();
        LogInfo("DX11ProxyUI: Test frame generated (counter incremented)");
    }
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::Text("Simulate frame generation (for testing)");
        ImGui::Text("In production, this will be called automatically");
        ImGui::Text("when frames are processed through the proxy");
        ImGui::Text("NO separate thread needed - it's part of Present() hook");
        ImGui::EndTooltip();
    }
    ImGui::EndDisabled();

    ImGui::EndDisabled();

    ImGui::Spacing();
}

} // namespace dx11_proxy

