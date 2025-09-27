#include "display_settings_debug_tab.hpp"
#include "../../settings/display_settings.hpp"
#include "../../utils.hpp"

#include <imgui.h>
#include <reshade.hpp>

namespace ui::new_ui {

void DrawDisplaySettingsDebugTab() {
    ImGui::Text("Display Settings Debug Information");
    ImGui::Separator();

    // Check if DisplaySettings is available
    if (!display_commander::settings::g_display_settings) {
        ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "DisplaySettings not initialized!");
        return;
    }

    auto& settings = *display_commander::settings::g_display_settings;

    // Display current settings
    ImGui::Text("Current Settings:");
    ImGui::Indent();

    std::string device_id = settings.GetLastDeviceId();
    ImGui::Text("Last Device ID: %s", device_id.empty() ? "(empty)" : device_id.c_str());

    int width = settings.GetLastWidth();
    int height = settings.GetLastHeight();
    ImGui::Text("Last Resolution: %dx%d", width, height);

    uint32_t numerator = settings.GetLastRefreshNumerator();
    uint32_t denominator = settings.GetLastRefreshDenominator();
    if (denominator != 0) {
        double refresh_hz = static_cast<double>(numerator) / static_cast<double>(denominator);
        ImGui::Text("Last Refresh Rate: %u/%u (%.2f Hz)", numerator, denominator, refresh_hz);
    } else {
        ImGui::Text("Last Refresh Rate: %u/%u (invalid)", numerator, denominator);
    }

    ImGui::Unindent();

    ImGui::Spacing();
    ImGui::Separator();

    // Action buttons
    ImGui::Text("Actions:");
    ImGui::Indent();

    if (ImGui::Button("Reload Settings")) {
        settings.LoadSettings();
        LogInfo("DisplaySettings debug: Reloaded settings");
    }
    ImGui::SameLine();
    if (ImGui::Button("Save Settings")) {
        settings.SaveSettings();
        LogInfo("DisplaySettings debug: Saved settings");
    }

    ImGui::Spacing();

    if (ImGui::Button("Validate and Fix Settings")) {
        bool result = settings.ValidateAndFixSettings();
        LogInfo("DisplaySettings debug: ValidateAndFixSettings returned %s", result ? "true" : "false");
    }
    ImGui::SameLine();
    if (ImGui::Button("Set to Primary Display")) {
        settings.SetToPrimaryDisplay();
        LogInfo("DisplaySettings debug: Set to primary display");
    }

    ImGui::Spacing();

    if (ImGui::Button("Set to Current Resolution")) {
        settings.SetToCurrentResolution();
        LogInfo("DisplaySettings debug: Set to current resolution");
    }
    ImGui::SameLine();
    if (ImGui::Button("Set to Current Refresh Rate")) {
        settings.SetToCurrentRefreshRate();
        LogInfo("DisplaySettings debug: Set to current refresh rate");
    }

    ImGui::Unindent();

    ImGui::Spacing();
    ImGui::Separator();

    // Debug information
    ImGui::Text("Debug Information:");
    ImGui::Indent();

    std::string debug_info = settings.GetDebugInfo();

    // Display debug info in a scrollable text area
    ImGui::BeginChild("DebugInfo", ImVec2(0, 200), true);
    ImGui::TextUnformatted(debug_info.c_str());
    ImGui::EndChild();

    ImGui::Unindent();
}

}  // namespace ui::new_ui
