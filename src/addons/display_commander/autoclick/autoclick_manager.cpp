#include "autoclick_manager.hpp"
#include "../globals.hpp"
#include "../settings/experimental_tab_settings.hpp"
#include "../ui/new_ui/settings_wrapper.hpp"
#include "../utils.hpp"
#include <imgui.h>
#include <windows.h>
#include <chrono>
#include <thread>

namespace autoclick {

// Global variables for auto-click functionality
std::atomic<bool> g_auto_click_thread_running{false};
std::thread g_auto_click_thread;
bool g_move_mouse = true;

// Helper function to perform a click at the specified coordinates
void PerformClick(int x, int y, int sequence_num, bool is_test) {
    HWND hwnd = g_last_swapchain_hwnd.load();
    if (!hwnd || !IsWindow(hwnd)) {
        LogWarn("%s click for sequence %d: No valid game window handle available", is_test ? "Test" : "Auto",
                sequence_num);
        return;
    }

    // Convert client coordinates to screen coordinates
    POINT client_pos = {x, y};
    POINT screen_pos = client_pos;
    ClientToScreen(hwnd, &screen_pos);

    // Move mouse to the target location if enabled
    if (g_move_mouse) {
        // Check if mouse position spoofing is enabled
        if (settings::g_experimentalTabSettings.mouse_spoofing_enabled.GetValue()) {
            // Use spoofing instead of actually moving the cursor
            s_spoofed_mouse_x.store(screen_pos.x);
            s_spoofed_mouse_y.store(screen_pos.y);
            LogInfo("Mouse position spoofed to (%d, %d) for sequence %d", screen_pos.x, screen_pos.y, sequence_num);
        } else {
            // Actually move the cursor
            SetCursorPos(screen_pos.x, screen_pos.y);
            std::this_thread::sleep_for(std::chrono::milliseconds(50)); // Small delay for mouse movement
        }
    }

    // Send click messages
    LPARAM lParam = MAKELPARAM(x, y);
    PostMessage(hwnd, WM_LBUTTONDOWN, MK_LBUTTON, lParam);
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    PostMessage(hwnd, WM_LBUTTONUP, MK_LBUTTON, lParam);

    LogInfo("%s click for sequence %d sent to game window at (%d, %d)%s", is_test ? "Test" : "Auto", sequence_num, x, y,
            g_move_mouse
                ? (settings::g_experimentalTabSettings.mouse_spoofing_enabled.GetValue() ? " - mouse position spoofed"
                                                                                         : " - mouse moved to screen")
                : " - mouse not moved");
}

// Helper function to draw a sequence using settings directly
void DrawSequence(int sequence_num) {
    int idx = sequence_num - 1; // Convert to 0-based index

    ImGui::TextColored(ImVec4(0.9f, 0.9f, 0.9f, 1.0f), "Sequence %d:", sequence_num);

    // Get current values from settings
    bool enabled = settings::g_experimentalTabSettings.sequence_enabled.GetValue(idx) != 0;
    int x = settings::g_experimentalTabSettings.sequence_x.GetValue(idx);
    int y = settings::g_experimentalTabSettings.sequence_y.GetValue(idx);
    int interval = settings::g_experimentalTabSettings.sequence_interval.GetValue(idx);

    // Debug logging for sequence values
    LogInfo("DrawSequence(%d) - enabled=%s, x=%d, y=%d, interval=%d", sequence_num, enabled ? "true" : "false", x, y,
            interval);

    // Checkbox for enabling this sequence
    if (ImGui::Checkbox(("Enabled##seq" + std::to_string(sequence_num)).c_str(), &enabled)) {
        settings::g_experimentalTabSettings.sequence_enabled.SetValue(idx, enabled ? 1 : 0);
        LogInfo("Click sequence %d %s", sequence_num, enabled ? "enabled" : "disabled");
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Enable/disable this click sequence.");
    }

    // Only show other controls if this sequence is enabled
    if (enabled) {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(120);
        if (ImGui::InputInt(("X##seq" + std::to_string(sequence_num)).c_str(), &x, 0, 0,
                            ImGuiInputTextFlags_CharsDecimal)) {
            settings::g_experimentalTabSettings.sequence_x.SetValue(idx, x);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("X coordinate for the click (game window client coordinates).");
        }

        ImGui::SameLine();
        ImGui::SetNextItemWidth(120);
        if (ImGui::InputInt(("Y##seq" + std::to_string(sequence_num)).c_str(), &y, 0, 0,
                            ImGuiInputTextFlags_CharsDecimal)) {
            settings::g_experimentalTabSettings.sequence_y.SetValue(idx, y);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Y coordinate for the click (game window client coordinates).");
        }

        ImGui::SameLine();
        ImGui::SetNextItemWidth(150);
        if (ImGui::InputInt(("Interval (ms)##seq" + std::to_string(sequence_num)).c_str(), &interval, 0, 0,
                            ImGuiInputTextFlags_CharsDecimal)) {
            // Clamp to reasonable values
            interval = (std::max)(100, (std::min)(60000, interval));
            settings::g_experimentalTabSettings.sequence_interval.SetValue(idx, interval);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Time interval between clicks for this sequence (100ms to 60 seconds).");
        }

        // Test button for this sequence
        ImGui::SameLine();
        if (ImGui::Button(("Test##seq" + std::to_string(sequence_num)).c_str())) {
            PerformClick(x, y, sequence_num, true);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Send a test click for this sequence.");
        }

        // Use current mouse position button
        ImGui::SameLine();
        if (ImGui::Button(("Use Current##seq" + std::to_string(sequence_num)).c_str())) {
            POINT cursor_pos;
            if (GetCursorPos(&cursor_pos)) {
                // Convert screen coordinates to client coordinates
                HWND hwnd = g_last_swapchain_hwnd.load();
                if (hwnd && IsWindow(hwnd)) {
                    POINT client_pos = cursor_pos;
                    ScreenToClient(hwnd, &client_pos);
                    settings::g_experimentalTabSettings.sequence_x.SetValue(idx, client_pos.x);
                    settings::g_experimentalTabSettings.sequence_y.SetValue(idx, client_pos.y);
                    LogInfo("Set sequence %d coordinates to current mouse position: (%d, %d)", sequence_num,
                            client_pos.x, client_pos.y);
                }
            }
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Set coordinates to current mouse position (relative to game window).");
        }
    }
    ImGui::Spacing();
}

// Auto-click thread function
void AutoClickThread() {
    g_auto_click_thread_running.store(true);

    while (settings::g_experimentalTabSettings.auto_click_enabled.GetValue()) {
        // Get the current game window handle
        HWND hwnd = g_last_swapchain_hwnd.load();
        if (hwnd && IsWindow(hwnd)) {
            // Process each enabled click sequence using settings directly
            for (int i = 0; i < 5; i++) {
                if (settings::g_experimentalTabSettings.sequence_enabled.GetValue(i) != 0) {
                    int x = settings::g_experimentalTabSettings.sequence_x.GetValue(i);
                    int y = settings::g_experimentalTabSettings.sequence_y.GetValue(i);
                    int interval = settings::g_experimentalTabSettings.sequence_interval.GetValue(i);
                    PerformClick(x, y, i + 1, false);
                    std::this_thread::sleep_for(std::chrono::milliseconds(interval));
                }
            }
        } else {
            LogWarn("Auto-click: No valid game window handle available");
            // Wait a bit before retrying
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        }
    }

    g_auto_click_thread_running.store(false);
}

// Function to toggle auto-click enabled state
void ToggleAutoClickEnabled() {
    bool auto_click_enabled = settings::g_experimentalTabSettings.auto_click_enabled.GetValue();
    settings::g_experimentalTabSettings.auto_click_enabled.SetValue(!auto_click_enabled);

    auto_click_enabled = !auto_click_enabled; // Update local variable

    if (auto_click_enabled) {
        // Start auto-click thread
        if (!g_auto_click_thread_running.load()) {
            g_auto_click_thread = std::thread(AutoClickThread);
            LogInfo("Auto-click sequences enabled via shortcut");

            // Auto-enable mouse spoofing if it's not explicitly disabled and mouse movement is enabled
            if (g_move_mouse && !settings::g_experimentalTabSettings.mouse_spoofing_enabled.GetValue()) {
                settings::g_experimentalTabSettings.mouse_spoofing_enabled.SetValue(true);
                LogInfo("Auto-enabled mouse position spoofing for better stealth");
            }
        }
    } else {
        // Stop auto-click thread
        if (g_auto_click_thread_running.load()) {
            // Wait for thread to finish
            if (g_auto_click_thread.joinable()) {
                g_auto_click_thread.join();
            }
            // Disable mouse spoofing when auto-click is disabled
            settings::g_experimentalTabSettings.mouse_spoofing_enabled.SetValue(false);
            LogInfo("Auto-click sequences disabled via shortcut - mouse spoofing also disabled");
        }
    }
}

// Function to draw mouse coordinates display
void DrawMouseCoordinatesDisplay() {
    ImGui::TextColored(ImVec4(0.8f, 0.8f, 1.0f, 1.0f), "=== Mouse Coordinates ===");

    // Get current mouse position
    POINT cursor_pos;
    if (GetCursorPos(&cursor_pos)) {
        ImGui::Text("Screen: (%ld, %ld)", cursor_pos.x, cursor_pos.y);

        // Convert to client coordinates if we have a game window
        HWND hwnd = g_last_swapchain_hwnd.load();
        if (hwnd && IsWindow(hwnd)) {
            POINT client_pos = cursor_pos;
            ScreenToClient(hwnd, &client_pos);
            ImGui::Text("Game Window: (%ld, %ld)", client_pos.x, client_pos.y);
        } else {
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "No game window detected");
        }
    } else {
        ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "Failed to get mouse position");
    }

    ImGui::Spacing();
    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Use 'Use Current' buttons above to set click coordinates");
}

