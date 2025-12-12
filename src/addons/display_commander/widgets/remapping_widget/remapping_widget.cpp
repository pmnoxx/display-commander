#include "remapping_widget.hpp"
#include "../../utils.hpp"
#include "../../utils/logging.hpp"
#include "../../config/display_commander_config.hpp"
#include "../../settings/main_tab_settings.hpp"
#include "../../ui/new_ui/settings_wrapper.hpp"

#include <reshade_imgui.hpp>

namespace display_commander::widgets::remapping_widget {
// Global widget instance
std::unique_ptr<RemappingWidget> RemappingWidget::g_remapping_widget = nullptr;

RemappingWidget::RemappingWidget() {
    // Initialize dialog state
    ResetDialogState();
}

void RemappingWidget::Initialize() {
    if (is_initialized_)
        return;

    LogInfo("RemappingWidget::Initialize() - Starting remapping widget initialization");

    // Load settings
    LoadSettings();

    is_initialized_ = true;
    LogInfo("RemappingWidget::Initialize() - Remapping widget initialization complete");
}

void RemappingWidget::Cleanup() {
    if (!is_initialized_)
        return;

    // Save settings
    SaveSettings();

    is_initialized_ = false;
}

void RemappingWidget::OnDraw() {
    if (!is_initialized_) {
        Initialize();
    }

    // Draw the remapping widget UI
    ImGui::TextColored(ImVec4(0.9f, 0.9f, 0.9f, 1.0f), "=== Gamepad Remapping ===");
    ImGui::Spacing();

    // Draw controller selector
    DrawControllerSelector();
    ImGui::Spacing();

    // Draw remapping settings
    DrawRemappingSettings();
    ImGui::Spacing();

    // Draw input method slider
    DrawInputMethodSlider();
    ImGui::Spacing();

    // Draw remapping list
    DrawRemappingList();
    ImGui::Spacing();

    // Draw dialogs
    if (show_add_remap_dialog_) {
        DrawAddRemapDialog();
    }
    if (show_edit_remap_dialog_) {
        DrawEditRemapDialog();
    }
}

void RemappingWidget::DrawControllerSelector() {
    if (ImGui::CollapsingHeader("Controller Selection", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Text("Select Controller:");
        ImGui::SameLine();

        const char *controllers[] = {"Controller 1", "Controller 2", "Controller 3", "Controller 4"};
        if (ImGui::Combo("##Controller", &selected_controller_, controllers, 4)) {
            LogInfo("RemappingWidget::DrawControllerSelector() - Selected controller %d", selected_controller_);
        }

        ImGui::Text("Note: Remappings apply to all controllers");
    }
}

void RemappingWidget::DrawRemappingSettings() {
    if (ImGui::CollapsingHeader("Remapping Settings", ImGuiTreeNodeFlags_DefaultOpen)) {
        auto &remapper = input_remapping::InputRemapper::get_instance();

        // Enable/disable remapping
        bool remapping_enabled = remapper.is_remapping_enabled();
        if (ImGui::Checkbox("Enable Gamepad Remapping", &remapping_enabled)) {
            remapper.set_remapping_enabled(remapping_enabled);
            LogInfo("RemappingWidget::DrawRemappingSettings() - Remapping %s",
                    remapping_enabled ? "enabled" : "disabled");
        }

        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("When enabled, gamepad buttons will be mapped to keyboard inputs");
        }

        // Block gamepad input to game when home button is pressed
        bool block_input_on_home_button = remapper.is_block_input_on_home_button();
        if (ImGui::Checkbox("Block Gamepad Input When Home Button Pressed", &block_input_on_home_button)) {
            remapper.set_block_input_on_home_button(block_input_on_home_button);
            LogInfo("RemappingWidget::DrawRemappingSettings() - Block input on home button %s",
                    block_input_on_home_button ? "enabled" : "disabled");
        }

        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("When enabled, blocks all gamepad input to the game (except home button) while home button is pressed.\n"
                             "This prevents accidental button presses while using shortcuts.\n"
                             "Remapping system will still work normally.");
        }

        ImGui::Spacing();

        // Enable Default Chords checkbox
        if (ui::new_ui::CheckboxSetting(settings::g_mainTabSettings.enable_default_chords, "Enable Default Gamepad Chords")) {
            // Re-initialize default chords if enabled, or remove them if disabled
            if (settings::g_mainTabSettings.enable_default_chords.GetValue()) {
                remapper.add_default_chords();
                LogInfo("Default chords enabled and added");
            } else {
                // Remove only default chords (not user-customized ones)
                remapper.remove_default_chords();
                LogInfo("Default chords disabled and removed");
            }
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Enable default gamepad chords:\n"
                             "- Home + D-Pad Up: Increase Volume\n"
                             "- Home + D-Pad Down: Decrease Volume\n"
                             "- Home + Right Shoulder: Mute/Unmute Audio\n"
                             "- Home + Menu: Toggle Performance Overlay\n"
                             "- Home + View: Take Screenshot\n\n"
                             "These chords are added to the input remapping system and can be customized.");
        }

