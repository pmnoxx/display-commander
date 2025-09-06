#include "xinput_tab.hpp"
#include "xinput_controller.hpp"
#include "../addon.hpp"
#include <deps/imgui/imgui.h>
#include <string>
#include <sstream>
#include <iomanip>

namespace display_commander::xinput {

// Tab configuration
static const std::string TAB_NAME = "XInput Controllers";
static const std::string TAB_ID = "xinput_controllers";

// UI state
static float left_motor_strength = 0.0f;
static float right_motor_strength = 0.0f;
static bool show_raw_values = false;
static bool show_toggle_values = false;

// Button names for display
static const std::array<const char*, 20> BUTTON_NAMES = {
    "LX", "LY", "RX", "RY",           // Sticks
    "LT", "RT",                       // Triggers
    "A", "B", "X", "Y",               // Face buttons
    "Start", "Back",                  // System buttons
    "DPad Up", "DPad Down", "DPad Left", "DPad Right", // D-Pad
    "L Thumb", "R Thumb",             // Thumb buttons
    "L Shoulder", "R Shoulder"        // Shoulder buttons
};

void InitXInputTab() {
    LogInfo("Initializing XInput tab");

    // Initialize XInput system
    if (!InitializeXInput()) {
        LogError("Failed to initialize XInput system");
        return;
    }

    // Load settings
    if (!reshade::get_config_value(nullptr, "DisplayCommander", "XInputShowRawValues", show_raw_values)) {
        show_raw_values = false;
    }

    if (!reshade::get_config_value(nullptr, "DisplayCommander", "XInputShowToggleValues", show_toggle_values)) {
        show_toggle_values = false;
    }

    LogInfo("XInput tab initialized successfully");
}

void DrawXInputTab() {
    auto* manager = GetXInputManager();
    if (!manager) {
        ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "XInput system not initialized");
        return;
    }

    // Update controller states
    manager->UpdateControllers();

    // Header
    ImGui::Text("XInput Controller Management");
    ImGui::Separator();

    // Global settings
    if (ImGui::CollapsingHeader("Global Settings", ImGuiTreeNodeFlags_DefaultOpen)) {

        // Display options
        ImGui::Checkbox("Show Raw Values", &show_raw_values);
        ImGui::SameLine();
        ImGui::Checkbox("Show Toggle Values", &show_toggle_values);

        if (ImGui::Button("Save Settings")) {
            reshade::set_config_value(nullptr, "DisplayCommander", "XInputShowRawValues", show_raw_values);
            reshade::set_config_value(nullptr, "DisplayCommander", "XInputShowToggleValues", show_toggle_values);
        }
    }

    ImGui::Spacing();

    // Controller status
    DWORD connected_count = manager->GetConnectedControllerCount();
    ImGui::Text("Connected Controllers: %lu", connected_count);

    if (connected_count == 0) {
        ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "No controllers detected. Connect an XInput-compatible controller.");
        return;
    }

    // Draw each controller
    for (DWORD i = 0; i < 4; ++i) {
        const auto& controller = manager->GetControllerState(i);

        if (!controller.connected) {
            continue;
        }

        std::string controller_title = "Controller " + std::to_string(i);
        if (ImGui::CollapsingHeader(controller_title.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
            // Controller info
            std::string info = manager->GetControllerInfo(i);
            ImGui::Text("%s", info.c_str());

            ImGui::Spacing();

            // Vibration controls
            if (ImGui::CollapsingHeader("Vibration Control", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::PushID(static_cast<int>(i));

                // Left motor
                ImGui::Text("Left Motor (Low Frequency)");
                ImGui::SliderFloat("##LeftMotor", &left_motor_strength, 0.0f, 1.0f, "%.2f");
                ImGui::SameLine();
                if (ImGui::Button("Test##Left")) {
                    manager->SetVibration(i, left_motor_strength, 0.0f);
                }

                // Right motor
                ImGui::Text("Right Motor (High Frequency)");
                ImGui::SliderFloat("##RightMotor", &right_motor_strength, 0.0f, 1.0f, "%.2f");
                ImGui::SameLine();
                if (ImGui::Button("Test##Right")) {
                    manager->SetVibration(i, 0.0f, right_motor_strength);
                }

                // Both motors
                ImGui::Text("Both Motors");
                if (ImGui::Button("Test Both##Both")) {
                    manager->SetVibration(i, left_motor_strength, right_motor_strength);
                }
                ImGui::SameLine();
                if (ImGui::Button("Stop##Stop")) {
                    manager->StopVibration(i);
                    left_motor_strength = 0.0f;
                    right_motor_strength = 0.0f;
                }

                ImGui::PopID();
            }

            ImGui::Spacing();

            // Input values display
            if (show_raw_values || show_toggle_values) {
                if (ImGui::CollapsingHeader("Input Values", ImGuiTreeNodeFlags_DefaultOpen)) {
                    const auto& raw_values = manager->GetRawValues(i);
                    const auto& toggle_values = manager->GetToggleValues(i);

                    // Create table for input values
                    if (ImGui::BeginTable("InputValues", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
                        ImGui::TableSetupColumn("Input");
                        if (show_raw_values) {
                            ImGui::TableSetupColumn("Raw");
                        }
                        if (show_toggle_values) {
                            ImGui::TableSetupColumn("Toggle");
                        }
                        ImGui::TableHeadersRow();

                        for (int j = 0; j < 20; ++j) {
                            ImGui::TableNextRow();

                            // Input name
                            ImGui::TableSetColumnIndex(0);
                            ImGui::Text("%s", BUTTON_NAMES[j]);

                            // Raw values
                            if (show_raw_values) {
                                ImGui::TableSetColumnIndex(1);
                                float raw_val = raw_values[j];

                                // Color code based on value
                                if (raw_val > 0.1f) {
                                    ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "%.3f", raw_val);
                                } else {
                                    ImGui::Text("%.3f", raw_val);
                                }
                            }

                            // Toggle values
                            if (show_toggle_values) {
                                ImGui::TableSetColumnIndex(show_raw_values ? 2 : 1);
                                float toggle_val = toggle_values[j];

                                // Color code based on value
                                if (toggle_val > 0.5f) {
                                    ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "%.3f", toggle_val);
                                } else {
                                    ImGui::Text("%.3f", toggle_val);
                                }
                            }
                        }

                        ImGui::EndTable();
                    }
                }
            }

            // Visual controller representation
            if (ImGui::CollapsingHeader("Visual Controller", ImGuiTreeNodeFlags_DefaultOpen)) {
                DrawControllerVisualization(i, controller);

            }
        }
    }
}

