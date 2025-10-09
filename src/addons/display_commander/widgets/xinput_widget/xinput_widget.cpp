#include "xinput_widget.hpp"
#include "../../globals.hpp"
#include "../../utils.hpp"
#include "../../hooks/xinput_hooks.hpp"
#include "../../hooks/timeslowdown_hooks.hpp"
#include <chrono>
#include <imgui.h>
#include <reshade.hpp>
#include <thread>
#include <vector>
#include <windows.h>


namespace display_commander::widgets::xinput_widget {

// Helper function to get original GetTickCount64 value (unhooked)
static ULONGLONG GetOriginalTickCount64() {
    if (display_commanderhooks::GetTickCount64_Original) {
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
        g_shared_state = std::make_shared<XInputSharedState>();

        // Initialize controller states
        for (int i = 0; i < XUSER_MAX_COUNT; ++i) {
            ZeroMemory(&g_shared_state->controller_states[i], sizeof(XINPUT_STATE));
            g_shared_state->controller_connected[i] = false;
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
    if (!is_initialized_) {
        Initialize();
    }

    if (!g_shared_state) {
        ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "XInput shared state not initialized");
        return;
    }

    // Draw the XInput widget UI
    ImGui::TextColored(ImVec4(0.9f, 0.9f, 0.9f, 1.0f), "=== XInput Controller Monitor ===");
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

    // Draw chord settings
    DrawChordSettings();
    ImGui::Spacing();

    // Draw controller selector
    DrawControllerSelector();
    ImGui::Spacing();

    // Draw selected controller state
    DrawControllerState();
}

void XInputWidget::DrawSettings() {
    if (ImGui::CollapsingHeader("Settings", ImGuiTreeNodeFlags_DefaultOpen)) {
        // Enable XInput hooks
        bool enable_hooks = g_shared_state->enable_xinput_hooks.load();
        if (ImGui::Checkbox("Enable XInput Hooks", &enable_hooks)) {
            g_shared_state->enable_xinput_hooks.store(enable_hooks);
            display_commanderhooks::InstallXInputHooks();
            SaveSettings();
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
    }
}

void XInputWidget::DrawEventCounters() {
    if (ImGui::CollapsingHeader("Event Counters", ImGuiTreeNodeFlags_DefaultOpen)) {
        uint64_t total_events = g_shared_state->total_events.load();
        uint64_t button_events = g_shared_state->button_events.load();
        uint64_t stick_events = g_shared_state->stick_events.load();
        uint64_t trigger_events = g_shared_state->trigger_events.load();

        ImGui::Text("Total Events: %llu", total_events);
        ImGui::Text("Button Events: %llu", button_events);
        ImGui::Text("Stick Events: %llu", stick_events);
        ImGui::Text("Trigger Events: %llu", trigger_events);

        // Reset button
        if (ImGui::Button("Reset Counters")) {
            g_shared_state->total_events.store(0);
            g_shared_state->button_events.store(0);
            g_shared_state->stick_events.store(0);
            g_shared_state->trigger_events.store(0);
        }
    }
}

void XInputWidget::DrawVibrationTest() {
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
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f),
                           "Note: Vibration will continue until stopped or controller disconnects");
    }
}

void XInputWidget::DrawControllerSelector() {
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
}

