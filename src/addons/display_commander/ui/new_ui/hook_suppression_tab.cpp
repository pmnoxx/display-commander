#include "hook_suppression_tab.hpp"
#include "../../settings/hook_suppression_settings.hpp"
#include "../../utils/logging.hpp"
#include <imgui.h>

namespace ui::new_ui {

void RenderHookSuppressionTab() {
    if (ImGui::BeginTabItem("Hook Suppression")) {
        ImGui::Text("Hook Suppression Settings");
        ImGui::Separator();

        ImGui::TextWrapped("Configure which hooks should be suppressed on subsequent game runs. "
                          "When a hook is successfully installed, it will be marked as 'installed' "
                          "and you can then suppress it to prevent it from being installed again.");

        ImGui::Spacing();

        // Hook suppression settings
        ImGui::Text("Suppress Hooks:");
        ImGui::Separator();

        // DXGI hooks
        bool suppress_dxgi = settings::g_hook_suppression_settings.suppress_dxgi_hooks.GetValue();
        bool dxgi_installed = settings::g_hook_suppression_settings.dxgi_hooks_installed.GetValue();
        if (ImGui::Checkbox("Suppress DXGI Hooks", &suppress_dxgi)) {
            settings::g_hook_suppression_settings.suppress_dxgi_hooks.SetValue(suppress_dxgi);
        }
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "(Installed: %s)", dxgi_installed ? "Yes" : "No");

        // D3D Device hooks
        bool suppress_d3d = settings::g_hook_suppression_settings.suppress_d3d_device_hooks.GetValue();
        bool d3d_installed = settings::g_hook_suppression_settings.d3d_device_hooks_installed.GetValue();
        if (ImGui::Checkbox("Suppress D3D Device Hooks", &suppress_d3d)) {
            settings::g_hook_suppression_settings.suppress_d3d_device_hooks.SetValue(suppress_d3d);
        }
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "(Installed: %s)", d3d_installed ? "Yes" : "No");

