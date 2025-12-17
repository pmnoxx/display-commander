#include "streamline_tab.hpp"
#include "../../globals.hpp"
#include "../../hooks/streamline_hooks.hpp"
#include "../../settings/streamline_tab_settings.hpp"
#include "../../utils.hpp"
#include "../../res/forkawesome.h"

#include <imgui.h>
#include <windows.h>
#include <string>
#include <filesystem>
#include <numeric>

namespace ui::new_ui {

void DrawStreamlineTab() {
    ImGui::Text("Streamline Tab - DLSS Information");
    ImGui::Separator();

    // Check if Streamline is loaded
    HMODULE sl_interposer = GetModuleHandleW(L"sl.interposer.dll");
    HMODULE sl_common = GetModuleHandleW(L"sl.common.dll");

    if (sl_interposer == nullptr) {
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "Streamline not detected - sl.interposer.dll not loaded");
        return;
    }

    ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "Streamline detected");
    ImGui::Text("sl.interposer.dll: 0x%p", sl_interposer);

    if (sl_common != nullptr) {
        ImGui::Text("sl.common.dll: 0x%p", sl_common);
    } else {
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "sl.common.dll: Not loaded");
    }

    ImGui::Spacing();

    // Streamline SDK Version (from slInit calls)
    ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "Streamline SDK Information:");
    ImGui::Separator();

    // Get version from the last slInit call
    uint64_t sdk_version = GetLastStreamlineSDKVersion();
    if (sdk_version > 0) {
        ImGui::Text("SDK Version: %llu", sdk_version);
    } else {
        ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "SDK Version: Not yet called");
        ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "Note: Version will be updated when slInit is called");
    }

    ImGui::Spacing();

    // DLSS Frame Generation Information
    ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "DLSS Frame Generation Information:");
    ImGui::Separator();

    bool dlss_g_loaded = g_dlss_g_loaded.load();
    ImGui::Text("DLSS-G Loaded: %s", dlss_g_loaded ? "Yes" : "No");

    if (dlss_g_loaded) {
        auto version_ptr = g_dlss_g_version.load();
        if (version_ptr) {
            ImGui::Text("DLSS-G Version: %s", version_ptr->c_str());
        } else {
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "DLSS-G Version: Unknown");
        }
    }

    ImGui::Spacing();

    // Streamline Event Counters
    ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "Streamline Event Counters:");
    ImGui::Separator();

    uint32_t sl_init_count = g_streamline_event_counters[STREAMLINE_EVENT_SL_INIT].load();
    uint32_t sl_feature_count = g_streamline_event_counters[STREAMLINE_EVENT_SL_IS_FEATURE_SUPPORTED].load();
    uint32_t sl_interface_count = g_streamline_event_counters[STREAMLINE_EVENT_SL_GET_NATIVE_INTERFACE].load();
    uint32_t sl_upgrade_count = g_streamline_event_counters[STREAMLINE_EVENT_SL_UPGRADE_INTERFACE].load();

    ImGui::Text("slInit calls: %u", sl_init_count);
    ImGui::Text("slIsFeatureSupported calls: %u", sl_feature_count);
    ImGui::Text("slGetNativeInterface calls: %u", sl_interface_count);
    ImGui::Text("slUpgradeInterface calls: %u", sl_upgrade_count);

    ImGui::Spacing();

    // DLSS Override Settings
    ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "DLSS Override Settings:");
    ImGui::Separator();

    // Enable DLSS Override
    bool dlss_override_enabled = settings::g_streamlineTabSettings.dlss_override_enabled.GetValue();
    if (ImGui::Checkbox("Enable DLSS Override", &dlss_override_enabled)) {
        settings::g_streamlineTabSettings.dlss_override_enabled.SetValue(dlss_override_enabled);
    }

    if (dlss_override_enabled) {
        ImGui::Indent();

        // Folder selection
        ImGui::Text("Override Folder:");
        static char folder_input_buffer[512] = "";
        static bool folder_input_initialized = false;

        // Initialize the input buffer with current value
        if (!folder_input_initialized) {
            std::string current_folder = settings::g_streamlineTabSettings.dlss_override_folder.GetValue();
            strncpy_s(folder_input_buffer, current_folder.c_str(), sizeof(folder_input_buffer) - 1);
            folder_input_buffer[sizeof(folder_input_buffer) - 1] = '\0';
            folder_input_initialized = true;
        }

        ImGui::InputText("##dlss_override_folder", folder_input_buffer, sizeof(folder_input_buffer));

        // Update settings when text changes
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            settings::g_streamlineTabSettings.dlss_override_folder.SetValue(std::string(folder_input_buffer));
        }

        // Help text
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Enter folder path (e.g., C:\\MyDLSSFiles). Place your DLL files in this folder.");

        // Show current folder status
        std::string current_folder = settings::g_streamlineTabSettings.dlss_override_folder.GetValue();
        if (!current_folder.empty()) {
            bool folder_exists = std::filesystem::exists(current_folder);
            if (folder_exists) {
                ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), ICON_FK_OK " Folder exists: %s", current_folder.c_str());

                // Check for expected DLL files
                std::vector<std::string> expected_dlls = {"nvngx_dlss.dll", "nvngx_dlssd.dll", "nvngx_dlssg.dll"};
                std::vector<std::string> found_dlls;

                for (const auto& dll_name : expected_dlls) {
                    std::string dll_path = current_folder + "\\" + dll_name;
                    if (std::filesystem::exists(dll_path)) {
                        found_dlls.push_back(dll_name);
                    }
                }

                if (!found_dlls.empty()) {
                    ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "Found DLLs: %s",
                        std::accumulate(found_dlls.begin(), found_dlls.end(), std::string(""),
                            [](const std::string& a, const std::string& b) { return a + (a.empty() ? "" : ", ") + b; }).c_str());
                } else {
                    ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "No DLSS DLL files found in folder");
                }
            } else {
                ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), ICON_FK_WARNING " Folder not found: %s", current_folder.c_str());
            }
        }

        ImGui::Spacing();

        // Individual DLL overrides
        ImGui::Text("Override DLLs:");

        bool dlss_override_dlss = settings::g_streamlineTabSettings.dlss_override_dlss.GetValue();
        if (ImGui::Checkbox("Override nvngx_dlss.dll (DLSS)", &dlss_override_dlss)) {
            settings::g_streamlineTabSettings.dlss_override_dlss.SetValue(dlss_override_dlss);
        }

        bool dlss_override_dlss_fg = settings::g_streamlineTabSettings.dlss_override_dlss_fg.GetValue();
        if (ImGui::Checkbox("Override nvngx_dlssd.dll (DLSS-FG)", &dlss_override_dlss_fg)) {
            settings::g_streamlineTabSettings.dlss_override_dlss_fg.SetValue(dlss_override_dlss_fg);
        }

        bool dlss_override_dlss_rr = settings::g_streamlineTabSettings.dlss_override_dlss_rr.GetValue();
        if (ImGui::Checkbox("Override nvngx_dlssg.dll (DLSS-RR)", &dlss_override_dlss_rr)) {
            settings::g_streamlineTabSettings.dlss_override_dlss_rr.SetValue(dlss_override_dlss_rr);
        }

        ImGui::Unindent();
    }

    ImGui::Spacing();

    // DLSS DLL Detection (simple approach)
    ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "DLSS DLL Detection:");
    ImGui::Separator();

    // Check for common DLSS DLLs
    const wchar_t* dlss_dlls[] = {
        L"nvngx_dlss.dll",
        L"nvngx_dlssg.dll",
        L"nvngx_dlssd.dll"
    };

    for (const auto& dll_name : dlss_dlls) {
        HMODULE dll_handle = GetModuleHandleW(dll_name);
        if (dll_handle != nullptr) {
            // Get the full path of the loaded DLL
            wchar_t dll_path[MAX_PATH];
            DWORD path_length = GetModuleFileNameW(dll_handle, dll_path, MAX_PATH);

            if (path_length > 0) {
                // Get version information
                std::string version_str = GetDLLVersionString(std::wstring(dll_path));
                ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "%S: Loaded (0x%p)", dll_name, dll_handle);
                ImGui::Text("  Version: %s", version_str.c_str());
            } else {
                ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "%S: Loaded (0x%p)", dll_name, dll_handle);
                ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "  Version: Unable to get path");
            }
        } else {
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "%S: Not loaded", dll_name);
        }
    }
}

} // namespace ui::new_ui
