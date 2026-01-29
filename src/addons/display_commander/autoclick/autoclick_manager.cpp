#include "autoclick_manager.hpp"
#include <imgui.h>
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <memory>
#include <thread>
#include "../globals.hpp"
#include "../res/forkawesome.h"
#include "../settings/experimental_tab_settings.hpp"
#include "../utils/logging.hpp"
#include "../utils/timing.hpp"
#include "../widgets/xinput_widget/xinput_widget.hpp"

namespace autoclick {

// Global variables for auto-click functionality
std::atomic<bool> g_auto_click_thread_running{false};
std::thread g_auto_click_thread;
HANDLE g_auto_click_timer_handle = nullptr;
const bool g_move_mouse = true;
const bool g_mouse_spoofing_enabled = true;

// UI state tracking for optimization
std::atomic<bool> g_ui_overlay_open{false};
std::atomic<LONGLONG> g_last_ui_draw_time_ns{0};

// Global variables for up/down key press functionality
std::atomic<bool> g_up_down_key_thread_running{false};
std::thread g_up_down_key_thread;
HANDLE g_up_down_key_timer_handle = nullptr;

// Global variables for button-only press functionality
std::atomic<bool> g_button_only_thread_running{false};
std::thread g_button_only_thread;
HANDLE g_button_only_timer_handle = nullptr;

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
            // Small delay for mouse movement using accurate timing
            LONGLONG wait_start_ns = utils::get_now_ns();
            LONGLONG wait_target_ns = wait_start_ns + (50 * utils::NS_TO_MS);
            utils::wait_until_ns(wait_target_ns, g_auto_click_timer_handle);
        }
    }

    // Send click messages
    LPARAM lParam = MAKELPARAM(x, y);
    PostMessage(hwnd, WM_LBUTTONDOWN, MK_LBUTTON, lParam);
    // Small delay between mouse down and up using accurate timing
    LONGLONG wait_start_ns = utils::get_now_ns();
    LONGLONG wait_target_ns = wait_start_ns + (10 * utils::NS_TO_MS);
    utils::wait_until_ns(wait_target_ns, g_auto_click_timer_handle);
    PostMessage(hwnd, WM_LBUTTONUP, MK_LBUTTON, lParam);

    LogInfo("%s click for sequence %d sent to game window at (%d, %d)%s", is_test ? "Test" : "Auto", sequence_num, x, y,
            g_move_mouse ? (g_mouse_spoofing_enabled ? " - mouse position spoofed" : " - mouse moved to screen")
                         : " - mouse not moved");
}

// Helper function to send keyboard key down message
// Uses SendInput (like gamepad remapping) for more reliable input injection
void SendKeyDown(HWND hwnd, int vk_code) {
    if (hwnd == nullptr || IsWindow(hwnd) == FALSE) {
        return;
    }
    // Use SendInput for system-level input injection (same as gamepad remapping)
    INPUT input = {};
    input.type = INPUT_KEYBOARD;
    input.ki.wVk = vk_code;
    input.ki.dwFlags = 0;  // Key down
    input.ki.time = 0;
    input.ki.dwExtraInfo = GetMessageExtraInfo();
    SendInput(1, &input, sizeof(INPUT));
}

// Helper function to send keyboard key up message
// Uses SendInput (like gamepad remapping) for more reliable input injection
void SendKeyUp(HWND hwnd, int vk_code) {
    if (hwnd == nullptr || IsWindow(hwnd) == FALSE) {
        return;
    }
    // Use SendInput for system-level input injection (same as gamepad remapping)
    INPUT input = {};
    input.type = INPUT_KEYBOARD;
    input.ki.wVk = vk_code;
    input.ki.dwFlags = KEYEVENTF_KEYUP;  // Key up
    input.ki.time = 0;
    input.ki.dwExtraInfo = GetMessageExtraInfo();
    SendInput(1, &input, sizeof(INPUT));
}