        // XInput hooks
        bool suppress_xinput = settings::g_hook_suppression_settings.suppress_xinput_hooks.GetValue();
        bool xinput_installed = settings::g_hook_suppression_settings.xinput_hooks_installed.GetValue();
        if (ImGui::Checkbox("Suppress XInput Hooks", &suppress_xinput)) {
            settings::g_hook_suppression_settings.suppress_xinput_hooks.SetValue(suppress_xinput);
        }
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "(Installed: %s)", xinput_installed ? "Yes" : "No");

        // DirectInput hooks
        bool suppress_dinput = settings::g_hook_suppression_settings.suppress_dinput_hooks.GetValue();
        bool dinput_installed = settings::g_hook_suppression_settings.dinput_hooks_installed.GetValue();
        if (ImGui::Checkbox("Suppress DirectInput Hooks", &suppress_dinput)) {
            settings::g_hook_suppression_settings.suppress_dinput_hooks.SetValue(suppress_dinput);
        }
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "(Installed: %s)", dinput_installed ? "Yes" : "No");

        // Streamline hooks
        bool suppress_streamline = settings::g_hook_suppression_settings.suppress_streamline_hooks.GetValue();
        bool streamline_installed = settings::g_hook_suppression_settings.streamline_hooks_installed.GetValue();
        if (ImGui::Checkbox("Suppress Streamline Hooks", &suppress_streamline)) {
            settings::g_hook_suppression_settings.suppress_streamline_hooks.SetValue(suppress_streamline);
        }
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "(Installed: %s)", streamline_installed ? "Yes" : "No");

        // NGX hooks
        bool suppress_ngx = settings::g_hook_suppression_settings.suppress_ngx_hooks.GetValue();
        bool ngx_installed = settings::g_hook_suppression_settings.ngx_hooks_installed.GetValue();
        if (ImGui::Checkbox("Suppress NGX Hooks", &suppress_ngx)) {
            settings::g_hook_suppression_settings.suppress_ngx_hooks.SetValue(suppress_ngx);
        }
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "(Installed: %s)", ngx_installed ? "Yes" : "No");

        // Windows Gaming Input hooks
        bool suppress_wgi = settings::g_hook_suppression_settings.suppress_windows_gaming_input_hooks.GetValue();
        bool wgi_installed = settings::g_hook_suppression_settings.windows_gaming_input_hooks_installed.GetValue();
        if (ImGui::Checkbox("Suppress Windows Gaming Input Hooks", &suppress_wgi)) {
            settings::g_hook_suppression_settings.suppress_windows_gaming_input_hooks.SetValue(suppress_wgi);
        }
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "(Installed: %s)", wgi_installed ? "Yes" : "No");

        // HID hooks
        bool suppress_hid = settings::g_hook_suppression_settings.suppress_hid_hooks.GetValue();
        bool hid_installed = settings::g_hook_suppression_settings.hid_hooks_installed.GetValue();
        if (ImGui::Checkbox("Suppress HID Hooks", &suppress_hid)) {
            settings::g_hook_suppression_settings.suppress_hid_hooks.SetValue(suppress_hid);
        }
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "(Installed: %s)", hid_installed ? "Yes" : "No");

        // API hooks
        bool suppress_api = settings::g_hook_suppression_settings.suppress_api_hooks.GetValue();
        bool api_installed = settings::g_hook_suppression_settings.api_hooks_installed.GetValue();
        if (ImGui::Checkbox("Suppress API Hooks", &suppress_api)) {
            settings::g_hook_suppression_settings.suppress_api_hooks.SetValue(suppress_api);
        }
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "(Installed: %s)", api_installed ? "Yes" : "No");

        // Sleep hooks
        bool suppress_sleep = settings::g_hook_suppression_settings.suppress_sleep_hooks.GetValue();
        bool sleep_installed = settings::g_hook_suppression_settings.sleep_hooks_installed.GetValue();
        if (ImGui::Checkbox("Suppress Sleep Hooks", &suppress_sleep)) {
            settings::g_hook_suppression_settings.suppress_sleep_hooks.SetValue(suppress_sleep);
        }
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "(Installed: %s)", sleep_installed ? "Yes" : "No");

        // Time slowdown hooks
        bool suppress_timeslowdown = settings::g_hook_suppression_settings.suppress_timeslowdown_hooks.GetValue();
        bool timeslowdown_installed = settings::g_hook_suppression_settings.timeslowdown_hooks_installed.GetValue();
        if (ImGui::Checkbox("Suppress Time Slowdown Hooks", &suppress_timeslowdown)) {
            settings::g_hook_suppression_settings.suppress_timeslowdown_hooks.SetValue(suppress_timeslowdown);
        }
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "(Installed: %s)", timeslowdown_installed ? "Yes" : "No");

        // Debug output hooks
        bool suppress_debug = settings::g_hook_suppression_settings.suppress_debug_output_hooks.GetValue();
        bool debug_installed = settings::g_hook_suppression_settings.debug_output_hooks_installed.GetValue();
        if (ImGui::Checkbox("Suppress Debug Output Hooks", &suppress_debug)) {
            settings::g_hook_suppression_settings.suppress_debug_output_hooks.SetValue(suppress_debug);
        }
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "(Installed: %s)", debug_installed ? "Yes" : "No");

        // LoadLibrary hooks
        bool suppress_loadlibrary = settings::g_hook_suppression_settings.suppress_loadlibrary_hooks.GetValue();
        bool loadlibrary_installed = settings::g_hook_suppression_settings.loadlibrary_hooks_installed.GetValue();
        if (ImGui::Checkbox("Suppress LoadLibrary Hooks", &suppress_loadlibrary)) {
            settings::g_hook_suppression_settings.suppress_loadlibrary_hooks.SetValue(suppress_loadlibrary);
        }
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "(Installed: %s)", loadlibrary_installed ? "Yes" : "No");

        // Display settings hooks
        bool suppress_display_settings = settings::g_hook_suppression_settings.suppress_display_settings_hooks.GetValue();
        bool display_settings_installed = settings::g_hook_suppression_settings.display_settings_hooks_installed.GetValue();
        if (ImGui::Checkbox("Suppress Display Settings Hooks", &suppress_display_settings)) {
            settings::g_hook_suppression_settings.suppress_display_settings_hooks.SetValue(suppress_display_settings);
        }
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "(Installed: %s)", display_settings_installed ? "Yes" : "No");

        // Windows message hooks
        bool suppress_windows_message = settings::g_hook_suppression_settings.suppress_windows_message_hooks.GetValue();
        bool windows_message_installed = settings::g_hook_suppression_settings.windows_message_hooks_installed.GetValue();
        if (ImGui::Checkbox("Suppress Windows Message Hooks", &suppress_windows_message)) {
            settings::g_hook_suppression_settings.suppress_windows_message_hooks.SetValue(suppress_windows_message);
        }
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "(Installed: %s)", windows_message_installed ? "Yes" : "No");

        // OpenGL hooks
        bool suppress_opengl = settings::g_hook_suppression_settings.suppress_opengl_hooks.GetValue();
        bool opengl_installed = settings::g_hook_suppression_settings.opengl_hooks_installed.GetValue();
        if (ImGui::Checkbox("Suppress OpenGL Hooks", &suppress_opengl)) {
            settings::g_hook_suppression_settings.suppress_opengl_hooks.SetValue(suppress_opengl);
        }
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "(Installed: %s)", opengl_installed ? "Yes" : "No");

        // HID suppression hooks
        bool suppress_hid_suppression = settings::g_hook_suppression_settings.suppress_hid_suppression_hooks.GetValue();
        bool hid_suppression_installed = settings::g_hook_suppression_settings.hid_suppression_hooks_installed.GetValue();
        if (ImGui::Checkbox("Suppress HID Suppression Hooks", &suppress_hid_suppression)) {
            settings::g_hook_suppression_settings.suppress_hid_suppression_hooks.SetValue(suppress_hid_suppression);
        }
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "(Installed: %s)", hid_suppression_installed ? "Yes" : "No");

        // NVAPI hooks
        bool suppress_nvapi = settings::g_hook_suppression_settings.suppress_nvapi_hooks.GetValue();
        bool nvapi_installed = settings::g_hook_suppression_settings.nvapi_hooks_installed.GetValue();
        if (ImGui::Checkbox("Suppress NVAPI Hooks", &suppress_nvapi)) {
            settings::g_hook_suppression_settings.suppress_nvapi_hooks.SetValue(suppress_nvapi);
        }
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "(Installed: %s)", nvapi_installed ? "Yes" : "No");

        // Process exit hooks
        bool suppress_process_exit = settings::g_hook_suppression_settings.suppress_process_exit_hooks.GetValue();
        bool process_exit_installed = settings::g_hook_suppression_settings.process_exit_hooks_installed.GetValue();
        if (ImGui::Checkbox("Suppress Process Exit Hooks", &suppress_process_exit)) {
            settings::g_hook_suppression_settings.suppress_process_exit_hooks.SetValue(suppress_process_exit);
        }
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "(Installed: %s)", process_exit_installed ? "Yes" : "No");

        ImGui::Spacing();
        ImGui::Separator();

        // Reset all installed flags
        if (ImGui::Button("Reset All Installation Flags")) {
            settings::g_hook_suppression_settings.dxgi_hooks_installed.SetValue(false);
            settings::g_hook_suppression_settings.d3d_device_hooks_installed.SetValue(false);
            settings::g_hook_suppression_settings.xinput_hooks_installed.SetValue(false);
            settings::g_hook_suppression_settings.dinput_hooks_installed.SetValue(false);
            settings::g_hook_suppression_settings.streamline_hooks_installed.SetValue(false);
            settings::g_hook_suppression_settings.ngx_hooks_installed.SetValue(false);
            settings::g_hook_suppression_settings.windows_gaming_input_hooks_installed.SetValue(false);
            settings::g_hook_suppression_settings.hid_hooks_installed.SetValue(false);
            settings::g_hook_suppression_settings.api_hooks_installed.SetValue(false);
            settings::g_hook_suppression_settings.sleep_hooks_installed.SetValue(false);
            settings::g_hook_suppression_settings.timeslowdown_hooks_installed.SetValue(false);
            settings::g_hook_suppression_settings.debug_output_hooks_installed.SetValue(false);
            settings::g_hook_suppression_settings.loadlibrary_hooks_installed.SetValue(false);
            settings::g_hook_suppression_settings.display_settings_hooks_installed.SetValue(false);
            settings::g_hook_suppression_settings.windows_message_hooks_installed.SetValue(false);
            settings::g_hook_suppression_settings.opengl_hooks_installed.SetValue(false);
            settings::g_hook_suppression_settings.hid_suppression_hooks_installed.SetValue(false);
            settings::g_hook_suppression_settings.nvapi_hooks_installed.SetValue(false);
            settings::g_hook_suppression_settings.process_exit_hooks_installed.SetValue(false);
            LogInfo("All hook installation flags reset");
        }
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "(Reset tracking for all hooks)");

        ImGui::EndTabItem();
    }
}

} // namespace ui::new_ui
