#include "experimental_tab.hpp"
#include "../../settings/experimental_tab_settings.hpp"
#include "../../globals.hpp"
#include "../../hooks/sleep_hooks.hpp"
#include "../../hooks/timeslowdown_hooks.hpp"
#include "../../utils.hpp"
#include <deps/imgui/imgui.h>
#include <windows.h>
#include <thread>
#include <atomic>
#include <chrono>

namespace ui::new_ui {

// Global variables for auto-click feature
static std::atomic<bool> g_auto_click_thread_running{false};
static std::thread g_auto_click_thread;

// UI state management
static bool g_move_mouse = true; // Whether to move mouse before clicking

// Initialize experimental tab
void InitExperimentalTab() {
    LogInfo("InitExperimentalTab() - Starting to load experimental tab settings");
    settings::g_experimentalTabSettings.LoadAll();

    // Apply the loaded settings to the actual hook system
    // This ensures the hook system matches the UI settings
    LogInfo("InitExperimentalTab() - Applying loaded timer hook settings to hook system");
    renodx::hooks::SetTimerHookType(renodx::hooks::HOOK_QUERY_PERFORMANCE_COUNTER,
        static_cast<renodx::hooks::TimerHookType>(settings::g_experimentalTabSettings.query_performance_counter_hook.GetValue()));
    renodx::hooks::SetTimerHookType(renodx::hooks::HOOK_GET_TICK_COUNT,
        static_cast<renodx::hooks::TimerHookType>(settings::g_experimentalTabSettings.get_tick_count_hook.GetValue()));
    renodx::hooks::SetTimerHookType(renodx::hooks::HOOK_GET_TICK_COUNT64,
        static_cast<renodx::hooks::TimerHookType>(settings::g_experimentalTabSettings.get_tick_count64_hook.GetValue()));
    renodx::hooks::SetTimerHookType(renodx::hooks::HOOK_TIME_GET_TIME,
        static_cast<renodx::hooks::TimerHookType>(settings::g_experimentalTabSettings.time_get_time_hook.GetValue()));
    renodx::hooks::SetTimerHookType(renodx::hooks::HOOK_GET_SYSTEM_TIME,
        static_cast<renodx::hooks::TimerHookType>(settings::g_experimentalTabSettings.get_system_time_hook.GetValue()));
    renodx::hooks::SetTimerHookType(renodx::hooks::HOOK_GET_SYSTEM_TIME_AS_FILE_TIME,
        static_cast<renodx::hooks::TimerHookType>(settings::g_experimentalTabSettings.get_system_time_as_file_time_hook.GetValue()));
    renodx::hooks::SetTimerHookType(renodx::hooks::HOOK_GET_SYSTEM_TIME_PRECISE_AS_FILE_TIME,
        static_cast<renodx::hooks::TimerHookType>(settings::g_experimentalTabSettings.get_system_time_precise_as_file_time_hook.GetValue()));
    renodx::hooks::SetTimerHookType(renodx::hooks::HOOK_GET_LOCAL_TIME,
        static_cast<renodx::hooks::TimerHookType>(settings::g_experimentalTabSettings.get_local_time_hook.GetValue()));
    renodx::hooks::SetTimerHookType(renodx::hooks::HOOK_NT_QUERY_SYSTEM_TIME,
        static_cast<renodx::hooks::TimerHookType>(settings::g_experimentalTabSettings.nt_query_system_time_hook.GetValue()));

    LogInfo("InitExperimentalTab() - Experimental tab settings loaded and applied to hook system");
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

    LogInfo("%s click for sequence %d sent to game window at (%d, %d)%s",
           is_test ? "Test" : "Auto", sequence_num, x, y,
           g_move_mouse ? (settings::g_experimentalTabSettings.mouse_spoofing_enabled.GetValue() ? " - mouse position spoofed" : " - mouse moved to screen") : " - mouse not moved");
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
    LogInfo("DrawSequence(%d) - enabled=%s, x=%d, y=%d, interval=%d", sequence_num, enabled ? "true" : "false", x, y, interval);

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
        if (ImGui::InputInt(("X##seq" + std::to_string(sequence_num)).c_str(), &x, 0, 0, ImGuiInputTextFlags_CharsDecimal)) {
            settings::g_experimentalTabSettings.sequence_x.SetValue(idx, x);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("X coordinate for the click (game window client coordinates).");
        }

        ImGui::SameLine();
        ImGui::SetNextItemWidth(120);
        if (ImGui::InputInt(("Y##seq" + std::to_string(sequence_num)).c_str(), &y, 0, 0, ImGuiInputTextFlags_CharsDecimal)) {
            settings::g_experimentalTabSettings.sequence_y.SetValue(idx, y);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Y coordinate for the click (game window client coordinates).");
        }

        ImGui::SameLine();
        ImGui::SetNextItemWidth(150);
        if (ImGui::InputInt(("Interval (ms)##seq" + std::to_string(sequence_num)).c_str(), &interval, 0, 0, ImGuiInputTextFlags_CharsDecimal)) {
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
            POINT mouse_pos;
            GetCursorPos(&mouse_pos);
            HWND hwnd = g_last_swapchain_hwnd.load();
            if (hwnd && IsWindow(hwnd)) {
                POINT client_pos = mouse_pos;
                ScreenToClient(hwnd, &client_pos);
                settings::g_experimentalTabSettings.sequence_x.SetValue(idx, client_pos.x);
                settings::g_experimentalTabSettings.sequence_y.SetValue(idx, client_pos.y);
                LogInfo("Click sequence %d coordinates set to current mouse position (%d, %d)", sequence_num, client_pos.x, client_pos.y);
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

void DrawExperimentalTab() {
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

    ImGui::Spacing();
    ImGui::Separator();

    // Draw buffer resolution upgrade section
    DrawBufferResolutionUpgrade();

    ImGui::Spacing();
    ImGui::Separator();

    // Draw texture format upgrade section
    DrawTextureFormatUpgrade();

    ImGui::Spacing();
    ImGui::Separator();

    // Draw auto-click feature
    DrawAutoClickFeature();

    ImGui::Spacing();
    ImGui::Separator();

    // Draw sleep hook controls
    DrawSleepHookControls();

    ImGui::Spacing();
    ImGui::Separator();

    // Draw time slowdown controls
    DrawTimeSlowdownControls();

    ImGui::Spacing();
    ImGui::Separator();

    // Draw mouse coordinates display
    DrawMouseCoordinatesDisplay();
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
        bool current_state = settings::g_experimentalTabSettings.auto_click_enabled.GetValue();
        settings::g_experimentalTabSettings.auto_click_enabled.SetValue(!current_state);

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
                // Disable mouse spoofing when auto-click is disabled
                settings::g_experimentalTabSettings.mouse_spoofing_enabled.SetValue(false);
                LogInfo("Auto-click sequences disabled via Ctrl+A shortcut - mouse spoofing also disabled");
            }
        }
    }

                // Master enable/disable checkbox
    if (CheckboxSetting(settings::g_experimentalTabSettings.auto_click_enabled, "Enable Auto-Click Sequences")) {
        bool auto_click_enabled = settings::g_experimentalTabSettings.auto_click_enabled.GetValue();

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
                // Disable mouse spoofing when auto-click is disabled
                settings::g_experimentalTabSettings.mouse_spoofing_enabled.SetValue(false);
                LogInfo("Auto-click sequences disabled - mouse spoofing also disabled");
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

    // Mouse position spoofing toggle
    bool auto_click_enabled = settings::g_experimentalTabSettings.auto_click_enabled.GetValue();

    // Disable spoofing checkbox if auto-click is not enabled
    if (!auto_click_enabled) {
        ImGui::BeginDisabled();
    }

    if (CheckboxSetting(settings::g_experimentalTabSettings.mouse_spoofing_enabled, "Spoof Mouse Position (Instead of Moving)")) {
        bool spoofing_enabled = settings::g_experimentalTabSettings.mouse_spoofing_enabled.GetValue();
        LogInfo("Mouse position spoofing %s", spoofing_enabled ? "enabled" : "disabled");
    }

    if (!auto_click_enabled) {
        ImGui::EndDisabled();
    }

    if (ImGui::IsItemHovered()) {
        if (auto_click_enabled) {
            ImGui::SetTooltip("Instead of physically moving the mouse cursor, spoof the mouse position using hooks.\n\nThis prevents the cursor from actually moving on screen while still making the game think the mouse is at the target location.\n\nOnly works when 'Move Mouse Before Clicking' is enabled and auto-click sequences are active.");
        } else {
            ImGui::SetTooltip("Mouse position spoofing is only available when auto-click sequences are enabled.\n\nEnable 'Enable Auto-Click Sequences' above to use this feature.");
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
        ImGui::TextColored(ImVec4(0.8f, 1.0f, 0.8f, 1.0f), "Sequences will execute in order: 1 ↁE2 ↁE3 ↁE4 ↁE5 ↁErepeat");
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
            ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "✁EMouse is over game window");
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
    if (settings::g_experimentalTabSettings.auto_click_enabled.GetValue()) {
        settings::g_experimentalTabSettings.auto_click_enabled.SetValue(false);
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
    if (CheckboxSetting(settings::g_experimentalTabSettings.backbuffer_format_override_enabled, "Enable Backbuffer Format Override")) {
        LogInfo("Backbuffer format override %s",
                settings::g_experimentalTabSettings.backbuffer_format_override_enabled.GetValue() ? "enabled" : "disabled");
    }

    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Override the backbuffer format during swapchain creation.\nRequires restart to take effect.");
    }

    // Format selection combo (only enabled when override is enabled)
    if (settings::g_experimentalTabSettings.backbuffer_format_override_enabled.GetValue()) {
        ImGui::Spacing();
        ImGui::Text("Target Format:");

        if (ComboSettingWrapper(settings::g_experimentalTabSettings.backbuffer_format_override, "Format")) {
            LogInfo("Backbuffer format override changed to: %s",
                    settings::g_experimentalTabSettings.backbuffer_format_override.GetLabels()[settings::g_experimentalTabSettings.backbuffer_format_override.GetValue()]);
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

void DrawBufferResolutionUpgrade() {
    ImGui::TextColored(ImVec4(0.8f, 0.8f, 1.0f, 1.0f), "=== Buffer Resolution Upgrade ===");

    // Warning about experimental nature
    ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "⚠ EXPERIMENTAL FEATURE - May cause performance issues!");
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("This feature upgrades internal buffer resolutions during resource creation.\nUse with caution as it may cause performance issues or rendering artifacts.");
    }

    ImGui::Spacing();

    // Enable/disable checkbox
    if (CheckboxSetting(settings::g_experimentalTabSettings.buffer_resolution_upgrade_enabled, "Enable Buffer Resolution Upgrade")) {
        LogInfo("Buffer resolution upgrade %s",
                settings::g_experimentalTabSettings.buffer_resolution_upgrade_enabled.GetValue() ? "enabled" : "disabled");
    }

    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Upgrade internal buffer resolutions during resource creation.\nRequires restart to take effect.");
    }

    // Resolution upgrade controls (only enabled when upgrade is enabled)
    if (settings::g_experimentalTabSettings.buffer_resolution_upgrade_enabled.GetValue()) {
        ImGui::Spacing();

        // Mode selection
        if (ComboSettingWrapper(settings::g_experimentalTabSettings.buffer_resolution_upgrade_mode, "Upgrade Mode")) {
            LogInfo("Buffer resolution upgrade mode changed to: %s",
                    settings::g_experimentalTabSettings.buffer_resolution_upgrade_mode.GetLabels()[settings::g_experimentalTabSettings.buffer_resolution_upgrade_mode.GetValue()]);
        }

        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Select the buffer resolution upgrade mode:\n"
                            "• Upgrade 1280x720 by Scale Factor: Specifically upgrade 1280x720 buffers by the scale factor\n"
                            "• Upgrade by Scale Factor: Scale all buffers by the specified factor\n"
                            "• Upgrade Custom Resolution: Upgrade specific resolution to custom target");
        }

        // Scale factor control (for both mode 0 and mode 1)
        if (settings::g_experimentalTabSettings.buffer_resolution_upgrade_mode.GetValue() == 0 ||
            settings::g_experimentalTabSettings.buffer_resolution_upgrade_mode.GetValue() == 1) {
            ImGui::Spacing();
            ImGui::Text("Scale Factor:");

            if (SliderIntSetting(settings::g_experimentalTabSettings.buffer_resolution_upgrade_scale_factor, "Scale Factor")) {
                LogInfo("Buffer resolution upgrade scale factor changed to: %d",
                        settings::g_experimentalTabSettings.buffer_resolution_upgrade_scale_factor.GetValue());
            }

            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Scale factor to apply to all buffer resolutions (1-4x)");
            }
        }

        // Custom resolution controls (for custom mode)
        if (settings::g_experimentalTabSettings.buffer_resolution_upgrade_mode.GetValue() == 2) { // Custom Resolution
            ImGui::Spacing();
            ImGui::Text("Target Resolution:");

            ImGui::SetNextItemWidth(120);
            if (SliderIntSetting(settings::g_experimentalTabSettings.buffer_resolution_upgrade_width, "Width")) {
                LogInfo("Buffer resolution upgrade width changed to: %d",
                        settings::g_experimentalTabSettings.buffer_resolution_upgrade_width.GetValue());
            }

            ImGui::SameLine();
            ImGui::SetNextItemWidth(120);
            if (SliderIntSetting(settings::g_experimentalTabSettings.buffer_resolution_upgrade_height, "Height")) {
                LogInfo("Buffer resolution upgrade height changed to: %d",
                        settings::g_experimentalTabSettings.buffer_resolution_upgrade_height.GetValue());
            }

            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Target resolution for buffer upgrades.\nWidth: 320-7680, Height: 240-4320");
            }
        }

