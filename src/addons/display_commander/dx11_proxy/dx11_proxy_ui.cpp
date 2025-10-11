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

