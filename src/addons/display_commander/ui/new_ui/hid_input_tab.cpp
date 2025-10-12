#include "hid_input_tab.hpp"
#include "../../hooks/hid_hooks.hpp"
#include "../../utils.hpp"
#include "../../settings/experimental_tab_settings.hpp"
#include "../../globals.hpp"
#include <imgui.h>
#include <reshade.hpp>
#include <chrono>

namespace ui::new_ui {

void InitHidInputTab() {
    LogInfo("Initializing HID Input tab");

    // Check if HID suppression is enabled from settings
    bool suppression_enabled = settings::g_experimentalTabSettings.suppress_hid_devices.GetValue();
    if (suppression_enabled) {
        LogInfo("HID suppression enabled from settings");
    }
}

void DrawHidInputTab() {
    ImGui::TextColored(ImVec4(0.8f, 1.0f, 0.8f, 1.0f), "=== HID Input Monitoring ===");
    ImGui::Text("Monitor HID device file reads and input activity");
    ImGui::Separator();

    // Hook status
    bool hooks_installed = display_commanderhooks::AreHidHooksInstalled();
    ImGui::TextColored(hooks_installed ? ImVec4(0.0f, 1.0f, 0.0f, 1.0f) : ImVec4(1.0f, 0.0f, 0.0f, 1.0f),
                       "HID Hooks Status: %s", hooks_installed ? "Installed" : "Not Installed");

    ImGui::Spacing();

    // Control buttons
    if (ImGui::Button("Install HID Hooks")) {
        if (display_commanderhooks::InstallHidHooks()) {
            LogInfo("HID hooks installed successfully");
        } else {
            LogError("Failed to install HID hooks");
        }
    }
    ImGui::SameLine();

    if (ImGui::Button("Uninstall HID Hooks")) {
        display_commanderhooks::UninstallHidHooks();
    }
    ImGui::SameLine();

    if (ImGui::Button("Reset Statistics")) {
        display_commanderhooks::ResetHidStatistics();
    }
    ImGui::SameLine();

    if (ImGui::Button("Clear File History")) {
        display_commanderhooks::ClearHidFileHistory();
    }
    ImGui::SameLine();

    if (ImGui::Button("Reset Suppression Stats")) {
        display_commanderhooks::ResetHidSuppressionStats();
    }

    ImGui::Spacing();
    ImGui::Separator();

    // HID Suppression Controls
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "HID Suppression:");

    // Use settings system for persistence
    bool suppression_enabled = settings::g_experimentalTabSettings.suppress_hid_devices.GetValue();
    if (ImGui::Checkbox("Suppress HID Device Access (needs restart)", &suppression_enabled)) {
        settings::g_experimentalTabSettings.suppress_hid_devices.SetValue(suppression_enabled);
        LogInfo("HID suppression toggled: %s", suppression_enabled ? "enabled" : "disabled");
    }

