// Source Code <Display Commander> // follow this order for includes in all files + add this comment at the top
#include "display_settings_internal.hpp"

namespace ui::new_ui {

void DrawDisplaySettings_WindowModeAndApply(display_commander::ui::IImGuiWrapper& imgui) {
    (void)imgui;
    CALL_GUARD_NO_TS();
    // Window Mode dropdown (with persistent setting)
    static bool was_ever_in_no_changes_mode = false;
    if (static_cast<WindowMode>(settings::g_mainTabSettings.window_mode.GetValue()) == WindowMode::kNoChanges) {
        was_ever_in_no_changes_mode = true;
    }

    PushFpsLimiterSliderColumnAlign(imgui, GetMainTabCheckboxColumnGutter(imgui));
    if (ComboSettingEnumWrapper(settings::g_mainTabSettings.window_mode, "Window Mode", imgui, 600.f,
                                &ui::colors::TEXT_LABEL)) {
        // Don't apply changes immediately - let the normal window management system handle it
        // This prevents crashes when changing modes during gameplay
        LogInfo("Window mode changed to %d", settings::g_mainTabSettings.window_mode.GetValue());
    }
    // Warn about restart may be needed for preventing fullscreen
    if (was_ever_in_no_changes_mode
        && static_cast<WindowMode>(settings::g_mainTabSettings.window_mode.GetValue()) != WindowMode::kNoChanges) {
        imgui.TextColored(ui::colors::TEXT_WARNING,
                          ICON_FK_WARNING "Warning: Restart may be needed for preventing fullscreen.");
    }

    // Aspect Ratio dropdown (only shown in Aspect Ratio mode)
    if (GetCurrentWindowMode() == WindowMode::kAspectRatio) {
        if (ComboSettingWrapper(settings::g_mainTabSettings.aspect_index, "Aspect Ratio", imgui)) {
            s_aspect_index = static_cast<AspectRatioType>(settings::g_mainTabSettings.aspect_index.GetValue());
            LogInfo("Aspect ratio changed");
        }
        if (imgui.IsItemHovered()) {
            imgui.SetTooltipEx("Choose the aspect ratio for window resizing.");
        }
    }
    if (GetCurrentWindowMode() == WindowMode::kAspectRatio) {
        // Width dropdown for aspect ratio mode
        if (ComboSettingWrapper(settings::g_mainTabSettings.window_aspect_width, "Window Width", imgui)) {
            LogInfo("Window width for aspect mode setting changed to: %d",
                    settings::g_mainTabSettings.window_aspect_width.GetValue());
        }
        if (imgui.IsItemHovered()) {
            imgui.SetTooltipEx(
                "Choose the width for the aspect ratio window. 'Display Width' uses the current monitor width.");
        }
    }

    // Window Alignment dropdown (only shown in Aspect Ratio mode)
    if (GetCurrentWindowMode() == WindowMode::kAspectRatio) {
        if (ComboSettingWrapper(settings::g_mainTabSettings.alignment, "Alignment", imgui)) {
            s_window_alignment = static_cast<WindowAlignment>(settings::g_mainTabSettings.alignment.GetValue());
            LogInfo("Window alignment changed");
        }
        if (imgui.IsItemHovered()) {
            imgui.SetTooltipEx(
                "Choose how to align the window when repositioning is needed. 0=Center, 1=Top Left, "
                "2=Top Right, 3=Bottom Left, 4=Bottom Right.");
        }
    }
    // Black curtain (game / other displays) controls
    DrawAdhdMultiMonitorControls(imgui);

    // Apply Changes button
    imgui.PushStyleColor(ImGuiCol_Text, ui::colors::ICON_SUCCESS);
    if (imgui.Button(ICON_FK_OK " Apply Changes")) {
        LogInfo("Apply Changes button clicked - forcing immediate window update");
        std::ostringstream oss;
        // All global settings on this tab are handled by the settings wrapper
        oss << "Apply Changes button clicked - forcing immediate window update";
        LogInfo(oss.str().c_str());
    }
    imgui.PopStyleColor();
    if (imgui.IsItemHovered()) {
        imgui.SetTooltipEx("Apply the current window size and position settings immediately.");
    }
}

}  // namespace ui::new_ui