// Helper function to draw a sequence using settings directly
void DrawSequence(int sequence_num) {
    int idx = sequence_num - 1;  // Convert to 0-based index

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
            // Check if UI overlay is open - if so, wait for 2 seconds using accurate timing
            if (g_ui_overlay_open.load()) {
                LogDebug("Auto-click: UI overlay is open, waiting for 2 seconds");
                LONGLONG wait_start_ns = utils::get_now_ns();
                LONGLONG wait_target_ns = wait_start_ns + (2000 * utils::NS_TO_MS);
                utils::wait_until_ns(wait_target_ns, g_auto_click_timer_handle);
                continue;
            }

            // Check if UI was drawn recently (within last 2 seconds) - if so, wait briefly using accurate timing
            LONGLONG now_ns = utils::get_now_ns();
            LONGLONG last_ui_draw = g_last_ui_draw_time_ns.load();
            if (last_ui_draw > 0 && (now_ns - last_ui_draw) < (2 * utils::SEC_TO_NS)) {
                LogDebug("Auto-click: UI was drawn recently, waiting for 500ms");
                LONGLONG wait_start_ns = utils::get_now_ns();
                LONGLONG wait_target_ns = wait_start_ns + (500 * utils::NS_TO_MS);
                utils::wait_until_ns(wait_target_ns, g_auto_click_timer_handle);
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
                        // Wait for interval using accurate timing
                        LONGLONG wait_start_ns = utils::get_now_ns();
                        LONGLONG wait_target_ns = wait_start_ns + (static_cast<LONGLONG>(interval) * utils::NS_TO_MS);
                        utils::wait_until_ns(wait_target_ns, g_auto_click_timer_handle);
                    }
                }
            } else {
                LogWarn("Auto-click: No valid game window handle available");
                // Wait a bit before retrying using accurate timing
                LONGLONG wait_start_ns = utils::get_now_ns();
                LONGLONG wait_target_ns = wait_start_ns + (1000 * utils::NS_TO_MS);
                utils::wait_until_ns(wait_target_ns, g_auto_click_timer_handle);
            }
        } else {
            // Auto-click is disabled, wait for 1 second using accurate timing
            LONGLONG wait_start_ns = utils::get_now_ns();
            LONGLONG wait_target_ns = wait_start_ns + (1000 * utils::NS_TO_MS);
            utils::wait_until_ns(wait_target_ns, g_auto_click_timer_handle);
        }
    }

    g_auto_click_thread_running.store(false);
}

// Data structures for up/down key press sequence
enum class GamepadActionType {
    SET_STICK_AND_BUTTONS,  // Set left stick Y and button mask
    WAIT,                   // Wait for a fixed duration
    HOLD,                   // Hold current state for a duration with early exit checking
    CLEAR                   // Clear all overrides
};

struct GamepadAction {
    GamepadActionType type;
    const char* log_message;
    float left_stick_y;     // For SET_STICK_AND_BUTTONS: stick value (INFINITY = not set)
    WORD button_mask;       // For SET_STICK_AND_BUTTONS: button mask
    LONGLONG duration_ms;   // For WAIT/HOLD: duration in milliseconds (0 = use seconds)
    LONGLONG duration_sec;  // For WAIT/HOLD: duration in seconds (0 = use milliseconds)
};

// Up/Down key press sequence definition
static const GamepadAction g_up_down_sequence[] = {
    // Set forward and button Y
    {GamepadActionType::SET_STICK_AND_BUTTONS, "Up/Down gamepad: Setting left stick Y forward and button Y", 1.0f,
     XINPUT_GAMEPAD_Y | XINPUT_GAMEPAD_A, 0, 0},

    // Wait 100ms before adding button A
    {GamepadActionType::WAIT, nullptr, INFINITY, 0, 1000, 0},
    {GamepadActionType::SET_STICK_AND_BUTTONS, "Up/Down gamepad: Setting left stick Y forward and button Y", 1.0f, 0, 0,
     0},

    // Wait 100ms before adding button A
    {GamepadActionType::WAIT, nullptr, INFINITY, 0, 100, 0},

    // Add button A (both Y and A pressed)
    {GamepadActionType::SET_STICK_AND_BUTTONS, "Up/Down gamepad: Adding button A (Y and A both pressed)", 1.0f,
     XINPUT_GAMEPAD_Y | XINPUT_GAMEPAD_A, 0, 0},

    // Hold for 10 seconds
    {GamepadActionType::HOLD, nullptr, INFINITY, 0, 0, 10},

    // Clear override
    {GamepadActionType::CLEAR, "Up/Down gamepad: Clearing left stick Y override", INFINITY, 0, 0, 0},

    // Wait 100ms before setting backward
    {GamepadActionType::WAIT, nullptr, INFINITY, 0, 100, 0},

    // Set backward and button Y
    {GamepadActionType::SET_STICK_AND_BUTTONS, "Up/Down gamepad: Setting left stick Y backward and button Y", -1.0f, 0,
     0, 0},

    // Hold for 3 seconds
    {GamepadActionType::HOLD, nullptr, INFINITY, 0, 0, 3},

    // Clear override
    {GamepadActionType::CLEAR, "Up/Down gamepad: Clearing left stick Y override", INFINITY, 0, 0, 0},

    // Wait 100ms before next cycle
    {GamepadActionType::WAIT, nullptr, INFINITY, 0, 100, 0}};