void XInputWidget::DrawControllerState() {
    if (selected_controller_ < 0 || selected_controller_ >= XUSER_MAX_COUNT) {
        ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "Invalid controller selected");
        return;
    }

    // Get controller state
    const XINPUT_STATE &state = g_shared_state->controller_states[selected_controller_];
    bool connected = g_shared_state->controller_connected[selected_controller_];

    if (!connected) {
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Controller %d not connected", selected_controller_);
        return;
    }

    // Draw controller info
    ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "Controller %d - Connected", selected_controller_);
    ImGui::Text("Packet Number: %lu", state.dwPacketNumber);

    // Debug: Show raw button state
    ImGui::Text("Raw Button State: 0x%04X", state.Gamepad.wButtons);
    ImGui::Text("Guide Button Constant: 0x%04X", XINPUT_GAMEPAD_GUIDE);

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
            {XINPUT_GAMEPAD_BACK, "Back"},
            {XINPUT_GAMEPAD_START, "Start"},
            {XINPUT_GAMEPAD_GUIDE, "Guide"},
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

                // Special styling for Guide button
                if (buttons[i].mask == XINPUT_GAMEPAD_GUIDE) {
                    ImGui::PushStyleColor(ImGuiCol_Button,
                                          pressed1 ? ImVec4(1.0f, 0.8f, 0.0f, 1.0f) : ImVec4(0.5f, 0.4f, 0.0f, 1.0f));
                } else {
                    ImGui::PushStyleColor(ImGuiCol_Button,
                                          pressed1 ? ImVec4(0.0f, 1.0f, 0.0f, 1.0f) : ImVec4(0.3f, 0.3f, 0.3f, 1.0f));
                }
                ImGui::Button(buttons[i].name, ImVec2(60, 30));
                ImGui::PopStyleColor();

                ImGui::SameLine();

                // Special styling for Guide button
                if (buttons[i + 1].mask == XINPUT_GAMEPAD_GUIDE) {
                    ImGui::PushStyleColor(ImGuiCol_Button,
                                          pressed2 ? ImVec4(1.0f, 0.8f, 0.0f, 1.0f) : ImVec4(0.5f, 0.4f, 0.0f, 1.0f));
                } else {
                    ImGui::PushStyleColor(ImGuiCol_Button,
                                          pressed2 ? ImVec4(0.0f, 1.0f, 0.0f, 1.0f) : ImVec4(0.3f, 0.3f, 0.3f, 1.0f));
                }
                ImGui::Button(buttons[i + 1].name, ImVec2(60, 30));
                ImGui::PopStyleColor();
            } else {
                // Single button on last row
                bool pressed = IsButtonPressed(gamepad.wButtons, buttons[i].mask);

                // Special styling for Guide button
                if (buttons[i].mask == XINPUT_GAMEPAD_GUIDE) {
                    ImGui::PushStyleColor(ImGuiCol_Button,
                                          pressed ? ImVec4(1.0f, 0.8f, 0.0f, 1.0f) : ImVec4(0.5f, 0.4f, 0.0f, 1.0f));
                } else {
                    ImGui::PushStyleColor(ImGuiCol_Button,
                                          pressed ? ImVec4(0.0f, 1.0f, 0.0f, 1.0f) : ImVec4(0.3f, 0.3f, 0.3f, 1.0f));
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

        // Apply final processing (deadzone + max_input mapping + min_output mapping + clamp)
        float lx_final = ProcessStickInput(lx, left_deadzone, left_max_input, left_min_output);
        float ly_final = ProcessStickInput(ly, left_deadzone, left_max_input, left_min_output);

        ImGui::Text("X: %.3f (Final: %.3f, Raw: %d)", lx, lx_final, gamepad.sThumbLX);
        ImGui::Text("Y: %.3f (Final: %.3f, Raw: %d)", ly, ly_final, gamepad.sThumbLY);

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

        // Apply final processing (deadzone + max_input mapping + min_output mapping + clamp)
        float rx_final = ProcessStickInput(rx, right_deadzone, right_max_input, right_min_output);
        float ry_final = ProcessStickInput(ry, right_deadzone, right_max_input, right_min_output);

        ImGui::Text("X: %.3f (Final: %.3f, Raw: %d)", rx, rx_final, gamepad.sThumbRX);
        ImGui::Text("Y: %.3f (Final: %.3f, Raw: %d)", ry, ry_final, gamepad.sThumbRY);

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
        ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "Visual representation of how stick input is processed");
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

            // For visualization, we want to show the actual curve behavior
            // Deadzone region should show 0, not min_output
            left_curve_y[i] = ProcessStickInput(input_values[i], left_deadzone, left_max_input, left_min_output);

            right_curve_y[i] = ProcessStickInput(input_values[i], right_deadzone, right_max_input, right_min_output);

            left_curve_x[i] = static_cast<float>(i);
            right_curve_x[i] = static_cast<float>(i);
        }

        // Left stick curve
        ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "Left Stick Input/Output Curve");
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
        ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "Right Stick Input/Output Curve");
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
        ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "Legend:");
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "Yellow = Deadzone (Vertical)");
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0f, 0.0f, 1.0f, 1.0f), "Magenta = Max Input (Vertical)");
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.0f, 1.0f, 1.0f, 1.0f), "Cyan = Min Output (Horizontal)");

        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "X-axis: Input (0.0 to 1.0) - Positive side only");
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Y-axis: Output (-1.0 to 1.0)");
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
            ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Battery information not available");
            return;
        }

        const XINPUT_BATTERY_INFORMATION &battery = g_shared_state->battery_info[controller_index];

        // Battery type
        std::string battery_type_str;
        ImVec4 type_color(1.0f, 1.0f, 1.0f, 1.0f);

        switch (battery.BatteryType) {
        case BATTERY_TYPE_DISCONNECTED:
            battery_type_str = "Disconnected";
            type_color = ImVec4(0.7f, 0.7f, 0.7f, 1.0f);
            break;
        case BATTERY_TYPE_WIRED:
            battery_type_str = "Wired (No Battery)";
            type_color = ImVec4(0.5f, 0.8f, 1.0f, 1.0f);
            break;
        case BATTERY_TYPE_ALKALINE:
            battery_type_str = "Alkaline Battery";
            type_color = ImVec4(1.0f, 1.0f, 0.0f, 1.0f);
            break;
        case BATTERY_TYPE_NIMH:
            battery_type_str = "NiMH Battery";
            type_color = ImVec4(0.0f, 1.0f, 0.0f, 1.0f);
            break;
        case BATTERY_TYPE_UNKNOWN:
            battery_type_str = "Unknown Battery Type";
            type_color = ImVec4(0.7f, 0.7f, 0.7f, 1.0f);
            break;
        default:
            battery_type_str = "Unknown";
            type_color = ImVec4(0.7f, 0.7f, 0.7f, 1.0f);
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
                level_color = ImVec4(1.0f, 0.0f, 0.0f, 1.0f);
                level_progress = 0.0f;
                break;
            case BATTERY_LEVEL_LOW:
                level_str = "Low";
                level_color = ImVec4(1.0f, 0.5f, 0.0f, 1.0f);
                level_progress = 0.25f;
                break;
            case BATTERY_LEVEL_MEDIUM:
                level_str = "Medium";
                level_color = ImVec4(1.0f, 1.0f, 0.0f, 1.0f);
                level_progress = 0.5f;
                break;
            case BATTERY_LEVEL_FULL:
                level_str = "Full";
                level_color = ImVec4(0.0f, 1.0f, 0.0f, 1.0f);
                level_progress = 1.0f;
                break;
            default:
                level_str = "Unknown";
                level_color = ImVec4(0.7f, 0.7f, 0.7f, 1.0f);
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
            ImGui::TextColored(ImVec4(0.5f, 0.8f, 1.0f, 1.0f), "No battery level (Wired device)");
        } else {
            ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Battery level not available");
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
        return "Back";
    case XINPUT_GAMEPAD_START:
        return "Start";
    case XINPUT_GAMEPAD_GUIDE:
        return "Guide";
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

    bool connected = g_shared_state->controller_connected[controller_index];
    return connected ? "Connected" : "Disconnected";
}

