#include "xinput_widget.hpp"
#include "../../globals.hpp"
#include "../../utils/logging.hpp"
#include "../../utils/general_utils.hpp"
#include "../../utils/srwlock_wrapper.hpp"
#include "../../hooks/xinput_hooks.hpp"
#include "../../hooks/hook_suppression_manager.hpp"
#include "../../hooks/timeslowdown_hooks.hpp"
#include "../../hooks/windows_hooks/windows_message_hooks.hpp"
#include "../../res/ui_colors.hpp"
#include "../../config/display_commander_config.hpp"
#include "../../settings/experimental_tab_settings.hpp"
#include "../../settings/hook_suppression_settings.hpp"
#include <algorithm>
#include <reshade_imgui.hpp>
#include <sstream>
#include <vector>
#include <windows.h>


namespace display_commander::widgets::xinput_widget {

// Helper function to get original GetTickCount64 value (unhooked)
static ULONGLONG GetOriginalTickCount64() {
    if (enabled_experimental_features && display_commanderhooks::GetTickCount64_Original) {
        return display_commanderhooks::GetTickCount64_Original();
    } else {
        return GetTickCount64();
    }
}

// Global shared state
std::shared_ptr<XInputSharedState> XInputWidget::g_shared_state = std::make_shared<XInputSharedState>();

// Global widget instance
std::unique_ptr<XInputWidget> g_xinput_widget = nullptr;

XInputWidget::XInputWidget() {
    // Initialize shared state if not already done
    if (!g_shared_state) {

        // Initialize controller states
        for (int i = 0; i < XUSER_MAX_COUNT; ++i) {
            ZeroMemory(&g_shared_state->controller_states[i], sizeof(XINPUT_STATE));
            g_shared_state->controller_connected[i] = ControllerState::Uninitialized;
            g_shared_state->last_packet_numbers[i] = 0;
            g_shared_state->last_update_times[i] = 0;

            // Initialize battery information
            ZeroMemory(&g_shared_state->battery_info[i], sizeof(XINPUT_BATTERY_INFORMATION));
            g_shared_state->last_battery_update_times[i] = 0;
            g_shared_state->battery_info_valid[i] = false;
        }
    }
}

void XInputWidget::Initialize() {
    if (is_initialized_)
        return;

    LogInfo("XInputWidget::Initialize() - Starting XInput widget initialization");

    // Load settings
    LoadSettings();

    is_initialized_ = true;
    LogInfo("XInputWidget::Initialize() - XInput widget initialization complete");
}

void XInputWidget::Cleanup() {
    if (!is_initialized_)
        return;

    // Save settings
    SaveSettings();

    is_initialized_ = false;
}

void XInputWidget::OnDraw() {
    ImGui::Indent();

    if (!is_initialized_) {
        Initialize();
    }

    if (!g_shared_state) {
        ImGui::TextColored(ui::colors::ICON_CRITICAL, "XInput shared state not initialized");
        ImGui::Unindent();
        return;
    }

    // Draw the XInput widget UI
    ImGui::TextColored(ui::colors::TEXT_DEFAULT, "=== XInput Controller Monitor ===");
    ImGui::Spacing();

    // Draw settings
    DrawSettings();
    ImGui::Spacing();

    // Draw event counters
    DrawEventCounters();
    ImGui::Spacing();

    // Draw vibration test
    DrawVibrationTest();
    ImGui::Spacing();

    // Draw autofire settings
    DrawAutofireSettings();
    ImGui::Spacing();

    // Draw controller selector
    DrawControllerSelector();
    ImGui::Spacing();

    // Draw selected controller state
    DrawControllerState();

    ImGui::Unindent();
}

void XInputWidget::DrawSettings() {
    ImGui::Indent();

    if (ImGui::CollapsingHeader("Settings", ImGuiTreeNodeFlags_DefaultOpen)) {
        // Enable XInput hooks (using HookSuppressionManager)
        bool suppress_hooks = display_commanderhooks::HookSuppressionManager::GetInstance().ShouldSuppressHook(display_commanderhooks::HookType::XINPUT);
        bool enable_hooks = !suppress_hooks;
        if (ImGui::Checkbox("Enable XInput Hooks", &enable_hooks)) {
            settings::g_hook_suppression_settings.suppress_xinput_hooks.SetValue(!enable_hooks);
            display_commanderhooks::InstallXInputHooks();
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Enable XInput API hooks for input processing and remapping");
        }

        ImGui::Spacing();


        // Swap A/B buttons
        bool swap_buttons = g_shared_state->swap_a_b_buttons.load();
        if (ImGui::Checkbox("Swap A/B Buttons", &swap_buttons)) {
            g_shared_state->swap_a_b_buttons.store(swap_buttons);
            SaveSettings();
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Swap the A and B button mappings");
        }

        // DualSense to XInput conversion
        bool dualsense_xinput = g_shared_state->enable_dualsense_xinput.load();
        if (ImGui::Checkbox("DualSense to XInput", &dualsense_xinput)) {
            g_shared_state->enable_dualsense_xinput.store(dualsense_xinput);
            SaveSettings();
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Convert DualSense controller input to XInput format");
        }

        // HID suppression enable
        bool hid_suppression = settings::g_experimentalTabSettings.hid_suppression_enabled.GetValue();
        if (ImGui::Checkbox("Enable HID Suppression", &hid_suppression)) {
            settings::g_experimentalTabSettings.hid_suppression_enabled.SetValue(hid_suppression);
            LogInfo("HID suppression %s", hid_suppression ? "enabled" : "disabled");
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Suppress HID input reading for games to prevent them from detecting controllers.\nUseful for preventing games from interfering with controller input handling.");
        }

        // HID CreateFile counters
        ImGui::Spacing();
        ImGui::TextColored(ui::colors::TEXT_DEFAULT, "HID CreateFile Detection:");
        uint64_t hid_total = g_shared_state->hid_createfile_total.load();
        uint64_t hid_dualsense = g_shared_state->hid_createfile_dualsense.load();
        ImGui::Text("HID CreateFile Total: %llu", hid_total);
        ImGui::Text("HID CreateFile DualSense: %llu", hid_dualsense);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Shows how many times the game tried to open HID devices via CreateFile.\nDualSense counter shows specifically DualSense controller access attempts.");
        }

        // Left stick deadzone setting
        float left_deadzone = g_shared_state->left_stick_deadzone.load();
        if (ImGui::SliderFloat("Left Stick Dead Zone (Min Input)", &left_deadzone, 0.0f, 50.0f, "%.0f%%")) {
            g_shared_state->left_stick_deadzone.store(left_deadzone);
            SaveSettings();
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "Ignores stick movement below this threshold (0%% = no deadzone, 15%% = ignores small movements)");
        }

        // Right stick deadzone setting
        float right_deadzone = g_shared_state->right_stick_deadzone.load();
        if (ImGui::SliderFloat("Right Stick Dead Zone (Min Input)", &right_deadzone, 0.0f, 50.0f, "%.0f%%")) {
            g_shared_state->right_stick_deadzone.store(right_deadzone);
            SaveSettings();
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "Ignores stick movement below this threshold (0%% = no deadzone, 15%% = ignores small movements)");
        }

        // Left stick sensitivity setting
        float left_max_input = g_shared_state->left_stick_max_input.load();
        float left_max_input_percent = left_max_input * 100.0f;
        if (ImGui::SliderFloat("Left Stick Sensitivity (Max Input)", &left_max_input_percent, 10.0f, 100.0f,
                               "%.0f%%")) {
            g_shared_state->left_stick_max_input.store(left_max_input_percent / 100.0f);
            SaveSettings();
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("How much stick movement is needed for full output (70%% = 70%% stick movement = 100%% "
                              "output, 100%% = normal)");
        }

        // Right stick sensitivity setting
        float right_max_input = g_shared_state->right_stick_max_input.load();
        float right_max_input_percent = right_max_input * 100.0f;
        if (ImGui::SliderFloat("Right Stick Sensitivity (Max Input)", &right_max_input_percent, 10.0f, 100.0f,
                               "%.0f%%")) {
            g_shared_state->right_stick_max_input.store(right_max_input_percent / 100.0f);
            SaveSettings();
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("How much stick movement is needed for full output (70%% = 70%% stick movement = 100%% "
                              "output, 100%% = normal)");
        }

        // Left stick remove game's deadzone setting
        float left_min_output = g_shared_state->left_stick_min_output.load();
        float left_min_output_percent = left_min_output * 100.0f;
        if (ImGui::SliderFloat("Left Stick Remove Game's Deadzone (Min Output)", &left_min_output_percent, 0.0f, 90.0f,
                               "%.0f%%")) {
            g_shared_state->left_stick_min_output.store(left_min_output_percent / 100.0f);
            SaveSettings();
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "Removes game's deadzone by setting minimum output (30%% = eliminates small movements, 0%% = normal)");
        }

        // Right stick remove game's deadzone setting
        float right_min_output = g_shared_state->right_stick_min_output.load();
        float right_min_output_percent = right_min_output * 100.0f;
        if (ImGui::SliderFloat("Right Stick Remove Game's Deadzone (Min Output)", &right_min_output_percent, 0.0f,
                               90.0f, "%.0f%%")) {
            g_shared_state->right_stick_min_output.store(right_min_output_percent / 100.0f);
            SaveSettings();
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "Removes game's deadzone by setting minimum output (30%% = eliminates small movements, 0%% = normal)");
        }

        ImGui::Separator();
        ImGui::Text("Stick Processing Mode");
        ImGui::Text("Choose how X/Y axes are processed together (circular) or separately (square):");

        // Left stick processing mode toggle
        bool left_circular = g_shared_state->left_stick_circular.load();
        const char* left_mode_text = left_circular ? "Circular (Default)" : "Square";
        if (ImGui::Checkbox("Left Stick: Circular Processing", &left_circular)) {
            g_shared_state->left_stick_circular.store(left_circular);
            SaveSettings();
        }
        ImGui::SameLine();
        ImGui::TextColored(ui::colors::TEXT_DIMMED, "(%s)", left_mode_text);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "Circular: X/Y axes processed together (radial deadzone preserves direction)\n"
                "Square: X/Y axes processed separately (independent deadzone per axis)\n"
                "Affects deadzone, anti-deadzone, and sensitivity settings");
        }

        // Right stick processing mode toggle
        bool right_circular = g_shared_state->right_stick_circular.load();
        const char* right_mode_text = right_circular ? "Circular (Default)" : "Square";
        if (ImGui::Checkbox("Right Stick: Circular Processing", &right_circular)) {
            g_shared_state->right_stick_circular.store(right_circular);
            SaveSettings();
        }
        ImGui::SameLine();
        ImGui::TextColored(ui::colors::TEXT_DIMMED, "(%s)", right_mode_text);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "Circular: X/Y axes processed together (radial deadzone preserves direction)\n"
                "Square: X/Y axes processed separately (independent deadzone per axis)\n"
                "Affects deadzone, anti-deadzone, and sensitivity settings");
        }

        ImGui::Separator();
        ImGui::Text("Stick Center Calibration");
        ImGui::Text("Adjust these values to recenter your analog sticks if they drift:");

        // Left stick center X setting
        float left_center_x = g_shared_state->left_stick_center_x.load();
        if (ImGui::SliderFloat("Left Stick Center X", &left_center_x, -1.0f, 1.0f, "%.3f")) {
            g_shared_state->left_stick_center_x.store(left_center_x);
            SaveSettings();
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("X-axis center offset for left stick (-1.0 to 1.0)");
        }

        // Left stick center Y setting
        float left_center_y = g_shared_state->left_stick_center_y.load();
        if (ImGui::SliderFloat("Left Stick Center Y", &left_center_y, -1.0f, 1.0f, "%.3f")) {
            g_shared_state->left_stick_center_y.store(left_center_y);
            SaveSettings();
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Y-axis center offset for left stick (-1.0 to 1.0)");
        }

        // Right stick center X setting
        float right_center_x = g_shared_state->right_stick_center_x.load();
        if (ImGui::SliderFloat("Right Stick Center X", &right_center_x, -1.0f, 1.0f, "%.3f")) {
            g_shared_state->right_stick_center_x.store(right_center_x);
            SaveSettings();
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("X-axis center offset for right stick (-1.0 to 1.0)");
        }

        // Right stick center Y setting
        float right_center_y = g_shared_state->right_stick_center_y.load();
        if (ImGui::SliderFloat("Right Stick Center Y", &right_center_y, -1.0f, 1.0f, "%.3f")) {
            g_shared_state->right_stick_center_y.store(right_center_y);
            SaveSettings();
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Y-axis center offset for right stick (-1.0 to 1.0)");
        }

        // Reset centers button
        if (ImGui::Button("Reset Stick Centers")) {
            g_shared_state->left_stick_center_x.store(0.0f);
            g_shared_state->left_stick_center_y.store(0.0f);
            g_shared_state->right_stick_center_x.store(0.0f);
            g_shared_state->right_stick_center_y.store(0.0f);
            SaveSettings();
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Reset all stick center offsets to 0.0");
        }

        ImGui::Separator();
        ImGui::Text("Vibration Amplification");
        ImGui::Text("Amplify controller vibration/rumble intensity:");

        // Vibration amplification setting
        float vibration_amp = g_shared_state->vibration_amplification.load();
        float vibration_amp_percent = vibration_amp * 100.0f;
        if (ImGui::SliderFloat("Vibration Amplification", &vibration_amp_percent, 0.0f, 1000.0f, "%.0f%%")) {
            g_shared_state->vibration_amplification.store(vibration_amp_percent / 100.0f);
            SaveSettings();
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "Amplify controller vibration intensity (100%% = normal, 200%% = double, 500%% = maximum).\n"
                "This affects all vibration commands from the game.");
        }

        // Reset vibration amplification button
        if (ImGui::Button("Reset Vibration Amplification")) {
            g_shared_state->vibration_amplification.store(1.0f);
            SaveSettings();
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Reset vibration amplification to 100%% (normal)");
        }
    }

    ImGui::Unindent();
}