// Button-only press sequence definition (Y/A buttons only, no stick movement)
static const GamepadAction g_button_only_sequence[] = {
    // Add button A (both Y and A pressed)
    {GamepadActionType::SET_STICK_AND_BUTTONS, "Button-only gamepad: Adding button A (Y and A both pressed)", INFINITY,
     XINPUT_GAMEPAD_Y | XINPUT_GAMEPAD_A, 0, 0},

    // Hold for 10 seconds
    {GamepadActionType::HOLD, nullptr, INFINITY, 0, 1000, 0},

    // Clear override
    {GamepadActionType::CLEAR, "Button-only gamepad: Clearing button override", INFINITY, 0, 0, 0},

    // Wait 100ms before next cycle
    {GamepadActionType::WAIT, nullptr, INFINITY, 0, 50, 0}};

// Helper function to execute a single gamepad action
static bool ExecuteGamepadAction(
    const GamepadAction& action,
    const std::shared_ptr<display_commander::widgets::xinput_widget::XInputSharedState>& shared_state,
    HANDLE timer_handle) {
    switch (action.type) {
        case GamepadActionType::SET_STICK_AND_BUTTONS: {
            if (action.log_message != nullptr) {
                LogInfo("%s", action.log_message);
            }
            // Only set left_stick_y if it's not INFINITY (INFINITY means don't override stick)
            if (action.left_stick_y != INFINITY) {
                shared_state->override_state.left_stick_y.store(action.left_stick_y);
            }
            shared_state->override_state.buttons_pressed_mask.store(action.button_mask);
            return true;
        }

        case GamepadActionType::WAIT: {
            LONGLONG duration_ns = action.duration_ms > 0 ? (action.duration_ms * utils::NS_TO_MS)
                                                          : (action.duration_sec * utils::SEC_TO_NS);
            LONGLONG wait_start_ns = utils::get_now_ns();
            LONGLONG wait_target_ns = wait_start_ns + duration_ns;
            utils::wait_until_ns(wait_target_ns, timer_handle);
            return true;
        }

        case GamepadActionType::HOLD: {
            LONGLONG duration_ns = action.duration_ms > 0 ? (action.duration_ms * utils::NS_TO_MS)
                                                          : (action.duration_sec * utils::SEC_TO_NS);
            LONGLONG hold_start_ns = utils::get_now_ns();
            LONGLONG hold_target_ns = hold_start_ns + duration_ns;

            // Check every 100ms for early exit while waiting
            while (utils::get_now_ns() < hold_target_ns) {
                if (!g_auto_click_enabled.load()) {
                    // Clear override on early exit
                    shared_state->override_state.left_stick_y.store(INFINITY);
                    shared_state->override_state.buttons_pressed_mask.store(0);
                    return false;  // Signal early exit
                }
                // Wait in 100ms chunks for early exit checking
                LONGLONG current_ns = utils::get_now_ns();
                LONGLONG next_check_ns = (std::min)(current_ns + (100 * utils::NS_TO_MS), hold_target_ns);
                utils::wait_until_ns(next_check_ns, timer_handle);
            }
            return true;
        }

        case GamepadActionType::CLEAR: {
            if (action.log_message != nullptr) {
                LogInfo("%s", action.log_message);
            }
            shared_state->override_state.left_stick_y.store(INFINITY);
            shared_state->override_state.buttons_pressed_mask.store(0);
            return true;
        }

        default: return false;
    }
}