bool XInputWidget::IsButtonPressed(WORD buttons, WORD button) const { return (buttons & button) != 0; }

void XInputWidget::LoadSettings() {
    // Load enable XInput hooks setting
    bool enable_hooks;
    if (reshade::get_config_value(nullptr, "DisplayCommander.XInputWidget", "EnableXInputHooks", enable_hooks)) {
        g_shared_state->enable_xinput_hooks.store(enable_hooks);
    }

    // Load swap A/B buttons setting
    bool swap_buttons;
    if (reshade::get_config_value(nullptr, "DisplayCommander.XInputWidget", "SwapABButtons", swap_buttons)) {
        g_shared_state->swap_a_b_buttons.store(swap_buttons);
    }

    // Load left stick sensitivity setting
    float left_max_input;
    if (reshade::get_config_value(nullptr, "DisplayCommander.XInputWidget", "LeftStickSensitivity", left_max_input)) {
        g_shared_state->left_stick_max_input.store(left_max_input);
    }

    // Load right stick sensitivity setting
    float right_max_input;
    if (reshade::get_config_value(nullptr, "DisplayCommander.XInputWidget", "RightStickSensitivity", right_max_input)) {
        g_shared_state->right_stick_max_input.store(right_max_input);
    }

    // Load left stick min input setting
    float left_deadzone;
    if (reshade::get_config_value(nullptr, "DisplayCommander.XInputWidget", "LeftStickMinInput", left_deadzone)) {
        g_shared_state->left_stick_deadzone.store(left_deadzone);
    }

    // Load right stick min input setting
    float right_deadzone;
    if (reshade::get_config_value(nullptr, "DisplayCommander.XInputWidget", "RightStickMinInput", right_deadzone)) {
        g_shared_state->right_stick_deadzone.store(right_deadzone);
    }

    // Load left stick max output setting
    float left_min_output;
    if (reshade::get_config_value(nullptr, "DisplayCommander.XInputWidget", "LeftStickMaxOutput", left_min_output)) {
        g_shared_state->left_stick_min_output.store(left_min_output);
    }

    // Load right stick max output setting
    float right_min_output;
    if (reshade::get_config_value(nullptr, "DisplayCommander.XInputWidget", "RightStickMaxOutput", right_min_output)) {
        g_shared_state->right_stick_min_output.store(right_min_output);
    }
}

