#include "hook_stats_tab.hpp"
#include "../imgui_wrapper_base.hpp"
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

using namespace display_commander::ui::wrapper_flags;

void DrawHookStatsTab(display_commander::ui::IImGuiWrapper& imgui) {
    imgui.TextColored(ImVec4(0.8f, 1.0f, 0.8f, 1.0f), "=== Hook Call Statistics ===");
    imgui.Text("Track the number of times each Windows message hook was called");
    imgui.Separator();

    if (imgui.Button("Reset All Statistics")) {
        display_commanderhooks::ResetAllHookStats();
    }
    imgui.SameLine();
    imgui.TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Click to reset all counters to zero");

    imgui.Spacing();
    imgui.Separator();

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

    int hook_count = display_commanderhooks::GetHookCount();

    for (const auto& group : DLL_GROUPS) {
        uint64_t group_total_calls = 0;
        uint64_t group_unsuppressed_calls = 0;
        int group_hook_count = 0;

        for (int i = 0; i < hook_count; ++i) {
            if (display_commanderhooks::GetHookDllGroup(i) == group) {
                const auto &stats = display_commanderhooks::GetHookStats(i);
                group_total_calls += stats.total_calls.load();
                group_unsuppressed_calls += stats.unsuppressed_calls.load();
                group_hook_count++;
            }
        }

        uint64_t group_suppressed_calls = group_total_calls - group_unsuppressed_calls;
        const char* group_name = display_commanderhooks::GetDllGroupName(group);

        imgui.PushID(group_name);

        const int tree_flags = TreeNodeFlags_DefaultOpen |
            (group_total_calls > 0 ? TreeNodeFlags_None : TreeNodeFlags_Leaf);
        bool group_open = imgui.CollapsingHeader(group_name, tree_flags);

        if (group_total_calls > 0) {
            imgui.SameLine();
            imgui.TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f),
                "(%llu calls, %.1f%% suppressed)",
                group_total_calls,
                group_total_calls > 0 ? static_cast<float>(group_suppressed_calls) / static_cast<float>(group_total_calls) * 100.0f : 0.0f);
        }

        if (group_open) {
            imgui.Indent();

            const int table_flags = TableFlags_Borders | TableFlags_RowBg | TableFlags_Resizable;
            if (imgui.BeginTable("HookStats", 4, table_flags)) {
                imgui.TableSetupColumn("Hook Name", TableColumnFlags_WidthFixed, 400.0f);
                imgui.TableSetupColumn("Total Calls", TableColumnFlags_WidthFixed, 120.0f);
                imgui.TableSetupColumn("Unsuppressed Calls", TableColumnFlags_WidthFixed, 150.0f);
                imgui.TableSetupColumn("Suppressed Calls", TableColumnFlags_WidthFixed, 150.0f);
                imgui.TableHeadersRow();

                for (int i = 0; i < hook_count; ++i) {
                    if (display_commanderhooks::GetHookDllGroup(i) == group) {
                        const auto &stats = display_commanderhooks::GetHookStats(i);
                        const char *hook_name = display_commanderhooks::GetHookName(i);

                        uint64_t total_calls = stats.total_calls.load();
                        uint64_t unsuppressed_calls = stats.unsuppressed_calls.load();
                        uint64_t suppressed_calls = total_calls - unsuppressed_calls;

                        imgui.TableNextRow();

                        imgui.TableSetColumnIndex(0);
                        imgui.Text("%s", hook_name);

                        imgui.TableSetColumnIndex(1);
                        imgui.Text("%llu", total_calls);

                        imgui.TableSetColumnIndex(2);
                        imgui.Text("%llu", unsuppressed_calls);

                        imgui.TableSetColumnIndex(3);
                        if (suppressed_calls > 0) {
                            imgui.TextColored(ImVec4(1.0f, 0.5f, 0.5f, 1.0f), "%llu", suppressed_calls);
                        } else {
                            imgui.Text("%llu", suppressed_calls);
                        }
                    }
                }

                imgui.EndTable();
            }

            imgui.Unindent();
        }

        imgui.PopID();
        imgui.Spacing();
    }

    imgui.Spacing();
    imgui.Separator();

    imgui.TextColored(ImVec4(0.8f, 0.8f, 1.0f, 1.0f), "Summary:");

    uint64_t total_all_calls = 0;
    uint64_t total_unsuppressed_calls = 0;

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

    imgui.Text("Total Hook Calls: %llu", total_all_calls);
    imgui.Text("Unsuppressed Calls: %llu", total_unsuppressed_calls);
    imgui.Text("Suppressed Calls: %llu", total_suppressed_calls);

    if (total_all_calls > 0) {
        float suppression_rate = static_cast<float>(total_suppressed_calls) / static_cast<float>(total_all_calls) * 100.0f;
        imgui.Text("Suppression Rate: %.2f%%", suppression_rate);
    }

    imgui.Spacing();
    imgui.Separator();

    imgui.TextColored(ImVec4(0.8f, 1.0f, 0.8f, 1.0f), "=== DirectInput Hook Controls ===");
    imgui.Text("Control DirectInput hook behavior and suppression");
    imgui.Separator();

    bool suppress_dinput = settings::g_experimentalTabSettings.suppress_dinput_hooks.GetValue();
    if (imgui.Checkbox("Suppress DirectInput Hooks", &suppress_dinput)) {
        settings::g_experimentalTabSettings.suppress_dinput_hooks.SetValue(suppress_dinput);
        s_suppress_dinput_hooks.store(suppress_dinput);
    }
    imgui.SameLine();
    imgui.TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "(Disable DirectInput hook processing)");

    bool dinput_device_state_blocking = settings::g_experimentalTabSettings.dinput_device_state_blocking.GetValue();
    if (imgui.Checkbox("DirectInput Device State Blocking", &dinput_device_state_blocking)) {
        settings::g_experimentalTabSettings.dinput_device_state_blocking.SetValue(dinput_device_state_blocking);
    }
    imgui.SameLine();
    imgui.TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "(Block mouse/keyboard input via DirectInput)");

    int device_hook_count = display_commanderhooks::GetDirectInputDeviceHookCount();
    imgui.Text("Hooked Devices: %d", device_hook_count);

    if (imgui.Button("Hook All DirectInput Devices")) {
        display_commanderhooks::HookAllDirectInputDevices();
    }
    imgui.SameLine();
    imgui.TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "(Manually hook existing devices)");

    imgui.Spacing();
    imgui.Separator();

    imgui.TextColored(ImVec4(0.8f, 1.0f, 0.8f, 1.0f), "=== DirectInput Device Information ===");
    imgui.Text("Track DirectInput device creation and connection status");
    imgui.Separator();

    const auto& devices = display_commanderhooks::GetDInputDevices();

    if (devices.empty()) {
        imgui.TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "No DirectInput devices created yet");
    } else {
        imgui.Text("Created Devices: %zu", devices.size());

        const int table_flags = TableFlags_Borders | TableFlags_RowBg | TableFlags_Resizable;
        if (imgui.BeginTable("DInputDevices", 4, table_flags)) {
            imgui.TableSetupColumn("Device Name", TableColumnFlags_WidthFixed, 150.0f);
            imgui.TableSetupColumn("Device Type", TableColumnFlags_WidthFixed, 120.0f);
            imgui.TableSetupColumn("Interface", TableColumnFlags_WidthFixed, 150.0f);
            imgui.TableSetupColumn("Creation Time", TableColumnFlags_WidthFixed, 200.0f);
            imgui.TableHeadersRow();

            for (const auto& device : devices) {
                imgui.TableNextRow();

                imgui.TableSetColumnIndex(0);
                imgui.Text("%s", device.device_name.c_str());

                imgui.TableSetColumnIndex(1);
                std::string device_type_name;
                switch (device.device_type) {
                    case 0x00000000: device_type_name = "Keyboard"; break;
                    case 0x00000001: device_type_name = "Mouse"; break;
                    case 0x00000002: device_type_name = "Joystick"; break;
                    case 0x00000003: device_type_name = "Gamepad"; break;
                    case 0x00000004: device_type_name = "Generic Device"; break;
                    default: device_type_name = "Unknown Device"; break;
                }
                imgui.Text("%s", device_type_name.c_str());

                imgui.TableSetColumnIndex(2);
                imgui.Text("%s", device.interface_name.c_str());

                imgui.TableSetColumnIndex(3);
                LONGLONG now = utils::get_now_ns();
                LONGLONG duration_ns = now - device.creation_time;
                LONGLONG duration_ms = duration_ns / utils::NS_TO_MS;
                imgui.Text("%lld ms ago", duration_ms);
            }

            imgui.EndTable();
        }

        if (imgui.Button("Clear Device History")) {
            display_commanderhooks::ClearDInputDevices();
        }
    }

    imgui.Spacing();
    imgui.Separator();

    imgui.TextColored(ImVec4(0.8f, 1.0f, 0.8f, 1.0f), "=== HID Device Type Statistics ===");
    imgui.Text("Track different types of HID devices accessed");
    imgui.Separator();

    const auto& device_stats = display_commanderhooks::GetHIDDeviceStats();
    uint64_t total_devices = device_stats.total_devices.load();
    uint64_t dualsense_devices = device_stats.dualsense_devices.load();
    uint64_t xbox_devices = device_stats.xbox_devices.load();
    uint64_t generic_devices = device_stats.generic_hid_devices.load();
    uint64_t unknown_devices = device_stats.unknown_devices.load();

    imgui.Text("Total HID Devices: %llu", total_devices);
    imgui.Text("DualSense Controllers: %llu", dualsense_devices);
    imgui.Text("Xbox Controllers: %llu", xbox_devices);
    imgui.Text("Generic HID Devices: %llu", generic_devices);
    imgui.Text("Unknown Devices: %llu", unknown_devices);

    if (total_devices > 0) {
        float dualsense_rate = static_cast<float>(dualsense_devices) / static_cast<float>(total_devices) * 100.0f;
        float xbox_rate = static_cast<float>(xbox_devices) / static_cast<float>(total_devices) * 100.0f;
        float generic_rate = static_cast<float>(generic_devices) / static_cast<float>(total_devices) * 100.0f;
        float unknown_rate = static_cast<float>(unknown_devices) / static_cast<float>(total_devices) * 100.0f;

        imgui.Spacing();
        imgui.Text("Device Distribution:");
        imgui.Text("DualSense: %.2f%%", dualsense_rate);
        imgui.Text("Xbox: %.2f%%", xbox_rate);
        imgui.Text("Generic HID: %.2f%%", generic_rate);
        imgui.Text("Unknown: %.2f%%", unknown_rate);
    }
}

} // namespace ui::new_ui
