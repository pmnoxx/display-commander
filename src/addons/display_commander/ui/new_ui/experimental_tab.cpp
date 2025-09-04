#include "experimental_tab.hpp"
#include "experimental_tab_settings.hpp"
#include "../../addon.hpp"
#include <deps/imgui/imgui.h>
#include <windows.h>
#include <thread>
#include <atomic>
#include <chrono>
#include <sstream>

namespace ui::new_ui {

// Global variables for auto-click feature
static std::atomic<bool> g_auto_click_thread_running{false};
static std::thread g_auto_click_thread;

// Static arrays for UI state management
static bool g_sequence_enabled[5] = {false, false, false, false, false};
static int g_sequence_x[5] = {0, 0, 0, 0, 0};
static int g_sequence_y[5] = {0, 0, 0, 0, 0};
static int g_sequence_interval[5] = {3000, 3000, 3000, 3000, 3000};
static bool g_move_mouse = true; // Whether to move mouse before clicking
static bool g_ui_initialized = false;

// Initialize experimental tab
void InitExperimentalTab() {
    g_experimentalTabSettings.LoadAll();
    LogInfo("Experimental tab settings loaded");
}

// Sync static arrays with settings
void SyncUIWithSettings() {
    if (!g_ui_initialized) {
        // Load from settings to static arrays
        g_sequence_enabled[0] = g_experimentalTabSettings.sequence_1_enabled.GetValue();
        g_sequence_enabled[1] = g_experimentalTabSettings.sequence_2_enabled.GetValue();
        g_sequence_enabled[2] = g_experimentalTabSettings.sequence_3_enabled.GetValue();
        g_sequence_enabled[3] = g_experimentalTabSettings.sequence_4_enabled.GetValue();
        g_sequence_enabled[4] = g_experimentalTabSettings.sequence_5_enabled.GetValue();

        g_sequence_x[0] = g_experimentalTabSettings.sequence_1_x.GetValue();
        g_sequence_x[1] = g_experimentalTabSettings.sequence_2_x.GetValue();
        g_sequence_x[2] = g_experimentalTabSettings.sequence_3_x.GetValue();
        g_sequence_x[3] = g_experimentalTabSettings.sequence_4_x.GetValue();
        g_sequence_x[4] = g_experimentalTabSettings.sequence_5_x.GetValue();

        g_sequence_y[0] = g_experimentalTabSettings.sequence_1_y.GetValue();
        g_sequence_y[1] = g_experimentalTabSettings.sequence_2_y.GetValue();
        g_sequence_y[2] = g_experimentalTabSettings.sequence_3_y.GetValue();
        g_sequence_y[3] = g_experimentalTabSettings.sequence_4_y.GetValue();
        g_sequence_y[4] = g_experimentalTabSettings.sequence_5_y.GetValue();

        g_sequence_interval[0] = g_experimentalTabSettings.sequence_1_interval.GetValue();
        g_sequence_interval[1] = g_experimentalTabSettings.sequence_2_interval.GetValue();
        g_sequence_interval[2] = g_experimentalTabSettings.sequence_3_interval.GetValue();
        g_sequence_interval[3] = g_experimentalTabSettings.sequence_4_interval.GetValue();
        g_sequence_interval[4] = g_experimentalTabSettings.sequence_5_interval.GetValue();

        g_ui_initialized = true;
        LogInfo("UI arrays synchronized with settings");
    }
}

// Save static arrays to settings
void SaveUIToSettings() {
    g_experimentalTabSettings.sequence_1_enabled.SetValue(g_sequence_enabled[0]);
    g_experimentalTabSettings.sequence_2_enabled.SetValue(g_sequence_enabled[1]);
    g_experimentalTabSettings.sequence_3_enabled.SetValue(g_sequence_enabled[2]);
    g_experimentalTabSettings.sequence_4_enabled.SetValue(g_sequence_enabled[3]);
    g_experimentalTabSettings.sequence_5_enabled.SetValue(g_sequence_enabled[4]);

    g_experimentalTabSettings.sequence_1_x.SetValue(g_sequence_x[0]);
    g_experimentalTabSettings.sequence_2_x.SetValue(g_sequence_x[1]);
    g_experimentalTabSettings.sequence_3_x.SetValue(g_sequence_x[2]);
    g_experimentalTabSettings.sequence_4_x.SetValue(g_sequence_x[3]);
    g_experimentalTabSettings.sequence_5_x.SetValue(g_sequence_x[4]);

    g_experimentalTabSettings.sequence_1_y.SetValue(g_sequence_y[0]);
    g_experimentalTabSettings.sequence_2_y.SetValue(g_sequence_y[1]);
    g_experimentalTabSettings.sequence_3_y.SetValue(g_sequence_y[2]);
    g_experimentalTabSettings.sequence_4_y.SetValue(g_sequence_y[3]);
    g_experimentalTabSettings.sequence_5_y.SetValue(g_sequence_y[4]);

    g_experimentalTabSettings.sequence_1_interval.SetValue(g_sequence_interval[0]);
    g_experimentalTabSettings.sequence_2_interval.SetValue(g_sequence_interval[1]);
    g_experimentalTabSettings.sequence_3_interval.SetValue(g_sequence_interval[2]);
    g_experimentalTabSettings.sequence_4_interval.SetValue(g_sequence_interval[3]);
    g_experimentalTabSettings.sequence_5_interval.SetValue(g_sequence_interval[4]);
}

// Helper function to perform a click at the specified coordinates
void PerformClick(int x, int y, int sequence_num, bool is_test = false) {
    HWND hwnd = g_last_swapchain_hwnd.load();
    if (!hwnd || !IsWindow(hwnd)) {
        LogWarn("%s click for sequence %d: No valid game window handle available",
               is_test ? "Test" : "Auto", sequence_num);
        return;
    }

    // Convert client coordinates to screen coordinates
    POINT client_pos = {x, y};
    POINT screen_pos = client_pos;
    ClientToScreen(hwnd, &screen_pos);

    // Move mouse to the target location if enabled
    if (g_move_mouse) {
       SetCursorPos(screen_pos.x, screen_pos.y);
      // PostMessage(hwnd, WM_MOUSEMOVE, 0, MAKELPARAM(screen_pos.x, screen_pos.y));
       std::this_thread::sleep_for(std::chrono::milliseconds(50)); // Small delay for mouse movement
    }

    // Send click messages
    LPARAM lParam = MAKELPARAM(x, y);
    PostMessage(hwnd, WM_LBUTTONDOWN, MK_LBUTTON, lParam);
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    PostMessage(hwnd, WM_LBUTTONUP, MK_LBUTTON, lParam);

    LogInfo("%s click for sequence %d sent to game window at (%d, %d)%s",
           is_test ? "Test" : "Auto", sequence_num, x, y,
           g_move_mouse ? " - mouse moved to screen" : " - mouse not moved");
}

// Helper function to draw a sequence using static arrays
void DrawSequence(int sequence_num) {
    int idx = sequence_num - 1; // Convert to 0-based index

    ImGui::TextColored(ImVec4(0.9f, 0.9f, 0.9f, 1.0f), "Sequence %d:", sequence_num);

    // Checkbox for enabling this sequence
    if (ImGui::Checkbox(("Enabled##seq" + std::to_string(sequence_num)).c_str(), &g_sequence_enabled[idx])) {
        LogInfo("Click sequence %d %s", sequence_num, g_sequence_enabled[idx] ? "enabled" : "disabled");
        SaveUIToSettings();
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Enable/disable this click sequence.");
    }

    // Only show other controls if this sequence is enabled
    if (g_sequence_enabled[idx]) {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(120);
        if (ImGui::InputInt(("X##seq" + std::to_string(sequence_num)).c_str(), &g_sequence_x[idx], 0, 0, ImGuiInputTextFlags_CharsDecimal)) {
            SaveUIToSettings();
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("X coordinate for the click (game window client coordinates).");
        }

        ImGui::SameLine();
        ImGui::SetNextItemWidth(120);
        if (ImGui::InputInt(("Y##seq" + std::to_string(sequence_num)).c_str(), &g_sequence_y[idx], 0, 0, ImGuiInputTextFlags_CharsDecimal)) {
            SaveUIToSettings();
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Y coordinate for the click (game window client coordinates).");
        }

        ImGui::SameLine();
        ImGui::SetNextItemWidth(150);
        if (ImGui::InputInt(("Interval (ms)##seq" + std::to_string(sequence_num)).c_str(), &g_sequence_interval[idx], 0, 0, ImGuiInputTextFlags_CharsDecimal)) {
            // Clamp to reasonable values
            g_sequence_interval[idx] = (std::max)(100, (std::min)(60000, g_sequence_interval[idx]));
            SaveUIToSettings();
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Time interval between clicks for this sequence (100ms to 60 seconds).");
        }

        // Test button for this sequence
        ImGui::SameLine();
        if (ImGui::Button(("Test##seq" + std::to_string(sequence_num)).c_str())) {
            PerformClick(g_sequence_x[idx], g_sequence_y[idx], sequence_num, true);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Send a test click for this sequence.");
        }

        // Use current mouse position button
        ImGui::SameLine();
        if (ImGui::Button(("Use Current##seq" + std::to_string(sequence_num)).c_str())) {
            POINT mouse_pos;
            GetCursorPos(&mouse_pos);
            HWND hwnd = g_last_swapchain_hwnd.load();
            if (hwnd && IsWindow(hwnd)) {
                POINT client_pos = mouse_pos;
                ScreenToClient(hwnd, &client_pos);
                g_sequence_x[idx] = client_pos.x;
                g_sequence_y[idx] = client_pos.y;
                LogInfo("Click sequence %d coordinates set to current mouse position (%d, %d)", sequence_num, client_pos.x, client_pos.y);
                SaveUIToSettings();
            }
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Set coordinates to current mouse position in game window.");
        }
    }
    ImGui::Spacing();
}

// Auto-click thread function
void AutoClickThread() {
    g_auto_click_thread_running.store(true);

    while (g_experimentalTabSettings.auto_click_enabled.GetValue()) {
        // Get the current game window handle
        HWND hwnd = g_last_swapchain_hwnd.load();
        if (hwnd && IsWindow(hwnd)) {
            // Process each enabled click sequence using static arrays
            for (int i = 0; i < 5; i++) {
                if (g_sequence_enabled[i]) {
                    PerformClick(g_sequence_x[i], g_sequence_y[i], i + 1, false);
                    std::this_thread::sleep_for(std::chrono::milliseconds(g_sequence_interval[i]));
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

void DrawExperimentalTab() {
    // Sync UI with settings on first draw
    SyncUIWithSettings();

    ImGui::Text("Experimental Tab - Advanced Features");
    ImGui::Separator();

    // Display current cursor position prominently at the top
    POINT mouse_pos;
    GetCursorPos(&mouse_pos);

    ImGui::Spacing();
    ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "=== LIVE CURSOR POSITION ===");
    ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "X: %ld  |  Y: %ld", mouse_pos.x, mouse_pos.y);

        // Show game window coordinates if available
    HWND hwnd = g_last_swapchain_hwnd.load();
    if (hwnd && IsWindow(hwnd)) {
        POINT client_pos = mouse_pos;
        ScreenToClient(hwnd, &client_pos);
        ImGui::TextColored(ImVec4(0.0f, 1.0f, 1.0f, 1.0f), "Game Window: X: %ld  |  Y: %ld", client_pos.x, client_pos.y);
    }

    // Copy coordinates buttons
    ImGui::Spacing();
    if (ImGui::Button("Copy Screen Coords")) {
        std::string coords = std::to_string(mouse_pos.x) + ", " + std::to_string(mouse_pos.y);
        if (OpenClipboard(nullptr)) {
            EmptyClipboard();
            HGLOBAL hClipboardData = GlobalAlloc(GMEM_DDESHARE, coords.length() + 1);
            if (hClipboardData) {
                char* pchData = (char*)GlobalLock(hClipboardData);
                if (pchData) {
                    strcpy_s(pchData, coords.length() + 1, coords.c_str());
                    GlobalUnlock(hClipboardData);
                    SetClipboardData(CF_TEXT, hClipboardData);
                }
            }
            CloseClipboard();
            LogInfo("Screen coordinates copied to clipboard: %s", coords.c_str());
        }
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Copy current screen coordinates to clipboard.");
    }

    if (hwnd && IsWindow(hwnd)) {
        ImGui::SameLine();
        if (ImGui::Button("Copy Game Window Coords")) {
            POINT client_pos = mouse_pos;
            ScreenToClient(hwnd, &client_pos);
            std::string coords = std::to_string(client_pos.x) + ", " + std::to_string(client_pos.y);
            if (OpenClipboard(nullptr)) {
                EmptyClipboard();
                HGLOBAL hClipboardData = GlobalAlloc(GMEM_DDESHARE, coords.length() + 1);
                if (hClipboardData) {
                    char* pchData = (char*)GlobalLock(hClipboardData);
                    if (pchData) {
                        strcpy_s(pchData, coords.length() + 1, coords.c_str());
                        GlobalUnlock(hClipboardData);
                        SetClipboardData(CF_TEXT, hClipboardData);
                    }
                }
                CloseClipboard();
                LogInfo("Game window coordinates copied to clipboard: %s", coords.c_str());
            }
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Copy current game window coordinates to clipboard.");
        }
    }

    ImGui::Spacing();
    ImGui::Separator();

    // Draw backbuffer format override section
    DrawBackbufferFormatOverride();

    #ifdef EXPERIMENTAL_TAB_PRIVATE
    ImGui::Spacing();
    ImGui::Separator();

    // Draw auto-click feature
    DrawAutoClickFeature();

    ImGui::Spacing();
    ImGui::Separator();

    // Draw mouse coordinates display
    DrawMouseCoordinatesDisplay();
    #endif
}

void DrawAutoClickFeature() {
    ImGui::TextColored(ImVec4(0.8f, 0.8f, 1.0f, 1.0f), "=== Auto-Click Sequences ===");

    // Warning about experimental nature
    ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "⚠ EXPERIMENTAL FEATURE - Use with caution!");
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("This feature sends mouse click messages directly to the game window.\nUse responsibly and be aware of game rules and terms of service.");
    }

        // Check for Ctrl+A keyboard shortcut
    if (ImGui::IsKeyDown(ImGuiKey_LeftCtrl) && ImGui::IsKeyPressed(ImGuiKey_A)) {
        bool current_state = g_experimentalTabSettings.auto_click_enabled.GetValue();
        g_experimentalTabSettings.auto_click_enabled.SetValue(!current_state);

        if (!current_state) {
            // Start auto-click thread
            if (!g_auto_click_thread_running.load()) {
                g_auto_click_thread = std::thread(AutoClickThread);
                LogInfo("Auto-click sequences enabled via Ctrl+A shortcut");
            }
        } else {
            // Stop auto-click thread
            if (g_auto_click_thread_running.load()) {
                // Wait for thread to finish
                if (g_auto_click_thread.joinable()) {
                    g_auto_click_thread.join();
                }
                LogInfo("Auto-click sequences disabled via Ctrl+A shortcut");
            }
        }
    }

                // Master enable/disable checkbox
    if (CheckboxSetting(g_experimentalTabSettings.auto_click_enabled, "Enable Auto-Click Sequences")) {
        bool auto_click_enabled = g_experimentalTabSettings.auto_click_enabled.GetValue();

        if (auto_click_enabled) {
            // Start auto-click thread
            if (!g_auto_click_thread_running.load()) {
                g_auto_click_thread = std::thread(AutoClickThread);
                LogInfo("Auto-click sequences enabled");
            }
        } else {
            // Stop auto-click thread
            if (g_auto_click_thread_running.load()) {
                // Wait for thread to finish
                if (g_auto_click_thread.joinable()) {
                    g_auto_click_thread.join();
                }
                LogInfo("Auto-click sequences disabled");
            }
        }
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Enable/disable all auto-click sequences. Each sequence can be individually configured below.\n\nKeyboard shortcut: Ctrl+A");
    }

    // Show keyboard shortcut info
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "(Ctrl+A)");
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Press Ctrl+A to quickly toggle auto-click sequences on/off.");
    }