// Main function to draw the auto-click feature UI
void DrawAutoClickFeature() {
    ImGui::TextColored(ImVec4(0.8f, 0.8f, 1.0f, 1.0f), "=== Auto-Click Sequences ===");

    // Warning about experimental nature
    ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "⚠ EXPERIMENTAL FEATURE - Use with caution!");
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("This feature sends mouse click messages directly to the game window.\nUse responsibly and "
                          "be aware of game rules and terms of service.");
    }

    // Master enable/disable checkbox
    if (CheckboxSetting(settings::g_experimentalTabSettings.auto_click_enabled, "Enable Auto-Click Sequences")) {
        bool auto_click_enabled = settings::g_experimentalTabSettings.auto_click_enabled.GetValue();

        if (auto_click_enabled) {
            // Start auto-click thread
            if (!g_auto_click_thread_running.load()) {
                g_auto_click_thread = std::thread(AutoClickThread);
                LogInfo("Auto-click sequences enabled");

                // Auto-enable mouse spoofing if it's not explicitly disabled and mouse movement is enabled
                if (g_move_mouse && !settings::g_experimentalTabSettings.mouse_spoofing_enabled.GetValue()) {
                    settings::g_experimentalTabSettings.mouse_spoofing_enabled.SetValue(true);
                    LogInfo("Auto-enabled mouse position spoofing for better stealth");
                }
            }
        } else {
            // Stop auto-click thread
            if (g_auto_click_thread_running.load()) {
                // Wait for thread to finish
                if (g_auto_click_thread.joinable()) {
                    g_auto_click_thread.join();
                }
                // Disable mouse spoofing when auto-click is disabled
                settings::g_experimentalTabSettings.mouse_spoofing_enabled.SetValue(false);
                LogInfo("Auto-click sequences disabled - mouse spoofing also disabled");
            }
        }
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Enable/disable all auto-click sequences. Each sequence can be individually configured "
                          "below.\n\nShortcut: Ctrl+P\n\nNote: Mouse position spoofing will be auto-enabled for better stealth when mouse movement is enabled.");
    }

    // Mouse movement toggle
    ImGui::Spacing();
    if (ImGui::Checkbox("Move Mouse Before Clicking", &g_move_mouse)) {
        LogInfo("Mouse movement %s", g_move_mouse ? "enabled" : "disabled");
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Whether to physically move the mouse cursor to the click location before sending click "
                          "messages.\n\nDisable this if the game detects mouse movement as suspicious behavior.");
    }

    // Mouse position spoofing toggle
    bool auto_click_enabled = settings::g_experimentalTabSettings.auto_click_enabled.GetValue();

    if (CheckboxSetting(settings::g_experimentalTabSettings.mouse_spoofing_enabled,
                        "Enable Mouse Position Spoofing")) {
        LogInfo("Mouse position spoofing %s", settings::g_experimentalTabSettings.mouse_spoofing_enabled.GetValue()
                                                   ? "enabled"
                                                   : "disabled");
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Spoof mouse position instead of actually moving the cursor.\nThis can help avoid detection "
                          "by games that monitor mouse movement.\n\nNote: This only works when 'Move Mouse Before "
                          "Clicking' is enabled.");
    }

    // Show warning when spoofing is enabled but auto-click is disabled
    if (settings::g_experimentalTabSettings.mouse_spoofing_enabled.GetValue() && !auto_click_enabled) {
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "⚠ Mouse spoofing enabled but auto-click sequences are disabled");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Mouse position spoofing is configured but will not work until auto-click sequences are enabled.");
        }
    }

    // Show current status
    if (settings::g_experimentalTabSettings.auto_click_enabled.GetValue()) {
        if (g_auto_click_thread_running.load()) {
            ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "✁EAuto-click sequences are ACTIVE");
        } else {
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "⚠ Auto-click sequences are STARTING...");
        }
    }

    ImGui::Spacing();

    // Display click sequences (up to 5)
    DrawSequence(1);
    DrawSequence(2);
    DrawSequence(3);
    DrawSequence(4);
    DrawSequence(5);

    // Summary information
    int enabled_sequences = 0;
    for (int i = 0; i < 5; i++) {
        if (settings::g_experimentalTabSettings.sequence_enabled.GetValue(i) != 0) {
            enabled_sequences++;
        }
    }

    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Active sequences: %d/5", enabled_sequences);

    if (enabled_sequences > 0 && settings::g_experimentalTabSettings.auto_click_enabled.GetValue()) {
        ImGui::TextColored(ImVec4(0.8f, 1.0f, 0.8f, 1.0f),
                           "Sequences will execute in order: 1 ↁE2 ↁE3 ↁE4 ↁE5 ↁErepeat");
    }

    ImGui::Spacing();
    ImGui::Separator();

    // Draw mouse coordinates display
    DrawMouseCoordinatesDisplay();
}

} // namespace autoclick