        // Show statistics
        const auto &remappings = remapper.get_remappings();
        ImGui::Text("Active Remappings: %zu", remappings.size());

        // Quick actions
        ImGui::Spacing();
        if (ImGui::Button("Add New Remapping")) {
            ResetDialogState();
            show_add_remap_dialog_ = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Clear All Remappings")) {
            if (ImGui::IsKeyDown(ImGuiKey_LeftCtrl) || ImGui::IsKeyDown(ImGuiKey_RightCtrl)) {
                remapper.clear_all_remaps();
                LogInfo("RemappingWidget::DrawRemappingSettings() - Cleared all remappings");
            } else {
                ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "Hold Ctrl and click to confirm");
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Reset Counters")) {
            if (ImGui::IsKeyDown(ImGuiKey_LeftCtrl) || ImGui::IsKeyDown(ImGuiKey_RightCtrl)) {
                ResetTriggerCounters();
                LogInfo("RemappingWidget::DrawRemappingSettings() - Reset all trigger counters");
            } else {
                ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "Hold Ctrl and click to confirm");
            }
        }
    }
}

void RemappingWidget::DrawInputMethodSlider() {
    if (ImGui::CollapsingHeader("Input Method Settings", ImGuiTreeNodeFlags_DefaultOpen)) {
        auto &remapper = input_remapping::InputRemapper::get_instance();

        // Get available input methods
        auto methods = input_remapping::get_available_keyboard_input_methods();
        int current_method = static_cast<int>(remapper.get_default_input_method());

        ImGui::Text("Default Input Method:");
        if (ImGui::SliderInt("##InputMethod", &current_method, 0, methods.size() - 1,
                             methods[current_method].c_str())) {
            remapper.set_default_input_method(static_cast<input_remapping::KeyboardInputMethod>(current_method));
            LogInfo("RemappingWidget::DrawInputMethodSlider() - Set input method to %s",
                    methods[current_method].c_str());
        }

        ImGui::TextDisabled("SendInput: Most reliable, works with most applications");
        ImGui::TextDisabled("keybd_event: Legacy method, may not work with some games");
        ImGui::TextDisabled("SendMessage: Sends to active window, may not work with fullscreen games");
        ImGui::TextDisabled("PostMessage: Asynchronous, may not work with some applications");
    }
}

void RemappingWidget::DrawRemappingList() {
    if (ImGui::CollapsingHeader("Current Remappings", ImGuiTreeNodeFlags_DefaultOpen)) {
        auto &remapper = input_remapping::InputRemapper::get_instance();
        const auto &remappings = remapper.get_remappings();

        if (remappings.empty()) {
            ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "No remappings configured");
            ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Click 'Add New Remapping' to get started");
        } else {
            // Create a table for remappings
            if (ImGui::BeginTable("RemappingsTable", 9, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
                ImGui::TableSetupColumn("Gamepad Button");
                ImGui::TableSetupColumn("Remap Type");
                ImGui::TableSetupColumn("Target");
                ImGui::TableSetupColumn("Input Method");
                ImGui::TableSetupColumn("Hold Mode");
                ImGui::TableSetupColumn("Chord Mode");
                ImGui::TableSetupColumn("Trigger Count");
                ImGui::TableSetupColumn("Enabled");
                ImGui::TableSetupColumn("Actions");
                ImGui::TableHeadersRow();

                for (size_t i = 0; i < remappings.size(); ++i) {
                    DrawRemapEntry(remappings[i], static_cast<int>(i));
                }

                ImGui::EndTable();
            }
        }
    }
}

