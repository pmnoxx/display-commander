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

        // setwidth for each combo
        const float combo_width = 400;
        imgui.SetNextItemWidth(combo_width);
        ui::new_ui::ComboSettingEnumWrapper(settings::g_mainTabSettings.keyboard_input_blocking, "##Keyboard",
                                                imgui);
        if (imgui.IsItemHovered()) {
            imgui.SetTooltipEx("Controls keyboard input blocking behavior.");
        }

        imgui.NextColumn();

        imgui.SetNextItemWidth(combo_width);
        ui::new_ui::ComboSettingEnumWrapper(settings::g_mainTabSettings.mouse_input_blocking, "##Mouse", imgui);
        if (imgui.IsItemHovered()) {
            imgui.SetTooltipEx("Controls mouse input blocking behavior.");
        }

        imgui.NextColumn();

        imgui.SetNextItemWidth(combo_width);
        ui::new_ui::ComboSettingEnumWrapper(settings::g_mainTabSettings.gamepad_input_blocking, "##Gamepad", imgui);
        if (imgui.IsItemHovered()) {
            imgui.SetTooltipEx("Controls gamepad input blocking behavior.");
        }

        imgui.Columns(1);

        // Cursor lock: one 3-way choice backed by the two (mutually exclusive) settings.
        enum : int { kCursorGameDefault = 0, kCursorLockToWindow = 1, kCursorAlwaysFree = 2 };
        int cursor_mode = settings::g_mainTabSettings.clip_cursor_enabled.GetValue()     ? kCursorLockToWindow
                          : settings::g_mainTabSettings.unclip_cursor_enabled.GetValue() ? kCursorAlwaysFree
                                                                                         : kCursorGameDefault;
        const int prev_cursor_mode = cursor_mode;

        imgui.TextUnformatted("Cursor lock:");
        imgui.SameLine();
        imgui.RadioButton("Game default", &cursor_mode, kCursorGameDefault);
        if (imgui.IsItemHovered()) {
            imgui.SetTooltipEx("Display Commander does not touch the cursor; the game decides whether it is confined.");
        }
        imgui.SameLine();
        imgui.RadioButton("Lock to window", &cursor_mode, kCursorLockToWindow);
        if (imgui.IsItemHovered()) {
            imgui.SetTooltipEx(
                "Keeps the mouse inside the game window while the game is in the foreground.\n"
                "Released when the game goes to the background.\n\n"
                "Fixes games that don't lock the cursor, which on multi-monitor setups lets\n"
                "the mouse wander to another screen and steal focus on click.");
        }
        imgui.SameLine();
        imgui.RadioButton("Always free", &cursor_mode, kCursorAlwaysFree);
        if (imgui.IsItemHovered()) {
            imgui.SetTooltipEx(
                "Ignores the game's attempts to confine the mouse (ClipCursor), so the cursor\n"
                "can always leave the window. Useful on multi-monitor setups or when a game\n"
                "traps the cursor.");
        }

        if (cursor_mode != prev_cursor_mode) {
            settings::g_mainTabSettings.clip_cursor_enabled.SetValue(cursor_mode == kCursorLockToWindow);
            settings::g_mainTabSettings.unclip_cursor_enabled.SetValue(cursor_mode == kCursorAlwaysFree);
            // Lock / unlock right away instead of waiting for the next focus change or game ClipCursor call. While the
            // ReShade overlay or the DC window is open ReShade controls ClipCursor (keeps the cursor free);
            // OnReShadePresent applies the new mode one frame after they close.
            if (!display_commanderhooks::ReShadeControlsCursor()) {
                display_commanderhooks::ApplyCursorLock(g_app_in_background.load());
            }
        }
        imgui.Unindent();
    }
}

}  // namespace ui::new_ui
