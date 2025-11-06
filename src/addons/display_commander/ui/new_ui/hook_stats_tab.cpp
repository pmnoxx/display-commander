#include "hook_stats_tab.hpp"
#include "../../hooks/windows_hooks/windows_message_hooks.hpp"
#include "../../hooks/dinput_hooks.hpp"
#include "../../hooks/opengl_hooks.hpp"
#include "../../hooks/display_settings_hooks.hpp"
#include "../../hooks/hid_statistics.hpp"
#include "../../settings/experimental_tab_settings.hpp"
#include "../../globals.hpp"
#include "../../utils/timing.hpp"

#include "../../res/forkawesome.h"
#include <reshade_imgui.hpp>

namespace ui::new_ui {

void DrawHookStatsTab() {
    ImGui::TextColored(ImVec4(0.8f, 1.0f, 0.8f, 1.0f), "=== Hook Call Statistics ===");
    ImGui::Text("Track the number of times each Windows message hook was called");
    ImGui::Separator();

    // Reset button
    if (ImGui::Button("Reset All Statistics")) {
        display_commanderhooks::ResetAllHookStats();
    }
    ImGui::SameLine();
    ImGui::Text("Click to reset all counters to zero");

    ImGui::Spacing();
    ImGui::Separator();

    // Define DLL groups using the new enum-based system
    static const display_commanderhooks::DllGroup DLL_GROUPS[] = {
        display_commanderhooks::DllGroup::USER32,
        display_commanderhooks::DllGroup::XINPUT1_4,
        display_commanderhooks::DllGroup::KERNEL32,
        display_commanderhooks::DllGroup::DINPUT8,
        display_commanderhooks::DllGroup::DINPUT,
        display_commanderhooks::DllGroup::OPENGL,
        display_commanderhooks::DllGroup::DISPLAY_SETTINGS,
        display_commanderhooks::DllGroup::HID_API
    };

    // Display statistics grouped by DLL
    int hook_count = display_commanderhooks::GetHookCount();

    for (const auto& group : DLL_GROUPS) {
        // Calculate group totals
        uint64_t group_total_calls = 0;
        uint64_t group_unsuppressed_calls = 0;
        int group_hook_count = 0;

        // Iterate through all hooks and find those belonging to this DLL group
        for (int i = 0; i < hook_count; ++i) {
            if (display_commanderhooks::GetHookDllGroup(i) == group) {
                const auto &stats = display_commanderhooks::GetHookStats(i);
                group_total_calls += stats.total_calls.load();
                group_unsuppressed_calls += stats.unsuppressed_calls.load();
                group_hook_count++;
            }
        }

        uint64_t group_suppressed_calls = group_total_calls - group_unsuppressed_calls;

        // Get DLL group name
        const char* group_name = display_commanderhooks::GetDllGroupName(group);

        // Create collapsible header for each DLL group
        ImGui::PushID(group_name);

        bool group_open = ImGui::CollapsingHeader(
            group_name,
            ImGuiTreeNodeFlags_DefaultOpen |
            (group_total_calls > 0 ? ImGuiTreeNodeFlags_None : ImGuiTreeNodeFlags_Leaf)
        );

        // Show group summary in header
        if (group_total_calls > 0) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f),
                "(%llu calls, %.1f%% suppressed)",
                group_total_calls,
                group_total_calls > 0 ? static_cast<float>(group_suppressed_calls) / static_cast<float>(group_total_calls) * 100.0f : 0.0f);
        }

        if (group_open) {
            ImGui::Indent();

            // Create table for this DLL's hooks
            if (ImGui::BeginTable("HookStats", 4,
                                  ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable)) {
                // Table headers
                ImGui::TableSetupColumn("Hook Name", ImGuiTableColumnFlags_WidthFixed, 400.0f);
                ImGui::TableSetupColumn("Total Calls", ImGuiTableColumnFlags_WidthFixed, 120.0f);
                ImGui::TableSetupColumn("Unsuppressed Calls", ImGuiTableColumnFlags_WidthFixed, 150.0f);
                ImGui::TableSetupColumn("Suppressed Calls", ImGuiTableColumnFlags_WidthFixed, 150.0f);
                ImGui::TableHeadersRow();

                // Display statistics for each hook in this group
                for (int i = 0; i < hook_count; ++i) {
                    if (display_commanderhooks::GetHookDllGroup(i) == group) {
                        const auto &stats = display_commanderhooks::GetHookStats(i);
                        const char *hook_name = display_commanderhooks::GetHookName(i);

                        uint64_t total_calls = stats.total_calls.load();
                        uint64_t unsuppressed_calls = stats.unsuppressed_calls.load();
                        uint64_t suppressed_calls = total_calls - unsuppressed_calls;

                        ImGui::TableNextRow();

                        // Hook name
                        ImGui::TableSetColumnIndex(0);
                        ImGui::Text("%s", hook_name);

                        // Total calls
                        ImGui::TableSetColumnIndex(1);
                        ImGui::Text("%llu", total_calls);

                        // Unsuppressed calls
                        ImGui::TableSetColumnIndex(2);
                        ImGui::Text("%llu", unsuppressed_calls);

                        // Suppressed calls
                        ImGui::TableSetColumnIndex(3);
                        if (suppressed_calls > 0) {
                            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.5f, 1.0f), "%llu", suppressed_calls);
                        } else {
                            ImGui::Text("%llu", suppressed_calls);
                        }
                    }
                }

                ImGui::EndTable();
            }

            ImGui::Unindent();
        }

        ImGui::PopID();
        ImGui::Spacing();
    }

    ImGui::Spacing();
    ImGui::Separator();

    // Summary statistics
    ImGui::TextColored(ImVec4(0.8f, 0.8f, 1.0f, 1.0f), "Summary:");

    uint64_t total_all_calls = 0;
    uint64_t total_unsuppressed_calls = 0;

    // Calculate totals across all DLL groups
    for (const auto& group : DLL_GROUPS) {
        for (int i = 0; i < hook_count; ++i) {
            if (display_commanderhooks::GetHookDllGroup(i) == group) {
                const auto &stats = display_commanderhooks::GetHookStats(i);
                total_all_calls += stats.total_calls.load();
                total_unsuppressed_calls += stats.unsuppressed_calls.load();
            }
        }
    }

    uint64_t total_suppressed_calls = total_all_calls - total_unsuppressed_calls;

    ImGui::Text("Total Hook Calls: %llu", total_all_calls);
    ImGui::Text("Unsuppressed Calls: %llu", total_unsuppressed_calls);
    ImGui::Text("Suppressed Calls: %llu", total_suppressed_calls);

    if (total_all_calls > 0) {
        float suppression_rate = static_cast<float>(total_suppressed_calls) / static_cast<float>(total_all_calls) * 100.0f;
        ImGui::Text("Suppression Rate: %.2f%%", suppression_rate);
    }

    ImGui::Spacing();
    ImGui::Separator();

    // DirectInput Hook Suppression
    ImGui::TextColored(ImVec4(0.8f, 1.0f, 0.8f, 1.0f), "=== DirectInput Hook Controls ===");
    ImGui::Text("Control DirectInput hook behavior and suppression");
    ImGui::Separator();

    // DirectInput hook suppression checkbox
    bool suppress_dinput = settings::g_experimentalTabSettings.suppress_dinput_hooks.GetValue();
    if (ImGui::Checkbox("Suppress DirectInput Hooks", &suppress_dinput)) {
        settings::g_experimentalTabSettings.suppress_dinput_hooks.SetValue(suppress_dinput);
        // Update the global atomic variable
        s_suppress_dinput_hooks.store(suppress_dinput);
    }
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "(Disable DirectInput hook processing)");

    // DirectInput device state blocking checkbox
    bool dinput_device_state_blocking = settings::g_experimentalTabSettings.dinput_device_state_blocking.GetValue();
    if (ImGui::Checkbox("DirectInput Device State Blocking", &dinput_device_state_blocking)) {
        settings::g_experimentalTabSettings.dinput_device_state_blocking.SetValue(dinput_device_state_blocking);
    }
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "(Block mouse/keyboard input via DirectInput)");

    // Show device hook count
    int device_hook_count = display_commanderhooks::GetDirectInputDeviceHookCount();
    ImGui::Text("Hooked Devices: %d", device_hook_count);

    // Manual hook button
    if (ImGui::Button("Hook All DirectInput Devices")) {
        display_commanderhooks::HookAllDirectInputDevices();
    }
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "(Manually hook existing devices)");

    ImGui::Spacing();
    ImGui::Separator();

    // DirectInput Device Information
    ImGui::TextColored(ImVec4(0.8f, 1.0f, 0.8f, 1.0f), "=== DirectInput Device Information ===");
    ImGui::Text("Track DirectInput device creation and connection status");
    ImGui::Separator();

    const auto& devices = display_commanderhooks::GetDInputDevices();

    if (devices.empty()) {
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "No DirectInput devices created yet");
    } else {
        ImGui::Text("Created Devices: %zu", devices.size());

        if (ImGui::BeginTable("DInputDevices", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable)) {
            ImGui::TableSetupColumn("Device Name", ImGuiTableColumnFlags_WidthFixed, 150.0f);
            ImGui::TableSetupColumn("Device Type", ImGuiTableColumnFlags_WidthFixed, 120.0f);
            ImGui::TableSetupColumn("Interface", ImGuiTableColumnFlags_WidthFixed, 150.0f);
            ImGui::TableSetupColumn("Creation Time", ImGuiTableColumnFlags_WidthFixed, 200.0f);
            ImGui::TableHeadersRow();

            for (const auto& device : devices) {
                ImGui::TableNextRow();

                ImGui::TableSetColumnIndex(0);
                ImGui::Text("%s", device.device_name.c_str());

                ImGui::TableSetColumnIndex(1);
                // Convert device type to string
                std::string device_type_name;
                switch (device.device_type) {
                    case 0x00000000: device_type_name = "Keyboard"; break;
                    case 0x00000001: device_type_name = "Mouse"; break;
                    case 0x00000002: device_type_name = "Joystick"; break;
                    case 0x00000003: device_type_name = "Gamepad"; break;
                    case 0x00000004: device_type_name = "Generic Device"; break;
                    default: device_type_name = "Unknown Device"; break;
                }
                ImGui::Text("%s", device_type_name.c_str());

                ImGui::TableSetColumnIndex(2);
                ImGui::Text("%s", device.interface_name.c_str());

                ImGui::TableSetColumnIndex(3);
                LONGLONG now = utils::get_now_ns();
                LONGLONG duration_ns = now - device.creation_time;
                LONGLONG duration_ms = duration_ns / utils::NS_TO_MS;
                ImGui::Text("%lld ms ago", duration_ms);
            }

            ImGui::EndTable();
        }

        if (ImGui::Button("Clear Device History")) {
            display_commanderhooks::ClearDInputDevices();
        }
    }

    ImGui::Spacing();
    ImGui::Separator();




    // HID Device Type Statistics
    ImGui::TextColored(ImVec4(0.8f, 1.0f, 0.8f, 1.0f), "=== HID Device Type Statistics ===");
    ImGui::Text("Track different types of HID devices accessed");
    ImGui::Separator();

    const auto& device_stats = display_commanderhooks::GetHIDDeviceStats();
    uint64_t total_devices = device_stats.total_devices.load();
    uint64_t dualsense_devices = device_stats.dualsense_devices.load();
    uint64_t xbox_devices = device_stats.xbox_devices.load();
    uint64_t generic_devices = device_stats.generic_hid_devices.load();
    uint64_t unknown_devices = device_stats.unknown_devices.load();

    ImGui::Text("Total HID Devices: %llu", total_devices);
    ImGui::Text("DualSense Controllers: %llu", dualsense_devices);
    ImGui::Text("Xbox Controllers: %llu", xbox_devices);
    ImGui::Text("Generic HID Devices: %llu", generic_devices);
    ImGui::Text("Unknown Devices: %llu", unknown_devices);

    if (total_devices > 0) {
        float dualsense_rate = static_cast<float>(dualsense_devices) / static_cast<float>(total_devices) * 100.0f;
        float xbox_rate = static_cast<float>(xbox_devices) / static_cast<float>(total_devices) * 100.0f;
        float generic_rate = static_cast<float>(generic_devices) / static_cast<float>(total_devices) * 100.0f;
        float unknown_rate = static_cast<float>(unknown_devices) / static_cast<float>(total_devices) * 100.0f;

        ImGui::Spacing();
        ImGui::Text("Device Distribution:");
        ImGui::Text("DualSense: %.2f%%", dualsense_rate);
        ImGui::Text("Xbox: %.2f%%", xbox_rate);
        ImGui::Text("Generic HID: %.2f%%", generic_rate);
        ImGui::Text("Unknown: %.2f%%", unknown_rate);
    }
}

} // namespace ui::new_ui