void RemappingWidget::DrawRemapEntry(const input_remapping::ButtonRemap &remap, int index) {
    ImGui::TableNextRow();

    // Gamepad Button
    ImGui::TableNextColumn();
    ImGui::Text("%s", GetGamepadButtonNameFromCode(remap.gamepad_button).c_str());

    // Remap Type
    ImGui::TableNextColumn();
    ImGui::Text("%s", input_remapping::get_remap_type_name(remap.remap_type).c_str());

    // Target (depends on remap type)
    ImGui::TableNextColumn();
    switch (remap.remap_type) {
    case input_remapping::RemapType::Keyboard:
        ImGui::Text("%s", remap.keyboard_name.c_str());
        break;
    case input_remapping::RemapType::Gamepad:
        ImGui::Text("%s", GetGamepadButtonNameFromCode(remap.gamepad_target_button).c_str());
        break;
    case input_remapping::RemapType::Action:
        ImGui::Text("%s", remap.action_name.c_str());
        break;
    default:
        ImGui::Text("Unknown");
        break;
    }

    // Input Method (only for keyboard)
    ImGui::TableNextColumn();
    if (remap.remap_type == input_remapping::RemapType::Keyboard) {
        ImGui::Text("%s", input_remapping::get_keyboard_input_method_name(remap.input_method).c_str());
    } else {
        ImGui::Text("-");
    }

    // Hold Mode
    ImGui::TableNextColumn();
    ImGui::Text("%s", remap.hold_mode ? "Yes" : "No");

    // Chord Mode
    ImGui::TableNextColumn();
    ImGui::Text("%s", remap.chord_mode ? "Yes" : "No");

    // Trigger Count
    ImGui::TableNextColumn();
    ImGui::Text("%llu", remap.trigger_count.load());

    // Enabled
    ImGui::TableNextColumn();
    bool enabled = remap.enabled;
    if (ImGui::Checkbox(("##Enabled" + std::to_string(index)).c_str(), &enabled)) {
        // Update enabled state - need to preserve remap type
        auto &remapper = input_remapping::InputRemapper::get_instance();
        if (remap.remap_type == input_remapping::RemapType::Keyboard) {
            remapper.update_remap_keyboard(remap.gamepad_button, remap.keyboard_vk, remap.keyboard_name, remap.input_method, remap.hold_mode, remap.chord_mode);
        } else if (remap.remap_type == input_remapping::RemapType::Gamepad) {
            remapper.update_remap_gamepad(remap.gamepad_button, remap.gamepad_target_button, remap.hold_mode, remap.chord_mode);
        } else if (remap.remap_type == input_remapping::RemapType::Action) {
            remapper.update_remap_action(remap.gamepad_button, remap.action_name, remap.hold_mode, remap.chord_mode);
        }
        LogInfo("RemappingWidget::DrawRemapEntry() - Toggled remap %d enabled state", index);
    }

    // Actions
    ImGui::TableNextColumn();
    if (ImGui::Button(("Edit##" + std::to_string(index)).c_str())) {
        editing_remap_index_ = index;
        LoadRemapToDialog(remap);
        show_edit_remap_dialog_ = true;
    }
    ImGui::SameLine();
    if (ImGui::Button(("Delete##" + std::to_string(index)).c_str())) {
        auto &remapper = input_remapping::InputRemapper::get_instance();
        remapper.remove_button_remap(remap.gamepad_button);
        LogInfo("RemappingWidget::DrawRemapEntry() - Deleted remap %d", index);
    }
}