    // Mouse movement toggle
    ImGui::Spacing();
    if (ImGui::Checkbox("Move Mouse Before Clicking", &g_move_mouse)) {
        LogInfo("Mouse movement %s", g_move_mouse ? "enabled" : "disabled");
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Whether to physically move the mouse cursor to the click location before sending click messages.\n\nDisable this if the game detects mouse movement as suspicious behavior.");
    }

    // Show current status
    if (g_experimentalTabSettings.auto_click_enabled.GetValue()) {
        if (g_auto_click_thread_running.load()) {
            ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "✓ Auto-click sequences are ACTIVE");
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
        if (g_sequence_enabled[i]) {
            enabled_sequences++;
        }
    }

    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Active sequences: %d/5", enabled_sequences);

    if (enabled_sequences > 0 && g_experimentalTabSettings.auto_click_enabled.GetValue()) {
        ImGui::TextColored(ImVec4(0.8f, 1.0f, 0.8f, 1.0f), "Sequences will execute in order: 1 → 2 → 3 → 4 → 5 → repeat");
    }
}

void DrawMouseCoordinatesDisplay() {
    ImGui::TextColored(ImVec4(0.8f, 0.8f, 1.0f, 1.0f), "=== Current Cursor Position ===");

    // Get current mouse position
    POINT mouse_pos;
    GetCursorPos(&mouse_pos);

    // Display current cursor position prominently
    ImGui::Spacing();
    ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.8f, 1.0f), "Current Cursor Position:");
    ImGui::TextColored(ImVec4(0.0f, 1.0f, 1.0f, 1.0f), "Screen: (%ld, %ld)", mouse_pos.x, mouse_pos.y);

    // Get game window handle and convert to client coordinates
    HWND hwnd = g_last_swapchain_hwnd.load();
    if (hwnd && IsWindow(hwnd)) {
        POINT client_pos = mouse_pos;
        ScreenToClient(hwnd, &client_pos);

        // Display client coordinates prominently
        ImGui::TextColored(ImVec4(0.0f, 1.0f, 1.0f, 1.0f), "Game Window: (%ld, %ld)", client_pos.x, client_pos.y);

        // Get window rectangle for reference
        RECT window_rect;
        if (GetWindowRect(hwnd, &window_rect)) {
            ImGui::Text("Game Window Screen Position: (%ld, %ld) to (%ld, %ld)",
                       window_rect.left, window_rect.top, window_rect.right, window_rect.bottom);
            ImGui::Text("Game Window Size: %ld x %ld",
                       window_rect.right - window_rect.left, window_rect.bottom - window_rect.top);
        }

        // Check if mouse is over the game window
        bool mouse_over_window = (mouse_pos.x >= window_rect.left && mouse_pos.x <= window_rect.right &&
                                 mouse_pos.y >= window_rect.top && mouse_pos.y <= window_rect.bottom);

        if (mouse_over_window) {
            ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "✓ Mouse is over game window");
        } else {
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "⚠ Mouse is outside game window");
        }

    } else {
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "No valid game window handle available");
    }

    // Refresh button to update coordinates
    if (ImGui::Button("Refresh Coordinates")) {
        // Coordinates are updated automatically, this is just for user feedback
        LogInfo("Mouse coordinates refreshed");
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Refresh the mouse coordinate display (coordinates update automatically).");
    }

    // Additional debugging info
    ImGui::Spacing();
    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Debug Information:");
    ImGui::Text("Game Window Handle: 0x%p", hwnd);
    ImGui::Text("Window Valid: %s", (hwnd && IsWindow(hwnd)) ? "Yes" : "No");

    // Show current foreground window for comparison
    HWND foreground_hwnd = GetForegroundWindow();
    ImGui::Text("Foreground Window: 0x%p", foreground_hwnd);
    ImGui::Text("Game Window is Foreground: %s", (hwnd == foreground_hwnd) ? "Yes" : "No");
}