        // Show current settings info
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Note: Changes require restart to take effect");

                // Show what the upgrade will do
        int mode = settings::g_experimentalTabSettings.buffer_resolution_upgrade_mode.GetValue();
        int scale = settings::g_experimentalTabSettings.buffer_resolution_upgrade_scale_factor.GetValue();
        if (mode == 0) {
            ImGui::TextColored(ImVec4(0.8f, 1.0f, 0.8f, 1.0f), "Will upgrade 1280x720 buffers to %dx%d (%dx scale)",
                    1280 * scale, 720 * scale, scale);
        } else if (mode == 1) {
            ImGui::TextColored(ImVec4(0.8f, 1.0f, 0.8f, 1.0f), "Will scale all buffers by %dx", scale);
        } else if (mode == 2) {
            ImGui::TextColored(ImVec4(0.8f, 1.0f, 0.8f, 1.0f), "Will upgrade buffers to: %dx%d",
                    settings::g_experimentalTabSettings.buffer_resolution_upgrade_width.GetValue(),
                    settings::g_experimentalTabSettings.buffer_resolution_upgrade_height.GetValue());
        }
    }
}

void DrawTextureFormatUpgrade() {
    ImGui::TextColored(ImVec4(0.8f, 0.8f, 1.0f, 1.0f), "=== Texture Format Upgrade ===");

    // Warning about experimental nature
    ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "⚠ EXPERIMENTAL FEATURE - May cause performance issues!");
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("This feature upgrades texture formats to RGB16A16 during resource creation.\nUse with caution as it may cause performance issues or rendering artifacts.");
    }

    ImGui::Spacing();

    // Enable/disable checkbox
    if (CheckboxSetting(settings::g_experimentalTabSettings.texture_format_upgrade_enabled, "Upgrade Textures to RGB16A16")) {
        LogInfo("Texture format upgrade %s",
                settings::g_experimentalTabSettings.texture_format_upgrade_enabled.GetValue() ? "enabled" : "disabled");
    }

    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Upgrade texture formats to RGB16A16 (16-bit per channel) for textures at 720p, 1440p, and 4K resolutions.\nRequires restart to take effect.");
    }

    // Show current settings info
    ImGui::Spacing();
    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Note: Changes require restart to take effect");

    if (settings::g_experimentalTabSettings.texture_format_upgrade_enabled.GetValue()) {
        ImGui::TextColored(ImVec4(0.8f, 1.0f, 0.8f, 1.0f), "Will upgrade texture formats to RGB16A16 (16-bit per channel) for 720p, 1440p, and 4K textures");
    }
}