void RemappingWidget::DrawAddRemapDialog() {
    ImGui::OpenPopup("Add Remapping");
    if (ImGui::BeginPopupModal("Add Remapping", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Configure new gamepad remapping");
        ImGui::Separator();

        // Gamepad Button Selection
        ImGui::Text("Gamepad Button:");
        const char *gamepad_buttons[] = {"A",          "B",          "X",           "Y",           "D-Pad Up",
                                         "D-Pad Down", "D-Pad Left", "D-Pad Right", "Menu",        "View",
                                         "Home",       "Left Stick", "Right Stick", "Left Bumper", "Right Bumper"};
        if (ImGui::Combo("##GamepadButton", &dialog_state_.selected_gamepad_button, gamepad_buttons, 15)) {
            // Update selection
        }

        // Remap Type Selection
        ImGui::Text("Remap Type:");
        const char *remap_types[] = {"Keyboard", "Gamepad", "Action"};
        if (ImGui::Combo("##RemapType", &dialog_state_.selected_remap_type, remap_types, 3)) {
            // Update selection
        }

        // Conditional fields based on remap type
        if (dialog_state_.selected_remap_type == 0) { // Keyboard
            // Keyboard Key Selection
            ImGui::Text("Keyboard Key:");
            auto keyboard_keys = input_remapping::get_available_keyboard_keys();
            std::vector<const char*> keyboard_keys_cstr;
            for (const auto& key : keyboard_keys) {
                keyboard_keys_cstr.push_back(key.c_str());
            }
            if (ImGui::Combo("##KeyboardKey", &dialog_state_.selected_keyboard_key,
                            keyboard_keys_cstr.data(), static_cast<int>(keyboard_keys_cstr.size()))) {
                // Update selection
            }

            // Input Method Selection
            ImGui::Text("Input Method:");
            const char *input_methods[] = {"SendInput", "keybd_event", "SendMessage", "PostMessage"};
            if (ImGui::Combo("##InputMethod", &dialog_state_.selected_input_method, input_methods, 4)) {
                // Update selection
            }
        } else if (dialog_state_.selected_remap_type == 1) { // Gamepad
            // Target Gamepad Button Selection
            ImGui::Text("Target Gamepad Button:");
            if (ImGui::Combo("##GamepadTargetButton", &dialog_state_.selected_gamepad_target_button, gamepad_buttons, 15)) {
                // Update selection
            }
        } else if (dialog_state_.selected_remap_type == 2) { // Action
            // Action Selection
            ImGui::Text("Action:");
            auto actions = input_remapping::get_available_actions();
            std::vector<const char*> actions_cstr;
            for (const auto& action : actions) {
                actions_cstr.push_back(action.c_str());
            }
            if (ImGui::Combo("##Action", &dialog_state_.selected_action,
                            actions_cstr.data(), static_cast<int>(actions_cstr.size()))) {
                // Update selection
            }
        }

        // Hold Mode
        ImGui::Checkbox("Hold Mode", &dialog_state_.hold_mode);
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("If enabled, the target will be held down while the gamepad button is pressed");
        }

        // Chord Mode
        ImGui::Checkbox("Chord Mode", &dialog_state_.chord_mode);
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("If enabled, the remapping will only work when the Home button is also pressed at the same time");
        }

        // Enabled
        ImGui::Checkbox("Enabled", &dialog_state_.enabled);

        ImGui::Separator();

        // Buttons
        if (ImGui::Button("Add")) {
            // Add the remapping based on type
            WORD gamepad_button = GetGamepadButtonFromIndex(dialog_state_.selected_gamepad_button);
            auto &remapper = input_remapping::InputRemapper::get_instance();

            if (dialog_state_.selected_remap_type == 0) { // Keyboard
                int keyboard_vk = GetKeyboardVkFromIndex(dialog_state_.selected_keyboard_key);
                std::string keyboard_name = GetKeyboardKeyName(dialog_state_.selected_keyboard_key);
                auto input_method = static_cast<input_remapping::KeyboardInputMethod>(dialog_state_.selected_input_method);
                input_remapping::ButtonRemap remap(gamepad_button, keyboard_vk, keyboard_name, dialog_state_.enabled,
                                                   input_method, dialog_state_.hold_mode, dialog_state_.chord_mode);
                remapper.add_button_remap(remap);
            } else if (dialog_state_.selected_remap_type == 1) { // Gamepad
                WORD target_button = GetGamepadButtonFromIndex(dialog_state_.selected_gamepad_target_button);
                input_remapping::ButtonRemap remap(gamepad_button, target_button, dialog_state_.enabled, dialog_state_.hold_mode, dialog_state_.chord_mode);
                remapper.add_button_remap(remap);
            } else if (dialog_state_.selected_remap_type == 2) { // Action
                auto actions = input_remapping::get_available_actions();
                if (dialog_state_.selected_action >= 0 && dialog_state_.selected_action < static_cast<int>(actions.size())) {
                    std::string action_name = actions[dialog_state_.selected_action];
                    input_remapping::ButtonRemap remap(gamepad_button, action_name, dialog_state_.enabled, dialog_state_.hold_mode, dialog_state_.chord_mode);
                    remapper.add_button_remap(remap);
                }
            }

            show_add_remap_dialog_ = false;
            ResetDialogState();
            LogInfo("RemappingWidget::DrawAddRemapDialog() - Added new remapping");
        }

        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            show_add_remap_dialog_ = false;
            ResetDialogState();
        }

        ImGui::EndPopup();
    }
}

