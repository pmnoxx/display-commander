#include "xinput_widget.hpp"
#include "../../utils.hpp"
#include <deps/imgui/imgui.h>
#include <include/reshade.hpp>
#include <thread>
#include <chrono>

namespace display_commander::widgets::xinput_widget {

// Global shared state
std::shared_ptr<XInputSharedState> XInputWidget::g_shared_state = nullptr;

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
        }
    }
}

void XInputWidget::Initialize() {
    if (is_initialized_) return;

    LogInfo("XInputWidget::Initialize() - Starting XInput widget initialization");

    // Load settings
    LoadSettings();

    is_initialized_ = true;
    LogInfo("XInputWidget::Initialize() - XInput widget initialization complete");
}

void XInputWidget::Cleanup() {
    if (!is_initialized_) return;

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

    // Draw controller selector
    DrawControllerSelector();
    ImGui::Spacing();

    // Draw selected controller state
    DrawControllerState();
}

void XInputWidget::DrawSettings() {
    if (ImGui::CollapsingHeader("Settings", ImGuiTreeNodeFlags_DefaultOpen)) {
        // Swap A/B buttons
        bool swap_buttons = g_shared_state->swap_a_b_buttons.load();
        if (ImGui::Checkbox("Swap A/B Buttons", &swap_buttons)) {
            g_shared_state->swap_a_b_buttons.store(swap_buttons);
            SaveSettings();
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Swap the A and B button mappings");
        }

        // Deadzone setting
        float deadzone = g_shared_state->deadzone.load();
        if (ImGui::SliderFloat("Deadzone", &deadzone, 0.0f, 1.0f, "%.3f")) {
            g_shared_state->deadzone.store(deadzone);
            SaveSettings();
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Deadzone for analog sticks (0.0 = no deadzone, 1.0 = full deadzone)");
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
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Note: Vibration will continue until stopped or controller disconnects");
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
    const XINPUT_STATE& state = g_shared_state->controller_states[selected_controller_];
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
        uint64_t now = GetTickCount64();
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
}

void XInputWidget::DrawButtonStates(const XINPUT_GAMEPAD& gamepad) {
    if (ImGui::CollapsingHeader("Buttons", ImGuiTreeNodeFlags_DefaultOpen)) {
        // Create a grid of buttons
        const struct {
            WORD mask;
            const char* name;
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
                    ImGui::PushStyleColor(ImGuiCol_Button, pressed1 ? ImVec4(1.0f, 0.8f, 0.0f, 1.0f) : ImVec4(0.5f, 0.4f, 0.0f, 1.0f));
                } else {
                    ImGui::PushStyleColor(ImGuiCol_Button, pressed1 ? ImVec4(0.0f, 1.0f, 0.0f, 1.0f) : ImVec4(0.3f, 0.3f, 0.3f, 1.0f));
                }
                ImGui::Button(buttons[i].name, ImVec2(60, 30));
                ImGui::PopStyleColor();

                ImGui::SameLine();

                // Special styling for Guide button
                if (buttons[i + 1].mask == XINPUT_GAMEPAD_GUIDE) {
                    ImGui::PushStyleColor(ImGuiCol_Button, pressed2 ? ImVec4(1.0f, 0.8f, 0.0f, 1.0f) : ImVec4(0.5f, 0.4f, 0.0f, 1.0f));
                } else {
                    ImGui::PushStyleColor(ImGuiCol_Button, pressed2 ? ImVec4(0.0f, 1.0f, 0.0f, 1.0f) : ImVec4(0.3f, 0.3f, 0.3f, 1.0f));
                }
                ImGui::Button(buttons[i + 1].name, ImVec2(60, 30));
                ImGui::PopStyleColor();
            } else {
                // Single button on last row
                bool pressed = IsButtonPressed(gamepad.wButtons, buttons[i].mask);

                // Special styling for Guide button
                if (buttons[i].mask == XINPUT_GAMEPAD_GUIDE) {
                    ImGui::PushStyleColor(ImGuiCol_Button, pressed ? ImVec4(1.0f, 0.8f, 0.0f, 1.0f) : ImVec4(0.5f, 0.4f, 0.0f, 1.0f));
                } else {
                    ImGui::PushStyleColor(ImGuiCol_Button, pressed ? ImVec4(0.0f, 1.0f, 0.0f, 1.0f) : ImVec4(0.3f, 0.3f, 0.3f, 1.0f));
                }
                ImGui::Button(buttons[i].name, ImVec2(60, 30));
                ImGui::PopStyleColor();
            }
        }
    }
}