    if (suppression_enabled) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "ACTIVE");

        uint64_t suppressed_calls = display_commanderhooks::GetHidSuppressedCallsCount();
        ImGui::Text("Suppressed Calls: %llu", suppressed_calls);

        ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.0f, 1.0f), "Warning: This will block all HID device access!");
    } else {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "INACTIVE");
    }

    ImGui::Spacing();
    ImGui::Separator();

    // Overall statistics
    const auto& hook_stats = display_commanderhooks::GetHidHookStats();
    ImGui::TextColored(ImVec4(0.8f, 0.8f, 1.0f, 1.0f), "Overall Statistics:");
    ImGui::Text("Total ReadFileEx Calls: %llu", hook_stats.total_readfileex_calls.load());
    ImGui::Text("Files Tracked: %llu", hook_stats.total_files_tracked.load());
    ImGui::Text("Total Bytes Read: %llu", hook_stats.total_bytes_read.load());

    if (suppression_enabled) {
        ImGui::Text("Suppressed Calls: %llu", display_commanderhooks::GetHidSuppressedCallsCount());
    }

    // Individual API Statistics
    ImGui::Spacing();
    ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.0f, 1.0f), "API Call Statistics:");

    // SetupDi APIs
    ImGui::Text("SetupDiGetClassDevs: %llu calls, %llu suppressed",
                hook_stats.setupdi_getclassdevs_calls.load(),
                hook_stats.setupdi_getclassdevs_suppressed.load());
    ImGui::Text("SetupDiEnumDeviceInterfaces: %llu calls, %llu suppressed",
                hook_stats.setupdi_enumdeviceinterfaces_calls.load(),
                hook_stats.setupdi_enumdeviceinterfaces_suppressed.load());
    ImGui::Text("SetupDiGetDeviceInterfaceDetail: %llu calls, %llu suppressed",
                hook_stats.setupdi_getdeviceinterfacedetail_calls.load(),
                hook_stats.setupdi_getdeviceinterfacedetail_suppressed.load());
    ImGui::Text("SetupDiEnumDeviceInfo: %llu calls, %llu suppressed",
                hook_stats.setupdi_enumdeviceinfo_calls.load(),
                hook_stats.setupdi_enumdeviceinfo_suppressed.load());
    ImGui::Text("SetupDiGetDeviceRegistryProperty: %llu calls, %llu suppressed",
                hook_stats.setupdi_getdeviceregistryproperty_calls.load(),
                hook_stats.setupdi_getdeviceregistryproperty_suppressed.load());

    // HidD APIs
    ImGui::Text("HidD_GetHidGuid: %llu calls, %llu suppressed",
                hook_stats.hidd_gethidguid_calls.load(),
                hook_stats.hidd_gethidguid_suppressed.load());
    ImGui::Text("HidD_GetAttributes: %llu calls, %llu suppressed",
                hook_stats.hidd_getattributes_calls.load(),
                hook_stats.hidd_getattributes_suppressed.load());
    ImGui::Text("HidD_GetPreparsedData: %llu calls, %llu suppressed",
                hook_stats.hidd_getpreparseddata_calls.load(),
                hook_stats.hidd_getpreparseddata_suppressed.load());
    ImGui::Text("HidD_FreePreparsedData: %llu calls, %llu suppressed",
                hook_stats.hidd_freepreparseddata_calls.load(),
                hook_stats.hidd_freepreparseddata_suppressed.load());

    ImGui::Spacing();
    ImGui::Separator();

    // File statistics table
    const auto file_stats = display_commanderhooks::GetHidFileStats();

    if (file_stats.empty()) {
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "No HID device files tracked yet");
    } else {
        ImGui::TextColored(ImVec4(0.8f, 1.0f, 0.8f, 1.0f), "Tracked HID Device Files:");
        ImGui::Text("Total Files: %zu", file_stats.size());

        ImGui::Spacing();

        // Create table for file statistics
        if (ImGui::BeginTable("HidFileStats", 5,
                              ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable)) {

            // Table headers
            ImGui::TableSetupColumn("Device Path", ImGuiTableColumnFlags_WidthFixed, 300.0f);
            ImGui::TableSetupColumn("Read Count", ImGuiTableColumnFlags_WidthFixed, 120.0f);
            ImGui::TableSetupColumn("Bytes Read", ImGuiTableColumnFlags_WidthFixed, 120.0f);
            ImGui::TableSetupColumn("First Read", ImGuiTableColumnFlags_WidthFixed, 150.0f);
            ImGui::TableSetupColumn("Last Read", ImGuiTableColumnFlags_WidthFixed, 150.0f);
            ImGui::TableHeadersRow();

            // Display statistics for each file
            for (const auto& pair : file_stats) {
                const auto& stats = pair.second;
                ImGui::TableNextRow();

                // Device path
                ImGui::TableSetColumnIndex(0);
                ImGui::Text("%s", stats.file_path.c_str());

                // Read count
                ImGui::TableSetColumnIndex(1);
                ImGui::Text("%llu", stats.read_count.load());

                // Bytes read
                ImGui::TableSetColumnIndex(2);
                ImGui::Text("%llu", stats.bytes_read.load());

                // First read time
                ImGui::TableSetColumnIndex(3);
                auto now = std::chrono::steady_clock::now();
                auto first_duration = std::chrono::duration_cast<std::chrono::milliseconds>(now - stats.first_read);
                ImGui::Text("%lld ms ago", first_duration.count());

                // Last read time
                ImGui::TableSetColumnIndex(4);
                auto last_duration = std::chrono::duration_cast<std::chrono::milliseconds>(now - stats.last_read);
                if (last_duration.count() < 1000) {
                    ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "%lld ms ago", last_duration.count());
                } else if (last_duration.count() < 10000) {
                    ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "%lld ms ago", last_duration.count());
                } else {
                    ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "%lld ms ago", last_duration.count());
                }
            }

            ImGui::EndTable();
        }

        ImGui::Spacing();

        // Summary statistics
        uint64_t total_reads = 0;
        uint64_t total_bytes = 0;
        auto oldest_read = std::chrono::steady_clock::now();
        auto newest_read = std::chrono::steady_clock::time_point{};

        for (const auto& pair : file_stats) {
            const auto& stats = pair.second;
            total_reads += stats.read_count.load();
            total_bytes += stats.bytes_read.load();

            if (stats.first_read < oldest_read) {
                oldest_read = stats.first_read;
            }
            if (stats.last_read > newest_read) {
                newest_read = stats.last_read;
            }
        }

        ImGui::TextColored(ImVec4(0.8f, 0.8f, 1.0f, 1.0f), "Summary:");
        ImGui::Text("Total Reads: %llu", total_reads);
        ImGui::Text("Total Bytes: %llu", total_bytes);

        if (total_reads > 0) {
            float avg_bytes_per_read = static_cast<float>(total_bytes) / static_cast<float>(total_reads);
            ImGui::Text("Average Bytes per Read: %.2f", avg_bytes_per_read);
        }

        if (newest_read > std::chrono::steady_clock::time_point{}) {
            auto session_duration = std::chrono::duration_cast<std::chrono::seconds>(newest_read - oldest_read);
            ImGui::Text("Session Duration: %lld seconds", session_duration.count());

            if (session_duration.count() > 0) {
                float reads_per_second = static_cast<float>(total_reads) / static_cast<float>(session_duration.count());
                ImGui::Text("Reads per Second: %.2f", reads_per_second);
            }
        }
    }

    ImGui::Spacing();
    ImGui::Separator();

    // Information section
    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Information:");
    ImGui::TextWrapped("This tab monitors HID device file reads through ReadFileEx hooks. "
                      "It tracks which device files are being accessed, how often, and how much data is being read. "
                      "This can help identify input devices and their usage patterns.");

    ImGui::Spacing();
    ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "Note: This feature is experimental and may impact performance.");
}

} // namespace ui::new_ui