// Up/Down key presss thread function - always running, sleeps when inactive
void UpDownKeyPressThread() {
    g_up_down_key_thread_running.store(true);
    LogInfo("Up/Down key press thread started");

    while (true) {
        // Check if auto-click is enabled (master switch)
        bool auto_click_enabled = g_auto_click_enabled.load();

        // Check if up/down key press is enabled
        if (auto_click_enabled && settings::g_experimentalTabSettings.up_down_key_press_enabled.GetValue()) {
            // Check if UI overlay is open - if so, wait for 2 seconds using accurate timing
            if (g_ui_overlay_open.load()) {
                LogDebug("Up/Down key press: UI overlay is open, waiting for 2 seconds");
                LONGLONG wait_start_ns = utils::get_now_ns();
                LONGLONG wait_target_ns = wait_start_ns + (2000 * utils::NS_TO_MS);
                utils::wait_until_ns(wait_target_ns, g_up_down_key_timer_handle);
                continue;
            }

            // Check if UI was drawn recently (within last 2 seconds) - if so, wait briefly using accurate timing
            LONGLONG now_ns = utils::get_now_ns();
            LONGLONG last_ui_draw = g_last_ui_draw_time_ns.load();
            if (last_ui_draw > 0 && (now_ns - last_ui_draw) < (2 * utils::SEC_TO_NS)) {
                LogDebug("Up/Down key press: UI was drawn recently, waiting for 500ms");
                LONGLONG wait_start_ns = utils::get_now_ns();
                LONGLONG wait_target_ns = wait_start_ns + (500 * utils::NS_TO_MS);
                utils::wait_until_ns(wait_target_ns, g_up_down_key_timer_handle);
                continue;
            }

            // Get the shared XInput state (works even if controller is disconnected - hooks will spoof connection)
            auto shared_state = display_commander::widgets::xinput_widget::XInputWidget::GetSharedState();
            if (shared_state) {
                // Execute the sequence of actions
                // Note: Override state works even when controller is disconnected - XInput hooks will spoof connection
                for (const auto& action : g_up_down_sequence) {
                    if (!ExecuteGamepadAction(action, shared_state, g_up_down_key_timer_handle)) {
                        // Early exit requested
                        break;
                    }
                }
            } else {
                // Shared state should always be available, but if not, wait briefly and retry
                LogDebug("Up/Down gamepad: Shared state not yet available, waiting...");
                LONGLONG wait_start_ns = utils::get_now_ns();
                LONGLONG wait_target_ns = wait_start_ns + (100 * utils::NS_TO_MS);
                utils::wait_until_ns(wait_target_ns, g_up_down_key_timer_handle);
            }
        } else {
            // Up/Down key press is disabled, wait for 1 second using accurate timing
            LONGLONG wait_start_ns = utils::get_now_ns();
            LONGLONG wait_target_ns = wait_start_ns + (1000 * utils::NS_TO_MS);
            utils::wait_until_ns(wait_target_ns, g_up_down_key_timer_handle);
        }
    }

    g_up_down_key_thread_running.store(false);
}

// Button-only press thread function - always running, sleeps when inactive
void ButtonOnlyPressThread() {
    g_button_only_thread_running.store(true);
    LogInfo("Button-only press thread started");

    while (true) {
        // Check if auto-click is enabled (master switch)
        bool auto_click_enabled = g_auto_click_enabled.load();

        // Check if button-only press is enabled
        if (auto_click_enabled && settings::g_experimentalTabSettings.button_only_press_enabled.GetValue()) {
            // Check if UI overlay is open - if so, wait for 2 seconds using accurate timing
            if (g_ui_overlay_open.load()) {
                LogDebug("Button-only press: UI overlay is open, waiting for 2 seconds");
                LONGLONG wait_start_ns = utils::get_now_ns();
                LONGLONG wait_target_ns = wait_start_ns + (2000 * utils::NS_TO_MS);
                utils::wait_until_ns(wait_target_ns, g_button_only_timer_handle);
                continue;
            }

            // Check if UI was drawn recently (within last 2 seconds) - if so, wait briefly using accurate timing
            LONGLONG now_ns = utils::get_now_ns();
            LONGLONG last_ui_draw = g_last_ui_draw_time_ns.load();
            if (last_ui_draw > 0 && (now_ns - last_ui_draw) < (2 * utils::SEC_TO_NS)) {
                LogDebug("Button-only press: UI was drawn recently, waiting for 500ms");
                LONGLONG wait_start_ns = utils::get_now_ns();
                LONGLONG wait_target_ns = wait_start_ns + (500 * utils::NS_TO_MS);
                utils::wait_until_ns(wait_target_ns, g_button_only_timer_handle);
                continue;
            }

            // Get the shared XInput state (works even if controller is disconnected - hooks will spoof connection)
            auto shared_state = display_commander::widgets::xinput_widget::XInputWidget::GetSharedState();
            if (shared_state) {
                // Execute the sequence of actions
                // Note: Override state works even when controller is disconnected - XInput hooks will spoof connection
                for (const auto& action : g_button_only_sequence) {
                    if (!ExecuteGamepadAction(action, shared_state, g_button_only_timer_handle)) {
                        // Early exit requested
                        break;
                    }
                }
            } else {
                // Shared state should always be available, but if not, wait briefly and retry
                LogDebug("Button-only gamepad: Shared state not yet available, waiting...");
                LONGLONG wait_start_ns = utils::get_now_ns();
                LONGLONG wait_target_ns = wait_start_ns + (100 * utils::NS_TO_MS);
                utils::wait_until_ns(wait_target_ns, g_button_only_timer_handle);
            }
        } else {
            // Button-only press is disabled, wait for 1 second using accurate timing
            LONGLONG wait_start_ns = utils::get_now_ns();
            LONGLONG wait_target_ns = wait_start_ns + (1000 * utils::NS_TO_MS);
            utils::wait_until_ns(wait_target_ns, g_button_only_timer_handle);
        }
    }

    g_button_only_thread_running.store(false);
}