void RemappingWidget::DrawEditRemapDialog() {
    ImGui::OpenPopup("Edit Remapping");
    if (ImGui::BeginPopupModal("Edit Remapping", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Edit gamepad remapping");
        ImGui::Separator();

        // Gamepad Button Selection
        ImGui::Text("Gamepad Button:");
        const char *gamepad_buttons[] = {"A",          "B",          "X",           "Y",           "D-Pad Up",
                                         "D-Pad Down", "D-Pad Left", "D-Pad Right", "Menu",        "View",
                                         "Home",       "Left Stick", "Right Stick", "Left Bumper", "Right Bumper"};
        ImGui::Combo("##GamepadButton", &dialog_state_.selected_gamepad_button, gamepad_buttons, 15);

        // Remap Type Selection
        ImGui::Text("Remap Type:");
        const char *remap_types[] = {"Keyboard", "Gamepad", "Action"};
        ImGui::Combo("##RemapType", &dialog_state_.selected_remap_type, remap_types, 3);

        // Conditional fields based on remap type
        if (dialog_state_.selected_remap_type == 0) { // Keyboard
            // Keyboard Key Selection
            ImGui::Text("Keyboard Key:");
            auto keyboard_keys = input_remapping::get_available_keyboard_keys();
            std::vector<const char*> keyboard_keys_cstr;
            for (const auto& key : keyboard_keys) {
                keyboard_keys_cstr.push_back(key.c_str());
            }
            ImGui::Combo("##KeyboardKey", &dialog_state_.selected_keyboard_key,
                        keyboard_keys_cstr.data(), static_cast<int>(keyboard_keys_cstr.size()));

            // Input Method Selection
            ImGui::Text("Input Method:");
            const char *input_methods[] = {"SendInput", "keybd_event", "SendMessage", "PostMessage"};
            ImGui::Combo("##InputMethod", &dialog_state_.selected_input_method, input_methods, 4);
        } else if (dialog_state_.selected_remap_type == 1) { // Gamepad
            // Target Gamepad Button Selection
            ImGui::Text("Target Gamepad Button:");
            ImGui::Combo("##GamepadTargetButton", &dialog_state_.selected_gamepad_target_button, gamepad_buttons, 15);
        } else if (dialog_state_.selected_remap_type == 2) { // Action
            // Action Selection
            ImGui::Text("Action:");
            auto actions = input_remapping::get_available_actions();
            std::vector<const char*> actions_cstr;
            for (const auto& action : actions) {
                actions_cstr.push_back(action.c_str());
            }
            ImGui::Combo("##Action", &dialog_state_.selected_action,
                        actions_cstr.data(), static_cast<int>(actions_cstr.size()));
        }

        // Hold Mode
        ImGui::Checkbox("Hold Mode", &dialog_state_.hold_mode);
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("If enabled, the target will be held down while the gamepad button is pressed");
        }

        // Chord Mode
        ImGui::Checkbox("Chord Mode", &dialog_state_.chord_mode);
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("If enabled, the remapping will only work when the Home button is also pressed at the same time");
        }

        // Enabled
        ImGui::Checkbox("Enabled", &dialog_state_.enabled);

        ImGui::Separator();

        if (ImGui::Button("Save")) {
            // Update the remapping based on type
            WORD gamepad_button = GetGamepadButtonFromIndex(dialog_state_.selected_gamepad_button);
            auto &remapper = input_remapping::InputRemapper::get_instance();

            if (dialog_state_.selected_remap_type == 0) { // Keyboard
                int keyboard_vk = GetKeyboardVkFromIndex(dialog_state_.selected_keyboard_key);
                std::string keyboard_name = GetKeyboardKeyName(dialog_state_.selected_keyboard_key);
                auto input_method = static_cast<input_remapping::KeyboardInputMethod>(dialog_state_.selected_input_method);
                remapper.update_remap_keyboard(gamepad_button, keyboard_vk, keyboard_name, input_method, dialog_state_.hold_mode, dialog_state_.chord_mode);
            } else if (dialog_state_.selected_remap_type == 1) { // Gamepad
                WORD target_button = GetGamepadButtonFromIndex(dialog_state_.selected_gamepad_target_button);
                remapper.update_remap_gamepad(gamepad_button, target_button, dialog_state_.hold_mode, dialog_state_.chord_mode);
            } else if (dialog_state_.selected_remap_type == 2) { // Action
                auto actions = input_remapping::get_available_actions();
                if (dialog_state_.selected_action >= 0 && dialog_state_.selected_action < static_cast<int>(actions.size())) {
                    std::string action_name = actions[dialog_state_.selected_action];
                    remapper.update_remap_action(gamepad_button, action_name, dialog_state_.hold_mode, dialog_state_.chord_mode);
                }
            }

            show_edit_remap_dialog_ = false;
            editing_remap_index_ = -1;
            ResetDialogState();
            LogInfo("RemappingWidget::DrawEditRemapDialog() - Updated remapping");
        }

        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            show_edit_remap_dialog_ = false;
            editing_remap_index_ = -1;
            ResetDialogState();
        }

        ImGui::EndPopup();
    }
}

