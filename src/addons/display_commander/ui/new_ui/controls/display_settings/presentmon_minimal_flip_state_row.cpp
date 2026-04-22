// Source Code <Display Commander> // follow this order for includes in all files + add this comment at the top
#include "presentmon_minimal_flip_state_row.hpp"

#include "display_settings_internal.hpp"
#include "../../../../features/presentmon/presentmon_minimal_etw.hpp"

// Libraries <ReShade / ImGui>
#include <imgui.h>

// Libraries <standard C++>
#include <string>

namespace ui::new_ui {

void DrawPresentMonMinimalFlipStateRow(display_commander::ui::IImGuiWrapper& imgui) {
    bool etw_on = settings::g_mainTabSettings.present_mon_etw_enabled.GetValue();

    display_commander::features::presentmon::PresentMonStateSnapshot snapshot{};
    ImVec4 state_color = ui::colors::TEXT_DIMMED;
    std::string state_paren = "(off)";

    if (etw_on) {
        display_commander::features::presentmon::EnsurePresentMonEtwStarted();
        snapshot = display_commander::features::presentmon::GetPresentMonStateSnapshot();

        const char* suffix = "";
        const char* value = "Unknown";
        state_color = ui::colors::TEXT_DIMMED;

        if (snapshot.has_data) {
            value = display_commander::features::presentmon::PresentMonModeToString(snapshot.mode);
            if (snapshot.is_live) {
                state_color = ui::colors::TEXT_SUCCESS;
                suffix = "";
            } else {
                state_color = ui::colors::TEXT_WARNING;
                suffix = " (stale)";
            }
        } else if (snapshot.session_running) {
            suffix = " (waiting)";
        } else if (snapshot.session_failed) {
            state_color = ui::colors::TEXT_ERROR;
            suffix = " (ETW unavailable)";
        }

        state_paren = "(";
        state_paren += value;
        state_paren += suffix;
        state_paren += ")";
    }

    PushFpsLimiterSliderColumnAlign(imgui, GetMainTabCheckboxColumnGutter(imgui), true);
    imgui.TextColored(ui::colors::TEXT_LABEL, "Flip state:");
    imgui.SameLine();
    imgui.TextColored(state_color, "%s", state_paren.c_str());
    if (imgui.IsItemHovered() && etw_on) {
        if (snapshot.has_data) {
            imgui.SetTooltipEx(
                "Last update age: %llu ms.\n"
                "Show display state for current process."
                "For maximum performance FLIP_DISCARD/FLIP_SEQUENTIAL is recommended."
                "DISCARD/SEQUENTIAL is traditional mode.",
                static_cast<unsigned long long>(snapshot.age_ms));
        } else if (snapshot.session_failed) {
            imgui.SetTooltipEx("PresentMon ETW session failed or provider access was denied.");
        } else {
            imgui.SetTooltipEx("Waiting for current-process Win32k composition events.");
        }
    }

    imgui.SameLine();
    if (imgui.Checkbox("Enabled", &etw_on)) {
        settings::g_mainTabSettings.present_mon_etw_enabled.SetValue(etw_on);
        if (etw_on) {
            display_commander::features::presentmon::EnsurePresentMonEtwStarted();
            LogInfo("PresentMon ETW enabled (user)");
        } else {
            display_commander::features::presentmon::ShutdownPresentMonEtw();
            LogInfo("PresentMon ETW disabled (user)");
        }
    }
    if (imgui.IsItemHovered()) {
        imgui.SetTooltipEx(
            "When enabled, starts a minimal Win32k ETW session to classify flip/composition state for this process. "
            "Off by default; some games or policies block tracing. Disable if you see access-denied ETW events or "
            "instability.");
    }
}

}  // namespace ui::new_ui