void XInputWidget::DrawEventCounters() {
    ImGui::Indent();

    if (ImGui::CollapsingHeader("Event Counters", ImGuiTreeNodeFlags_DefaultOpen)) {
        uint64_t total_events = g_shared_state->total_events.load();
        uint64_t button_events = g_shared_state->button_events.load();
        uint64_t stick_events = g_shared_state->stick_events.load();
        uint64_t trigger_events = g_shared_state->trigger_events.load();

        ImGui::Text("Total Events: %llu", total_events);
        ImGui::Text("Button Events: %llu", button_events);
        ImGui::Text("Stick Events: %llu", stick_events);
        ImGui::Text("Trigger Events: %llu", trigger_events);

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::TextColored(ui::colors::TEXT_DEFAULT, "XInput Call Rate (Smooth)");

        // Display smooth call rate for XInputGetState
        uint64_t getstate_update_ns = g_shared_state->xinput_getstate_update_ns.load();
        if (getstate_update_ns > 0) {
            double getstate_rate_hz = 1000000000.0 / getstate_update_ns; // Convert ns to Hz
            ImGui::Text("XInputGetState Rate: %.1f Hz (%.2f ms)", getstate_rate_hz, getstate_update_ns / 1000000.0);
        } else {
            ImGui::TextColored(ui::colors::TEXT_DIMMED, "XInputGetState Rate: No data");
        }

        // Display smooth call rate for XInputGetStateEx
        uint64_t getstateex_update_ns = g_shared_state->xinput_getstateex_update_ns.load();
        if (getstateex_update_ns > 0) {
            double getstateex_rate_hz = 1000000000.0 / getstateex_update_ns; // Convert ns to Hz
            ImGui::Text("XInputGetStateEx Rate: %.1f Hz (%.2f ms)", getstateex_rate_hz, getstateex_update_ns / 1000000.0);
        } else {
            ImGui::TextColored(ui::colors::TEXT_DIMMED, "XInputGetStateEx Rate: No data");
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::TextColored(ui::colors::TEXT_DEFAULT, "XInputGetCapabilities Hook Statistics");

        // Display hook statistics for XInputGetCapabilities
        const auto &capabilities_stats = display_commanderhooks::GetHookStats(display_commanderhooks::HOOK_XInputGetCapabilities);
        uint64_t capabilities_total_calls = capabilities_stats.total_calls.load();
        uint64_t capabilities_unsuppressed_calls = capabilities_stats.unsuppressed_calls.load();
        uint64_t capabilities_suppressed_calls = capabilities_total_calls - capabilities_unsuppressed_calls;

        ImGui::Text("XInputGetCapabilities_Detour Calls: %llu", capabilities_total_calls);
        ImGui::Text("XInputGetCapabilities_Original Calls: %llu", capabilities_unsuppressed_calls);
        if (capabilities_suppressed_calls > 0) {
            ImGui::TextColored(ui::colors::TEXT_DIMMED, "Suppressed Calls: %llu", capabilities_suppressed_calls);
        }

        // Reset button
        if (ImGui::Button("Reset Counters")) {
            g_shared_state->total_events.store(0);
            g_shared_state->button_events.store(0);
            g_shared_state->stick_events.store(0);
            g_shared_state->trigger_events.store(0);
            g_shared_state->xinput_getstate_update_ns.store(0);
            g_shared_state->xinput_getstateex_update_ns.store(0);
            g_shared_state->last_xinput_call_time_ns.store(0);
            g_shared_state->hid_createfile_total.store(0);
            g_shared_state->hid_createfile_dualsense.store(0);
        }
    }

    ImGui::Unindent();
}

void XInputWidget::DrawVibrationTest() {
    ImGui::Indent();

    if (ImGui::CollapsingHeader("Vibration Test", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Text("Test controller vibration motors:");
        ImGui::Spacing();

        // Show current controller selection
        ImGui::Text("Testing Controller: %d", selected_controller_);
        ImGui::Spacing();

        // Left motor test
        if (ImGui::Button("Test Left Motor", ImVec2(120, 30))) {
            TestLeftMotor();
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Test the left (low-frequency) vibration motor");
        }

        ImGui::SameLine();

        // Right motor test
        if (ImGui::Button("Test Right Motor", ImVec2(120, 30))) {
            TestRightMotor();
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Test the right (high-frequency) vibration motor");
        }

        ImGui::Spacing();

        // Stop vibration
        if (ImGui::Button("Stop Vibration", ImVec2(120, 30))) {
            StopVibration();
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Stop all vibration on the selected controller");
        }

        ImGui::SameLine();

        // Test both motors
        if (ImGui::Button("Test Both Motors", ImVec2(120, 30))) {
            TestLeftMotor();
            TestRightMotor();
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Test both vibration motors simultaneously");
        }

        ImGui::Spacing();
        ImGui::TextColored(ui::colors::TEXT_DIMMED,
                           "Note: Vibration will continue until stopped or controller disconnects");
    }

    ImGui::Unindent();
}

void XInputWidget::DrawControllerSelector() {
    ImGui::Indent();

    ImGui::Text("Controller:");
    ImGui::SameLine();

    // Create controller list
    std::vector<std::string> controller_names;
    for (int i = 0; i < XUSER_MAX_COUNT; ++i) {
        std::string status = GetControllerStatus(i);
        controller_names.push_back("Controller " + std::to_string(i) + " - " + status);
    }

    ImGui::PushID("controller_selector");
    if (ImGui::BeginCombo("##controller", controller_names[selected_controller_].c_str())) {
        for (int i = 0; i < XUSER_MAX_COUNT; ++i) {
            const bool is_selected = (i == selected_controller_);
            if (ImGui::Selectable(controller_names[i].c_str(), is_selected)) {
                selected_controller_ = i;
            }
            if (is_selected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }
    ImGui::PopID();

    ImGui::Unindent();
}

void XInputWidget::DrawControllerState() {
    ImGui::Indent();

    if (selected_controller_ < 0 || selected_controller_ >= XUSER_MAX_COUNT) {
        ImGui::TextColored(ui::colors::ICON_CRITICAL, "Invalid controller selected");
        ImGui::Unindent();
        return;
    }

    // Get controller state (thread-safe read)
    utils::SRWLockShared lock(g_shared_state->state_lock);
    const XINPUT_STATE &state = g_shared_state->controller_states[selected_controller_];
    ControllerState controller_state = g_shared_state->controller_connected[selected_controller_];

    if (controller_state == ControllerState::Uninitialized) {
        ImGui::TextColored(ui::colors::TEXT_DIMMED, "Controller %d - Uninitialized", selected_controller_);
        return;
    } else if (controller_state == ControllerState::Unconnected) {
        ImGui::TextColored(ui::colors::TEXT_DIMMED, "Controller %d - Disconnected", selected_controller_);
        return;
    }

    // Draw controller info
    ImGui::TextColored(ui::colors::STATUS_ACTIVE, "Controller %d - Connected", selected_controller_);
    ImGui::Text("Packet Number: %lu", state.dwPacketNumber);

    // Debug: Show raw button state
    ImGui::Text("Raw Button State: 0x%04X", state.Gamepad.wButtons);
    ImGui::Text("Home Button Constant: 0x%04X", XINPUT_GAMEPAD_GUIDE);

    // Get last update time
    uint64_t last_update = g_shared_state->last_update_times[selected_controller_].load();
    if (last_update > 0) {
        // Convert to milliseconds for display
        uint64_t now = GetOriginalTickCount64();
        uint64_t age_ms = now - last_update;
        ImGui::Text("Last Update: %llu ms ago", age_ms);
    }

    ImGui::Spacing();

    // Draw button states
    DrawButtonStates(state.Gamepad);
    ImGui::Spacing();

    // Draw stick states
    DrawStickStates(state.Gamepad);
    ImGui::Spacing();

    // Draw trigger states
    DrawTriggerStates(state.Gamepad);
    ImGui::Spacing();

    // Draw battery status
    DrawBatteryStatus(selected_controller_);

    // Draw DualSense report if dualsense_to_xinput is enabled
    if (g_shared_state->enable_dualsense_xinput.load()) {
        ImGui::Spacing();
        DrawDualSenseReport(selected_controller_);
    }

    ImGui::Unindent();
}

void XInputWidget::DrawButtonStates(const XINPUT_GAMEPAD &gamepad) {
    if (ImGui::CollapsingHeader("Buttons", ImGuiTreeNodeFlags_DefaultOpen)) {
        // Create a grid of buttons
        const struct {
            WORD mask;
            const char *name;
        } buttons[] = {
            {XINPUT_GAMEPAD_A, "A"},
            {XINPUT_GAMEPAD_B, "B"},
            {XINPUT_GAMEPAD_X, "X"},
            {XINPUT_GAMEPAD_Y, "Y"},
            {XINPUT_GAMEPAD_LEFT_SHOULDER, "LB"},
            {XINPUT_GAMEPAD_RIGHT_SHOULDER, "RB"},
            {XINPUT_GAMEPAD_BACK, "View"},
            {XINPUT_GAMEPAD_START, "Menu"},
            {XINPUT_GAMEPAD_GUIDE, "Home"},
            {XINPUT_GAMEPAD_LEFT_THUMB, "LS"},
            {XINPUT_GAMEPAD_RIGHT_THUMB, "RS"},
            {XINPUT_GAMEPAD_DPAD_UP, "D-Up"},
            {XINPUT_GAMEPAD_DPAD_DOWN, "D-Down"},
            {XINPUT_GAMEPAD_DPAD_LEFT, "D-Left"},
            {XINPUT_GAMEPAD_DPAD_RIGHT, "D-Right"},
        };

        for (size_t i = 0; i < sizeof(buttons) / sizeof(buttons[0]); i += 2) {
            if (i + 1 < sizeof(buttons) / sizeof(buttons[0])) {
                // Draw two buttons per row
                bool pressed1 = IsButtonPressed(gamepad.wButtons, buttons[i].mask);
                bool pressed2 = IsButtonPressed(gamepad.wButtons, buttons[i + 1].mask);

                // Special styling for Home button
                if (buttons[i].mask == XINPUT_GAMEPAD_GUIDE) {
                    ImGui::PushStyleColor(ImGuiCol_Button,
                                          pressed1 ? ui::colors::ICON_WARNING : ui::colors::ICON_DARK_ORANGE);
                } else {
                    ImGui::PushStyleColor(ImGuiCol_Button,
                                          pressed1 ? ui::colors::STATUS_ACTIVE : ui::colors::ICON_DARK_GRAY);
                }
                ImGui::Button(buttons[i].name, ImVec2(60, 30));
                ImGui::PopStyleColor();

                ImGui::SameLine();

                // Special styling for Home button
                if (buttons[i + 1].mask == XINPUT_GAMEPAD_GUIDE) {
                    ImGui::PushStyleColor(ImGuiCol_Button,
                                          pressed2 ? ui::colors::ICON_WARNING : ui::colors::ICON_DARK_ORANGE);
                } else {
                    ImGui::PushStyleColor(ImGuiCol_Button,
                                          pressed2 ? ui::colors::STATUS_ACTIVE : ui::colors::ICON_DARK_GRAY);
                }
                ImGui::Button(buttons[i + 1].name, ImVec2(60, 30));
                ImGui::PopStyleColor();
            } else {
                // Single button on last row
                bool pressed = IsButtonPressed(gamepad.wButtons, buttons[i].mask);

                // Special styling for Home button
                if (buttons[i].mask == XINPUT_GAMEPAD_GUIDE) {
                    ImGui::PushStyleColor(ImGuiCol_Button,
                                          pressed ? ui::colors::ICON_WARNING : ui::colors::ICON_DARK_ORANGE);
                } else {
                    ImGui::PushStyleColor(ImGuiCol_Button,
                                          pressed ? ui::colors::STATUS_ACTIVE : ui::colors::ICON_DARK_GRAY);
                }
                ImGui::Button(buttons[i].name, ImVec2(60, 30));
                ImGui::PopStyleColor();
            }
        }
    }
}

void XInputWidget::DrawStickStates(const XINPUT_GAMEPAD &gamepad) {
    if (ImGui::CollapsingHeader("Analog Sticks", ImGuiTreeNodeFlags_DefaultOpen)) {
        float left_max_input = g_shared_state->left_stick_max_input.load();
        float right_max_input = g_shared_state->right_stick_max_input.load();
        float left_min_output = g_shared_state->left_stick_min_output.load();
        float right_min_output = g_shared_state->right_stick_min_output.load();
        float left_deadzone = g_shared_state->left_stick_deadzone.load() / 100.0f;   // Convert percentage to decimal
        float right_deadzone = g_shared_state->right_stick_deadzone.load() / 100.0f; // Convert percentage to decimal

        // Left stick
        ImGui::Text("Left Stick:");
        float lx = ShortToFloat(gamepad.sThumbLX);
        float ly = ShortToFloat(gamepad.sThumbLY);

        // Apply center calibration (recenter the stick)
        float left_center_x = g_shared_state->left_stick_center_x.load();
        float left_center_y = g_shared_state->left_stick_center_y.load();
        float lx_recentered = lx - left_center_x;
        float ly_recentered = ly - left_center_y;

        // Apply final processing (use appropriate mode based on toggle)
        float lx_final = lx_recentered;
        float ly_final = ly_recentered;
        bool left_circular = g_shared_state->left_stick_circular.load();
        if (left_circular) {
            ProcessStickInputRadial(lx_final, ly_final, left_deadzone, left_max_input, left_min_output);
        } else {
            ProcessStickInputSquare(lx_final, ly_final, left_deadzone, left_max_input, left_min_output);
        }

        ImGui::Text("X: %.3f (Raw) -> %.3f (Recentered) -> %.3f (Final) [Raw: %d]", lx, lx_recentered, lx_final, gamepad.sThumbLX);
        ImGui::Text("Y: %.3f (Raw) -> %.3f (Recentered) -> %.3f (Final) [Raw: %d]", ly, ly_recentered, ly_final, gamepad.sThumbLY);

        // Visual representation
        ImGui::Text("Position:");
        ImVec2 canvas_pos = ImGui::GetCursorScreenPos();
        ImVec2 canvas_size = ImVec2(100, 100);
        ImDrawList *draw_list = ImGui::GetWindowDrawList();

        // Draw circle
        ImVec2 center = ImVec2(canvas_pos.x + canvas_size.x * 0.5f, canvas_pos.y + canvas_size.y * 0.5f);
        draw_list->AddCircle(center, canvas_size.x * 0.4f, ImColor(100, 100, 100, 255), 32, 2.0f);

        // Draw crosshairs
        draw_list->AddLine(ImVec2(canvas_pos.x, center.y), ImVec2(canvas_pos.x + canvas_size.x, center.y),
                           ImColor(100, 100, 100, 255), 1.0f);
        draw_list->AddLine(ImVec2(center.x, canvas_pos.y), ImVec2(center.x, canvas_pos.y + canvas_size.y),
                           ImColor(100, 100, 100, 255), 1.0f);

        // Draw stick position (using final processed values for visual representation)
        ImVec2 stick_pos =
            ImVec2(center.x + lx_final * canvas_size.x * 0.4f, center.y - ly_final * canvas_size.y * 0.4f);
        draw_list->AddCircleFilled(stick_pos, 5.0f, ImColor(0, 255, 0, 255));

        ImGui::Dummy(canvas_size);

        // Right stick
        ImGui::Text("Right Stick:");
        float rx = ShortToFloat(gamepad.sThumbRX);
        float ry = ShortToFloat(gamepad.sThumbRY);

        // Apply center calibration (recenter the stick)
        float right_center_x = g_shared_state->right_stick_center_x.load();
        float right_center_y = g_shared_state->right_stick_center_y.load();
        float rx_recentered = rx - right_center_x;
        float ry_recentered = ry - right_center_y;

        // Apply final processing (use appropriate mode based on toggle)
        float rx_final = rx_recentered;
        float ry_final = ry_recentered;
        bool right_circular = g_shared_state->right_stick_circular.load();
        if (right_circular) {
            ProcessStickInputRadial(rx_final, ry_final, right_deadzone, right_max_input, right_min_output);
        } else {
            ProcessStickInputSquare(rx_final, ry_final, right_deadzone, right_max_input, right_min_output);
        }

        ImGui::Text("X: %.3f (Raw) -> %.3f (Recentered) -> %.3f (Final) [Raw: %d]", rx, rx_recentered, rx_final, gamepad.sThumbRX);
        ImGui::Text("Y: %.3f (Raw) -> %.3f (Recentered) -> %.3f (Final) [Raw: %d]", ry, ry_recentered, ry_final, gamepad.sThumbRY);

        // Visual representation for right stick
        ImGui::Text("Position:");
        canvas_pos = ImGui::GetCursorScreenPos();
        draw_list = ImGui::GetWindowDrawList();

        // Draw circle
        center = ImVec2(canvas_pos.x + canvas_size.x * 0.5f, canvas_pos.y + canvas_size.y * 0.5f);
        draw_list->AddCircle(center, canvas_size.x * 0.4f, ImColor(100, 100, 100, 255), 32, 2.0f);

        // Draw crosshairs
        draw_list->AddLine(ImVec2(canvas_pos.x, center.y), ImVec2(canvas_pos.x + canvas_size.x, center.y),
                           ImColor(100, 100, 100, 255), 1.0f);
        draw_list->AddLine(ImVec2(center.x, canvas_pos.y), ImVec2(center.x, canvas_pos.y + canvas_size.y),
                           ImColor(100, 100, 100, 255), 1.0f);

        // Draw stick position (using final processed values for visual representation)
        stick_pos = ImVec2(center.x + rx_final * canvas_size.x * 0.4f, center.y - ry_final * canvas_size.y * 0.4f);
        draw_list->AddCircleFilled(stick_pos, 5.0f, ImColor(0, 255, 0, 255));

        ImGui::Dummy(canvas_size);

        // Draw extended visualization with input/output curves
        DrawStickStatesExtended(left_deadzone, left_max_input, left_min_output, right_deadzone, right_max_input,
                                right_min_output);
    }
}

void XInputWidget::DrawStickStatesExtended(float left_deadzone, float left_max_input, float left_min_output,
                                           float right_deadzone, float right_max_input, float right_min_output) {
    if (ImGui::CollapsingHeader("Input/Output Curves", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::TextColored(ui::colors::TEXT_DEFAULT, "Visual representation of how stick input is processed");
        ImGui::Spacing();

        // Generate curve data for both sticks
        const int curve_points = 400;
        std::vector<float> left_curve_x(curve_points);
        std::vector<float> left_curve_y(curve_points);
        std::vector<float> right_curve_x(curve_points);
        std::vector<float> right_curve_y(curve_points);
        std::vector<float> input_values(curve_points);

        // Generate input values from 0.0 to 1.0 (positive side only for clarity)
        for (int i = 0; i < curve_points; ++i) {
            input_values[i] = static_cast<float>(i) / (curve_points - 1);

            // For radial deadzone visualization, simulate moving stick from center to edge
            // This shows the magnitude transformation (radial deadzone preserves direction)
            float x = input_values[i];
            float y = 0.0f; // Move along X axis for simplicity

            // Left stick - apply processing based on mode
            float lx_test = x;
            float ly_test = y;
            bool left_circular = g_shared_state->left_stick_circular.load();
            if (left_circular) {
                ProcessStickInputRadial(lx_test, ly_test, left_deadzone, left_max_input, left_min_output);
            } else {
                ProcessStickInputSquare(lx_test, ly_test, left_deadzone, left_max_input, left_min_output);
            }
            left_curve_y[i] = std::sqrt(lx_test * lx_test + ly_test * ly_test); // Show output magnitude

            // Right stick - apply processing based on mode
            float rx_test = x;
            float ry_test = y;
            bool right_circular = g_shared_state->right_stick_circular.load();
            if (right_circular) {
                ProcessStickInputRadial(rx_test, ry_test, right_deadzone, right_max_input, right_min_output);
            } else {
                ProcessStickInputSquare(rx_test, ry_test, right_deadzone, right_max_input, right_min_output);
            }
            right_curve_y[i] = std::sqrt(rx_test * rx_test + ry_test * ry_test); // Show output magnitude

            left_curve_x[i] = static_cast<float>(i);
            right_curve_x[i] = static_cast<float>(i);
        }

        // Left stick curve
        ImGui::TextColored(ui::colors::STATUS_ACTIVE, "Left Stick Input/Output Curve");
        ImGui::Text("Deadzone: %.1f%%, Max Input: %.1f%%, Min Output: %.1f%%", left_deadzone * 100.0f,
                    left_max_input * 100.0f, left_min_output * 100.0f);

        // Create plot for left stick (0.0 to 1.0 input range)
        ImGui::PlotLines("##LeftStickCurve", left_curve_y.data(), curve_points, 0, "Left Stick Output", 0.0f, 1.0f,
                         ImVec2(-1, 150));

        // Add reference lines
        ImDrawList *draw_list = ImGui::GetWindowDrawList();
        ImVec2 plot_pos = ImGui::GetItemRectMin();
        ImVec2 plot_size = ImGui::GetItemRectSize();

        // Draw deadzone reference line (vertical)
        float deadzone_x = plot_pos.x + left_deadzone * plot_size.x;
        draw_list->AddLine(ImVec2(deadzone_x, plot_pos.y), ImVec2(deadzone_x, plot_pos.y + plot_size.y),
                           ImColor(255, 255, 0, 128), 2.0f);

        // Draw max input reference line (vertical)
        float max_input_x = plot_pos.x + left_max_input * plot_size.x;
        draw_list->AddLine(ImVec2(max_input_x, plot_pos.y), ImVec2(max_input_x, plot_pos.y + plot_size.y),
                           ImColor(255, 0, 255, 128), 2.0f);

        // Draw min output reference line (horizontal)
        float min_output_y = plot_pos.y + plot_size.y - left_min_output * plot_size.y;
        draw_list->AddLine(ImVec2(plot_pos.x, min_output_y), ImVec2(plot_pos.x + plot_size.x, min_output_y),
                           ImColor(0, 255, 255, 128), 2.0f);

        ImGui::Spacing();

        // Right stick curve
        ImGui::TextColored(ui::colors::STATUS_ACTIVE, "Right Stick Input/Output Curve");
        ImGui::Text("Deadzone: %.1f%%, Max Input: %.1f%%, Min Output: %.1f%%", right_deadzone * 100.0f,
                    right_max_input * 100.0f, right_min_output * 100.0f);

        // Create plot for right stick (0.0 to 1.0 input range)
        ImGui::PlotLines("##RightStickCurve", right_curve_y.data(), curve_points, 0, "Right Stick Output", 0.0f, 1.0f,
                         ImVec2(-1, 150));

        // Add reference lines for right stick
        plot_pos = ImGui::GetItemRectMin();
        plot_size = ImGui::GetItemRectSize();

        // Draw deadzone reference line (vertical)
        float right_deadzone_x = plot_pos.x + right_deadzone * plot_size.x;
        draw_list->AddLine(ImVec2(right_deadzone_x, plot_pos.y), ImVec2(right_deadzone_x, plot_pos.y + plot_size.y),
                           ImColor(255, 255, 0, 128), 2.0f);

        // Draw max input reference line (vertical)
        float right_max_input_x = plot_pos.x + right_max_input * plot_size.x;
        draw_list->AddLine(ImVec2(right_max_input_x, plot_pos.y), ImVec2(right_max_input_x, plot_pos.y + plot_size.y),
                           ImColor(255, 0, 255, 128), 2.0f);

        // Draw min output reference line (horizontal)
        float right_min_output_y = plot_pos.y + plot_size.y - right_min_output * plot_size.y;
        draw_list->AddLine(ImVec2(plot_pos.x, right_min_output_y), ImVec2(plot_pos.x + plot_size.x, right_min_output_y),
                           ImColor(0, 255, 255, 128), 2.0f);

        ImGui::Spacing();

        // Legend
        ImGui::TextColored(ui::colors::TEXT_VALUE, "Legend:");
        ImGui::SameLine();
        ImGui::TextColored(ui::colors::TEXT_VALUE, "Yellow = Radial Deadzone (Vertical)");
        ImGui::SameLine();
        ImGui::TextColored(ui::colors::ICON_SPECIAL, "Magenta = Max Input (Vertical)");
        ImGui::SameLine();
        ImGui::TextColored(ui::colors::ICON_ANALYSIS, "Cyan = Min Output (Horizontal)");
        ImGui::Spacing();
        ImGui::TextColored(ui::colors::TEXT_DIMMED, "Note: Radial deadzone preserves stick direction (circular deadzone)");
        ImGui::Spacing();
        ImGui::TextColored(ui::colors::TEXT_DIMMED, "X-axis: Input (0.0 to 1.0) - Positive side only");
        ImGui::TextColored(ui::colors::TEXT_DIMMED, "Y-axis: Output (-1.0 to 1.0)");
    }
}

void XInputWidget::DrawTriggerStates(const XINPUT_GAMEPAD &gamepad) {
    if (ImGui::CollapsingHeader("Triggers", ImGuiTreeNodeFlags_DefaultOpen)) {
        // Left trigger
        ImGui::Text("Left Trigger: %u/255 (%.1f%%)", gamepad.bLeftTrigger,
                    (static_cast<float>(gamepad.bLeftTrigger) / 255.0f) * 100.0f);

        // Visual bar for left trigger
        float left_trigger_norm = static_cast<float>(gamepad.bLeftTrigger) / 255.0f;
        ImGui::ProgressBar(left_trigger_norm, ImVec2(-1, 0), "");

        // Right trigger
        ImGui::Text("Right Trigger: %u/255 (%.1f%%)", gamepad.bRightTrigger,
                    (static_cast<float>(gamepad.bRightTrigger) / 255.0f) * 100.0f);

        // Visual bar for right trigger
        float right_trigger_norm = static_cast<float>(gamepad.bRightTrigger) / 255.0f;
        ImGui::ProgressBar(right_trigger_norm, ImVec2(-1, 0), "");
    }
}

void XInputWidget::DrawBatteryStatus(int controller_index) {
    if (controller_index >= XUSER_MAX_COUNT || !g_shared_state) {
        return;
    }

    if (ImGui::CollapsingHeader("Battery Status", ImGuiTreeNodeFlags_DefaultOpen)) {
        bool battery_valid = g_shared_state->battery_info_valid[controller_index].load();

        if (!battery_valid) {
            ImGui::TextColored(ui::colors::TEXT_DIMMED, "Battery information not available");
            return;
        }

        const XINPUT_BATTERY_INFORMATION &battery = g_shared_state->battery_info[controller_index];

        // Battery type
        std::string battery_type_str;
        ImVec4 type_color(1.0f, 1.0f, 1.0f, 1.0f);

        switch (battery.BatteryType) {
        case BATTERY_TYPE_DISCONNECTED:
            battery_type_str = "Disconnected";
            type_color = ui::colors::TEXT_DIMMED;
            break;
        case BATTERY_TYPE_WIRED:
            battery_type_str = "Wired (No Battery)";
            type_color = ui::colors::TEXT_INFO;
            break;
        case BATTERY_TYPE_ALKALINE:
            battery_type_str = "Alkaline Battery";
            type_color = ui::colors::TEXT_VALUE;
            break;
        case BATTERY_TYPE_NIMH:
            battery_type_str = "NiMH Battery";
            type_color = ui::colors::STATUS_ACTIVE;
            break;
        case BATTERY_TYPE_UNKNOWN:
            battery_type_str = "Unknown Battery Type";
            type_color = ui::colors::TEXT_DIMMED;
            break;
        default:
            battery_type_str = "Unknown";
            type_color = ui::colors::TEXT_DIMMED;
            break;
        }

        ImGui::TextColored(type_color, "Type: %s", battery_type_str.c_str());

        // Battery level (only show for devices with actual batteries)
        if (battery.BatteryType != BATTERY_TYPE_DISCONNECTED && battery.BatteryType != BATTERY_TYPE_UNKNOWN &&
            battery.BatteryType != BATTERY_TYPE_WIRED) {

            std::string level_str;
            ImVec4 level_color(1.0f, 1.0f, 1.0f, 1.0f);
            float level_progress = 0.0f;

            switch (battery.BatteryLevel) {
            case BATTERY_LEVEL_EMPTY:
                level_str = "Empty";
                level_color = ui::colors::ICON_CRITICAL;
                level_progress = 0.0f;
                break;
            case BATTERY_LEVEL_LOW:
                level_str = "Low";
                level_color = ui::colors::ICON_ORANGE;
                level_progress = 0.25f;
                break;
            case BATTERY_LEVEL_MEDIUM:
                level_str = "Medium";
                level_color = ui::colors::TEXT_VALUE;
                level_progress = 0.5f;
                break;
            case BATTERY_LEVEL_FULL:
                level_str = "Full";
                level_color = ui::colors::STATUS_ACTIVE;
                level_progress = 1.0f;
                break;
            default:
                level_str = "Unknown";
                level_color = ui::colors::TEXT_DIMMED;
                level_progress = 0.0f;
                break;
            }

            ImGui::TextColored(level_color, "Level: %s", level_str.c_str());

            // Visual battery level bar
            ImGui::PushStyleColor(ImGuiCol_PlotHistogram, level_color);
            ImGui::ProgressBar(level_progress, ImVec2(-1, 0), "");
            ImGui::PopStyleColor();
        } else if (battery.BatteryType == BATTERY_TYPE_WIRED) {
            // For wired devices, show a simple message that no battery level is available
            ImGui::TextColored(ui::colors::TEXT_INFO, "No battery level (Wired device)");
        } else {
            ImGui::TextColored(ui::colors::TEXT_DIMMED, "Battery level not available");
        }
    }
}

std::string XInputWidget::GetButtonName(WORD button) const {
    switch (button) {
    case XINPUT_GAMEPAD_A:
        return "A";
    case XINPUT_GAMEPAD_B:
        return "B";
    case XINPUT_GAMEPAD_X:
        return "X";
    case XINPUT_GAMEPAD_Y:
        return "Y";
    case XINPUT_GAMEPAD_LEFT_SHOULDER:
        return "LB";
    case XINPUT_GAMEPAD_RIGHT_SHOULDER:
        return "RB";
    case XINPUT_GAMEPAD_BACK:
        return "View";
    case XINPUT_GAMEPAD_START:
        return "Menu";
    case XINPUT_GAMEPAD_GUIDE:
        return "Home";
    case XINPUT_GAMEPAD_LEFT_THUMB:
        return "LS";
    case XINPUT_GAMEPAD_RIGHT_THUMB:
        return "RS";
    case XINPUT_GAMEPAD_DPAD_UP:
        return "D-Up";
    case XINPUT_GAMEPAD_DPAD_DOWN:
        return "D-Down";
    case XINPUT_GAMEPAD_DPAD_LEFT:
        return "D-Left";
    case XINPUT_GAMEPAD_DPAD_RIGHT:
        return "D-Right";
    default:
        return "Unknown";
    }
}

std::string XInputWidget::GetControllerStatus(int controller_index) const {
    if (controller_index < 0 || controller_index >= XUSER_MAX_COUNT) {
        return "Invalid";
    }

    // Thread-safe read
    utils::SRWLockShared lock(g_shared_state->state_lock);
    ControllerState state = g_shared_state->controller_connected[controller_index];
    switch (state) {
        case ControllerState::Uninitialized:
            return "Uninitialized";
        case ControllerState::Connected:
            return "Connected";
        case ControllerState::Unconnected:
            return "Disconnected";
        default:
            return "Unknown";
    }
}

bool XInputWidget::IsButtonPressed(WORD buttons, WORD button) const { return (buttons & button) != 0; }

void XInputWidget::LoadSettings() {
    // Load swap A/B buttons setting
    bool swap_buttons;
    if (display_commander::config::get_config_value("DisplayCommander.XInputWidget", "SwapABButtons", swap_buttons)) {
        g_shared_state->swap_a_b_buttons.store(swap_buttons);
    }

    // Load DualSense to XInput conversion setting
    bool dualsense_xinput;
    if (display_commander::config::get_config_value("DisplayCommander.XInputWidget", "EnableDualSenseXInput", dualsense_xinput)) {
        g_shared_state->enable_dualsense_xinput.store(dualsense_xinput);
    }

    // Load left stick sensitivity setting
    float left_max_input;
    if (display_commander::config::get_config_value("DisplayCommander.XInputWidget", "LeftStickSensitivity", left_max_input)) {
        g_shared_state->left_stick_max_input.store(left_max_input);
    }

    // Load right stick sensitivity setting
    float right_max_input;
    if (display_commander::config::get_config_value("DisplayCommander.XInputWidget", "RightStickSensitivity", right_max_input)) {
        g_shared_state->right_stick_max_input.store(right_max_input);
    }

    // Load left stick min input setting
    float left_deadzone;
    if (display_commander::config::get_config_value("DisplayCommander.XInputWidget", "LeftStickMinInput", left_deadzone)) {
        g_shared_state->left_stick_deadzone.store(left_deadzone);
    }

    // Load right stick min input setting
    float right_deadzone;
    if (display_commander::config::get_config_value("DisplayCommander.XInputWidget", "RightStickMinInput", right_deadzone)) {
        g_shared_state->right_stick_deadzone.store(right_deadzone);
    }

    // Load left stick max output setting
    float left_min_output;
    if (display_commander::config::get_config_value("DisplayCommander.XInputWidget", "LeftStickMaxOutput", left_min_output)) {
        g_shared_state->left_stick_min_output.store(left_min_output);
    }

    // Load right stick max output setting
    float right_min_output;
    if (display_commander::config::get_config_value("DisplayCommander.XInputWidget", "RightStickMaxOutput", right_min_output)) {
        g_shared_state->right_stick_min_output.store(right_min_output);
    }

    // Load stick center calibration settings
    float left_center_x;
    if (display_commander::config::get_config_value("DisplayCommander.XInputWidget", "LeftStickCenterX", left_center_x)) {
        g_shared_state->left_stick_center_x.store(left_center_x);
    }

    float left_center_y;
    if (display_commander::config::get_config_value("DisplayCommander.XInputWidget", "LeftStickCenterY", left_center_y)) {
        g_shared_state->left_stick_center_y.store(left_center_y);
    }

    float right_center_x;
    if (display_commander::config::get_config_value("DisplayCommander.XInputWidget", "RightStickCenterX", right_center_x)) {
        g_shared_state->right_stick_center_x.store(right_center_x);
    }

    float right_center_y;
    if (display_commander::config::get_config_value("DisplayCommander.XInputWidget", "RightStickCenterY", right_center_y)) {
        g_shared_state->right_stick_center_y.store(right_center_y);
    }

    // Load vibration amplification setting
    float vibration_amp;
    if (display_commander::config::get_config_value("DisplayCommander.XInputWidget", "VibrationAmplification", vibration_amp)) {
        g_shared_state->vibration_amplification.store(vibration_amp);
    }

    // Load stick processing mode settings
    bool left_circular;
    if (display_commander::config::get_config_value("DisplayCommander.XInputWidget", "LeftStickCircular", left_circular)) {
        g_shared_state->left_stick_circular.store(left_circular);
    }

    bool right_circular;
    if (display_commander::config::get_config_value("DisplayCommander.XInputWidget", "RightStickCircular", right_circular)) {
        g_shared_state->right_stick_circular.store(right_circular);
    }

    // Load autofire settings
    bool autofire_enabled;
    if (display_commander::config::get_config_value("DisplayCommander.XInputWidget", "AutofireEnabled", autofire_enabled)) {
        g_shared_state->autofire_enabled.store(autofire_enabled);
    }

    // Load autofire frame settings (backward compatibility: try old name first, then new names)
    uint32_t autofire_frame_interval;
    if (display_commander::config::get_config_value("DisplayCommander.XInputWidget", "AutofireFrameInterval", autofire_frame_interval)) {
        // Migrate old setting to new format (use as hold_down, set hold_up to same value)
        g_shared_state->autofire_hold_down_frames.store(autofire_frame_interval);
        g_shared_state->autofire_hold_up_frames.store(autofire_frame_interval);
    } else {
        // Load new settings
        uint32_t hold_down_frames;
        if (display_commander::config::get_config_value("DisplayCommander.XInputWidget", "AutofireHoldDownFrames", hold_down_frames)) {
            g_shared_state->autofire_hold_down_frames.store(hold_down_frames);
        }
        uint32_t hold_up_frames;
        if (display_commander::config::get_config_value("DisplayCommander.XInputWidget", "AutofireHoldUpFrames", hold_up_frames)) {
            g_shared_state->autofire_hold_up_frames.store(hold_up_frames);
        }
    }

    // Load autofire button list
    std::string autofire_buttons_str;
    if (display_commander::config::get_config_value("DisplayCommander.XInputWidget", "AutofireButtons", autofire_buttons_str)) {
        // Parse comma-separated list of button masks
        std::istringstream iss(autofire_buttons_str);
        std::string token;
        while (std::getline(iss, token, ',')) {
            try {
                WORD button_mask = static_cast<WORD>(std::stoul(token, nullptr, 16)); // Parse as hex
                AddAutofireButton(button_mask);
            } catch (...) {
                // Ignore invalid entries
            }
        }
    }

    // Load autofire trigger list
    std::string autofire_triggers_str;
    if (display_commander::config::get_config_value("DisplayCommander.XInputWidget", "AutofireTriggers", autofire_triggers_str)) {
        // Parse comma-separated list of trigger types (LT, RT)
        std::istringstream iss(autofire_triggers_str);
        std::string token;
        while (std::getline(iss, token, ',')) {
            // Trim whitespace
            size_t start = token.find_first_not_of(" \t");
            if (start != std::string::npos) {
                size_t end = token.find_last_not_of(" \t");
                token = token.substr(start, end - start + 1);

                if (token == "LT" || token == "lt" || token == "LeftTrigger") {
                    AddAutofireTrigger(XInputSharedState::TriggerType::LeftTrigger);
                } else if (token == "RT" || token == "rt" || token == "RightTrigger") {
                    AddAutofireTrigger(XInputSharedState::TriggerType::RightTrigger);
                }
            }
        }
    }
}

void XInputWidget::SaveSettings() {
    // Save swap A/B buttons setting
    display_commander::config::set_config_value("DisplayCommander.XInputWidget", "SwapABButtons",
                              g_shared_state->swap_a_b_buttons.load());

    // Save DualSense to XInput conversion setting
    display_commander::config::set_config_value("DisplayCommander.XInputWidget", "EnableDualSenseXInput",
                              g_shared_state->enable_dualsense_xinput.load());

    // Save left stick sensitivity setting
    display_commander::config::set_config_value("DisplayCommander.XInputWidget", "LeftStickSensitivity",
                              g_shared_state->left_stick_max_input.load());

    // Save right stick sensitivity setting
    display_commander::config::set_config_value("DisplayCommander.XInputWidget", "RightStickSensitivity",
                              g_shared_state->right_stick_max_input.load());

    // Save left stick min input setting
    display_commander::config::set_config_value("DisplayCommander.XInputWidget", "LeftStickMinInput",
                              g_shared_state->left_stick_deadzone.load());

    // Save right stick min input setting
    display_commander::config::set_config_value("DisplayCommander.XInputWidget", "RightStickMinInput",
                              g_shared_state->right_stick_deadzone.load());

    // Save left stick max output setting
    display_commander::config::set_config_value("DisplayCommander.XInputWidget", "LeftStickMaxOutput",
                              g_shared_state->left_stick_min_output.load());

    // Save right stick max output setting
    display_commander::config::set_config_value("DisplayCommander.XInputWidget", "RightStickMaxOutput",
                              g_shared_state->right_stick_min_output.load());

    // Save stick center calibration settings
    display_commander::config::set_config_value("DisplayCommander.XInputWidget", "LeftStickCenterX",
                              g_shared_state->left_stick_center_x.load());

    display_commander::config::set_config_value("DisplayCommander.XInputWidget", "LeftStickCenterY",
                              g_shared_state->left_stick_center_y.load());

    display_commander::config::set_config_value("DisplayCommander.XInputWidget", "RightStickCenterX",
                              g_shared_state->right_stick_center_x.load());

    display_commander::config::set_config_value("DisplayCommander.XInputWidget", "RightStickCenterY",
                              g_shared_state->right_stick_center_y.load());

    // Save vibration amplification setting
    display_commander::config::set_config_value("DisplayCommander.XInputWidget", "VibrationAmplification",
                              g_shared_state->vibration_amplification.load());

    // Save stick processing mode settings
    display_commander::config::set_config_value("DisplayCommander.XInputWidget", "LeftStickCircular",
                              g_shared_state->left_stick_circular.load());

    display_commander::config::set_config_value("DisplayCommander.XInputWidget", "RightStickCircular",
                              g_shared_state->right_stick_circular.load());

    // Save autofire settings
    display_commander::config::set_config_value("DisplayCommander.XInputWidget", "AutofireEnabled",
                              g_shared_state->autofire_enabled.load());

    display_commander::config::set_config_value("DisplayCommander.XInputWidget", "AutofireHoldDownFrames",
                              static_cast<uint32_t>(g_shared_state->autofire_hold_down_frames.load()));
    display_commander::config::set_config_value("DisplayCommander.XInputWidget", "AutofireHoldUpFrames",
                              static_cast<uint32_t>(g_shared_state->autofire_hold_up_frames.load()));

    // Save autofire button list as comma-separated hex values
    utils::SRWLockExclusive lock(g_shared_state->autofire_lock);

    std::string autofire_buttons_str;
    for (const auto &af_button : g_shared_state->autofire_buttons) {
        if (!autofire_buttons_str.empty()) {
            autofire_buttons_str += ",";
        }
        char hex_str[16];
        sprintf_s(hex_str, "%04X", af_button.button_mask);
        autofire_buttons_str += hex_str;
    }

    display_commander::config::set_config_value("DisplayCommander.XInputWidget", "AutofireButtons", autofire_buttons_str);

    // Save autofire trigger list as comma-separated trigger names (LT, RT)
    std::string autofire_triggers_str;
    for (const auto &af_trigger : g_shared_state->autofire_triggers) {
        if (!autofire_triggers_str.empty()) {
            autofire_triggers_str += ",";
        }
        if (af_trigger.trigger_type == XInputSharedState::TriggerType::LeftTrigger) {
            autofire_triggers_str += "LT";
        } else if (af_trigger.trigger_type == XInputSharedState::TriggerType::RightTrigger) {
            autofire_triggers_str += "RT";
        }
    }

    display_commander::config::set_config_value("DisplayCommander.XInputWidget", "AutofireTriggers", autofire_triggers_str);
}

std::shared_ptr<XInputSharedState> XInputWidget::GetSharedState() { return g_shared_state; }

// Global functions for integration
void InitializeXInputWidget() {
    if (!g_xinput_widget) {
        g_xinput_widget = std::make_unique<XInputWidget>();
        g_xinput_widget->Initialize();

        // Initialize UI state - assume closed initially
        auto shared_state = XInputWidget::GetSharedState();
        if (shared_state) {
            shared_state->ui_overlay_open.store(false);
        }
    }
}

void CleanupXInputWidget() {
    if (g_xinput_widget) {
        g_xinput_widget->Cleanup();
        g_xinput_widget.reset();
    }
}

void DrawXInputWidget() {
    ImGui::Indent();

    if (g_xinput_widget) {
        g_xinput_widget->OnDraw();
    }

    ImGui::Unindent();
}

// Global functions for hooks to use
void UpdateXInputState(DWORD user_index, const XINPUT_STATE *state) {
    auto shared_state = XInputWidget::GetSharedState();
    if (!shared_state || user_index >= XUSER_MAX_COUNT || !state) {
        return;
    }

    // Thread-safe update
    utils::SRWLockExclusive lock(shared_state->state_lock);

    // Update controller state
    shared_state->controller_states[user_index] = *state;
    shared_state->controller_connected[user_index] = ControllerState::Connected;
    shared_state->last_packet_numbers[user_index] = state->dwPacketNumber;
    shared_state->last_update_times[user_index] = GetOriginalTickCount64();

    // Increment event counters
    shared_state->total_events.fetch_add(1);
}

void IncrementEventCounter(const std::string &event_type) {
    auto shared_state = XInputWidget::GetSharedState();
    if (!shared_state)
        return;

    if (event_type == "button") {
        shared_state->button_events.fetch_add(1);
    } else if (event_type == "stick") {
        shared_state->stick_events.fetch_add(1);
    } else if (event_type == "trigger") {
        shared_state->trigger_events.fetch_add(1);
    }
}

// Vibration test functions
void XInputWidget::TestLeftMotor() {
    if (selected_controller_ < 0 || selected_controller_ >= XUSER_MAX_COUNT) {
        LogError("XInputWidget::TestLeftMotor() - Invalid controller index: %d", selected_controller_);
        return;
    }

    XINPUT_VIBRATION vibration = {};
    vibration.wLeftMotorSpeed = 65535; // Maximum intensity
    vibration.wRightMotorSpeed = 0;    // Right motor off

    DWORD result = display_commanderhooks::XInputSetState_Direct ? display_commanderhooks::XInputSetState_Direct(selected_controller_, &vibration) : ERROR_DEVICE_NOT_CONNECTED;
    if (result == ERROR_SUCCESS) {
        LogInfo("XInputWidget::TestLeftMotor() - Left motor test started for controller %d", selected_controller_);
    } else {
        LogError("XInputWidget::TestLeftMotor() - Failed to set vibration for controller %d, error: %lu",
                 selected_controller_, result);
    }
}

void XInputWidget::TestRightMotor() {
    if (selected_controller_ < 0 || selected_controller_ >= XUSER_MAX_COUNT) {
        LogError("XInputWidget::TestRightMotor() - Invalid controller index: %d", selected_controller_);
        return;
    }

    XINPUT_VIBRATION vibration = {};
    vibration.wLeftMotorSpeed = 0;      // Left motor off
    vibration.wRightMotorSpeed = 65535; // Maximum intensity

    DWORD result = display_commanderhooks::XInputSetState_Direct ? display_commanderhooks::XInputSetState_Direct(selected_controller_, &vibration) : ERROR_DEVICE_NOT_CONNECTED;
    if (result == ERROR_SUCCESS) {
        LogInfo("XInputWidget::TestRightMotor() - Right motor test started for controller %d", selected_controller_);
    } else {
        LogError("XInputWidget::TestRightMotor() - Failed to set vibration for controller %d, error: %lu",
                 selected_controller_, result);
    }
}

void XInputWidget::StopVibration() {
    if (selected_controller_ < 0 || selected_controller_ >= XUSER_MAX_COUNT) {
        LogError("XInputWidget::StopVibration() - Invalid controller index: %d", selected_controller_);
        return;
    }

    XINPUT_VIBRATION vibration = {};
    vibration.wLeftMotorSpeed = 0; // Both motors off
    vibration.wRightMotorSpeed = 0;

    DWORD result = display_commanderhooks::XInputSetState_Direct ? display_commanderhooks::XInputSetState_Direct(selected_controller_, &vibration) : ERROR_DEVICE_NOT_CONNECTED;
    if (result == ERROR_SUCCESS) {
        LogInfo("XInputWidget::StopVibration() - Vibration stopped for controller %d", selected_controller_);
    } else {
        LogError("XInputWidget::StopVibration() - Failed to stop vibration for controller %d, error: %lu",
                 selected_controller_, result);
    }
}


void CheckAndHandleScreenshot() {
    try {
        auto shared_state = XInputWidget::GetSharedState();
        if (!shared_state)
            return;

        // Check if screenshot should be triggered
        if (shared_state->trigger_screenshot.load()) {
            // Reset the flag
            shared_state->trigger_screenshot.store(false);

            // Get the ReShade runtime instance
            reshade::api::effect_runtime *runtime = GetFirstReShadeRuntime();

            if (runtime != nullptr) {
                // Use PrintScreen key simulation to trigger ReShade's built-in screenshot system
                // This is the safest and most reliable method
                try {
                    LogInfo("XXX Triggering ReShade screenshot via PrintScreen key simulation");

                    // Simulate PrintScreen key press to trigger ReShade's screenshot
                    INPUT input = {};
                    input.type = INPUT_KEYBOARD;
                    input.ki.wVk = VK_SNAPSHOT; // PrintScreen key
                    input.ki.dwFlags = 0;       // Key down

                    // Send key down
                    UINT result = SendInput(1, &input, sizeof(INPUT));
                    if (result == 0) {
                        LogError("XXX SendInput failed for key down, error: %lu", GetLastError());
                    }

                    // Small delay to ensure the key press is registered
                    Sleep(50);

                    // Send key up
                    input.ki.dwFlags = KEYEVENTF_KEYUP;
                    result = SendInput(1, &input, sizeof(INPUT));
                    if (result == 0) {
                        LogError("XXX SendInput failed for key up, error: %lu", GetLastError());
                    }

                    LogInfo("XXX PrintScreen key simulation completed successfully");

                } catch (const std::exception &e) {
                    LogError("XXX Exception in PrintScreen simulation: %s", e.what());
                } catch (...) {
                    LogError("XXX Unknown exception in PrintScreen simulation");
                }
            } else {
                LogError("XXX ReShade runtime not available for screenshot");
            }
        }
    } catch (const std::exception &e) {
        LogError("XXX Exception in CheckAndHandleScreenshot: %s", e.what());
    } catch (...) {
        LogError("XXX Unknown exception in CheckAndHandleScreenshot");
    }
}

// Global function to update battery status for a controller
void UpdateBatteryStatus(DWORD user_index) {
    if (user_index >= XUSER_MAX_COUNT) {
        return;
    }

    auto shared_state = XInputWidget::GetSharedState();
    if (!shared_state) {
        return;
    }

    // Check if we need to update battery status (update every 5 seconds)
    auto current_time = GetOriginalTickCount64();
    auto last_update = shared_state->last_battery_update_times[user_index].load();

    if (current_time - last_update < 5000) { // 5 seconds
        return;
    }

    // Update battery information for gamepad
    XINPUT_BATTERY_INFORMATION battery_info = {};
    DWORD result = display_commanderhooks::XInputGetBatteryInformation_Direct ? display_commanderhooks::XInputGetBatteryInformation_Direct(user_index, BATTERY_DEVTYPE_GAMEPAD, &battery_info) : ERROR_DEVICE_NOT_CONNECTED;

    if (result == ERROR_SUCCESS) {
        shared_state->battery_info[user_index] = battery_info;
        shared_state->battery_info_valid[user_index] = true;
        shared_state->last_battery_update_times[user_index] = current_time;

        LogInfo("XXX Controller %lu battery: Type=%d, Level=%d", user_index, battery_info.BatteryType,
                battery_info.BatteryLevel);
    } else {
        // Mark battery info as invalid if we can't get it
        shared_state->battery_info_valid[user_index] = false;
        LogWarn("XXX Failed to get battery info for controller %lu: %lu", user_index, result);
    }
}

void XInputWidget::DrawDualSenseReport(int controller_index) {
    if (ImGui::CollapsingHeader("DualSense Input Report", ImGuiTreeNodeFlags_DefaultOpen)) {
        // Check if DualSense HID wrapper is available
        if (!display_commander::dualsense::g_dualsense_hid_wrapper) {
            ImGui::TextColored(ui::colors::TEXT_DIMMED, "DualSense HID wrapper not available");
            return;
        }

        // Get devices from HID wrapper
        const auto& devices = display_commander::dualsense::g_dualsense_hid_wrapper->GetDevices();

        if (devices.empty()) {
            ImGui::TextColored(ui::colors::TEXT_DIMMED, "No DualSense devices detected");
            return;
        }

        // Find the device that corresponds to the selected controller
        // For now, we'll use the first available device
        // In a more sophisticated implementation, we'd map controller indices to device indices
        const auto& device = devices[0];

        if (!device.is_connected) {
            ImGui::TextColored(ui::colors::TEXT_DIMMED, "DualSense device not connected");
            return;
        }

        // Display basic device info
        ImGui::TextColored(ui::colors::STATUS_ACTIVE, "Device: %s",
                          device.device_name.empty() ? "DualSense Controller" : device.device_name.c_str());
        ImGui::Text("Connection: %s", device.connection_type.c_str());
        ImGui::Text("Vendor ID: 0x%04X", device.vendor_id);
        ImGui::Text("Product ID: 0x%04X", device.product_id);

        // Display last update time
        if (device.last_update_time > 0) {
            DWORD now = GetTickCount();
            DWORD age_ms = now - device.last_update_time;
            ImGui::Text("Last Update: %lu ms ago", age_ms);
        }

        ImGui::Spacing();

        // Display input report size and first few bytes
        if (device.hid_device && device.hid_device->input_report.size() > 0) {
            ImGui::Text("Input Report Size: %zu bytes", device.hid_device->input_report.size());

            // Show first 16 bytes in hex format
            const auto& inputReport = device.hid_device->input_report;
            std::string hex_string = "";
            for (size_t i = 0; i < (inputReport.size() < 16 ? inputReport.size() : 16); ++i) {
                char hex_byte[4];
                sprintf_s(hex_byte, "%02X ", inputReport[i]);
                hex_string += hex_byte;
            }
            ImGui::Text("First 16 bytes: %s", hex_string.c_str());

            ImGui::Spacing();

            // Display Special-K DualSense data if available
            if (ImGui::CollapsingHeader("Special-K DualSense Data", ImGuiTreeNodeFlags_DefaultOpen)) {
                const auto& sk_data = device.sk_dualsense_data;

                // Basic input data
                if (ImGui::CollapsingHeader("Input Data", ImGuiTreeNodeFlags_DefaultOpen)) {
                    ImGui::Columns(2, "SKInputColumns", false);

                    // Sticks
                    ImGui::Text("Left Stick: X=%d, Y=%d", sk_data.LeftStickX, sk_data.LeftStickY);
                    ImGui::NextColumn();
                    ImGui::Text("Right Stick: X=%d, Y=%d", sk_data.RightStickX, sk_data.RightStickY);
                    ImGui::NextColumn();

                    // Triggers
                    ImGui::Text("Left Trigger: %d", sk_data.TriggerLeft);
                    ImGui::NextColumn();
                    ImGui::Text("Right Trigger: %d", sk_data.TriggerRight);
                    ImGui::NextColumn();

                    // D-pad
                    const char* dpad_names[] = {"Up", "Up-Right", "Right", "Down-Right", "Down", "Down-Left", "Left", "Up-Left", "None"};
                    ImGui::Text("D-Pad: %s", dpad_names[static_cast<int>(sk_data.DPad)]);
                    ImGui::NextColumn();
                    ImGui::Text("Sequence: %d", sk_data.SeqNo);
                    ImGui::NextColumn();

                    ImGui::Columns(1);
                }

                // Button states
                if (ImGui::CollapsingHeader("Button States", ImGuiTreeNodeFlags_DefaultOpen)) {
                    ImGui::Columns(3, "SKButtonColumns", false);

                    // Face buttons
                    ImGui::Text("Square: %s", sk_data.ButtonSquare ? "PRESSED" : "Released");
                    ImGui::NextColumn();
                    ImGui::Text("Cross: %s", sk_data.ButtonCross ? "PRESSED" : "Released");
                    ImGui::NextColumn();
                    ImGui::Text("Circle: %s", sk_data.ButtonCircle ? "PRESSED" : "Released");
                    ImGui::NextColumn();
                    ImGui::Text("Triangle: %s", sk_data.ButtonTriangle ? "PRESSED" : "Released");
                    ImGui::NextColumn();

                    // Shoulder buttons
                    ImGui::Text("L1: %s", sk_data.ButtonL1 ? "PRESSED" : "Released");
                    ImGui::NextColumn();
                    ImGui::Text("R1: %s", sk_data.ButtonR1 ? "PRESSED" : "Released");
                    ImGui::NextColumn();
                    ImGui::Text("L2: %s", sk_data.ButtonL2 ? "PRESSED" : "Released");
                    ImGui::NextColumn();
                    ImGui::Text("R2: %s", sk_data.ButtonR2 ? "PRESSED" : "Released");
                    ImGui::NextColumn();

                    // System buttons
                    ImGui::Text("Create: %s", sk_data.ButtonCreate ? "PRESSED" : "Released");
                    ImGui::NextColumn();
                    ImGui::Text("Options: %s", sk_data.ButtonOptions ? "PRESSED" : "Released");
                    ImGui::NextColumn();
                    ImGui::Text("L3: %s", sk_data.ButtonL3 ? "PRESSED" : "Released");
                    ImGui::NextColumn();
                    ImGui::Text("R3: %s", sk_data.ButtonR3 ? "PRESSED" : "Released");
                    ImGui::NextColumn();
                    ImGui::Text("Home: %s", sk_data.ButtonHome ? "PRESSED" : "Released");
                    ImGui::NextColumn();
                    ImGui::Text("Touchpad: %s", sk_data.ButtonPad ? "PRESSED" : "Released");
                    ImGui::NextColumn();
                    ImGui::Text("Mute: %s", sk_data.ButtonMute ? "PRESSED" : "Released");
                    ImGui::NextColumn();

                    // Edge controller buttons
                    if (sk_data.ButtonLeftFunction || sk_data.ButtonRightFunction || sk_data.ButtonLeftPaddle || sk_data.ButtonRightPaddle) {
                        ImGui::Text("Left Function: %s", sk_data.ButtonLeftFunction ? "PRESSED" : "Released");
                        ImGui::NextColumn();
                        ImGui::Text("Right Function: %s", sk_data.ButtonRightFunction ? "PRESSED" : "Released");
                        ImGui::NextColumn();
                        ImGui::Text("Left Paddle: %s", sk_data.ButtonLeftPaddle ? "PRESSED" : "Released");
                        ImGui::NextColumn();
                        ImGui::Text("Right Paddle: %s", sk_data.ButtonRightPaddle ? "PRESSED" : "Released");
                        ImGui::NextColumn();
                    }

                    ImGui::Columns(1);
                }

                // Battery and power
                if (ImGui::CollapsingHeader("Battery & Power")) {
                    ImGui::Columns(2, "SKPowerColumns", false);

                    ImGui::Text("Battery: %d%%", sk_data.PowerPercent * 10);
                    ImGui::NextColumn();
                    const char* power_state_names[] = {"Unknown", "Charging", "Discharging", "Not Charging", "Full"};
                    ImGui::Text("Power State: %s", power_state_names[static_cast<int>(sk_data.PowerState)]);
                    ImGui::NextColumn();
                    ImGui::Text("USB Data: %s", sk_data.PluggedUsbData ? "Yes" : "No");
                    ImGui::NextColumn();
                    ImGui::Text("USB Power: %s", sk_data.PluggedUsbPower ? "Yes" : "No");
                    ImGui::NextColumn();
                    ImGui::Text("Headphones: %s", sk_data.PluggedHeadphones ? "Yes" : "No");
                    ImGui::NextColumn();
                    ImGui::Text("Microphone: %s", sk_data.PluggedMic ? "Yes" : "No");
                    ImGui::NextColumn();
                    ImGui::Text("External Mic: %s", sk_data.PluggedExternalMic ? "Yes" : "No");
                    ImGui::NextColumn();
                    ImGui::Text("Mic Muted: %s", sk_data.MicMuted ? "Yes" : "No");
                    ImGui::NextColumn();
                    ImGui::Text("Haptic Filter: %s", sk_data.HapticLowPassFilter ? "On" : "Off");
                    ImGui::NextColumn();

                    ImGui::Columns(1);
                }

                // Motion sensors
                if (ImGui::CollapsingHeader("Motion Sensors")) {
                    ImGui::Columns(2, "SKMotionColumns", false);

                    ImGui::Text("Angular Velocity X: %d", sk_data.AngularVelocityX);
                    ImGui::NextColumn();
                    ImGui::Text("Angular Velocity Y: %d", sk_data.AngularVelocityY);
                    ImGui::NextColumn();
                    ImGui::Text("Angular Velocity Z: %d", sk_data.AngularVelocityZ);
                    ImGui::NextColumn();
                    ImGui::Text("Accelerometer X: %d", sk_data.AccelerometerX);
                    ImGui::NextColumn();
                    ImGui::Text("Accelerometer Y: %d", sk_data.AccelerometerY);
                    ImGui::NextColumn();
                    ImGui::Text("Accelerometer Z: %d", sk_data.AccelerometerZ);
                    ImGui::NextColumn();
                    ImGui::Text("Temperature: %d°C", sk_data.Temperature);
                    ImGui::NextColumn();
                    ImGui::Text("Sensor Timestamp: %u", sk_data.SensorTimestamp);
                    ImGui::NextColumn();

                    ImGui::Columns(1);
                }

                // Adaptive triggers
                if (ImGui::CollapsingHeader("Adaptive Triggers")) {
                    ImGui::Columns(2, "SKTriggerColumns", false);

                    ImGui::Text("Left Trigger Status: %d", sk_data.TriggerLeftStatus);
                    ImGui::NextColumn();
                    ImGui::Text("Right Trigger Status: %d", sk_data.TriggerRightStatus);
                    ImGui::NextColumn();
                    ImGui::Text("Left Stop Location: %d", sk_data.TriggerLeftStopLocation);
                    ImGui::NextColumn();
                    ImGui::Text("Right Stop Location: %d", sk_data.TriggerRightStopLocation);
                    ImGui::NextColumn();
                    ImGui::Text("Left Effect: %d", sk_data.TriggerLeftEffect);
                    ImGui::NextColumn();
                    ImGui::Text("Right Effect: %d", sk_data.TriggerRightEffect);
                    ImGui::NextColumn();

                    ImGui::Columns(1);
                }

                // Timestamps
                if (ImGui::CollapsingHeader("Timestamps")) {
                    ImGui::Text("Host Timestamp: %u", sk_data.HostTimestamp);
                    ImGui::Text("Device Timestamp: %u", sk_data.DeviceTimeStamp);
                    ImGui::Text("Sensor Timestamp: %u", sk_data.SensorTimestamp);
                }
            }
        } else {
            ImGui::TextColored(ui::colors::TEXT_DIMMED, "No input report data available");
        }
    }
}

// Autofire functions
void XInputWidget::DrawAutofireSettings() {
    ImGui::Indent();

    if (ImGui::CollapsingHeader("Autofire Settings", ImGuiTreeNodeFlags_DefaultOpen)) {
        auto shared_state = GetSharedState();
        if (!shared_state) {
            ImGui::Unindent();
            return;
        }

        // Master enable/disable
        bool autofire_enabled = shared_state->autofire_enabled.load();
        if (ImGui::Checkbox("Enable Autofire", &autofire_enabled)) {
            shared_state->autofire_enabled.store(autofire_enabled);

            // When disabling autofire, reset all autofire button and trigger states
            if (!autofire_enabled) {
                utils::SRWLockExclusive lock(shared_state->autofire_lock);
                for (auto &af_button : shared_state->autofire_buttons) {
                    af_button.is_holding_down.store(true);
                    af_button.phase_start_frame_id.store(0);
                }
                for (auto &af_trigger : shared_state->autofire_triggers) {
                    af_trigger.is_holding_down.store(true);
                    af_trigger.phase_start_frame_id.store(0);
                }
            }

            SaveSettings();
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Enable autofire for selected buttons. When a button is held, it will cycle between hold down and hold up phases.");
        }

        if (autofire_enabled) {
            ImGui::Spacing();

            // Hold down frames setting
            uint32_t hold_down_frames = shared_state->autofire_hold_down_frames.load();
            int hold_down_frames_int = static_cast<int>(hold_down_frames);

            ImGui::Text("Hold Down (frames): %d", hold_down_frames_int);
            ImGui::SameLine();

            // Input field for precise control
            ImGui::SetNextItemWidth(80);
            if (ImGui::InputInt("##HoldDownFramesInput", &hold_down_frames_int, 1, 5, ImGuiInputTextFlags_EnterReturnsTrue)) {
                if (hold_down_frames_int < 1) {
                    hold_down_frames_int = 1;
                } else if (hold_down_frames_int > 1000) {
                    hold_down_frames_int = 1000;
                }
                shared_state->autofire_hold_down_frames.store(static_cast<uint32_t>(hold_down_frames_int));
                SaveSettings();
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Number of frames to hold button down (1-1000). Enter a value or use slider below.");
            }

            // Slider for quick adjustment (1-60 range for common use)
            int slider_value_down = hold_down_frames_int;
            if (slider_value_down > 60) slider_value_down = 60; // Clamp for slider display
            if (ImGui::SliderInt("##HoldDownFramesSlider", &slider_value_down, 1, 60, "%d frames")) {
                shared_state->autofire_hold_down_frames.store(static_cast<uint32_t>(slider_value_down));
                SaveSettings();
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Quick adjustment slider (1-60 frames). Use input field above for values > 60.");
            }

            ImGui::Spacing();

            // Hold up frames setting
            uint32_t hold_up_frames = shared_state->autofire_hold_up_frames.load();
            int hold_up_frames_int = static_cast<int>(hold_up_frames);

            ImGui::Text("Hold Up (frames): %d", hold_up_frames_int);
            ImGui::SameLine();

            // Input field for precise control
            ImGui::SetNextItemWidth(80);
            if (ImGui::InputInt("##HoldUpFramesInput", &hold_up_frames_int, 1, 5, ImGuiInputTextFlags_EnterReturnsTrue)) {
                if (hold_up_frames_int < 1) {
                    hold_up_frames_int = 1;
                } else if (hold_up_frames_int > 1000) {
                    hold_up_frames_int = 1000;
                }
                shared_state->autofire_hold_up_frames.store(static_cast<uint32_t>(hold_up_frames_int));
                SaveSettings();
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Number of frames to hold button up (1-1000). Enter a value or use slider below.");
            }

            // Slider for quick adjustment (1-60 range for common use)
            int slider_value_up = hold_up_frames_int;
            if (slider_value_up > 60) slider_value_up = 60; // Clamp for slider display
            if (ImGui::SliderInt("##HoldUpFramesSlider", &slider_value_up, 1, 60, "%d frames")) {
                shared_state->autofire_hold_up_frames.store(static_cast<uint32_t>(slider_value_up));
                SaveSettings();
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Quick adjustment slider (1-60 frames). Use input field above for values > 60.");
            }

            // Show effective rate information
            uint32_t total_cycle_frames = hold_down_frames + hold_up_frames;
            if (total_cycle_frames > 0) {
                ImGui::TextColored(ui::colors::TEXT_DIMMED, "  Cycle: %u frames total | At 60 FPS: ~%.1f cycles/sec | At 120 FPS: ~%.1f cycles/sec",
                    total_cycle_frames, 60.0f / total_cycle_frames, 120.0f / total_cycle_frames);
            }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Text("Select buttons for autofire:");

            // Button selection checkboxes
            const struct {
                WORD mask;
                const char *name;
            } buttons[] = {
                {XINPUT_GAMEPAD_A, "A"},
                {XINPUT_GAMEPAD_B, "B"},
                {XINPUT_GAMEPAD_X, "X"},
                {XINPUT_GAMEPAD_Y, "Y"},
                {XINPUT_GAMEPAD_LEFT_SHOULDER, "LB"},
                {XINPUT_GAMEPAD_RIGHT_SHOULDER, "RB"},
                {XINPUT_GAMEPAD_BACK, "View"},
                {XINPUT_GAMEPAD_START, "Menu"},
                {XINPUT_GAMEPAD_LEFT_THUMB, "LS"},
                {XINPUT_GAMEPAD_RIGHT_THUMB, "RS"},
                {XINPUT_GAMEPAD_DPAD_UP, "D-Up"},
                {XINPUT_GAMEPAD_DPAD_DOWN, "D-Down"},
                {XINPUT_GAMEPAD_DPAD_LEFT, "D-Left"},
                {XINPUT_GAMEPAD_DPAD_RIGHT, "D-Right"},
            };

            // Helper lambda to check if button exists (thread-safe)
            auto is_autofire_button = [&shared_state](WORD button_mask) -> bool {
                utils::SRWLockShared lock(shared_state->autofire_lock);
                for (const auto &af_button : shared_state->autofire_buttons) {
                    if (af_button.button_mask == button_mask) {
                        return true;
                    }
                }
                return false;
            };

            // Display checkboxes in a grid
            for (size_t i = 0; i < sizeof(buttons) / sizeof(buttons[0]); i += 2) {
                if (i + 1 < sizeof(buttons) / sizeof(buttons[0])) {
                    // Two buttons per row
                    bool is_enabled1 = is_autofire_button(buttons[i].mask);
                    bool is_enabled2 = is_autofire_button(buttons[i + 1].mask);

                    if (ImGui::Checkbox(buttons[i].name, &is_enabled1)) {
                        {
                            // Acquire lock only for modification
                            utils::SRWLockExclusive lock(shared_state->autofire_lock);
                        if (is_enabled1) {
                                // Add button
                            bool exists = false;
                            for (const auto &af_button : shared_state->autofire_buttons) {
                                if (af_button.button_mask == buttons[i].mask) {
                                    exists = true;
                                    break;
                                }
                            }
                            if (!exists) {
                                shared_state->autofire_buttons.push_back(XInputSharedState::AutofireButton(buttons[i].mask));
                                LogInfo("XInputWidget::DrawAutofireSettings() - Added autofire for button 0x%04X", buttons[i].mask);
                            }
                        } else {
                                // Remove button
                            auto it = std::remove_if(shared_state->autofire_buttons.begin(), shared_state->autofire_buttons.end(),
                                                    [&buttons, i](const XInputSharedState::AutofireButton &af_button) {
                                                        return af_button.button_mask == buttons[i].mask;
                                                    });
                            shared_state->autofire_buttons.erase(it, shared_state->autofire_buttons.end());
                            LogInfo("XInputWidget::DrawAutofireSettings() - Removed autofire for button 0x%04X", buttons[i].mask);
                        }
                        } // Lock released here
                        SaveSettings();
                    }

                    ImGui::SameLine();

                    if (ImGui::Checkbox(buttons[i + 1].name, &is_enabled2)) {
                        {
                            // Acquire lock only for modification
                            utils::SRWLockExclusive lock(shared_state->autofire_lock);
                        if (is_enabled2) {
                                // Add button
                            bool exists = false;
                            for (const auto &af_button : shared_state->autofire_buttons) {
                                if (af_button.button_mask == buttons[i + 1].mask) {
                                    exists = true;
                                    break;
                                }
                            }
                            if (!exists) {
                                shared_state->autofire_buttons.push_back(XInputSharedState::AutofireButton(buttons[i + 1].mask));
                                LogInfo("XInputWidget::DrawAutofireSettings() - Added autofire for button 0x%04X", buttons[i + 1].mask);
                            }
                        } else {
                                // Remove button
                            auto it = std::remove_if(shared_state->autofire_buttons.begin(), shared_state->autofire_buttons.end(),
                                                    [&buttons, i](const XInputSharedState::AutofireButton &af_button) {
                                                        return af_button.button_mask == buttons[i + 1].mask;
                                                    });
                            shared_state->autofire_buttons.erase(it, shared_state->autofire_buttons.end());
                            LogInfo("XInputWidget::DrawAutofireSettings() - Removed autofire for button 0x%04X", buttons[i + 1].mask);
                        }
                        } // Lock released here
                        SaveSettings();
                    }
                } else {
                    // Single button on last row
                    bool is_enabled = is_autofire_button(buttons[i].mask);
                    if (ImGui::Checkbox(buttons[i].name, &is_enabled)) {
                        {
                            // Acquire lock only for modification
                            utils::SRWLockExclusive lock(shared_state->autofire_lock);
                        if (is_enabled) {
                                // Add button
                            bool exists = false;
                            for (const auto &af_button : shared_state->autofire_buttons) {
                                if (af_button.button_mask == buttons[i].mask) {
                                    exists = true;
                                    break;
                                }
                            }
                            if (!exists) {
                                shared_state->autofire_buttons.push_back(XInputSharedState::AutofireButton(buttons[i].mask));
                                LogInfo("XInputWidget::DrawAutofireSettings() - Added autofire for button 0x%04X", buttons[i].mask);
                            }
                        } else {
                                // Remove button
                            auto it = std::remove_if(shared_state->autofire_buttons.begin(), shared_state->autofire_buttons.end(),
                                                    [&buttons, i](const XInputSharedState::AutofireButton &af_button) {
                                                        return af_button.button_mask == buttons[i].mask;
                                                    });
                            shared_state->autofire_buttons.erase(it, shared_state->autofire_buttons.end());
                            LogInfo("XInputWidget::DrawAutofireSettings() - Removed autofire for button 0x%04X", buttons[i].mask);
                        }
                        } // Lock released here
                        SaveSettings();
                    }
                }
            }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Text("Select triggers for autofire:");

            // Trigger selection checkboxes
            bool is_lt_enabled = IsAutofireTrigger(XInputSharedState::TriggerType::LeftTrigger);
            bool is_rt_enabled = IsAutofireTrigger(XInputSharedState::TriggerType::RightTrigger);

            if (ImGui::Checkbox("LT (Left Trigger)", &is_lt_enabled)) {
                if (is_lt_enabled) {
                    AddAutofireTrigger(XInputSharedState::TriggerType::LeftTrigger);
                    LogInfo("XInputWidget::DrawAutofireSettings() - Added autofire for LT");
                } else {
                    RemoveAutofireTrigger(XInputSharedState::TriggerType::LeftTrigger);
                    LogInfo("XInputWidget::DrawAutofireSettings() - Removed autofire for LT");
                }
                SaveSettings();
            }

            ImGui::SameLine();

            if (ImGui::Checkbox("RT (Right Trigger)", &is_rt_enabled)) {
                if (is_rt_enabled) {
                    AddAutofireTrigger(XInputSharedState::TriggerType::RightTrigger);
                    LogInfo("XInputWidget::DrawAutofireSettings() - Added autofire for RT");
                } else {
                    RemoveAutofireTrigger(XInputSharedState::TriggerType::RightTrigger);
                    LogInfo("XInputWidget::DrawAutofireSettings() - Removed autofire for RT");
                }
                SaveSettings();
            }
        }
    }

    ImGui::Unindent();
}

void XInputWidget::AddAutofireButton(WORD button_mask) {
    auto shared_state = GetSharedState();
    if (!shared_state) {
        return;
    }

    // Thread-safe access
    utils::SRWLockExclusive lock(shared_state->autofire_lock);

    // Check if button already exists
    bool exists = false;
    for (const auto &af_button : shared_state->autofire_buttons) {
        if (af_button.button_mask == button_mask) {
            exists = true;
            break;
        }
    }

    if (!exists) {
        shared_state->autofire_buttons.push_back(XInputSharedState::AutofireButton(button_mask));
        LogInfo("XInputWidget::AddAutofireButton() - Added autofire for button 0x%04X", button_mask);
    }
}

void XInputWidget::RemoveAutofireButton(WORD button_mask) {
    auto shared_state = GetSharedState();
    if (!shared_state) {
        return;
    }

    // Thread-safe access
    utils::SRWLockExclusive lock(shared_state->autofire_lock);

    // Remove button from list
    auto it = std::remove_if(shared_state->autofire_buttons.begin(), shared_state->autofire_buttons.end(),
                              [button_mask](const XInputSharedState::AutofireButton &af_button) {
                                  return af_button.button_mask == button_mask;
                              });
    shared_state->autofire_buttons.erase(it, shared_state->autofire_buttons.end());

    LogInfo("XInputWidget::RemoveAutofireButton() - Removed autofire for button 0x%04X", button_mask);
}

bool XInputWidget::IsAutofireButton(WORD button_mask) const {
    auto shared_state = GetSharedState();
    if (!shared_state) {
        return false;
    }

    // Thread-safe access
    utils::SRWLockExclusive lock(shared_state->autofire_lock);

    bool found = false;
    for (const auto &af_button : shared_state->autofire_buttons) {
        if (af_button.button_mask == button_mask) {
            found = true;
            break;
        }
    }

    return found;
}

void XInputWidget::AddAutofireTrigger(XInputSharedState::TriggerType trigger_type) {
    auto shared_state = GetSharedState();
    if (!shared_state) {
        return;
    }

    // Thread-safe access
    utils::SRWLockExclusive lock(shared_state->autofire_lock);

    // Check if trigger already exists
    bool exists = false;
    for (const auto &af_trigger : shared_state->autofire_triggers) {
        if (af_trigger.trigger_type == trigger_type) {
            exists = true;
            break;
        }
    }

    if (!exists) {
        shared_state->autofire_triggers.push_back(XInputSharedState::AutofireTrigger(trigger_type));
        LogInfo("XInputWidget::AddAutofireTrigger() - Added autofire for trigger %s",
                trigger_type == XInputSharedState::TriggerType::LeftTrigger ? "LT" : "RT");
    }
}

void XInputWidget::RemoveAutofireTrigger(XInputSharedState::TriggerType trigger_type) {
    auto shared_state = GetSharedState();
    if (!shared_state) {
        return;
    }

    // Thread-safe access
    utils::SRWLockExclusive lock(shared_state->autofire_lock);

    // Remove trigger from list
    auto it = std::remove_if(shared_state->autofire_triggers.begin(), shared_state->autofire_triggers.end(),
                              [trigger_type](const XInputSharedState::AutofireTrigger &af_trigger) {
                                  return af_trigger.trigger_type == trigger_type;
                              });
    shared_state->autofire_triggers.erase(it, shared_state->autofire_triggers.end());

    LogInfo("XInputWidget::RemoveAutofireTrigger() - Removed autofire for trigger %s",
            trigger_type == XInputSharedState::TriggerType::LeftTrigger ? "LT" : "RT");
}

bool XInputWidget::IsAutofireTrigger(XInputSharedState::TriggerType trigger_type) const {
    auto shared_state = GetSharedState();
    if (!shared_state) {
        return false;
    }

    // Thread-safe access
    utils::SRWLockShared lock(shared_state->autofire_lock);

    bool found = false;
    for (const auto &af_trigger : shared_state->autofire_triggers) {
        if (af_trigger.trigger_type == trigger_type) {
            found = true;
            break;
        }
    }

    return found;
}

// Global function for hooks to use
void ProcessAutofire(DWORD user_index, XINPUT_STATE *pState) {
    if (!pState) {
        return;
    }

    auto shared_state = XInputWidget::GetSharedState();
    if (!shared_state) {
        return;
    }

    // Check if autofire is enabled
    bool autofire_enabled = shared_state->autofire_enabled.load();
    if (!autofire_enabled) {
        // When autofire is disabled, reset all autofire button and trigger states to prevent stale state
        utils::SRWLockExclusive lock(shared_state->autofire_lock);
        for (auto &af_button : shared_state->autofire_buttons) {
            af_button.is_holding_down.store(true);
            af_button.phase_start_frame_id.store(0);
        }
        for (auto &af_trigger : shared_state->autofire_triggers) {
            af_trigger.is_holding_down.store(true);
            af_trigger.phase_start_frame_id.store(0);
        }
        return;
    }

    // Get current frame ID
    uint64_t current_frame_id = g_global_frame_id.load();
    uint32_t hold_down_frames = shared_state->autofire_hold_down_frames.load();
    uint32_t hold_up_frames = shared_state->autofire_hold_up_frames.load();

    // Thread-safe access to autofire_buttons and autofire_triggers
    utils::SRWLockExclusive lock(shared_state->autofire_lock);

    // Store original button state before processing
    WORD original_buttons = pState->Gamepad.wButtons;
    BYTE original_left_trigger = pState->Gamepad.bLeftTrigger;
    BYTE original_right_trigger = pState->Gamepad.bRightTrigger;

    // Process each autofire button directly from shared state
    for (auto &af_button : shared_state->autofire_buttons) {
        WORD button_mask = af_button.button_mask;
        bool is_pressed = (original_buttons & button_mask) != 0;

        if (is_pressed) {
            // Button is held down, process autofire cycle
            uint64_t phase_start_frame = af_button.phase_start_frame_id.load();
            bool is_holding_down = af_button.is_holding_down.load();

            // Initialize phase if this is the first frame
            if (phase_start_frame == 0) {
                af_button.phase_start_frame_id.store(current_frame_id);
                af_button.is_holding_down.store(true);
                is_holding_down = true;
                phase_start_frame = current_frame_id;
            }

            uint64_t frames_in_current_phase = current_frame_id - phase_start_frame;

            if (is_holding_down) {
                // Currently holding down - check if we should switch to hold up phase
                if (frames_in_current_phase >= hold_down_frames) {
                    // Switch to hold up phase
                    af_button.is_holding_down.store(false);
                    af_button.phase_start_frame_id.store(current_frame_id);
                    // Turn button off
                    pState->Gamepad.wButtons &= ~button_mask;
                } else {
                    // Keep holding down
                    pState->Gamepad.wButtons |= button_mask;
                }
            } else {
                // Currently holding up - check if we should switch to hold down phase
                if (frames_in_current_phase >= hold_up_frames) {
                    // Switch to hold down phase
                    af_button.is_holding_down.store(true);
                    af_button.phase_start_frame_id.store(current_frame_id);
                    // Turn button on
                    pState->Gamepad.wButtons |= button_mask;
                } else {
                    // Keep holding up
                    pState->Gamepad.wButtons &= ~button_mask;
                }
            }
        } else {
            // Button is not pressed, reset state
            af_button.is_holding_down.store(true);
            af_button.phase_start_frame_id.store(0);
        }
    }

    // Process each autofire trigger directly from shared state
    // Use threshold of 30 to avoid accidental activation from slight trigger pressure
    const BYTE trigger_threshold = 30;
    for (auto &af_trigger : shared_state->autofire_triggers) {
        BYTE original_trigger_value = (af_trigger.trigger_type == XInputSharedState::TriggerType::LeftTrigger)
                                       ? original_left_trigger
                                       : original_right_trigger;
        bool is_pressed = original_trigger_value > trigger_threshold;

        if (is_pressed) {
            // Trigger is held down, process autofire cycle
            uint64_t phase_start_frame = af_trigger.phase_start_frame_id.load();
            bool is_holding_down = af_trigger.is_holding_down.load();

            // Initialize phase if this is the first frame
            if (phase_start_frame == 0) {
                af_trigger.phase_start_frame_id.store(current_frame_id);
                af_trigger.is_holding_down.store(true);
                is_holding_down = true;
                phase_start_frame = current_frame_id;
            }

            uint64_t frames_in_current_phase = current_frame_id - phase_start_frame;

            if (is_holding_down) {
                // Currently holding down - check if we should switch to hold up phase
                if (frames_in_current_phase >= hold_down_frames) {
                    // Switch to hold up phase
                    af_trigger.is_holding_down.store(false);
                    af_trigger.phase_start_frame_id.store(current_frame_id);
                    // Turn trigger off (set to 0)
                    if (af_trigger.trigger_type == XInputSharedState::TriggerType::LeftTrigger) {
                        pState->Gamepad.bLeftTrigger = 0;
                    } else {
                        pState->Gamepad.bRightTrigger = 0;
                    }
                } else {
                    // Keep holding down (set to full value 255)
                    if (af_trigger.trigger_type == XInputSharedState::TriggerType::LeftTrigger) {
                        pState->Gamepad.bLeftTrigger = 255;
                    } else {
                        pState->Gamepad.bRightTrigger = 255;
                    }
                }
            } else {
                // Currently holding up - check if we should switch to hold down phase
                if (frames_in_current_phase >= hold_up_frames) {
                    // Switch to hold down phase
                    af_trigger.is_holding_down.store(true);
                    af_trigger.phase_start_frame_id.store(current_frame_id);
                    // Turn trigger on (set to full value 255)
                    if (af_trigger.trigger_type == XInputSharedState::TriggerType::LeftTrigger) {
                        pState->Gamepad.bLeftTrigger = 255;
                    } else {
                        pState->Gamepad.bRightTrigger = 255;
                    }
                } else {
                    // Keep holding up (set to 0)
                    if (af_trigger.trigger_type == XInputSharedState::TriggerType::LeftTrigger) {
                        pState->Gamepad.bLeftTrigger = 0;
                    } else {
                        pState->Gamepad.bRightTrigger = 0;
                    }
                }
            }
        } else {
            // Trigger is not pressed, reset state
            af_trigger.is_holding_down.store(true);
            af_trigger.phase_start_frame_id.store(0);
        }
    }
}

} // namespace display_commander::widgets::xinput_widget