void XInputWidget::DrawStickStates(const XINPUT_GAMEPAD& gamepad) {
    if (ImGui::CollapsingHeader("Analog Sticks", ImGuiTreeNodeFlags_DefaultOpen)) {
        float deadzone = g_shared_state->deadzone.load();

        // Left stick
        ImGui::Text("Left Stick:");
        float lx = ApplyDeadzone(static_cast<float>(gamepad.sThumbLX) / 32767.0f, deadzone);
        float ly = ApplyDeadzone(static_cast<float>(gamepad.sThumbLY) / 32767.0f, deadzone);

        ImGui::Text("X: %.3f (Raw: %d)", lx, gamepad.sThumbLX);
        ImGui::Text("Y: %.3f (Raw: %d)", ly, gamepad.sThumbLY);

        // Visual representation
        ImGui::Text("Position:");
        ImVec2 canvas_pos = ImGui::GetCursorScreenPos();
        ImVec2 canvas_size = ImVec2(100, 100);
        ImDrawList* draw_list = ImGui::GetWindowDrawList();

        // Draw circle
        ImVec2 center = ImVec2(canvas_pos.x + canvas_size.x * 0.5f, canvas_pos.y + canvas_size.y * 0.5f);
        draw_list->AddCircle(center, canvas_size.x * 0.4f, ImColor(100, 100, 100, 255), 32, 2.0f);

        // Draw crosshairs
        draw_list->AddLine(ImVec2(canvas_pos.x, center.y), ImVec2(canvas_pos.x + canvas_size.x, center.y), ImColor(100, 100, 100, 255), 1.0f);
        draw_list->AddLine(ImVec2(center.x, canvas_pos.y), ImVec2(center.x, canvas_pos.y + canvas_size.y), ImColor(100, 100, 100, 255), 1.0f);

        // Draw stick position
        ImVec2 stick_pos = ImVec2(center.x + lx * canvas_size.x * 0.4f, center.y - ly * canvas_size.y * 0.4f);
        draw_list->AddCircleFilled(stick_pos, 5.0f, ImColor(0, 255, 0, 255));

        ImGui::Dummy(canvas_size);

        // Right stick
        ImGui::Text("Right Stick:");
        float rx = ApplyDeadzone(static_cast<float>(gamepad.sThumbRX) / 32767.0f, deadzone);
        float ry = ApplyDeadzone(static_cast<float>(gamepad.sThumbRY) / 32767.0f, deadzone);

        ImGui::Text("X: %.3f (Raw: %d)", rx, gamepad.sThumbRX);
        ImGui::Text("Y: %.3f (Raw: %d)", ry, gamepad.sThumbRY);

        // Visual representation for right stick
        ImGui::Text("Position:");
        canvas_pos = ImGui::GetCursorScreenPos();
        draw_list = ImGui::GetWindowDrawList();

        // Draw circle
        center = ImVec2(canvas_pos.x + canvas_size.x * 0.5f, canvas_pos.y + canvas_size.y * 0.5f);
        draw_list->AddCircle(center, canvas_size.x * 0.4f, ImColor(100, 100, 100, 255), 32, 2.0f);

        // Draw crosshairs
        draw_list->AddLine(ImVec2(canvas_pos.x, center.y), ImVec2(canvas_pos.x + canvas_size.x, center.y), ImColor(100, 100, 100, 255), 1.0f);
        draw_list->AddLine(ImVec2(center.x, canvas_pos.y), ImVec2(center.x, canvas_pos.y + canvas_size.y), ImColor(100, 100, 100, 255), 1.0f);

        // Draw stick position
        stick_pos = ImVec2(center.x + rx * canvas_size.x * 0.4f, center.y - ry * canvas_size.y * 0.4f);
        draw_list->AddCircleFilled(stick_pos, 5.0f, ImColor(0, 255, 0, 255));

        ImGui::Dummy(canvas_size);
    }
}

void XInputWidget::DrawTriggerStates(const XINPUT_GAMEPAD& gamepad) {
    if (ImGui::CollapsingHeader("Triggers", ImGuiTreeNodeFlags_DefaultOpen)) {
        // Left trigger
        ImGui::Text("Left Trigger: %u/255 (%.1f%%)",
                   gamepad.bLeftTrigger,
                   (static_cast<float>(gamepad.bLeftTrigger) / 255.0f) * 100.0f);

        // Visual bar for left trigger
        float left_trigger_norm = static_cast<float>(gamepad.bLeftTrigger) / 255.0f;
        ImGui::ProgressBar(left_trigger_norm, ImVec2(-1, 0), "");

        // Right trigger
        ImGui::Text("Right Trigger: %u/255 (%.1f%%)",
                   gamepad.bRightTrigger,
                   (static_cast<float>(gamepad.bRightTrigger) / 255.0f) * 100.0f);

        // Visual bar for right trigger
        float right_trigger_norm = static_cast<float>(gamepad.bRightTrigger) / 255.0f;
        ImGui::ProgressBar(right_trigger_norm, ImVec2(-1, 0), "");
    }
}

