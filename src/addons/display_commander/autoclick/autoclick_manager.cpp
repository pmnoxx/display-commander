#include "autoclick_manager.hpp"
#include "../globals.hpp"
#include "../settings/experimental_tab_settings.hpp"
#include "../ui/new_ui/settings_wrapper.hpp"
#include "../utils.hpp"
#include "../utils/timing.hpp"
#include <imgui.h>
#include <windows.h>
#include <chrono>
#include <thread>

namespace autoclick {

// Global variables for auto-click functionality
std::atomic<bool> g_auto_click_thread_running{false};
std::thread g_auto_click_thread;
const bool g_move_mouse = true;
const bool g_mouse_spoofing_enabled = true;

// UI state tracking for optimization
std::atomic<bool> g_ui_overlay_open{false};
std::atomic<LONGLONG> g_last_ui_draw_time_ns{0};

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
        if (g_mouse_spoofing_enabled) {
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
                ? (g_mouse_spoofing_enabled ? " - mouse position spoofed"
                                           : " - mouse moved to screen")
                : " - mouse not moved");
}

// Helper function to draw a sequence using settings directly
void DrawSequence(int sequence_num) {
    int idx = sequence_num - 1; // Convert to 0-based index

    ImGui::TextColored(ImVec4(0.9f, 0.9f, 0.9f, 1.0f), "%d:", sequence_num);
    ImGui::SameLine();

    // Get current values from settings
    bool enabled = settings::g_experimentalTabSettings.sequence_enabled.GetValue(idx) != 0;
    int x = settings::g_experimentalTabSettings.sequence_x.GetValue(idx);
    int y = settings::g_experimentalTabSettings.sequence_y.GetValue(idx);
    int interval = settings::g_experimentalTabSettings.sequence_interval.GetValue(idx);

    // Debug logging for sequence values
   // LogInfo("DrawSequence(%d) - enabled=%s, x=%d, y=%d, interval=%d", sequence_num, enabled ? "true" : "false", x, y,
   //         interval);

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

// Auto-click thread function - always running, sleeps when inactive
void AutoClickThread() {
    g_auto_click_thread_running.store(true);
    LogInfo("Auto-click thread started");

    while (true) {
        // Check if auto-click is enabled
        if (g_auto_click_enabled.load()) {
            // Check if UI overlay is open - if so, sleep for 2 seconds
            if (g_ui_overlay_open.load()) {
                LogDebug("Auto-click: UI overlay is open, sleeping for 2 seconds");
                std::this_thread::sleep_for(std::chrono::milliseconds(2000));
                continue;
            }

            // Check if UI was drawn recently (within last 2 seconds) - if so, sleep briefly
            LONGLONG now_ns = utils::get_now_ns();
            LONGLONG last_ui_draw = g_last_ui_draw_time_ns.load();
            if (last_ui_draw > 0 && (now_ns - last_ui_draw) < (2 * utils::SEC_TO_NS)) {
                LogDebug("Auto-click: UI was drawn recently, sleeping for 500ms");
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
                continue;
            }

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
        } else {
            // Auto-click is disabled, sleep for 1 second
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        }
    }

    g_auto_click_thread_running.store(false);
}

// Function to start the auto-click thread
void StartAutoClickThread() {
    if (!g_auto_click_thread_running.load()) {
        g_auto_click_thread = std::thread(AutoClickThread);
        LogInfo("Auto-click thread started");
    }
}

// Function to stop the auto-click thread
void StopAutoClickThread() {
    if (g_auto_click_thread_running.load()) {
        // Note: We can't actually stop the thread since it runs forever
        // The thread will just sleep when auto-click is disabled
        LogInfo("Auto-click thread will sleep when disabled");
    }
}

// Function to toggle auto-click enabled state
void ToggleAutoClickEnabled() {
    bool new_auto_click_enabled = !g_auto_click_enabled.load();

    g_auto_click_enabled.store(new_auto_click_enabled);

    LogInfo("ToggleAutoClickEnabled - new state: %s", new_auto_click_enabled ? "enabled" : "disabled");

    if (new_auto_click_enabled) {
        LogInfo("Auto-click sequences enabled via shortcut");
    } else {
        LogInfo("Auto-click sequences disabled via shortcut");
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
    bool auto_click_enabled = g_auto_click_enabled.load();
    if (ImGui::Checkbox("Enable Auto-Click Sequences", &auto_click_enabled)) {
        g_auto_click_enabled.store(auto_click_enabled);

        if (auto_click_enabled) {
            LogInfo("Auto-click sequences enabled");
        } else {
            LogInfo("Auto-click sequences disabled");
        }
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Enable/disable all auto-click sequences. Each sequence can be individually configured "
                          "below.\n\nShortcut: Ctrl+P (can be enabled in Developer tab)\n\nNote: Mouse position spoofing is always enabled for better stealth.");
    }
    // Mouse position spoofing is always enabled
    ImGui::TextColored(ImVec4(0.8f, 1.0f, 0.8f, 1.0f), "✓ Mouse position spoofing is always enabled for better stealth");

    // Show current status
    if (g_auto_click_enabled.load()) {
        if (g_auto_click_thread_running.load()) {
            ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "✁EAuto-click sequences are ACTIVE");
        } else {
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "⚠ Auto-click sequences are STARTING...");
        }
    }

    ImGui::Spacing();

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

    if (enabled_sequences > 0 && g_auto_click_enabled.load()) {
        ImGui::TextColored(ImVec4(0.8f, 1.0f, 0.8f, 1.0f),
                           "Sequences will execute in order: 1 ↁE2 ↁE3 ↁE4 ↁE5 ↁErepeat");
    }

    ImGui::Spacing();
    ImGui::Separator();

    // Draw mouse coordinates display
    DrawMouseCoordinatesDisplay();
}

// UI state management functions
void UpdateUIOverlayState(bool is_open) {
    g_ui_overlay_open.store(is_open);
    LogDebug("Auto-click: UI overlay state updated to %s", is_open ? "open" : "closed");
}

void UpdateLastUIDrawTime() {
    LONGLONG now_ns = utils::get_now_ns();
    g_last_ui_draw_time_ns.store(now_ns);
    //LogDebug("Auto-click: UI draw time updated to %lld ns", now_ns);
}

} // namespace autoclick