std::string RemappingWidget::GetGamepadButtonName(int button_index) const {
    auto buttons = input_remapping::get_available_gamepad_buttons();
    if (button_index >= 0 && button_index < static_cast<int>(buttons.size())) {
        return buttons[button_index];
    }
    return "Unknown";
}

std::string RemappingWidget::GetKeyboardKeyName(int key_index) const {
    auto keys = input_remapping::get_available_keyboard_keys();
    if (key_index >= 0 && key_index < static_cast<int>(keys.size())) {
        return keys[key_index];
    }
    return "Unknown";
}

std::string RemappingWidget::GetGamepadButtonNameFromCode(WORD button_code) const {
    switch (button_code) {
    case XINPUT_GAMEPAD_DPAD_UP:
        return "D-Pad Up";
    case XINPUT_GAMEPAD_DPAD_DOWN:
        return "D-Pad Down";
    case XINPUT_GAMEPAD_DPAD_LEFT:
        return "D-Pad Left";
    case XINPUT_GAMEPAD_DPAD_RIGHT:
        return "D-Pad Right";
    case XINPUT_GAMEPAD_START:
        return "Menu";
    case XINPUT_GAMEPAD_BACK:
        return "View";
    case XINPUT_GAMEPAD_LEFT_THUMB:
        return "Left Stick";
    case XINPUT_GAMEPAD_RIGHT_THUMB:
        return "Right Stick";
    case XINPUT_GAMEPAD_LEFT_SHOULDER:
        return "Left Bumper";
    case XINPUT_GAMEPAD_RIGHT_SHOULDER:
        return "Right Bumper";
    case XINPUT_GAMEPAD_A:
        return "A";
    case XINPUT_GAMEPAD_B:
        return "B";
    case XINPUT_GAMEPAD_X:
        return "X";
    case XINPUT_GAMEPAD_Y:
        return "Y";
    case XINPUT_GAMEPAD_GUIDE:
        return "Home";
    default:
        return "Unknown";
    }
}