void XInputWidget::SaveSettings() {
    // Save enable XInput hooks setting
    reshade::set_config_value(nullptr, "DisplayCommander.XInputWidget", "EnableXInputHooks",
                              g_shared_state->enable_xinput_hooks.load());

    // Save swap A/B buttons setting
    reshade::set_config_value(nullptr, "DisplayCommander.XInputWidget", "SwapABButtons",
                              g_shared_state->swap_a_b_buttons.load());

    // Save left stick sensitivity setting
    reshade::set_config_value(nullptr, "DisplayCommander.XInputWidget", "LeftStickSensitivity",
                              g_shared_state->left_stick_max_input.load());

    // Save right stick sensitivity setting
    reshade::set_config_value(nullptr, "DisplayCommander.XInputWidget", "RightStickSensitivity",
                              g_shared_state->right_stick_max_input.load());

    // Save left stick min input setting
    reshade::set_config_value(nullptr, "DisplayCommander.XInputWidget", "LeftStickMinInput",
                              g_shared_state->left_stick_deadzone.load());

    // Save right stick min input setting
    reshade::set_config_value(nullptr, "DisplayCommander.XInputWidget", "RightStickMinInput",
                              g_shared_state->right_stick_deadzone.load());

    // Save left stick max output setting
    reshade::set_config_value(nullptr, "DisplayCommander.XInputWidget", "LeftStickMaxOutput",
                              g_shared_state->left_stick_min_output.load());

    // Save right stick max output setting
    reshade::set_config_value(nullptr, "DisplayCommander.XInputWidget", "RightStickMaxOutput",
                              g_shared_state->right_stick_min_output.load());
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
    if (g_xinput_widget) {
        g_xinput_widget->OnDraw();
    }
}