std::string XInputWidget::GetButtonName(WORD button) const {
    switch (button) {
        case XINPUT_GAMEPAD_A: return "A";
        case XINPUT_GAMEPAD_B: return "B";
        case XINPUT_GAMEPAD_X: return "X";
        case XINPUT_GAMEPAD_Y: return "Y";
        case XINPUT_GAMEPAD_LEFT_SHOULDER: return "LB";
        case XINPUT_GAMEPAD_RIGHT_SHOULDER: return "RB";
        case XINPUT_GAMEPAD_BACK: return "Back";
        case XINPUT_GAMEPAD_START: return "Start";
        case XINPUT_GAMEPAD_GUIDE: return "Guide";
        case XINPUT_GAMEPAD_LEFT_THUMB: return "LS";
        case XINPUT_GAMEPAD_RIGHT_THUMB: return "RS";
        case XINPUT_GAMEPAD_DPAD_UP: return "D-Up";
        case XINPUT_GAMEPAD_DPAD_DOWN: return "D-Down";
        case XINPUT_GAMEPAD_DPAD_LEFT: return "D-Left";
        case XINPUT_GAMEPAD_DPAD_RIGHT: return "D-Right";
        default: return "Unknown";
    }
}

std::string XInputWidget::GetControllerStatus(int controller_index) const {
    if (controller_index < 0 || controller_index >= XUSER_MAX_COUNT) {
        return "Invalid";
    }

    bool connected = g_shared_state->controller_connected[controller_index];
    return connected ? "Connected" : "Disconnected";
}

float XInputWidget::ApplyDeadzone(float value, float deadzone) const {
    if (std::abs(value) < deadzone) {
        return 0.0f;
    }

    // Scale the value to remove the deadzone
    float sign = (value >= 0.0f) ? 1.0f : -1.0f;
    float scaled = (std::abs(value) - deadzone) / (1.0f - deadzone);
    return sign * (scaled < 1.0f ? scaled : 1.0f);
}

bool XInputWidget::IsButtonPressed(WORD buttons, WORD button) const {
    return (buttons & button) != 0;
}

void XInputWidget::LoadSettings() {
    // Load swap A/B buttons setting
    bool swap_buttons;
    if (reshade::get_config_value(nullptr, "DisplayCommander.XInputWidget", "SwapABButtons", swap_buttons)) {
        g_shared_state->swap_a_b_buttons.store(swap_buttons);
    }

    // Load deadzone setting
    float deadzone;
    if (reshade::get_config_value(nullptr, "DisplayCommander.XInputWidget", "Deadzone", deadzone)) {
        g_shared_state->deadzone.store(deadzone);
    }
}

void XInputWidget::SaveSettings() {
    // Save swap A/B buttons setting
    reshade::set_config_value(nullptr, "DisplayCommander.XInputWidget", "SwapABButtons", g_shared_state->swap_a_b_buttons.load());

    // Save deadzone setting
    reshade::set_config_value(nullptr, "DisplayCommander.XInputWidget", "Deadzone", g_shared_state->deadzone.load());
}

std::shared_ptr<XInputSharedState> XInputWidget::GetSharedState() {
    return g_shared_state;
}

// Global functions for integration
void InitializeXInputWidget() {
    if (!g_xinput_widget) {
        g_xinput_widget = std::make_unique<XInputWidget>();
        g_xinput_widget->Initialize();
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
void UpdateXInputState(DWORD user_index, const XINPUT_STATE* state) {
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
    shared_state->last_update_times[user_index] = GetTickCount64();

    // Increment event counters
    shared_state->total_events.fetch_add(1);

    shared_state->is_updating.store(false);
}

void IncrementEventCounter(const std::string& event_type) {
    auto shared_state = XInputWidget::GetSharedState();
    if (!shared_state) return;

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
    vibration.wLeftMotorSpeed = 65535;  // Maximum intensity
    vibration.wRightMotorSpeed = 0;     // Right motor off

    DWORD result = XInputSetState(selected_controller_, &vibration);
    if (result == ERROR_SUCCESS) {
        LogInfo("XInputWidget::TestLeftMotor() - Left motor test started for controller %d", selected_controller_);
    } else {
        LogError("XInputWidget::TestLeftMotor() - Failed to set vibration for controller %d, error: %lu", selected_controller_, result);
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

    DWORD result = XInputSetState(selected_controller_, &vibration);
    if (result == ERROR_SUCCESS) {
        LogInfo("XInputWidget::TestRightMotor() - Right motor test started for controller %d", selected_controller_);
    } else {
        LogError("XInputWidget::TestRightMotor() - Failed to set vibration for controller %d, error: %lu", selected_controller_, result);
    }
}

void XInputWidget::StopVibration() {
    if (selected_controller_ < 0 || selected_controller_ >= XUSER_MAX_COUNT) {
        LogError("XInputWidget::StopVibration() - Invalid controller index: %d", selected_controller_);
        return;
    }

    XINPUT_VIBRATION vibration = {};
    vibration.wLeftMotorSpeed = 0;  // Both motors off
    vibration.wRightMotorSpeed = 0;

    DWORD result = XInputSetState(selected_controller_, &vibration);
    if (result == ERROR_SUCCESS) {
        LogInfo("XInputWidget::StopVibration() - Vibration stopped for controller %d", selected_controller_);
    } else {
        LogError("XInputWidget::StopVibration() - Failed to stop vibration for controller %d, error: %lu", selected_controller_, result);
    }
}

} // namespace display_commander::widgets::xinput_widget