// Cleanup function to stop background threads
void CleanupExperimentalTab() {
    // Stop auto-click thread
    if (g_experimentalTabSettings.auto_click_enabled.GetValue()) {
        g_experimentalTabSettings.auto_click_enabled.SetValue(false);
        if (g_auto_click_thread_running.load() && g_auto_click_thread.joinable()) {
            g_auto_click_thread.join();
        }
        LogInfo("Experimental tab cleanup: Auto-click thread stopped");
    }
}

void DrawBackbufferFormatOverride() {
    ImGui::TextColored(ImVec4(0.8f, 0.8f, 1.0f, 1.0f), "=== Backbuffer Format Override ===");

    // Warning about experimental nature
    ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "⚠ EXPERIMENTAL FEATURE - May cause compatibility issues!");
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("This feature overrides the backbuffer format during swapchain creation.\nUse with caution as it may cause rendering issues or crashes in some games.");
    }

    ImGui::Spacing();

    // Enable/disable checkbox
    if (CheckboxSetting(g_experimentalTabSettings.backbuffer_format_override_enabled, "Enable Backbuffer Format Override")) {
        LogInfo("Backbuffer format override %s",
                g_experimentalTabSettings.backbuffer_format_override_enabled.GetValue() ? "enabled" : "disabled");
    }

    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Override the backbuffer format during swapchain creation.\nRequires restart to take effect.");
    }

    // Format selection combo (only enabled when override is enabled)
    if (g_experimentalTabSettings.backbuffer_format_override_enabled.GetValue()) {
        ImGui::Spacing();
        ImGui::Text("Target Format:");

        if (ComboSettingWrapper(g_experimentalTabSettings.backbuffer_format_override, "Format")) {
            LogInfo("Backbuffer format override changed to: %s",
                    g_experimentalTabSettings.backbuffer_format_override.GetLabels()[g_experimentalTabSettings.backbuffer_format_override.GetValue()]);
        }

        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Select the target backbuffer format:\n"
                            "• R8G8B8A8_UNORM: Standard 8-bit per channel (32-bit total)\n"
                            "• R10G10B10A2_UNORM: 10-bit RGB + 2-bit alpha (32-bit total)\n"
                            "• R16G16B16A16_FLOAT: 16-bit HDR floating point (64-bit total)");
        }

        // Show current format info
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Note: Changes require restart to take effect");
    }
}

} // namespace ui::new_ui

