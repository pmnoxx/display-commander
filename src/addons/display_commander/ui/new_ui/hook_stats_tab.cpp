#include "hook_stats_tab.hpp"
#include "../../hooks/windows_hooks/windows_message_hooks.hpp"
#include "../../hooks/dinput_hooks.hpp"
#include "../../hooks/opengl_hooks.hpp"
#include "../../hooks/display_settings_hooks.hpp"
#include "../../hooks/hid_statistics.hpp"
#include "../../settings/experimental_tab_settings.hpp"
#include "../../globals.hpp"

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

    // Define DLL groupings for hooks
    struct DllGroup {
        const char* name;
        int start_index;
        int end_index;
    };

    static const DllGroup DLL_GROUPS[] = {
        {.name = "user32.dll", .start_index = 0, .end_index = 34},      // GetMessageA to DisplayConfigGetDeviceInfo
        {.name = "xinput1_4.dll", .start_index = 35, .end_index = 36},  // XInputGetState, XInputGetStateEx
        {.name = "kernel32.dll", .start_index = 37, .end_index = 42},   // Sleep, SleepEx, WaitForSingleObject, WaitForMultipleObjects, SetUnhandledExceptionFilter, IsDebuggerPresent
        {.name = "dinput8.dll", .start_index = 43, .end_index = 43},    // DirectInput8Create
        {.name = "dinput.dll", .start_index = 44, .end_index = 44}      // DirectInputCreate
    };

    // Display statistics grouped by DLL
    int hook_count = display_commanderhooks::GetHookCount();

    for (const auto& group : DLL_GROUPS) {
        // Calculate group totals
        uint64_t group_total_calls = 0;
        uint64_t group_unsuppressed_calls = 0;
        int group_hook_count = 0;

        for (int i = group.start_index; i <= group.end_index && i < hook_count; ++i) {
            const auto &stats = display_commanderhooks::GetHookStats(i);
            group_total_calls += stats.total_calls.load();
            group_unsuppressed_calls += stats.unsuppressed_calls.load();
            group_hook_count++;
        }

        uint64_t group_suppressed_calls = group_total_calls - group_unsuppressed_calls;

        // Create collapsible header for each DLL group
        ImGui::PushID(group.name);

        bool group_open = ImGui::CollapsingHeader(
            group.name,
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
                ImGui::TableSetupColumn("Hook Name", ImGuiTableColumnFlags_WidthFixed, 200.0f);
                ImGui::TableSetupColumn("Total Calls", ImGuiTableColumnFlags_WidthFixed, 120.0f);
                ImGui::TableSetupColumn("Unsuppressed Calls", ImGuiTableColumnFlags_WidthFixed, 150.0f);
                ImGui::TableSetupColumn("Suppressed Calls", ImGuiTableColumnFlags_WidthFixed, 150.0f);
                ImGui::TableHeadersRow();

                // Display statistics for each hook in this group
                for (int i = group.start_index; i <= group.end_index && i < hook_count; ++i) {
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
        for (int i = group.start_index; i <= group.end_index && i < hook_count; ++i) {
            const auto &stats = display_commanderhooks::GetHookStats(i);
            total_all_calls += stats.total_calls.load();
            total_unsuppressed_calls += stats.unsuppressed_calls.load();
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
                auto now = std::chrono::steady_clock::now();
                auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(now - device.creation_time);
                ImGui::Text("%lld ms ago", duration.count());
            }

            ImGui::EndTable();
        }

        if (ImGui::Button("Clear Device History")) {
            display_commanderhooks::ClearDInputDevices();
        }
    }

    ImGui::Spacing();
    ImGui::Separator();

    // OpenGL Hook Statistics
    ImGui::TextColored(ImVec4(0.8f, 1.0f, 0.8f, 1.0f), "=== OpenGL Hook Statistics ===");
    ImGui::Text("Track the number of times each OpenGL/WGL function was called");
    ImGui::Separator();

    // Reset OpenGL statistics button
    if (ImGui::Button("Reset OpenGL Statistics")) {
        for (int i = 0; i < NUM_OPENGL_HOOKS; ++i) {
            g_opengl_hook_counters[i].store(0);
        }
        g_opengl_hook_total_count.store(0);
    }
    ImGui::SameLine();
    ImGui::Text("Click to reset all OpenGL counters to zero");

    ImGui::Spacing();
    ImGui::Separator();

    // Function names array
    static const char* opengl_function_names[] = {
        "wglSwapBuffers",
        "wglMakeCurrent",
        "wglCreateContext",
        "wglDeleteContext",
        "wglChoosePixelFormat",
        "wglSetPixelFormat",
        "wglGetPixelFormat",
        "wglDescribePixelFormat",
        "wglCreateContextAttribsARB",
        "wglChoosePixelFormatARB",
        "wglGetPixelFormatAttribivARB",
        "wglGetPixelFormatAttribfvARB",
        "wglGetProcAddress",
        "wglSwapIntervalEXT",
        "wglGetSwapIntervalEXT"
    };

    uint64_t total_opengl_calls = 0;

    // Display OpenGL hook statistics in a table
    if (ImGui::BeginTable("OpenGLHooks", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable)) {
        ImGui::TableSetupColumn("Function Name", ImGuiTableColumnFlags_WidthFixed, 300.0f);
        ImGui::TableSetupColumn("Call Count", ImGuiTableColumnFlags_WidthFixed, 150.0f);
        ImGui::TableHeadersRow();
        for (int i = 0; i < NUM_OPENGL_HOOKS; ++i) {
            uint64_t call_count = g_opengl_hook_counters[i].load();
            total_opengl_calls += call_count;

            ImGui::TableNextRow();

            // Function name
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("%s", opengl_function_names[i]);

            // Call count
            ImGui::TableSetColumnIndex(1);
            if (call_count > 0) {
                ImGui::TextColored(ImVec4(0.8f, 1.0f, 0.8f, 1.0f), "%llu", call_count);
            } else {
                ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "%llu", call_count);
            }
        }

        ImGui::EndTable();
    }

    ImGui::Spacing();
    ImGui::Separator();

    // OpenGL summary statistics
    ImGui::TextColored(ImVec4(0.8f, 0.8f, 1.0f, 1.0f), "OpenGL Summary:");
    ImGui::Text("Total OpenGL Hook Calls: %llu", g_opengl_hook_total_count.load());
    ImGui::Text("OpenGL Functions Called: %llu", total_opengl_calls);

    // Show if OpenGL hooks are installed
    bool opengl_hooks_installed = AreOpenGLHooksInstalled();
    ImGui::Text("OpenGL Hooks Status: %s", opengl_hooks_installed ? "Installed" : "Not Installed");
    if (opengl_hooks_installed) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), ICON_FK_OK);
    } else {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), ICON_FK_CANCEL);
    }

    ImGui::Spacing();
    ImGui::Separator();

    // Display Settings Hook Statistics
    ImGui::TextColored(ImVec4(0.8f, 1.0f, 0.8f, 1.0f), "=== Display Settings Hook Statistics ===");
    ImGui::Text("Track the number of times each display settings function was called");
    ImGui::Separator();

    // Reset Display Settings statistics button
    if (ImGui::Button("Reset Display Settings Statistics")) {
        for (int i = 0; i < NUM_DISPLAY_SETTINGS_HOOKS; ++i) {
            g_display_settings_hook_counters[i].store(0);
        }
        g_display_settings_hook_total_count.store(0);
    }
    ImGui::SameLine();
    ImGui::Text("Click to reset all display settings counters to zero");

    ImGui::Spacing();
    ImGui::Separator();

    // Function names array
    static const char* display_settings_function_names[] = {
        "ChangeDisplaySettingsA",
        "ChangeDisplaySettingsW",
        "ChangeDisplaySettingsExA",
        "ChangeDisplaySettingsExW",
        "SetWindowPos",
        "ShowWindow",
        "SetWindowLongA",
        "SetWindowLongW",
        "SetWindowLongPtrA",
        "SetWindowLongPtrW"
    };

    uint64_t total_display_settings_calls = 0;

    // Display display settings hook statistics in a table
    if (ImGui::BeginTable("DisplaySettingsHooks", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable)) {
        ImGui::TableSetupColumn("Function Name", ImGuiTableColumnFlags_WidthFixed, 300.0f);
        ImGui::TableSetupColumn("Call Count", ImGuiTableColumnFlags_WidthFixed, 150.0f);
        ImGui::TableHeadersRow();
        for (int i = 0; i < NUM_DISPLAY_SETTINGS_HOOKS; ++i) {
            uint64_t call_count = g_display_settings_hook_counters[i].load();
            total_display_settings_calls += call_count;

            ImGui::TableNextRow();

            // Function name
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("%s", display_settings_function_names[i]);

            // Call count
            ImGui::TableSetColumnIndex(1);
            if (call_count > 0) {
                ImGui::TextColored(ImVec4(0.8f, 1.0f, 0.8f, 1.0f), "%llu", call_count);
            } else {
                ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "%llu", call_count);
            }
        }

        ImGui::EndTable();
    }

    ImGui::Spacing();
    ImGui::Separator();

    // Display settings summary statistics
    ImGui::TextColored(ImVec4(0.8f, 0.8f, 1.0f, 1.0f), "Display Settings Summary:");
    ImGui::Text("Total Display Settings Hook Calls: %llu", g_display_settings_hook_total_count.load());
    ImGui::Text("Display Settings Functions Called: %llu", total_display_settings_calls);

    // Show if display settings hooks are installed
    bool display_settings_hooks_installed = AreDisplaySettingsHooksInstalled();
    ImGui::Text("Display Settings Hooks Status: %s", display_settings_hooks_installed ? "Installed" : "Not Installed");
    if (display_settings_hooks_installed) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), ICON_FK_OK);
    } else {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), ICON_FK_CANCEL);
    }

    ImGui::Spacing();
    ImGui::Separator();

    // HID API Statistics
    ImGui::TextColored(ImVec4(0.8f, 1.0f, 0.8f, 1.0f), "=== HID API Statistics ===");
    ImGui::Text("Track HID device access calls (CreateFile, ReadFile, WriteFile, etc.)");
    ImGui::Separator();

    // Reset HID statistics button
    if (ImGui::Button("Reset HID Statistics")) {
        display_commanderhooks::ResetAllHIDStats();
    }
    ImGui::SameLine();
    ImGui::Text("Click to reset all HID counters to zero");

    ImGui::Spacing();
    ImGui::Separator();

    // Initialize HID statistics totals
    uint64_t total_hid_calls = 0;
    uint64_t total_successful = 0;
    uint64_t total_failed = 0;
    uint64_t total_blocked = 0;

    // Display HID API statistics in a table
    if (ImGui::BeginTable("HIDAPIStats", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable)) {
        ImGui::TableSetupColumn("API Function", ImGuiTableColumnFlags_WidthFixed, 200.0f);
        ImGui::TableSetupColumn("Total Calls", ImGuiTableColumnFlags_WidthFixed, 100.0f);
        ImGui::TableSetupColumn("Successful", ImGuiTableColumnFlags_WidthFixed, 100.0f);
        ImGui::TableSetupColumn("Failed", ImGuiTableColumnFlags_WidthFixed, 100.0f);
        ImGui::TableSetupColumn("Blocked", ImGuiTableColumnFlags_WidthFixed, 100.0f);
        ImGui::TableHeadersRow();

        for (int i = 0; i < display_commanderhooks::GetHIDAPICount(); ++i) {
            const auto& stats = display_commanderhooks::GetHIDAPIStats(static_cast<display_commanderhooks::HIDAPIType>(i));
            const char* api_name = display_commanderhooks::GetHIDAPIName(static_cast<display_commanderhooks::HIDAPIType>(i));

            uint64_t total_calls = stats.total_calls.load();
            uint64_t successful_calls = stats.successful_calls.load();
            uint64_t failed_calls = stats.failed_calls.load();
            uint64_t blocked_calls = stats.blocked_calls.load();

            total_hid_calls += total_calls;
            total_successful += successful_calls;
            total_failed += failed_calls;
            total_blocked += blocked_calls;

            ImGui::TableNextRow();

            // API name
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("%s", api_name);

            // Total calls
            ImGui::TableSetColumnIndex(1);
            if (total_calls > 0) {
                ImGui::TextColored(ImVec4(0.8f, 1.0f, 0.8f, 1.0f), "%llu", total_calls);
            } else {
                ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "%llu", total_calls);
            }

            // Successful calls
            ImGui::TableSetColumnIndex(2);
            if (successful_calls > 0) {
                ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "%llu", successful_calls);
            } else {
                ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "%llu", successful_calls);
            }

            // Failed calls
            ImGui::TableSetColumnIndex(3);
            if (failed_calls > 0) {
                ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "%llu", failed_calls);
            } else {
                ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "%llu", failed_calls);
            }

            // Blocked calls
            ImGui::TableSetColumnIndex(4);
            if (blocked_calls > 0) {
                ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "%llu", blocked_calls);
            } else {
                ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "%llu", blocked_calls);
            }
        }

        ImGui::EndTable();
    }

    ImGui::Spacing();
    ImGui::Separator();

    // HID API summary statistics
    ImGui::TextColored(ImVec4(0.8f, 0.8f, 1.0f, 1.0f), "HID API Summary:");
    ImGui::Text("Total HID API Calls: %llu", total_hid_calls);
    ImGui::Text("Successful Calls: %llu", total_successful);
    ImGui::Text("Failed Calls: %llu", total_failed);
    ImGui::Text("Blocked Calls: %llu", total_blocked);

    if (total_hid_calls > 0) {
        float success_rate = static_cast<float>(total_successful) / static_cast<float>(total_hid_calls) * 100.0f;
        float failure_rate = static_cast<float>(total_failed) / static_cast<float>(total_hid_calls) * 100.0f;
        float block_rate = static_cast<float>(total_blocked) / static_cast<float>(total_hid_calls) * 100.0f;
        ImGui::Text("Success Rate: %.2f%%", success_rate);
        ImGui::Text("Failure Rate: %.2f%%", failure_rate);
        ImGui::Text("Block Rate: %.2f%%", block_rate);
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
