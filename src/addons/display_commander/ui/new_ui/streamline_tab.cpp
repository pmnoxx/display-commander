#include "streamline_tab.hpp"
#include "../../globals.hpp"
#include "../../hooks/streamline_hooks.hpp"
#include "../../utils.hpp"

#include <imgui.h>
#include <windows.h>
#include <string>

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

    bool dlls_g_loaded = g_dlls_g_loaded.load();
    ImGui::Text("DLSS-G Loaded: %s", dlls_g_loaded ? "Yes" : "No");

    if (dlls_g_loaded) {
        auto version_ptr = g_dlls_g_version.load();
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

    uint32_t sl_init_count = g_swapchain_event_counters[SWAPCHAIN_EVENT_STREAMLINE_SL_INIT].load();
    uint32_t sl_feature_count = g_swapchain_event_counters[SWAPCHAIN_EVENT_STREAMLINE_SL_IS_FEATURE_SUPPORTED].load();
    uint32_t sl_interface_count = g_swapchain_event_counters[SWAPCHAIN_EVENT_STREAMLINE_SL_GET_NATIVE_INTERFACE].load();
    uint32_t sl_upgrade_count = g_swapchain_event_counters[SWAPCHAIN_EVENT_STREAMLINE_SL_UPGRADE_INTERFACE].load();

    ImGui::Text("slInit calls: %u", sl_init_count);
    ImGui::Text("slIsFeatureSupported calls: %u", sl_feature_count);
    ImGui::Text("slGetNativeInterface calls: %u", sl_interface_count);
    ImGui::Text("slUpgradeInterface calls: %u", sl_upgrade_count);

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
