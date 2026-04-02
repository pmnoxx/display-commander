// Source Code <Display Commander> // follow this order for includes in all files + add this comment at the top
#include "panels_internal.hpp"
#include "../../../globals.hpp"
#include "../../../hooks/windows_hooks/windows_message_hooks.hpp"
#include "../../../settings/main_tab_settings.hpp"
#include "../settings_wrapper.hpp"
#include "../../ui_colors.hpp"

namespace ui::new_ui {

void DrawMainTabOptionalPanelInputControl(display_commander::ui::IImGuiWrapper& imgui) {
    imgui.Spacing();
    g_rendering_ui_section.store("ui:tab:main_new:input", std::memory_order_release);
    ui::colors::PushHeaderColors(&imgui);
    const bool input_control_open = imgui.CollapsingHeader("Input Control", ImGuiTreeNodeFlags_None);
    ui::colors::PopCollapsingHeaderColors(&imgui);
    if (input_control_open) {
        imgui.Indent();

        imgui.Columns(3, "InputBlockingColumns", true);
        imgui.Text("Suppress Keyboard");
        imgui.NextColumn();
        imgui.Text("Suppress Mouse");
        imgui.NextColumn();
        imgui.Text("Suppress Gamepad");
        imgui.NextColumn();

        if (ui::new_ui::ComboSettingEnumWrapper(settings::g_mainTabSettings.keyboard_input_blocking, "##Keyboard",
                                                imgui)) {
            if (settings::g_mainTabSettings.keyboard_input_blocking.GetValue()
                == static_cast<int>(InputBlockingMode::kDisabled)) {
            }
        }
        if (imgui.IsItemHovered()) {
            imgui.SetTooltipEx("Controls keyboard input blocking behavior.");
        }

        imgui.NextColumn();

        if (ui::new_ui::ComboSettingEnumWrapper(settings::g_mainTabSettings.mouse_input_blocking, "##Mouse", imgui)) {
            if (settings::g_mainTabSettings.mouse_input_blocking.GetValue()
                == static_cast<int>(InputBlockingMode::kDisabled)) {
            }
        }
        if (imgui.IsItemHovered()) {
            imgui.SetTooltipEx("Controls mouse input blocking behavior.");
        }

        imgui.NextColumn();

        ui::new_ui::ComboSettingEnumWrapper(settings::g_mainTabSettings.gamepad_input_blocking, "##Gamepad", imgui);
        if (imgui.IsItemHovered()) {
            imgui.SetTooltipEx("Controls gamepad input blocking behavior.");
        }

        imgui.Columns(1);
        imgui.Spacing();

        bool clip_cursor = settings::g_mainTabSettings.clip_cursor_enabled.GetValue();
        if (imgui.Checkbox("Clip Cursor", &clip_cursor)) {
            settings::g_mainTabSettings.clip_cursor_enabled.SetValue(clip_cursor);
            if (clip_cursor) {
                settings::g_mainTabSettings.unclip_cursor_enabled.SetValue(false);
                if (!g_app_in_background.load()) {
                    display_commanderhooks::ClipCursorToGameWindow();
                }
            } else {
                display_commanderhooks::ClipCursor_Direct(nullptr);
            }
        }
        if (imgui.IsItemHovered()) {
            imgui.SetTooltipEx(
                "Limits mouse movement to the game window when the game is in foreground.\n"
                "Unlocks cursor when game is in background.\n\n"
                "This fixes games which don't lock the mouse cursor, preventing focus switches\n"
                "on multimonitor setups when moving the mouse and clicking.\n\n"
                "Mutually exclusive with Unclip Cursor.");
        }
        imgui.SameLine();

        bool unclip_cursor = settings::g_mainTabSettings.unclip_cursor_enabled.GetValue();
        if (imgui.Checkbox("Unclip Cursor", &unclip_cursor)) {
            settings::g_mainTabSettings.unclip_cursor_enabled.SetValue(unclip_cursor);
            if (unclip_cursor) {
                settings::g_mainTabSettings.clip_cursor_enabled.SetValue(false);
                display_commanderhooks::ClipCursor_Direct(nullptr);
            }
        }
        if (imgui.IsItemHovered()) {
            imgui.SetTooltipEx(
                "Stops the game from confining the mouse with ClipCursor (cursor can leave the window).\n"
                "Useful on multi-monitor setups or when a game traps the cursor.\n\n"
                "Mutually exclusive with Clip Cursor.");
        }
    }
}

}  // namespace ui::new_ui
