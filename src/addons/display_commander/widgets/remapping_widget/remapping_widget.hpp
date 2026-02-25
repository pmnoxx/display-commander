#pragma once

#include "../../input_remapping/input_remapping.hpp"

#include <windows.h>

#include <imgui.h>
#include <xinput.h>

#include <memory>
#include <string>

namespace display_commander {
namespace ui {
struct IImGuiWrapper;
}
}  // namespace display_commander

namespace display_commander::widgets::remapping_widget {
// Remapping widget class
class RemappingWidget {
  public:
    RemappingWidget();
    ~RemappingWidget() = default;

    // Main draw function - call this from the main tab (uses ImGui wrapper for ReShade or standalone UI)
    void OnDraw(display_commander::ui::IImGuiWrapper& imgui);

    // Initialize the widget (call once at startup)
    void Initialize();

    // Cleanup the widget (call at shutdown)
    void Cleanup();

  private:
    // UI state
    bool is_initialized_ = false;
    int selected_controller_ = 0;
    bool show_add_remap_dialog_ = false;
    bool show_edit_remap_dialog_ = false;
    int editing_remap_index_ = -1;

    // Add/Edit dialog state
    struct RemapDialogState {
        int selected_remap_type = 0; // 0=Keyboard, 1=Gamepad, 2=Action
        int selected_gamepad_button = 0;
        int selected_keyboard_key = 0;
        int selected_input_method = 0;
        int selected_gamepad_target_button = 0;
        int selected_action = 0;
        bool hold_mode = true;
        bool chord_mode = false;
        bool enabled = true;
    } dialog_state_;

    // UI helper functions (all take ImGui wrapper for ReShade/standalone)
    void DrawRemappingSettings(display_commander::ui::IImGuiWrapper& imgui);
    void DrawRemappingList(display_commander::ui::IImGuiWrapper& imgui);
    void DrawAddRemapDialog(display_commander::ui::IImGuiWrapper& imgui);
    void DrawEditRemapDialog(display_commander::ui::IImGuiWrapper& imgui);
    void DrawInputMethodSlider(display_commander::ui::IImGuiWrapper& imgui);
    void DrawControllerSelector(display_commander::ui::IImGuiWrapper& imgui);
    void DrawRemapEntry(display_commander::ui::IImGuiWrapper& imgui, const input_remapping::ButtonRemap& remap,
                        int index);

    // Helper functions
    std::string GetGamepadButtonName(int button_index) const;
    std::string GetKeyboardKeyName(int key_index) const;
    std::string GetGamepadButtonNameFromCode(WORD button_code) const;
    WORD GetGamepadButtonFromIndex(int index) const;
    int GetKeyboardVkFromIndex(int index) const;
    void ResetDialogState();
    void LoadRemapToDialog(const input_remapping::ButtonRemap &remap);

    // Settings management
    void LoadSettings();
    void SaveSettings();

    // Counter management
    void ResetTriggerCounters();

  public:
    // Global widget instance
    static std::unique_ptr<RemappingWidget> g_remapping_widget;
};

// Global functions for integration
void InitializeRemappingWidget();
void CleanupRemappingWidget();
void DrawRemappingWidget(display_commander::ui::IImGuiWrapper& imgui);
} // namespace display_commander::widgets::remapping_widget
