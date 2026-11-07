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

        // A table, not Columns(): legacy columns indent their content by the item spacing, so the headings sat to the
        // right of the "Cursor lock:" row below. A table without outer padding starts at the section's left edge.
        if (imgui.BeginTable("InputBlockingColumns", 3,
                             ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchSame)) {
            imgui.TableNextRow();
            imgui.TableNextColumn();
            imgui.Text("Block keyboard");
            imgui.TableNextColumn();
            imgui.Text("Block mouse");
            imgui.TableNextColumn();
            imgui.Text("Block gamepad");
            imgui.TableNextRow();
            imgui.TableNextColumn();

#define DC_INPUT_BLOCKING_MODES_HELP                                      \
    "\n\nOff: the game always receives it.\n"                             \
    "Always: the game never receives it, even while focused.\n"           \
    "When game is not focused: ignored while another window is active,\n" \
    "e.g. when you type on another monitor."

            // Each combo fills its column, leaving room for the reset button drawn after it when the value is not the
            // default (square, frame height; see ComboSettingEnumWrapper).
            const ImGuiStyle& style = imgui.GetStyle();
            const float combo_width =
                -(imgui.GetTextLineHeight() + (style.FramePadding.y * 2.0f) + style.ItemSpacing.x);
            imgui.SetNextItemWidth(combo_width);
            ui::new_ui::ComboSettingEnumWrapper(settings::g_mainTabSettings.keyboard_input_blocking, "##Keyboard",
                                                    imgui);
            if (imgui.IsItemHovered()) {
                imgui.SetTooltipEx(
                    "Hides keyboard input from the game. Display Commander and the ReShade overlay still receive "
                    "it." DC_INPUT_BLOCKING_MODES_HELP);
            }

            imgui.TableNextColumn();

            imgui.SetNextItemWidth(combo_width);
            ui::new_ui::ComboSettingEnumWrapper(settings::g_mainTabSettings.mouse_input_blocking, "##Mouse", imgui);
            if (imgui.IsItemHovered()) {
                imgui.SetTooltipEx(
                    "Hides mouse clicks, movement and wheel from the game. Display Commander and the ReShade overlay "
                    "still receive them." DC_INPUT_BLOCKING_MODES_HELP
                    "\nWhile a controller is connected: hides the mouse while the game reads an\n"
                    "Xbox-compatible (XInput) controller. Stops games from flickering between\n"
                    "mouse and controller button prompts. Only while the game is focused.");
            }

            imgui.TableNextColumn();

            imgui.SetNextItemWidth(combo_width);
            ui::new_ui::ComboSettingEnumWrapper(settings::g_mainTabSettings.gamepad_input_blocking, "##Gamepad", imgui);
            if (imgui.IsItemHovered()) {
                imgui.SetTooltipEx(
                    "Hides Xbox-compatible (XInput) controller input from the game and stops its "
                    "vibration." DC_INPUT_BLOCKING_MODES_HELP "\nHas no effect while Continue Rendering is on.");
            }
#undef DC_INPUT_BLOCKING_MODES_HELP

            imgui.EndTable();
        }

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