// Global functions for hooks to use
void UpdateXInputState(DWORD user_index, const XINPUT_STATE *state) {
    auto shared_state = XInputWidget::GetSharedState();
    if (!shared_state || user_index >= XUSER_MAX_COUNT || !state) {
        return;
    }

    // Thread-safe update
    while (shared_state->is_updating.exchange(true)) {
        // Wait for other thread to finish
        std::this_thread::sleep_for(std::chrono::microseconds(1));
    }

    // Update controller state
    shared_state->controller_states[user_index] = *state;
    shared_state->controller_connected[user_index] = true;
    shared_state->last_packet_numbers[user_index] = state->dwPacketNumber;
    shared_state->last_update_times[user_index] = GetOriginalTickCount64();

    // Increment event counters
    shared_state->total_events.fetch_add(1);

    shared_state->is_updating.store(false);
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

// Chord detection functions
void XInputWidget::DrawChordSettings() {
    if (ImGui::CollapsingHeader("Chord Detection", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Text("Button combinations that trigger actions:");
        ImGui::Spacing();

        if (g_shared_state->chords.empty()) {
            if (ImGui::Button("Initialize Default Chords")) {
                InitializeDefaultChords();
            }
        } else {
            for (size_t i = 0; i < g_shared_state->chords.size(); ++i) {
                XInputSharedState::Chord &chord = g_shared_state->chords[i];

                ImGui::PushID(static_cast<int>(i));

                // Enable/disable checkbox
                ImGui::Checkbox("##enabled", &chord.enabled);
                ImGui::SameLine();

                // Chord name and buttons
                std::string button_names = GetChordButtonNames(chord.buttons);
                ImGui::Text("%s: %s", chord.name.c_str(), button_names.c_str());

                // Show current state
                if (chord.is_pressed.load()) {
                    ImGui::SameLine();
                    ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "PRESSED");
                }

                // Action description
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "(%s)", chord.action.c_str());

                ImGui::PopID();
            }

            ImGui::Spacing();
            if (ImGui::Button("Reset to Defaults")) {
                g_shared_state->chords.clear();
                InitializeDefaultChords();
            }
        }
    }
}

void XInputWidget::InitializeDefaultChords() {
    g_shared_state->chords.clear();

    // Guide + Back = Screenshot
    XInputSharedState::Chord screenshot_chord;
    screenshot_chord.buttons = XINPUT_GAMEPAD_GUIDE | XINPUT_GAMEPAD_BACK;
    screenshot_chord.name = "Screenshot";
    screenshot_chord.action = "Take screenshot";
    screenshot_chord.enabled = true;
    g_shared_state->chords.push_back(screenshot_chord);

    // Guide + Start = Toggle UI (placeholder)
    XInputSharedState::Chord toggle_ui_chord;
    toggle_ui_chord.buttons = XINPUT_GAMEPAD_GUIDE | XINPUT_GAMEPAD_START;
    toggle_ui_chord.name = "Toggle UI";
    toggle_ui_chord.action = "Toggle ReShade UI";
    toggle_ui_chord.enabled = true;
    g_shared_state->chords.push_back(toggle_ui_chord);

    // Guide + A = Vibration Test (placeholder)
    XInputSharedState::Chord vibration_chord;
    vibration_chord.buttons = XINPUT_GAMEPAD_GUIDE | XINPUT_GAMEPAD_A;
    vibration_chord.name = "Vibration Test";
    vibration_chord.action = "Test controller vibration";
    vibration_chord.enabled = true;
    g_shared_state->chords.push_back(vibration_chord);
}

void XInputWidget::ProcessChordDetection(DWORD user_index, WORD button_state) {
    if (!g_shared_state)
        return;

    // Update current button state
    g_shared_state->current_button_state.store(button_state);

    // Check if any chord is currently pressed
    bool any_chord_pressed = false;

    // Check each chord
    for (XInputSharedState::Chord &chord : g_shared_state->chords) {
        if (!chord.enabled)
            continue;

        bool was_pressed = chord.is_pressed.load();
        bool is_pressed = (button_state & chord.buttons) == chord.buttons;

        if (is_pressed && !was_pressed) {
            // Chord just pressed
            chord.is_pressed.store(true);
            chord.last_press_time.store(GetOriginalTickCount64());
            ExecuteChordAction(chord, user_index);
            LogInfo("XXX Chord '%s' pressed on controller %lu", chord.name.c_str(), user_index);
            any_chord_pressed = true;
        } else if (!is_pressed && was_pressed) {
            // Chord just released
            chord.is_pressed.store(false);
            LogInfo("XXX Chord '%s' released on controller %lu", chord.name.c_str(), user_index);
        } else if (is_pressed) {
            // Chord is still pressed
            any_chord_pressed = true;
        }
    }

    // Update input suppression state
    g_shared_state->suppress_input.store(any_chord_pressed);
}