void DrawSleepHookControls() {
    ImGui::TextColored(ImVec4(0.8f, 0.8f, 1.0f, 1.0f), "=== Sleep Hook Controls ===");
    ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "⚠ EXPERIMENTAL FEATURE - Hooks game sleep calls for FPS control!");
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("This feature hooks Windows Sleep APIs (Sleep, SleepEx, WaitForSingleObject, WaitForMultipleObjects) to modify sleep durations.\nUseful for games that use sleep-based FPS limiting like Unity games.");
    }

    ImGui::Spacing();

    // Enable/disable checkbox
    if (CheckboxSetting(settings::g_experimentalTabSettings.sleep_hook_enabled, "Enable Sleep Hooks")) {
        LogInfo("Sleep hooks %s", settings::g_experimentalTabSettings.sleep_hook_enabled.GetValue() ? "enabled" : "disabled");
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Enable hooks for Windows Sleep APIs to modify sleep durations for FPS control.");
    }

    // Render thread only checkbox
    if (CheckboxSetting(settings::g_experimentalTabSettings.sleep_hook_render_thread_only, "Render Thread Only")) {
        LogInfo("Sleep hooks render thread only %s", settings::g_experimentalTabSettings.sleep_hook_render_thread_only.GetValue() ? "enabled" : "disabled");
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Only modify sleep calls on the render thread. If render thread is unknown, sleep calls are not modified.");
    }

    if (settings::g_experimentalTabSettings.sleep_hook_enabled.GetValue()) {
        ImGui::Spacing();

        // Sleep multiplier slider
        if (SliderFloatSetting(settings::g_experimentalTabSettings.sleep_multiplier, "Sleep Multiplier", "%.2fx")) {
            LogInfo("Sleep multiplier set to %.2fx", settings::g_experimentalTabSettings.sleep_multiplier.GetValue());
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Multiplier applied to sleep durations. 1.0 = no change, 0.5 = half duration, 2.0 = double duration.");
        }

        // Min sleep duration slider
        if (SliderIntSetting(settings::g_experimentalTabSettings.min_sleep_duration_ms, "Min Sleep Duration (ms)", "%d ms")) {
            LogInfo("Min sleep duration set to %d ms", settings::g_experimentalTabSettings.min_sleep_duration_ms.GetValue());
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Minimum sleep duration in milliseconds. 0 = no minimum limit.");
        }

        // Max sleep duration slider
        if (SliderIntSetting(settings::g_experimentalTabSettings.max_sleep_duration_ms, "Max Sleep Duration (ms)", "%d ms")) {
            LogInfo("Max sleep duration set to %d ms", settings::g_experimentalTabSettings.max_sleep_duration_ms.GetValue());
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Maximum sleep duration in milliseconds. 0 = no maximum limit.");
        }

        ImGui::Spacing();

        // Show current settings summary
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Current Settings:");
        ImGui::TextColored(ImVec4(0.8f, 1.0f, 0.8f, 1.0f), "  Multiplier: %.2fx", settings::g_experimentalTabSettings.sleep_multiplier.GetValue());
        ImGui::TextColored(ImVec4(0.8f, 1.0f, 0.8f, 1.0f), "  Min Duration: %d ms", settings::g_experimentalTabSettings.min_sleep_duration_ms.GetValue());
        ImGui::TextColored(ImVec4(0.8f, 1.0f, 0.8f, 1.0f), "  Max Duration: %d ms", settings::g_experimentalTabSettings.max_sleep_duration_ms.GetValue());

        // Show hook statistics if available
        if (renodx::hooks::g_sleep_hook_stats.total_calls.load() > 0) {
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(0.8f, 0.8f, 1.0f, 1.0f), "Hook Statistics:");
            ImGui::TextColored(ImVec4(0.8f, 1.0f, 0.8f, 1.0f), "  Total Calls: %llu", renodx::hooks::g_sleep_hook_stats.total_calls.load());
            ImGui::TextColored(ImVec4(0.8f, 1.0f, 0.8f, 1.0f), "  Modified Calls: %llu", renodx::hooks::g_sleep_hook_stats.modified_calls.load());

            uint64_t total_original = renodx::hooks::g_sleep_hook_stats.total_original_duration_ms.load();
            uint64_t total_modified = renodx::hooks::g_sleep_hook_stats.total_modified_duration_ms.load();
            if (total_original > 0) {
                ImGui::TextColored(ImVec4(0.8f, 1.0f, 0.8f, 1.0f), "  Total Original Duration: %llu ms", total_original);
                ImGui::TextColored(ImVec4(0.8f, 1.0f, 0.8f, 1.0f), "  Total Modified Duration: %llu ms", total_modified);
                ImGui::TextColored(ImVec4(0.8f, 1.0f, 0.8f, 1.0f), "  Time Saved: %lld ms", static_cast<int64_t>(total_original) - static_cast<int64_t>(total_modified));
            }
        }
    }
}