WORD RemappingWidget::GetGamepadButtonFromIndex(int index) const {
    // Map index to XInput button constants
    switch (index) {
    case 0:
        return XINPUT_GAMEPAD_A;
    case 1:
        return XINPUT_GAMEPAD_B;
    case 2:
        return XINPUT_GAMEPAD_X;
    case 3:
        return XINPUT_GAMEPAD_Y;
    case 4:
        return XINPUT_GAMEPAD_DPAD_UP;
    case 5:
        return XINPUT_GAMEPAD_DPAD_DOWN;
    case 6:
        return XINPUT_GAMEPAD_DPAD_LEFT;
    case 7:
        return XINPUT_GAMEPAD_DPAD_RIGHT;
    case 8:
        return XINPUT_GAMEPAD_START;
    case 9:
        return XINPUT_GAMEPAD_BACK;
    case 10:
        return XINPUT_GAMEPAD_GUIDE;
    case 11:
        return XINPUT_GAMEPAD_LEFT_THUMB;
    case 12:
        return XINPUT_GAMEPAD_RIGHT_THUMB;
    case 13:
        return XINPUT_GAMEPAD_LEFT_SHOULDER;
    case 14:
        return XINPUT_GAMEPAD_RIGHT_SHOULDER;
    default:
        return 0;
    }
}

int RemappingWidget::GetKeyboardVkFromIndex(int index) const {
    // Map index to virtual key codes
    switch (index) {
    case 0:
        return VK_SPACE;
    case 1:
        return VK_RETURN;
    case 2:
        return VK_ESCAPE;
    case 3:
        return VK_TAB;
    case 4:
        return VK_SHIFT;
    case 5:
        return VK_CONTROL;
    case 6:
        return VK_MENU;
    case 7:
        return VK_F1;
    case 8:
        return VK_F2;
    case 9:
        return VK_F3;
    case 10:
        return VK_F4;
    case 11:
        return VK_F5;
    case 12:
        return VK_F6;
    case 13:
        return VK_F7;
    case 14:
        return VK_F8;
    case 15:
        return VK_F9;
    case 16:
        return VK_F10;
    case 17:
        return VK_F11;
    case 18:
        return VK_F12;
    case 19:
        return VK_F13;
    case 20:
        return VK_F14;
    case 21:
        return VK_F15;
    case 22:
        return VK_F16;
    case 23:
        return VK_F17;
    case 24:
        return VK_F18;
    case 25:
        return VK_F19;
    case 26:
        return VK_F20;
    case 27:
        return VK_F21;
    case 28:
        return VK_F22;
    case 29:
        return VK_F23;
    case 30:
        return VK_F24;
    case 31:
        return VK_OEM_3; // Tilde key
    case 32:
        return 'A';
    case 33:
        return 'B';
    case 34:
        return 'C';
    case 35:
        return 'D';
    case 36:
        return 'E';
    case 37:
        return 'F';
    case 38:
        return 'G';
    case 39:
        return 'H';
    case 40:
        return 'I';
    case 41:
        return 'J';
    case 42:
        return 'K';
    case 43:
        return 'L';
    case 44:
        return 'M';
    case 45:
        return 'N';
    case 46:
        return 'O';
    case 47:
        return 'P';
    case 48:
        return 'Q';
    case 49:
        return 'R';
    case 50:
        return 'S';
    case 51:
        return 'T';
    case 52:
        return 'U';
    case 53:
        return 'V';
    case 54:
        return 'W';
    case 55:
        return 'X';
    case 56:
        return 'Y';
    case 57:
        return 'Z';
    default:
        return 0;
    }
}

void RemappingWidget::ResetDialogState() {
    dialog_state_.selected_remap_type = 0;
    dialog_state_.selected_gamepad_button = 0;
    dialog_state_.selected_keyboard_key = 0;
    dialog_state_.selected_input_method = 0;
    dialog_state_.selected_gamepad_target_button = 0;
    dialog_state_.selected_action = 0;
    dialog_state_.hold_mode = true;
    dialog_state_.chord_mode = false;
    dialog_state_.enabled = true;
}