void XInputWidget::ExecuteChordAction(const XInputSharedState::Chord &chord, DWORD user_index) {
    if (chord.action == "Take screenshot") {
        // Take screenshot using ReShade API
        LogInfo("XXX Taking screenshot via chord detection");

        // Get the ReShade runtime instance
        reshade::api::effect_runtime *runtime = g_reshade_runtime.load();

        if (runtime != nullptr) {
            // Set the screenshot trigger flag - this will be handled in the present event
            g_shared_state->trigger_screenshot.store(true);
            LogInfo("XXX Screenshot triggered via XInput chord detection");
        } else {
            LogError("XXX ReShade runtime not available for screenshot");
        }
    } else if (chord.action == "Toggle ReShade UI") {
        // Toggle ReShade UI using ReShade's built-in API
        LogInfo("XXX Toggling ReShade UI via chord detection");

        // Get the ReShade runtime instance
        reshade::api::effect_runtime *runtime = g_reshade_runtime.load();

        if (runtime != nullptr) {
            try {
                // Get current UI state and toggle it
                bool current_state = g_shared_state->ui_overlay_open.load();
                bool new_state = !current_state;

                // Use ReShade's open_overlay method to set the new state
                bool success = runtime->open_overlay(new_state, reshade::api::input_source::gamepad);

                if (success) {
                    // Update our tracked state
                    g_shared_state->ui_overlay_open.store(new_state);
                    LogInfo("XXX ReShade UI toggled via chord detection (%s)", new_state ? "opened" : "closed");
                } else {
                    LogError("XXX Failed to toggle ReShade UI via chord detection");
                }

            } catch (const std::exception &e) {
                LogError("XXX Exception toggling ReShade UI: %s", e.what());
            } catch (...) {
                LogError("XXX Unknown exception toggling ReShade UI");
            }
        } else {
            LogError("XXX ReShade runtime not available for UI toggle");
        }
    } else if (chord.action == "Test controller vibration") {
        // Test vibration on the controller that triggered the chord
        XINPUT_VIBRATION vibration = {};
        vibration.wLeftMotorSpeed = FloatToShort(1.0f); // Medium intensity
        vibration.wRightMotorSpeed = FloatToShort(1.0f);

        DWORD result = display_commanderhooks::XInputSetState_Direct ? display_commanderhooks::XInputSetState_Direct(user_index, &vibration) : ERROR_DEVICE_NOT_CONNECTED;
        if (result == ERROR_SUCCESS) {
            LogInfo("XXX Vibration test triggered via chord on controller %lu", user_index);
        } else {
            LogError("XXX Failed to trigger vibration via chord on controller %lu, error: %lu", user_index, result);
        }
    }
}

std::string XInputWidget::GetChordButtonNames(WORD buttons) const {
    std::vector<std::string> names;

    if (buttons & XINPUT_GAMEPAD_A)
        names.push_back("A");
    if (buttons & XINPUT_GAMEPAD_B)
        names.push_back("B");
    if (buttons & XINPUT_GAMEPAD_X)
        names.push_back("X");
    if (buttons & XINPUT_GAMEPAD_Y)
        names.push_back("Y");
    if (buttons & XINPUT_GAMEPAD_LEFT_SHOULDER)
        names.push_back("LB");
    if (buttons & XINPUT_GAMEPAD_RIGHT_SHOULDER)
        names.push_back("RB");
    if (buttons & XINPUT_GAMEPAD_BACK)
        names.push_back("Back");
    if (buttons & XINPUT_GAMEPAD_START)
        names.push_back("Start");
    if (buttons & XINPUT_GAMEPAD_GUIDE)
        names.push_back("Guide");
    if (buttons & XINPUT_GAMEPAD_LEFT_THUMB)
        names.push_back("LS");
    if (buttons & XINPUT_GAMEPAD_RIGHT_THUMB)
        names.push_back("RS");
    if (buttons & XINPUT_GAMEPAD_DPAD_UP)
        names.push_back("D-Up");
    if (buttons & XINPUT_GAMEPAD_DPAD_DOWN)
        names.push_back("D-Down");
    if (buttons & XINPUT_GAMEPAD_DPAD_LEFT)
        names.push_back("D-Left");
    if (buttons & XINPUT_GAMEPAD_DPAD_RIGHT)
        names.push_back("D-Right");

    std::string result;
    for (size_t i = 0; i < names.size(); ++i) {
        if (i > 0)
            result += " + ";
        result += names[i];
    }

    return result;
}