// Function to start the auto-click thread
void StartAutoClickThread() {
    if (!g_auto_click_thread_running.load()) {
        g_auto_click_thread = std::thread(AutoClickThread);
        LogInfo("Auto-click thread started");
    }
}

// Function to start the up/down key press thread
void StartUpDownKeyPressThread() {
    if (!g_up_down_key_thread_running.load()) {
        g_up_down_key_thread = std::thread(UpDownKeyPressThread);
        LogInfo("Up/Down key press thread started");
    }
}

// Function to start the button-only press thread
void StartButtonOnlyPressThread() {
    if (!g_button_only_thread_running.load()) {
        g_button_only_thread = std::thread(ButtonOnlyPressThread);
        LogInfo("Button-only press thread started");
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
    ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), ICON_FK_WARNING " EXPERIMENTAL FEATURE - Use with caution!");
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(
            "This feature sends mouse click messages directly to the game window.\nUse responsibly and "
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
        ImGui::SetTooltip(
            "Enable/disable all auto-click sequences. Each sequence can be individually configured "
            "below.\n\nShortcut: Ctrl+P (can be enabled in Advanced tab)\n\nNote: Mouse position spoofing is always "
            "enabled for better stealth.");
    }
    // Mouse position spoofing is always enabled
    ImGui::TextColored(ImVec4(0.8f, 1.0f, 0.8f, 1.0f),
                       ICON_FK_OK " Mouse position spoofing is always enabled for better stealth");

    // Show current status
    if (g_auto_click_enabled.load()) {
        if (g_auto_click_thread_running.load()) {
            ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), ICON_FK_OK " Auto-click sequences are ACTIVE");
        } else {
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), ICON_FK_WARNING " Auto-click sequences are STARTING...");
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

    // Up/Down key press automation
    bool up_down_enabled = settings::g_experimentalTabSettings.up_down_key_press_enabled.GetValue();
    bool auto_click_enabled_state = g_auto_click_enabled.load();

    // Disable checkbox if auto-click is off
    if (!auto_click_enabled_state) {
        ImGui::BeginDisabled();
    }

    if (ImGui::Checkbox("W/S Key Press (10s W, 3s S, repeat)", &up_down_enabled)) {
        settings::g_experimentalTabSettings.up_down_key_press_enabled.SetValue(up_down_enabled);
        if (up_down_enabled) {
            LogInfo("W/S key press automation enabled");
        } else {
            LogInfo("W/S key press automation disabled");
        }
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(
            "Automatically presses W key for 10 seconds, then S key for 3 seconds, repeating forever.\nSequence: W "
            "down → wait 10s → W up → wait 100ms → S down → wait 3s → S up → wait 100ms → repeat.\nUses W and S keys "
            "with SendInput API.\n\nRequires 'Enable Auto-Click Sequences' to be enabled.");
    }

    if (!auto_click_enabled_state) {
        ImGui::EndDisabled();
    }

    // Button-only press automation
    bool button_only_enabled = settings::g_experimentalTabSettings.button_only_press_enabled.GetValue();

    // Disable checkbox if auto-click is off
    if (!auto_click_enabled_state) {
        ImGui::BeginDisabled();
    }

    if (ImGui::Checkbox("Y/A Button Press Only (10s hold, repeat)", &button_only_enabled)) {
        settings::g_experimentalTabSettings.button_only_press_enabled.SetValue(button_only_enabled);
        if (button_only_enabled) {
            LogInfo("Button-only press automation enabled");
        } else {
            LogInfo("Button-only press automation disabled");
        }
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(
            "Automatically presses Y button, then adds A button (Y+A), holds for 10 seconds, then clears and "
            "repeats.\nSequence: Y down → wait 100ms → Y+A down → hold 10s → clear → wait 100ms → repeat.\nNo stick "
            "movement - buttons only.\n\nRequires 'Enable Auto-Click Sequences' to be enabled.");
    }

    if (!auto_click_enabled_state) {
        ImGui::EndDisabled();
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

    // Also update the frame ID when UI is drawn
    g_last_ui_drawn_frame_id.store(g_global_frame_id.load());
    // LogDebug("Auto-click: UI draw time updated to %lld ns", now_ns);
}

}  // namespace autoclick