void RemappingWidget::LoadRemapToDialog(const input_remapping::ButtonRemap &remap) {
    // Set remap type
    dialog_state_.selected_remap_type = static_cast<int>(remap.remap_type);

    // Find the index for the gamepad button
    auto gamepad_buttons = input_remapping::get_available_gamepad_buttons();
    for (size_t i = 0; i < gamepad_buttons.size(); ++i) {
        if (GetGamepadButtonFromIndex(static_cast<int>(i)) == remap.gamepad_button) {
            dialog_state_.selected_gamepad_button = static_cast<int>(i);
            break;
        }
    }

    // Load type-specific fields
    if (remap.remap_type == input_remapping::RemapType::Keyboard) {
        // Find the index for the keyboard key
        auto keyboard_keys = input_remapping::get_available_keyboard_keys();
        for (size_t i = 0; i < keyboard_keys.size(); ++i) {
            if (GetKeyboardVkFromIndex(static_cast<int>(i)) == remap.keyboard_vk) {
                dialog_state_.selected_keyboard_key = static_cast<int>(i);
                break;
            }
        }
        dialog_state_.selected_input_method = static_cast<int>(remap.input_method);
    } else if (remap.remap_type == input_remapping::RemapType::Gamepad) {
        // Find the index for the target gamepad button
        for (size_t i = 0; i < gamepad_buttons.size(); ++i) {
            if (GetGamepadButtonFromIndex(static_cast<int>(i)) == remap.gamepad_target_button) {
                dialog_state_.selected_gamepad_target_button = static_cast<int>(i);
                break;
            }
        }
    } else if (remap.remap_type == input_remapping::RemapType::Action) {
        // Find the index for the action
        auto actions = input_remapping::get_available_actions();
        for (size_t i = 0; i < actions.size(); ++i) {
            if (actions[i] == remap.action_name) {
                dialog_state_.selected_action = static_cast<int>(i);
                break;
            }
        }
    }

    dialog_state_.hold_mode = remap.hold_mode;
    dialog_state_.chord_mode = remap.chord_mode;
    dialog_state_.enabled = remap.enabled;
}

void RemappingWidget::LoadSettings() {
    // Load selected controller
    int selected_controller;
    if (display_commander::config::get_config_value("DisplayCommander.RemappingWidget", "SelectedController",
                                  selected_controller)) {
        selected_controller_ = selected_controller;
    }

    LogInfo("RemappingWidget::LoadSettings() - Settings loaded");
}

void RemappingWidget::SaveSettings() {
    // Save selected controller
    display_commander::config::set_config_value("DisplayCommander.RemappingWidget", "SelectedController", selected_controller_);

    LogInfo("RemappingWidget::SaveSettings() - Settings saved");
}

void RemappingWidget::ResetTriggerCounters() {
    auto &remapper = input_remapping::InputRemapper::get_instance();
    const auto &remappings = remapper.get_remappings();

    // Reset all trigger counts
    for (const auto &remap : remappings) {
        input_remapping::ButtonRemap *mutable_remap = const_cast<input_remapping::ButtonRemap *>(&remap);
        mutable_remap->trigger_count.store(0);
    }

    LogInfo("RemappingWidget::ResetTriggerCounters() - Reset %zu trigger counters", remappings.size());
}

// Global functions
void InitializeRemappingWidget() {
    if (!RemappingWidget::g_remapping_widget) {
        RemappingWidget::g_remapping_widget = std::make_unique<RemappingWidget>();
        RemappingWidget::g_remapping_widget->Initialize();
    }
}

void CleanupRemappingWidget() {
    if (RemappingWidget::g_remapping_widget) {
        RemappingWidget::g_remapping_widget->Cleanup();
        RemappingWidget::g_remapping_widget.reset();
    }
}

void DrawRemappingWidget() {
    if (RemappingWidget::g_remapping_widget) {
        RemappingWidget::g_remapping_widget->OnDraw();
    }
}
} // namespace display_commander::widgets::remapping_widget