// Global function for hooks to use
void ProcessChordDetection(DWORD user_index, WORD button_state) {
    auto shared_state = XInputWidget::GetSharedState();
    if (!shared_state)
        return;

    // Check if any chord is currently pressed
    bool any_chord_pressed = false;

    // Check each chord
    for (XInputSharedState::Chord &chord : shared_state->chords) {
        if (!chord.enabled)
            continue;

        bool was_pressed = chord.is_pressed.load();
        bool is_pressed = (button_state & chord.buttons) == chord.buttons;

        if (is_pressed && !was_pressed) {
            // Chord just pressed
            chord.is_pressed.store(true);
            chord.last_press_time.store(GetOriginalTickCount64());

            // Execute action
            if (chord.action == "Take screenshot") {
                LogInfo("XXX Taking screenshot via chord detection");

                // Get the ReShade runtime instance
                reshade::api::effect_runtime *runtime = g_reshade_runtime.load();

                if (runtime != nullptr) {
                    // Set the screenshot trigger flag - this will be handled in the present event
                    shared_state->trigger_screenshot.store(true);
                    LogInfo("XXX Screenshot triggered via XInput chord detection");
                } else {
                    LogError("XXX ReShade runtime not available for screenshot");
                }
            } else if (chord.action == "Toggle ReShade UI") {
                // Toggle ReShade UI using ReShade's built-in API
                LogInfo("XXX Toggling ReShade UI via chord detection");

                // Get the ReShade runtime instance
                reshade::api::effect_runtime *runtime = g_reshade_runtime.load();

                if (runtime != nullptr) {
                    try {
                        // Get current UI state and toggle it
                        bool current_state = shared_state->ui_overlay_open.load();
                        bool new_state = !current_state;

                        // Use ReShade's open_overlay method to set the new state
                        bool success = runtime->open_overlay(new_state, reshade::api::input_source::gamepad);

                        if (success) {
                            // Update our tracked state
                            shared_state->ui_overlay_open.store(new_state);
                            LogInfo("XXX ReShade UI toggled via chord detection (%s)", new_state ? "opened" : "closed");
                        } else {
                            LogError("XXX Failed to toggle ReShade UI via chord detection");
                        }

                    } catch (const std::exception &e) {
                        LogError("XXX Exception toggling ReShade UI: %s", e.what());
                    } catch (...) {
                        LogError("XXX Unknown exception toggling ReShade UI");
                    }
                } else {
                    LogError("XXX ReShade runtime not available for UI toggle");
                }
            } else if (chord.action == "Test controller vibration") {
                XINPUT_VIBRATION vibration = {};
                vibration.wLeftMotorSpeed = FloatToShort(1.0f);
                vibration.wRightMotorSpeed = FloatToShort(1.0f);

                DWORD result = display_commanderhooks::XInputSetState_Direct ? display_commanderhooks::XInputSetState_Direct(user_index, &vibration) : ERROR_DEVICE_NOT_CONNECTED;
                if (result == ERROR_SUCCESS) {
                    LogInfo("XXX Vibration test triggered via chord on controller %lu", user_index);
                } else {
                    LogError("XXX Failed to trigger vibration via chord on controller %lu, error: %lu", user_index,
                             result);
                }
            }

            LogInfo("XXX Chord '%s' pressed on controller %lu", chord.name.c_str(), user_index);
            any_chord_pressed = true;
        } else if (!is_pressed && was_pressed) {
            // Chord just released
            chord.is_pressed.store(false);
            LogInfo("XXX Chord '%s' released on controller %lu", chord.name.c_str(), user_index);
        } else if (is_pressed) {
            // Chord is still pressed
            any_chord_pressed = true;
        }
    }

    // Update input suppression state
    shared_state->suppress_input.store(any_chord_pressed);
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
            reshade::api::effect_runtime *runtime = g_reshade_runtime.load();

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

} // namespace display_commander::widgets::xinput_widget