void DrawControllerVisualization(DWORD controller_index, const ControllerState& controller) {
    const auto& raw_values = controller.raw_values;

    // Controller background
    ImGui::Text("Controller Visualization");

    // Create a simple text-based representation
    std::ostringstream oss;

    // D-Pad
    oss << "D-Pad: ";
    if (raw_values[12] > 0.5f) oss << "↑";
    if (raw_values[13] > 0.5f) oss << "↓";
    if (raw_values[14] > 0.5f) oss << "←";
    if (raw_values[15] > 0.5f) oss << "→";
    if (raw_values[12] <= 0.5f && raw_values[13] <= 0.5f &&
        raw_values[14] <= 0.5f && raw_values[15] <= 0.5f) {
        oss << "None";
    }
    oss << "\n";

    // Face buttons
    oss << "Face Buttons: ";
    if (raw_values[6] > 0.5f) oss << "A ";
    if (raw_values[7] > 0.5f) oss << "B ";
    if (raw_values[8] > 0.5f) oss << "X ";
    if (raw_values[9] > 0.5f) oss << "Y ";
    if (raw_values[6] <= 0.5f && raw_values[7] <= 0.5f &&
        raw_values[8] <= 0.5f && raw_values[9] <= 0.5f) {
        oss << "None";
    }
    oss << "\n";

    // Shoulder buttons
    oss << "Shoulders: ";
    if (raw_values[18] > 0.5f) oss << "L ";
    if (raw_values[19] > 0.5f) oss << "R ";
    if (raw_values[18] <= 0.5f && raw_values[19] <= 0.5f) {
        oss << "None";
    }
    oss << "\n";

    // Triggers
    oss << "Triggers: LT=" << std::fixed << std::setprecision(2) << raw_values[4]
        << " RT=" << raw_values[5] << "\n";

    // Sticks
    oss << "Left Stick: X=" << std::fixed << std::setprecision(2) << raw_values[0]
        << " Y=" << raw_values[1] << "\n";
    oss << "Right Stick: X=" << std::fixed << std::setprecision(2) << raw_values[2]
        << " Y=" << raw_values[3] << "\n";

    // System buttons
    oss << "System: ";
    if (raw_values[10] > 0.5f) oss << "Start ";
    if (raw_values[11] > 0.5f) oss << "Back ";
    if (raw_values[10] <= 0.5f && raw_values[11] <= 0.5f) {
        oss << "None";
    }
    oss << "\n";

    // Thumb buttons
    oss << "Thumb: ";
    if (raw_values[16] > 0.5f) oss << "L ";
    if (raw_values[17] > 0.5f) oss << "R ";
    if (raw_values[16] <= 0.5f && raw_values[17] <= 0.5f) {
        oss << "None";
    }

    ImGui::Text("%s", oss.str().c_str());
}

const std::string& GetXInputTabName() {
    return TAB_NAME;
}

const std::string& GetXInputTabId() {
    return TAB_ID;
}

} // namespace display_commander::xinput