void DrawTimeSlowdownControls() {
    ImGui::TextColored(ImVec4(0.8f, 0.8f, 1.0f, 1.0f), "=== Time Slowdown Controls ===");
    ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "⚠ EXPERIMENTAL FEATURE - Manipulates game time via multiple timer APIs!");
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("This feature hooks multiple timer APIs to manipulate game time.\nUseful for bypassing FPS limits and slowing down/speeding up games that use various timing methods.");
    }

    ImGui::Spacing();

    // Enable/disable checkbox
    if (CheckboxSetting(settings::g_experimentalTabSettings.timeslowdown_enabled, "Enable Time Slowdown")) {
        LogInfo("Time slowdown %s", settings::g_experimentalTabSettings.timeslowdown_enabled.GetValue() ? "enabled" : "disabled");
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Enable time manipulation via timer API hooks.");
    }

    if (settings::g_experimentalTabSettings.timeslowdown_enabled.GetValue()) {
        ImGui::Spacing();

        // Time multiplier slider
        if (SliderFloatSetting(settings::g_experimentalTabSettings.timeslowdown_multiplier, "Time Multiplier", "%.2fx")) {
            LogInfo("Time multiplier set to %.2fx", settings::g_experimentalTabSettings.timeslowdown_multiplier.GetValue());
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Multiplier for game time. 1.0 = normal speed, 0.5 = half speed, 2.0 = double speed.");
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // Timer Hook Selection
        ImGui::TextColored(ImVec4(0.9f, 0.9f, 1.0f, 1.0f), "Timer Hook Selection:");
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Choose which timer APIs to hook (None/Enabled/Render Thread Only/Everything Except Render Thread)");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Select which timer APIs to hook for time manipulation.\nOnly one hook type can be active at a time per API.\nRender Thread Only applies the hook only to the rendering thread.\nEverything Except Render Thread applies to all threads except the rendering thread.");
        }

        ImGui::Spacing();

        // QueryPerformanceCounter hook
        if (ComboSettingWrapper(settings::g_experimentalTabSettings.query_performance_counter_hook, "QueryPerformanceCounter")) {
            renodx::hooks::TimerHookType type = static_cast<renodx::hooks::TimerHookType>(settings::g_experimentalTabSettings.query_performance_counter_hook.GetValue());
            renodx::hooks::SetTimerHookType(renodx::hooks::HOOK_QUERY_PERFORMANCE_COUNTER, type);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("High-resolution timer used by most modern games for precise timing.");
        }

        // GetTickCount hook
        if (ComboSettingWrapper(settings::g_experimentalTabSettings.get_tick_count_hook, "GetTickCount")) {
            renodx::hooks::TimerHookType type = static_cast<renodx::hooks::TimerHookType>(settings::g_experimentalTabSettings.get_tick_count_hook.GetValue());
            renodx::hooks::SetTimerHookType(renodx::hooks::HOOK_GET_TICK_COUNT, type);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("32-bit millisecond timer, commonly used by older games.");
        }

        // GetTickCount64 hook
        if (ComboSettingWrapper(settings::g_experimentalTabSettings.get_tick_count64_hook, "GetTickCount64")) {
            renodx::hooks::TimerHookType type = static_cast<renodx::hooks::TimerHookType>(settings::g_experimentalTabSettings.get_tick_count64_hook.GetValue());
            renodx::hooks::SetTimerHookType(renodx::hooks::HOOK_GET_TICK_COUNT64, type);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("64-bit millisecond timer, used by some modern games.");
        }

        // timeGetTime hook
        if (ComboSettingWrapper(settings::g_experimentalTabSettings.time_get_time_hook, "timeGetTime")) {
            renodx::hooks::TimerHookType type = static_cast<renodx::hooks::TimerHookType>(settings::g_experimentalTabSettings.time_get_time_hook.GetValue());
            renodx::hooks::SetTimerHookType(renodx::hooks::HOOK_TIME_GET_TIME, type);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Multimedia timer, often used for audio/video timing.");
        }

        // GetSystemTime hook
        if (ComboSettingWrapper(settings::g_experimentalTabSettings.get_system_time_hook, "GetSystemTime")) {
            renodx::hooks::TimerHookType type = static_cast<renodx::hooks::TimerHookType>(settings::g_experimentalTabSettings.get_system_time_hook.GetValue());
            renodx::hooks::SetTimerHookType(renodx::hooks::HOOK_GET_SYSTEM_TIME, type);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("System time in SYSTEMTIME format, used by some games for timestamps.");
        }

        // GetSystemTimeAsFileTime hook
        if (ComboSettingWrapper(settings::g_experimentalTabSettings.get_system_time_as_file_time_hook, "GetSystemTimeAsFileTime")) {
            renodx::hooks::TimerHookType type = static_cast<renodx::hooks::TimerHookType>(settings::g_experimentalTabSettings.get_system_time_as_file_time_hook.GetValue());
            renodx::hooks::SetTimerHookType(renodx::hooks::HOOK_GET_SYSTEM_TIME_AS_FILE_TIME, type);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("System time in FILETIME format, used by some games for high-precision timestamps.");
        }


        // GetSystemTimePreciseAsFileTime hook
        if (ComboSettingWrapper(settings::g_experimentalTabSettings.get_system_time_precise_as_file_time_hook, "GetSystemTimePreciseAsFileTime")) {
            renodx::hooks::TimerHookType type = static_cast<renodx::hooks::TimerHookType>(settings::g_experimentalTabSettings.get_system_time_precise_as_file_time_hook.GetValue());
            renodx::hooks::SetTimerHookType(renodx::hooks::HOOK_GET_SYSTEM_TIME_PRECISE_AS_FILE_TIME, type);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("High-precision system time (Windows 8+), used by modern games for precise timing.");
        }

        // GetLocalTime hook
        if (ComboSettingWrapper(settings::g_experimentalTabSettings.get_local_time_hook, "GetLocalTime")) {
            renodx::hooks::TimerHookType type = static_cast<renodx::hooks::TimerHookType>(settings::g_experimentalTabSettings.get_local_time_hook.GetValue());
            renodx::hooks::SetTimerHookType(renodx::hooks::HOOK_GET_LOCAL_TIME, type);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Local system time (vs UTC), used by some games for timezone-aware timing.");
        }

        // NtQuerySystemTime hook
        if (ComboSettingWrapper(settings::g_experimentalTabSettings.nt_query_system_time_hook, "NtQuerySystemTime")) {
            renodx::hooks::TimerHookType type = static_cast<renodx::hooks::TimerHookType>(settings::g_experimentalTabSettings.nt_query_system_time_hook.GetValue());
            renodx::hooks::SetTimerHookType(renodx::hooks::HOOK_NT_QUERY_SYSTEM_TIME, type);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Native API system time, used by some games for low-level timing access.");
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // Show current settings summary
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Current Settings:");
        ImGui::TextColored(ImVec4(0.8f, 1.0f, 0.8f, 1.0f), "  Time Multiplier: %.2fx", settings::g_experimentalTabSettings.timeslowdown_multiplier.GetValue());

        // Show hook status
        bool hooks_installed = renodx::hooks::AreTimeslowdownHooksInstalled();
        ImGui::TextColored(ImVec4(0.8f, 1.0f, 0.8f, 1.0f), "  Hooks Status: %s", hooks_installed ? "Installed" : "Not Installed");

        // Show current runtime values
        double current_multiplier = renodx::hooks::GetTimeslowdownMultiplier();
        bool current_enabled = renodx::hooks::IsTimeslowdownEnabled();
        ImGui::TextColored(ImVec4(0.8f, 1.0f, 0.8f, 1.0f), "  Runtime Multiplier: %.2fx", current_multiplier);
        ImGui::TextColored(ImVec4(0.8f, 1.0f, 0.8f, 1.0f), "  Runtime Enabled: %s", current_enabled ? "Yes" : "No");

        // Show active hooks
        ImGui::TextColored(ImVec4(0.8f, 1.0f, 0.8f, 1.0f), "  Active Hooks:");
        const char* hook_names[] = {
            "QueryPerformanceCounter", "GetTickCount", "GetTickCount64",
            "timeGetTime", "GetSystemTime", "GetSystemTimeAsFileTime",
            "GetSystemTimePreciseAsFileTime", "GetLocalTime", "NtQuerySystemTime"
        };
        const char* hook_constants[] = {
            renodx::hooks::HOOK_QUERY_PERFORMANCE_COUNTER,
            renodx::hooks::HOOK_GET_TICK_COUNT,
            renodx::hooks::HOOK_GET_TICK_COUNT64,
            renodx::hooks::HOOK_TIME_GET_TIME,
            renodx::hooks::HOOK_GET_SYSTEM_TIME,
            renodx::hooks::HOOK_GET_SYSTEM_TIME_AS_FILE_TIME,
            renodx::hooks::HOOK_GET_SYSTEM_TIME_PRECISE_AS_FILE_TIME,
            renodx::hooks::HOOK_GET_LOCAL_TIME,
            renodx::hooks::HOOK_NT_QUERY_SYSTEM_TIME
        };

        for (int i = 0; i < 9; i++) {
            if (renodx::hooks::IsTimerHookEnabled(hook_constants[i])) {
                const char* mode;
                if (renodx::hooks::IsTimerHookRenderThreadOnly(hook_constants[i])) {
                    mode = " (Render Thread)";
                } else if (renodx::hooks::IsTimerHookEverythingExceptRenderThread(hook_constants[i])) {
                    mode = " (Everything Except Render Thread)";
                } else {
                    mode = " (All Threads)";
                }
                ImGui::TextColored(ImVec4(0.6f, 1.0f, 0.6f, 1.0f), "    %s%s", hook_names[i], mode);
            }
        }

        ImGui::Spacing();
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "⚠ WARNING: This affects all time-based game logic!");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Time slowdown affects all game systems that use the selected timer APIs for timing.\nThis includes physics, animations, AI, and other time-dependent systems.\nUse 'Render Thread Only' to minimize impact on non-rendering systems.\nUse 'Everything Except Render Thread' to target non-rendering systems while leaving rendering unaffected.");
        }
    }
}


} // namespace ui::new_ui

