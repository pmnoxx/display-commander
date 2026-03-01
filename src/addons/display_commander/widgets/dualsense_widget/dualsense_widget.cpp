#include "dualsense_widget.hpp"
#include "../../ui/imgui_wrapper_base.hpp"
#include <hidsdi.h>
#include <initguid.h>
#include <setupapi.h>
#include <windows.h>
#include <cstring>
#include <reshade_imgui.hpp>
#include <vector>
#include "../../config/display_commander_config.hpp"
#include "../../hooks/dualsense_hooks.hpp"
#include "../../utils.hpp"
#include "../../utils/logging.hpp"
#include "../../utils/timing.hpp"

// Define GUID_DEVINTERFACE_HID if not already defined
#ifndef GUID_DEVINTERFACE_HID
DEFINE_GUID(GUID_DEVINTERFACE_HID, 0x4d1e55b2, 0xf16f, 0x11cf, 0x88, 0xcb, 0x00, 0x11, 0x11, 0x00, 0x00, 0x30);
#endif

#pragma comment(lib, "setupapi.lib")
#pragma comment(lib, "hid.lib")

namespace display_commander::widgets::dualsense_widget {

// Global shared state
std::shared_ptr<DualSenseSharedState> DualSenseWidget::g_shared_state_ds = nullptr;

// Global widget instance
std::unique_ptr<DualSenseWidget> g_dualsense_widget = nullptr;

DualSenseWidget::DualSenseWidget() {
    // Initialize shared state if not already done
    if (!g_shared_state_ds) {
        g_shared_state_ds = std::make_shared<DualSenseSharedState>();
    }
}

void DualSenseWidget::Initialize() {
    if (is_initialized_) return;

    display_commander::hooks::InitializeDualSenseSupport();

    LogInfo("DualSenseWidget::Initialize() - Starting DualSense widget initialization");

    // Load settings
    LoadSettings();

    // Initialize HID wrapper
    display_commander::dualsense::InitializeDualSenseHID();

    is_initialized_ = true;
    LogInfo("DualSenseWidget::Initialize() - DualSense widget initialization complete");
}

void DualSenseWidget::Cleanup() {
    if (!is_initialized_) return;

    // Save settings
    SaveSettings();

    // Cleanup HID wrapper
    display_commander::dualsense::CleanupDualSenseHID();

    is_initialized_ = false;
}

void DualSenseWidget::OnDraw(display_commander::ui::IImGuiWrapper& imgui) {
    if (!is_initialized_) {
        Initialize();
    }

    if (!g_shared_state_ds) {
        imgui.TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "DualSense shared state not initialized");
        return;
    }

    DrawSettings(imgui);
    imgui.Spacing();

    DrawEventCounters(imgui);
    imgui.Spacing();

    DrawDeviceList(imgui);
    imgui.Spacing();

    DrawDeviceInfo(imgui);
}

void DualSenseWidget::DrawSettings(display_commander::ui::IImGuiWrapper& imgui) {
    if (imgui.CollapsingHeader("DualSense Settings",
                               display_commander::ui::wrapper_flags::TreeNodeFlags_DefaultOpen)) {
        // Enable DualSense detection
        bool enable_detection = g_shared_state_ds->enable_dualsense_detection.load();
        if (imgui.Checkbox("Enable DualSense Detection", &enable_detection)) {
            g_shared_state_ds->enable_dualsense_detection.store(enable_detection);
            SaveSettings();
        }
        if (imgui.IsItemHovered()) {
            imgui.SetTooltip("Enable detection and monitoring of DualSense controllers");
        }

        if (enable_detection) {
            // Show device IDs
            bool show_device_ids = g_shared_state_ds->show_device_ids.load();
            if (imgui.Checkbox("Show Device IDs", &show_device_ids)) {
                g_shared_state_ds->show_device_ids.store(show_device_ids);
                SaveSettings();
            }
            if (imgui.IsItemHovered()) {
                imgui.SetTooltip("Display vendor and product IDs for each device");
            }

            // Show connection type
            bool show_connection_type = g_shared_state_ds->show_connection_type.load();
            if (imgui.Checkbox("Show Connection Type", &show_connection_type)) {
                g_shared_state_ds->show_connection_type.store(show_connection_type);
                SaveSettings();
            }
            if (imgui.IsItemHovered()) {
                imgui.SetTooltip("Display whether device is connected via USB or Bluetooth");
            }

            // Show battery info
            bool show_battery_info = g_shared_state_ds->show_battery_info.load();
            if (imgui.Checkbox("Show Battery Information", &show_battery_info)) {
                g_shared_state_ds->show_battery_info.store(show_battery_info);
                SaveSettings();
            }
            if (imgui.IsItemHovered()) {
                imgui.SetTooltip("Display battery level and charging status");
            }

            // Show advanced features
            bool show_advanced_features = g_shared_state_ds->show_advanced_features.load();
            if (imgui.Checkbox("Show Advanced Features", &show_advanced_features)) {
                g_shared_state_ds->show_advanced_features.store(show_advanced_features);
                SaveSettings();
            }
            if (imgui.IsItemHovered()) {
                imgui.SetTooltip("Display DualSense-specific features like adaptive triggers and touchpad");
            }

            imgui.Spacing();

            // HID device type selection
            int hid_type = g_shared_state_ds->selected_hid_type.load();
            const char* hid_types[] = {"Auto (All Supported)", "DualSense Regular Only", "DualSense Edge Only",
                                       "DualShock 4 Only", "All Sony Controllers"};

            if (imgui.Combo("Device Type Filter", &hid_type, hid_types, 5)) {
                g_shared_state_ds->selected_hid_type.store(hid_type);
                display_commander::dualsense::g_dualsense_hid_wrapper->SetHIDTypeFilter(hid_type);
                SaveSettings();
            }
            if (imgui.IsItemHovered()) {
                imgui.SetTooltip("Select which type of Sony controllers to detect and monitor");
            }

            imgui.Spacing();

            // Manual refresh button
            if (imgui.Button("Refresh Device List")) {
                display_commander::dualsense::EnumerateDualSenseDevices();
            }
            if (imgui.IsItemHovered()) {
                imgui.SetTooltip("Manually refresh the list of connected devices");
            }
        }
    }
}

void DualSenseWidget::DrawEventCounters(display_commander::ui::IImGuiWrapper& imgui) {
    if (imgui.CollapsingHeader("Event Counters", display_commander::ui::wrapper_flags::TreeNodeFlags_DefaultOpen)) {
        uint64_t total_events = g_shared_state_ds->total_events.load();
        uint64_t button_events = g_shared_state_ds->button_events.load();
        uint64_t stick_events = g_shared_state_ds->stick_events.load();
        uint64_t trigger_events = g_shared_state_ds->trigger_events.load();
        uint64_t touchpad_events = g_shared_state_ds->touchpad_events.load();

        imgui.Text("Total Events: %llu", total_events);
        imgui.Text("Button Events: %llu", button_events);
        imgui.Text("Stick Events: %llu", stick_events);
        imgui.Text("Trigger Events: %llu", trigger_events);
        imgui.Text("Touchpad Events: %llu", touchpad_events);

        // Reset button
        if (imgui.Button("Reset Counters")) {
            g_shared_state_ds->total_events.store(0);
            g_shared_state_ds->button_events.store(0);
            g_shared_state_ds->stick_events.store(0);
            g_shared_state_ds->trigger_events.store(0);
            g_shared_state_ds->touchpad_events.store(0);
        }
    }
}

void DualSenseWidget::DrawDeviceSelector(display_commander::ui::IImGuiWrapper& imgui) {
    (void)imgui;
    // Reserved for future device selector UI
}

void DualSenseWidget::DrawDeviceList(display_commander::ui::IImGuiWrapper& imgui) {
    if (imgui.CollapsingHeader("Connected Devices", display_commander::ui::wrapper_flags::TreeNodeFlags_DefaultOpen)) {
        if (!g_shared_state_ds->enable_dualsense_detection.load()) {
            imgui.TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "DualSense detection is disabled");
            return;
        }

        // Update device states periodically
        static LONGLONG last_update = utils::get_now_ns();
        LONGLONG now = utils::get_now_ns();
        if ((now - last_update) > 100 * utils::NS_TO_MS) {
            UpdateDeviceStates();
            last_update = now;
        }

        // Get devices from HID wrapper
        const auto& hid_devices = display_commander::dualsense::g_dualsense_hid_wrapper->GetDevices();
        g_shared_state_ds->devices.assign(hid_devices.begin(), hid_devices.end());
        const auto& devices = g_shared_state_ds->devices;

        if (devices.empty()) {
            imgui.TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "No DualSense devices detected");
            imgui.TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f),
                               "Make sure your DualSense controller is connected via USB or Bluetooth");
        } else {
            imgui.Text("Found %zu DualSense device(s):", devices.size());
            imgui.Spacing();

            for (size_t i = 0; i < devices.size(); ++i) {
                const auto& device = devices[i];

                imgui.PushID(static_cast<int>(i));

                // Device status indicator
                ImVec4 status_color =
                    device.is_connected ? ImVec4(0.0f, 1.0f, 0.0f, 1.0f) : ImVec4(0.7f, 0.7f, 0.7f, 1.0f);

                imgui.TextColored(status_color, "●");
                imgui.SameLine();

                // Device name and basic info
                std::string device_name = device.device_name.empty() ? "DualSense Controller" : device.device_name;

                if (g_shared_state_ds->show_connection_type.load()) {
                    device_name += " (" + device.connection_type + ")";
                }

                if (g_shared_state_ds->show_device_ids.load()) {
                    char vid_str[16], pid_str[16];
                    sprintf_s(vid_str, "%04X", device.vendor_id);
                    sprintf_s(pid_str, "%04X", device.product_id);
                    device_name += " [VID:0x" + std::string(vid_str) + " PID:0x" + std::string(pid_str) + "]";
                }

                if (imgui.Selectable(device_name.c_str(), selected_device_ == static_cast<int>(i))) {
                    selected_device_ = static_cast<int>(i);
                }

                // Show additional info on hover
                if (imgui.IsItemHovered()) {
                    imgui.SetTooltip("Click to select this device for detailed view");
                }

                imgui.PopID();
            }
        }
    }
}

void DualSenseWidget::DrawDeviceInfo(display_commander::ui::IImGuiWrapper& imgui) {
    if (selected_device_ < 0 || selected_device_ >= static_cast<int>(g_shared_state_ds->devices.size())) {
        return;
    }

    const auto& device = g_shared_state_ds->devices[selected_device_];

    imgui.TextColored(ImVec4(0.9f, 0.9f, 0.9f, 1.0f), "=== Device Details ===");
    imgui.Spacing();

    DrawDeviceDetails(imgui, device);
}

void DualSenseWidget::DrawDeviceDetails(display_commander::ui::IImGuiWrapper& imgui,
                                         const DualSenseDeviceInfo& device) {
    // Basic device information
    imgui.TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "Device: %s",
                       device.device_name.empty() ? "DualSense Controller" : device.device_name.c_str());

    imgui.Text("Connection: %s", device.connection_type.c_str());
    imgui.Text("Vendor ID: 0x%04X", device.vendor_id);
    imgui.Text("Product ID: 0x%04X", device.product_id);
    imgui.Text("Status: %s", device.is_connected ? "Connected" : "Disconnected");

    if (device.is_wireless) {
        imgui.TextColored(ImVec4(0.0f, 0.8f, 1.0f, 1.0f), "Wireless: Yes");
    } else {
        imgui.TextColored(ImVec4(0.8f, 0.8f, 0.0f, 1.0f), "Wireless: No (USB)");
    }

    imgui.Spacing();

    // Device type information
    imgui.Text("Device Type: %s", GetDeviceTypeString(device).c_str());

    // Last update time
    if (device.last_update_time > 0) {
        DWORD now = GetTickCount();
        DWORD age_ms = now - device.last_update_time;
        imgui.Text("Last Update: %lu ms ago", age_ms);
    }

    // HID report rate (packet-number-changed path ever called?)
    if (device.packet_rate_ever_called) {
        imgui.Text("HID reports: %.1f/sec", device.last_packet_rate_hz);
        if (imgui.IsItemHovered()) {
            imgui.SetTooltip("Input report rate (ReadFile succeeded; packet-number-changed branch has run)");
        }
    } else {
        imgui.TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "HID reports: never");
        if (imgui.IsItemHovered()) {
            imgui.SetTooltip("No successful HID read yet; packet-number-changed branch has not run");
        }
    }

    imgui.Spacing();

    // Controller state (if connected)
    if (device.is_connected) {
        DrawButtonStates(imgui, device);
        imgui.Spacing();
        DrawStickStates(imgui, device);
        imgui.Spacing();
        DrawTriggerStates(imgui, device);
        imgui.Spacing();
    }

    // Battery information
    if (g_shared_state_ds->show_battery_info.load()) {
        DrawBatteryStatus(imgui, device);
        imgui.Spacing();
    }

    // Advanced features
    if (g_shared_state_ds->show_advanced_features.load()) {
        DrawAdvancedFeatures(imgui, device);
    }

    // Input report debug display
    if (device.hid_device && device.hid_device->input_report.size() > 0) {
        imgui.Text("Input Report Size: %zu bytes", device.hid_device->input_report.size());
        imgui.Text("First 8 bytes: %02X %02X %02X %02X %02X %02X %02X %02X", device.hid_device->input_report[0],
                    device.hid_device->input_report[1], device.hid_device->input_report[2],
                    device.hid_device->input_report[3], device.hid_device->input_report[4],
                    device.hid_device->input_report[5], device.hid_device->input_report[6],
                    device.hid_device->input_report[7]);
    } else {
        imgui.Text("No input report data available");
    }
    imgui.Spacing();

    DrawInputReport(imgui, device);

    // Raw input parsing
    DrawRawButtonStates(imgui, device);
    imgui.Spacing();
    DrawRawStickStates(imgui, device);
    imgui.Spacing();
    DrawRawTriggerStates(imgui, device);
    imgui.Spacing();
}

void DualSenseWidget::DrawButtonStates(display_commander::ui::IImGuiWrapper& imgui,
                                        const DualSenseDeviceInfo& device) {
    if (imgui.CollapsingHeader("Buttons", display_commander::ui::wrapper_flags::TreeNodeFlags_DefaultOpen)) {
        // Use XInput state (converted from Special-K DualSense data)
        WORD buttons = device.current_state.Gamepad.wButtons;

        // Create a grid of buttons
        const struct {
            WORD mask;
            const char* name;
        } button_list[] = {
            {XINPUT_GAMEPAD_A, "A"},
            {XINPUT_GAMEPAD_B, "B"},
            {XINPUT_GAMEPAD_X, "X"},
            {XINPUT_GAMEPAD_Y, "Y"},
            {XINPUT_GAMEPAD_LEFT_SHOULDER, "LB"},
            {XINPUT_GAMEPAD_RIGHT_SHOULDER, "RB"},
            {XINPUT_GAMEPAD_BACK, "View"},
            {XINPUT_GAMEPAD_START, "Menu"},
            {XINPUT_GAMEPAD_LEFT_THUMB, "LS"},
            {XINPUT_GAMEPAD_RIGHT_THUMB, "RS"},
            {XINPUT_GAMEPAD_DPAD_UP, "D-Up"},
            {XINPUT_GAMEPAD_DPAD_DOWN, "D-Down"},
            {XINPUT_GAMEPAD_DPAD_LEFT, "D-Left"},
            {XINPUT_GAMEPAD_DPAD_RIGHT, "D-Right"},
        };

        for (size_t i = 0; i < sizeof(button_list) / sizeof(button_list[0]); i += 2) {
            if (i + 1 < sizeof(button_list) / sizeof(button_list[0])) {
                // Draw two buttons per row
                bool pressed1 = IsButtonPressed(buttons, button_list[i].mask);
                bool pressed2 = IsButtonPressed(buttons, button_list[i + 1].mask);

                imgui.PushStyleColor(ImGuiCol_Button,
                                      pressed1 ? ImVec4(0.0f, 1.0f, 0.0f, 1.0f) : ImVec4(0.3f, 0.3f, 0.3f, 1.0f));
                imgui.Button(button_list[i].name, ImVec2(60, 30));
                imgui.PopStyleColor();

                imgui.SameLine();

                imgui.PushStyleColor(ImGuiCol_Button,
                                      pressed2 ? ImVec4(0.0f, 1.0f, 0.0f, 1.0f) : ImVec4(0.3f, 0.3f, 0.3f, 1.0f));
                imgui.Button(button_list[i + 1].name, ImVec2(60, 30));
                imgui.PopStyleColor();
            } else {
                // Single button on last row
                bool pressed = IsButtonPressed(buttons, button_list[i].mask);

                imgui.PushStyleColor(ImGuiCol_Button,
                                      pressed ? ImVec4(0.0f, 1.0f, 0.0f, 1.0f) : ImVec4(0.3f, 0.3f, 0.3f, 1.0f));
                imgui.Button(button_list[i].name, ImVec2(60, 30));
                imgui.PopStyleColor();
            }
        }
    }
}

void DualSenseWidget::DrawStickStates(display_commander::ui::IImGuiWrapper& imgui,
                                       const DualSenseDeviceInfo& device) {
    if (imgui.CollapsingHeader("Analog Sticks", display_commander::ui::wrapper_flags::TreeNodeFlags_DefaultOpen)) {
        // Use XInput state (converted from Special-K DualSense data)
        SHORT leftX = device.current_state.Gamepad.sThumbLX;
        SHORT leftY = device.current_state.Gamepad.sThumbLY;
        SHORT rightX = device.current_state.Gamepad.sThumbRX;
        SHORT rightY = device.current_state.Gamepad.sThumbRY;

        // Left stick
        imgui.Text("Left Stick:");
        float lx = ShortToFloat(leftX);
        float ly = ShortToFloat(leftY);
        imgui.Text("X: %.3f (Raw: %d)", lx, leftX);
        imgui.Text("Y: %.3f (Raw: %d)", ly, leftY);

        // Visual representation
        imgui.Text("Position:");
        ImVec2 canvas_pos = imgui.GetCursorScreenPos();
        ImVec2 canvas_size = ImVec2(100, 100);
        display_commander::ui::IImDrawList* draw_list = imgui.GetWindowDrawList();

        // Draw circle
        ImVec2 center = ImVec2(canvas_pos.x + canvas_size.x * 0.5f, canvas_pos.y + canvas_size.y * 0.5f);
        draw_list->AddCircle(center, canvas_size.x * 0.4f, ImColor(100, 100, 100, 255), 32, 2.0f);

        // Draw crosshairs
        draw_list->AddLine(ImVec2(canvas_pos.x, center.y), ImVec2(canvas_pos.x + canvas_size.x, center.y),
                           ImColor(100, 100, 100, 255), 1.0f);
        draw_list->AddLine(ImVec2(center.x, canvas_pos.y), ImVec2(center.x, canvas_pos.y + canvas_size.y),
                           ImColor(100, 100, 100, 255), 1.0f);

        // Draw stick position
        ImVec2 stick_pos = ImVec2(center.x + lx * canvas_size.x * 0.4f, center.y - ly * canvas_size.y * 0.4f);
        draw_list->AddCircleFilled(stick_pos, 5.0f, ImColor(0, 255, 0, 255));

        imgui.Dummy(canvas_size);

        // Right stick
        imgui.Text("Right Stick:");
        float rx = ShortToFloat(rightX);
        float ry = ShortToFloat(rightY);
        imgui.Text("X: %.3f (Raw: %d)", rx, rightX);
        imgui.Text("Y: %.3f (Raw: %d)", ry, rightY);

        // Visual representation for right stick
        imgui.Text("Position:");
        canvas_pos = imgui.GetCursorScreenPos();
        draw_list = imgui.GetWindowDrawList();

        // Draw circle
        center = ImVec2(canvas_pos.x + canvas_size.x * 0.5f, canvas_pos.y + canvas_size.y * 0.5f);
        draw_list->AddCircle(center, canvas_size.x * 0.4f, ImColor(100, 100, 100, 255), 32, 2.0f);

        // Draw crosshairs
        draw_list->AddLine(ImVec2(canvas_pos.x, center.y), ImVec2(canvas_pos.x + canvas_size.x, center.y),
                           ImColor(100, 100, 100, 255), 1.0f);
        draw_list->AddLine(ImVec2(center.x, canvas_pos.y), ImVec2(center.x, canvas_pos.y + canvas_size.y),
                           ImColor(100, 100, 100, 255), 1.0f);

        // Draw stick position
        stick_pos = ImVec2(center.x + rx * canvas_size.x * 0.4f, center.y - ry * canvas_size.y * 0.4f);
        draw_list->AddCircleFilled(stick_pos, 5.0f, ImColor(0, 255, 0, 255));

        imgui.Dummy(canvas_size);
    }
}

void DualSenseWidget::DrawTriggerStates(display_commander::ui::IImGuiWrapper& imgui,
                                         const DualSenseDeviceInfo& device) {
    if (imgui.CollapsingHeader("Triggers", display_commander::ui::wrapper_flags::TreeNodeFlags_DefaultOpen)) {
        // Use XInput state (converted from Special-K DualSense data)
        USHORT leftTrigger = device.current_state.Gamepad.bLeftTrigger;
        USHORT rightTrigger = device.current_state.Gamepad.bRightTrigger;

        // Left trigger
        imgui.Text("Left Trigger: %u/65535 (%.1f%%)", leftTrigger,
                    (static_cast<float>(leftTrigger) / 65535.0f) * 100.0f);

        // Visual bar for left trigger
        float left_trigger_norm = static_cast<float>(leftTrigger) / 65535.0f;
        imgui.ProgressBar(left_trigger_norm, ImVec2(-1, 0), "");

        // Right trigger
        imgui.Text("Right Trigger: %u/65535 (%.1f%%)", rightTrigger,
                    (static_cast<float>(rightTrigger) / 65535.0f) * 100.0f);

        // Visual bar for right trigger
        float right_trigger_norm = static_cast<float>(rightTrigger) / 65535.0f;
        imgui.ProgressBar(right_trigger_norm, ImVec2(-1, 0), "");
    }
}

void DualSenseWidget::DrawBatteryStatus(display_commander::ui::IImGuiWrapper& imgui,
                                         const DualSenseDeviceInfo& device) {
    if (imgui.CollapsingHeader("Battery Status", display_commander::ui::wrapper_flags::TreeNodeFlags_DefaultOpen)) {
        if (!device.battery_info_valid) {
            imgui.TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Battery information not available");
            return;
        }

        // Battery level
        std::string level_str;
        ImVec4 level_color(1.0f, 1.0f, 1.0f, 1.0f);
        float level_progress = 0.0f;

        switch (device.battery_level) {
            case 0:
                level_str = "Empty";
                level_color = ImVec4(1.0f, 0.0f, 0.0f, 1.0f);
                level_progress = 0.0f;
                break;
            case 1:
                level_str = "Low";
                level_color = ImVec4(1.0f, 0.5f, 0.0f, 1.0f);
                level_progress = 0.25f;
                break;
            case 2:
                level_str = "Medium";
                level_color = ImVec4(1.0f, 1.0f, 0.0f, 1.0f);
                level_progress = 0.5f;
                break;
            case 3:
                level_str = "High";
                level_color = ImVec4(0.0f, 1.0f, 0.0f, 1.0f);
                level_progress = 0.75f;
                break;
            case 4:
                level_str = "Full";
                level_color = ImVec4(0.0f, 1.0f, 0.0f, 1.0f);
                level_progress = 1.0f;
                break;
            default:
                level_str = "Unknown";
                level_color = ImVec4(0.7f, 0.7f, 0.7f, 1.0f);
                level_progress = 0.0f;
                break;
        }

        imgui.TextColored(level_color, "Level: %s", level_str.c_str());

        // Visual battery level bar
        imgui.PushStyleColor(ImGuiCol_PlotHistogram, level_color);
        imgui.ProgressBar(level_progress, ImVec2(-1, 0), "");
        imgui.PopStyleColor();
    }
}

void DualSenseWidget::DrawAdvancedFeatures(display_commander::ui::IImGuiWrapper& imgui,
                                           const DualSenseDeviceInfo& device) {
    if (imgui.CollapsingHeader("Advanced Features", display_commander::ui::wrapper_flags::TreeNodeFlags_DefaultOpen)) {
        imgui.Text("Adaptive Triggers: %s", device.has_adaptive_triggers ? "Yes" : "No");
        imgui.Text("Touchpad: %s", device.has_touchpad ? "Yes" : "No");
        imgui.Text("Microphone: %s", device.has_microphone ? "Yes" : "No");
        imgui.Text("Speaker: %s", device.has_speaker ? "Yes" : "No");

        if (device.has_touchpad) {
            imgui.TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Touchpad input not yet implemented");
        }

        if (device.has_adaptive_triggers) {
            imgui.TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Adaptive trigger control not yet implemented");
        }
    }
}

std::string DualSenseWidget::GetButtonName(WORD button) const {
    switch (button) {
        case XINPUT_GAMEPAD_A:              return "A";
        case XINPUT_GAMEPAD_B:              return "B";
        case XINPUT_GAMEPAD_X:              return "X";
        case XINPUT_GAMEPAD_Y:              return "Y";
        case XINPUT_GAMEPAD_LEFT_SHOULDER:  return "LB";
        case XINPUT_GAMEPAD_RIGHT_SHOULDER: return "RB";
        case XINPUT_GAMEPAD_BACK:           return "View";
        case XINPUT_GAMEPAD_START:          return "Menu";
        case XINPUT_GAMEPAD_LEFT_THUMB:     return "LS";
        case XINPUT_GAMEPAD_RIGHT_THUMB:    return "RS";
        case XINPUT_GAMEPAD_DPAD_UP:        return "D-Up";
        case XINPUT_GAMEPAD_DPAD_DOWN:      return "D-Down";
        case XINPUT_GAMEPAD_DPAD_LEFT:      return "D-Left";
        case XINPUT_GAMEPAD_DPAD_RIGHT:     return "D-Right";
        default:                            return "Unknown";
    }
}

std::string DualSenseWidget::GetDeviceStatus(const DualSenseDeviceInfo& device) const {
    return device.is_connected ? "Connected" : "Disconnected";
}

bool DualSenseWidget::IsButtonPressed(WORD buttons, WORD button) const { return (buttons & button) != 0; }

std::string DualSenseWidget::GetConnectionTypeString(const DualSenseDeviceInfo& device) const {
    return device.connection_type;
}

std::string DualSenseWidget::GetDeviceTypeString(const DualSenseDeviceInfo& device) const {
    if (device.vendor_id == 0x054c) {  // Sony
        switch (device.product_id) {
            case 0x0CE6: return "DualSense Controller";       // Regular DualSense
            case 0x0DF2: return "DualSense Edge Controller";  // DualSense Edge
            case 0x05C4: return "DualShock 4 Controller";
            case 0x09CC: return "DualShock 4 Controller (Rev 2)";
            case 0x0BA0: return "DualShock 4 Controller (Dongle)";
            default:     return "Sony Controller";
        }
    }
    return "Unknown Controller";
}

std::string DualSenseWidget::GetHIDTypeString(int hid_type) const {
    switch (hid_type) {
        case 0:  return "Auto (All Supported)";
        case 1:  return "DualSense Regular Only";
        case 2:  return "DualSense Edge Only";
        case 3:  return "DualShock 4 Only";
        case 4:  return "All Sony Controllers";
        default: return "Unknown";
    }
}

bool DualSenseWidget::IsDeviceTypeEnabled(USHORT product_id) const {
    int hid_type = g_shared_state_ds->selected_hid_type.load();
    return display_commander::dualsense::g_dualsense_hid_wrapper->IsDeviceTypeEnabled(0x054c, product_id, hid_type);
}

static const char* k_ds_section = "DisplayCommander.DualSenseWidget";

void DualSenseWidget::LoadSettings() {
    // Use Display Commander config so the widget works with and without ReShade (e.g. .NO_RESHADE mode)
    bool enable_detection;
    if (display_commander::config::get_config_value(k_ds_section, "EnableDetection", enable_detection)) {
        g_shared_state_ds->enable_dualsense_detection.store(enable_detection);
    }

    bool show_device_ids;
    if (display_commander::config::get_config_value(k_ds_section, "ShowDeviceIds", show_device_ids)) {
        g_shared_state_ds->show_device_ids.store(show_device_ids);
    }

    bool show_connection_type;
    if (display_commander::config::get_config_value(k_ds_section, "ShowConnectionType", show_connection_type)) {
        g_shared_state_ds->show_connection_type.store(show_connection_type);
    }

    bool show_battery_info;
    if (display_commander::config::get_config_value(k_ds_section, "ShowBatteryInfo", show_battery_info)) {
        g_shared_state_ds->show_battery_info.store(show_battery_info);
    }

    bool show_advanced_features;
    if (display_commander::config::get_config_value(k_ds_section, "ShowAdvancedFeatures", show_advanced_features)) {
        g_shared_state_ds->show_advanced_features.store(show_advanced_features);
    }

    int hid_type;
    if (display_commander::config::get_config_value(k_ds_section, "HIDTypeFilter", hid_type)) {
        g_shared_state_ds->selected_hid_type.store(hid_type);
    }
}

void DualSenseWidget::SaveSettings() {
    display_commander::config::set_config_value(k_ds_section, "EnableDetection",
                                                g_shared_state_ds->enable_dualsense_detection.load());
    display_commander::config::set_config_value(k_ds_section, "ShowDeviceIds",
                                                g_shared_state_ds->show_device_ids.load());
    display_commander::config::set_config_value(k_ds_section, "ShowConnectionType",
                                                g_shared_state_ds->show_connection_type.load());
    display_commander::config::set_config_value(k_ds_section, "ShowBatteryInfo",
                                                g_shared_state_ds->show_battery_info.load());
    display_commander::config::set_config_value(k_ds_section, "ShowAdvancedFeatures",
                                                g_shared_state_ds->show_advanced_features.load());
    display_commander::config::set_config_value(k_ds_section, "HIDTypeFilter",
                                                g_shared_state_ds->selected_hid_type.load());
    display_commander::config::save_config("DualSense widget");
}

void DualSenseWidget::UpdateDeviceStates() {
    // Update device states using HID wrapper
    display_commander::dualsense::UpdateDualSenseDeviceStates();
}

std::shared_ptr<DualSenseSharedState> DualSenseWidget::GetSharedState() { return g_shared_state_ds; }

// Global functions for integration
void InitializeDualSenseWidget() {
    if (!g_dualsense_widget) {
        g_dualsense_widget = std::make_unique<DualSenseWidget>();
        g_dualsense_widget->Initialize();
    }
}

void CleanupDualSenseWidget() {
    if (g_dualsense_widget) {
        g_dualsense_widget->Cleanup();
        g_dualsense_widget.reset();
    }
}

void DrawDualSenseWidget(display_commander::ui::IImGuiWrapper& imgui) {
    if (g_dualsense_widget) {
        g_dualsense_widget->OnDraw(imgui);
    }
}

void DualSenseWidget::DrawInputReport(display_commander::ui::IImGuiWrapper& imgui,
                                      const DualSenseDeviceInfo& device) {
    if (!imgui.CollapsingHeader("Input Report Debug (Special-K Format)",
                                display_commander::ui::wrapper_flags::TreeNodeFlags_DefaultOpen)) {
        return;
    }

    // Get the current input report from the device
    if (device.hid_device && device.hid_device->input_report.size() > 0) {
        const auto& inputReport = device.hid_device->input_report;
        int reportSize = static_cast<int>(inputReport.size());

        imgui.Text("Report Size: %d bytes", reportSize);
        imgui.Text("Connection: %s", device.connection_type.c_str());
        imgui.Text("Special-K Data Size: %zu bytes", sizeof(SK_HID_DualSense_GetStateData));

        // Determine data offset based on connection type
        int dataOffset = device.is_wireless ? 2 : 1;  // Skip report ID and sequence for BT
        int dataSize = device.is_wireless ? 63 : 63;  // Special-K data is always 63 bytes

        if (reportSize >= dataOffset + dataSize) {
            imgui.Text("Special-K Data Offset: %d", dataOffset);
            imgui.Spacing();

            // Show Special-K fields in a table format
            if (imgui.BeginTable("SpecialKReport", 6, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
                imgui.TableSetupColumn("Field Name");
                imgui.TableSetupColumn("Offset");
                imgui.TableSetupColumn("Size");
                imgui.TableSetupColumn("Raw Value");
                imgui.TableSetupColumn("Interpreted Value");
                imgui.TableSetupColumn("Description");
                imgui.TableHeadersRow();

                // Display Special-K fields
                DrawSpecialKFieldRow(imgui, "LeftStickX", dataOffset + 0, 1, inputReport, device);
                DrawSpecialKFieldRow(imgui, "LeftStickY", dataOffset + 1, 1, inputReport, device);
                DrawSpecialKFieldRow(imgui, "RightStickX", dataOffset + 2, 1, inputReport, device);
                DrawSpecialKFieldRow(imgui, "RightStickY", dataOffset + 3, 1, inputReport, device);
                DrawSpecialKFieldRow(imgui, "TriggerLeft", dataOffset + 4, 1, inputReport, device);
                DrawSpecialKFieldRow(imgui, "TriggerRight", dataOffset + 5, 1, inputReport, device);
                DrawSpecialKFieldRow(imgui, "SeqNo", dataOffset + 6, 1, inputReport, device);

                // Byte 7: D-pad and face buttons (bit fields)
                DrawSpecialKBitFieldRow(imgui, "DPad", dataOffset + 7, 0, 4, inputReport, device, "D-pad direction");
                DrawSpecialKBitFieldRow(imgui, "ButtonSquare", dataOffset + 7, 4, 1, inputReport, device, "Square button");
                DrawSpecialKBitFieldRow(imgui, "ButtonCross", dataOffset + 7, 5, 1, inputReport, device, "Cross button");
                DrawSpecialKBitFieldRow(imgui, "ButtonCircle", dataOffset + 7, 6, 1, inputReport, device, "Circle button");
                DrawSpecialKBitFieldRow(imgui, "ButtonTriangle", dataOffset + 7, 7, 1, inputReport, device, "Triangle button");

                // Byte 8: Shoulder and trigger buttons
                DrawSpecialKBitFieldRow(imgui, "ButtonL1", dataOffset + 8, 0, 1, inputReport, device, "L1 button");
                DrawSpecialKBitFieldRow(imgui, "ButtonR1", dataOffset + 8, 1, 1, inputReport, device, "R1 button");
                DrawSpecialKBitFieldRow(imgui, "ButtonL2", dataOffset + 8, 2, 1, inputReport, device, "L2 button");
                DrawSpecialKBitFieldRow(imgui, "ButtonR2", dataOffset + 8, 3, 1, inputReport, device, "R2 button");
                DrawSpecialKBitFieldRow(imgui, "ButtonCreate", dataOffset + 8, 4, 1, inputReport, device,
                                        "Create/Share button");
                DrawSpecialKBitFieldRow(imgui, "ButtonOptions", dataOffset + 8, 5, 1, inputReport, device, "Options button");
                DrawSpecialKBitFieldRow(imgui, "ButtonL3", dataOffset + 8, 6, 1, inputReport, device, "L3 button");
                DrawSpecialKBitFieldRow(imgui, "ButtonR3", dataOffset + 8, 7, 1, inputReport, device, "R3 button");

                // Byte 9: Home, pad, mute, and Edge buttons
                DrawSpecialKBitFieldRow(imgui, "ButtonHome", dataOffset + 9, 0, 1, inputReport, device, "Home/PS button");
                DrawSpecialKBitFieldRow(imgui, "ButtonPad", dataOffset + 9, 1, 1, inputReport, device, "Touchpad button");
                DrawSpecialKBitFieldRow(imgui, "ButtonMute", dataOffset + 9, 2, 1, inputReport, device, "Mute button");
                DrawSpecialKBitFieldRow(imgui, "UNK1", dataOffset + 9, 3, 1, inputReport, device, "Unknown bit 1");
                DrawSpecialKBitFieldRow(imgui, "ButtonLeftFunction", dataOffset + 9, 4, 1, inputReport, device,
                                        "Left Function (Edge)");
                DrawSpecialKBitFieldRow(imgui, "ButtonRightFunction", dataOffset + 9, 5, 1, inputReport, device,
                                        "Right Function (Edge)");
                DrawSpecialKBitFieldRow(imgui, "ButtonLeftPaddle", dataOffset + 9, 6, 1, inputReport, device,
                                        "Left Paddle (Edge)");
                DrawSpecialKBitFieldRow(imgui, "ButtonRightPaddle", dataOffset + 9, 7, 1, inputReport, device,
                                        "Right Paddle (Edge)");

                DrawSpecialKFieldRow(imgui, "UNK2", dataOffset + 10, 1, inputReport, device);
                DrawSpecialKFieldRow(imgui, "UNK_COUNTER", dataOffset + 11, 4, inputReport, device, "32-bit counter");
                DrawSpecialKFieldRow(imgui, "AngularVelocityX", dataOffset + 15, 2, inputReport, device, "16-bit signed");
                DrawSpecialKFieldRow(imgui, "AngularVelocityZ", dataOffset + 17, 2, inputReport, device, "16-bit signed");
                DrawSpecialKFieldRow(imgui, "AngularVelocityY", dataOffset + 19, 2, inputReport, device, "16-bit signed");
                DrawSpecialKFieldRow(imgui, "AccelerometerX", dataOffset + 21, 2, inputReport, device, "16-bit signed");
                DrawSpecialKFieldRow(imgui, "AccelerometerY", dataOffset + 23, 2, inputReport, device, "16-bit signed");
                DrawSpecialKFieldRow(imgui, "AccelerometerZ", dataOffset + 25, 2, inputReport, device, "16-bit signed");
                DrawSpecialKFieldRow(imgui, "SensorTimestamp", dataOffset + 27, 4, inputReport, device, "32-bit timestamp");
                DrawSpecialKFieldRow(imgui, "Temperature", dataOffset + 31, 1, inputReport, device, "8-bit signed");

                // Touch data (9 bytes)
                for (int i = 0; i < 9; i++) {
                    DrawSpecialKFieldRow(imgui, ("TouchData[" + std::to_string(i) + "]").c_str(), dataOffset + 32 + i, 1,
                                         inputReport, device);
                }

                // Trigger status and effects
                DrawSpecialKBitFieldRow(imgui, "TriggerRightStopLocation", dataOffset + 41, 0, 4, inputReport, device,
                                        "0-9 range");
                DrawSpecialKBitFieldRow(imgui, "TriggerRightStatus", dataOffset + 41, 4, 4, inputReport, device,
                                        "Status flags");
                DrawSpecialKBitFieldRow(imgui, "TriggerLeftStopLocation", dataOffset + 42, 0, 4, inputReport, device,
                                        "0-9 range");
                DrawSpecialKBitFieldRow(imgui, "TriggerLeftStatus", dataOffset + 42, 4, 4, inputReport, device,
                                        "Status flags");

                DrawSpecialKFieldRow(imgui, "HostTimestamp", dataOffset + 43, 4, inputReport, device, "32-bit timestamp");

                DrawSpecialKBitFieldRow(imgui, "TriggerRightEffect", dataOffset + 47, 0, 4, inputReport, device,
                                        "Active effect");
                DrawSpecialKBitFieldRow(imgui, "TriggerLeftEffect", dataOffset + 47, 4, 4, inputReport, device,
                                        "Active effect");

                DrawSpecialKFieldRow(imgui, "DeviceTimeStamp", dataOffset + 48, 4, inputReport, device, "32-bit timestamp");

                // Power information
                DrawSpecialKBitFieldRow(imgui, "PowerPercent", dataOffset + 52, 0, 4, inputReport, device, "0-10 range");
                DrawSpecialKBitFieldRow(imgui, "PowerState", dataOffset + 52, 4, 4, inputReport, device, "Power state enum");

                // Connection status
                DrawSpecialKBitFieldRow(imgui, "PluggedHeadphones", dataOffset + 53, 0, 1, inputReport, device,
                                        "Headphones connected");
                DrawSpecialKBitFieldRow(imgui, "PluggedMic", dataOffset + 53, 1, 1, inputReport, device,
                                        "Microphone connected");
                DrawSpecialKBitFieldRow(imgui, "MicMuted", dataOffset + 53, 2, 1, inputReport, device, "Microphone muted");
                DrawSpecialKBitFieldRow(imgui, "PluggedUsbData", dataOffset + 53, 3, 1, inputReport, device,
                                        "USB data connected");
                DrawSpecialKBitFieldRow(imgui, "PluggedUsbPower", dataOffset + 53, 4, 1, inputReport, device,
                                        "USB power connected");
                DrawSpecialKBitFieldRow(imgui, "PluggedUnk1", dataOffset + 53, 5, 3, inputReport, device, "Unknown bits");

                DrawSpecialKBitFieldRow(imgui, "PluggedExternalMic", dataOffset + 54, 0, 1, inputReport, device,
                                        "External mic active");
                DrawSpecialKBitFieldRow(imgui, "HapticLowPassFilter", dataOffset + 54, 1, 1, inputReport, device,
                                        "Haptic filter active");
                DrawSpecialKBitFieldRow(imgui, "PluggedUnk3", dataOffset + 54, 2, 6, inputReport, device, "Unknown bits");

                // AES CMAC (8 bytes)
                for (int i = 0; i < 8; i++) {
                    DrawSpecialKFieldRow(imgui, ("AesCmac[" + std::to_string(i) + "]").c_str(), dataOffset + 55 + i, 1,
                                         inputReport, device);
                }

                imgui.EndTable();
            }
        } else {
            imgui.TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "Insufficient data for Special-K format");
        }

    } else {
        imgui.TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "No input report data available");
    }
}

std::string DualSenseWidget::GetByteDescription(int offset, const std::string& connectionType) const {
    if (connectionType == "Bluetooth") {
        // Bluetooth report format
        switch (offset) {
            case 0:  return "Report ID";
            case 1:  return "Buttons 1";
            case 2:  return "Buttons 2";
            case 3:  return "D-Pad";
            case 4:  return "Left Stick X (low)";
            case 5:  return "Left Stick X (high)";
            case 6:  return "Left Stick Y (low)";
            case 7:  return "Left Stick Y (high)";
            case 8:  return "Right Stick X (low)";
            case 9:  return "Right Stick X (high)";
            case 10: return "Right Stick Y (low)";
            case 11: return "Right Stick Y (high)";
            case 12: return "Left Trigger (low)";
            case 13: return "Left Trigger (high)";
            case 14: return "Right Trigger (low)";
            case 15: return "Right Trigger (high)";
            case 16: return "Counter";
            case 17: return "Battery";
            case 18: return "Touchpad 1";
            case 19: return "Touchpad 2";
            case 20: return "Touchpad 3";
            case 21: return "Touchpad 4";
            case 22: return "Touchpad 5";
            case 23: return "Touchpad 6";
            case 24: return "Touchpad 7";
            case 25: return "Touchpad 8";
            case 26: return "Touchpad 9";
            case 27: return "Touchpad 10";
            case 28: return "Touchpad 11";
            case 29: return "Touchpad 12";
            case 30: return "Touchpad 13";
            case 31: return "Touchpad 14";
            case 32: return "Touchpad 15";
            case 33: return "Touchpad 16";
            case 34: return "Touchpad 17";
            case 35: return "Touchpad 18";
            case 36: return "Touchpad 19";
            case 37: return "Touchpad 20";
            case 38: return "Touchpad 21";
            case 39: return "Touchpad 22";
            case 40: return "Touchpad 23";
            case 41: return "Touchpad 24";
            case 42: return "Touchpad 25";
            case 43: return "Touchpad 26";
            case 44: return "Touchpad 27";
            case 45: return "Touchpad 28";
            case 46: return "Touchpad 29";
            case 47: return "Touchpad 30";
            case 48: return "Touchpad 31";
            case 49: return "Touchpad 32";
            case 50: return "Touchpad 33";
            case 51: return "Touchpad 34";
            case 52: return "Touchpad 35";
            case 53: return "Touchpad 36";
            case 54: return "Touchpad 37";
            case 55: return "Touchpad 38";
            case 56: return "Touchpad 39";
            case 57: return "Touchpad 40";
            case 58: return "Touchpad 41";
            case 59: return "Touchpad 42";
            case 60: return "Touchpad 43";
            case 61: return "Touchpad 44";
            case 62: return "Touchpad 45";
            case 63: return "Touchpad 46";
            case 64: return "Touchpad 47";
            case 65: return "Touchpad 48";
            case 66: return "Touchpad 49";
            case 67: return "Touchpad 50";
            case 68: return "Touchpad 51";
            case 69: return "Touchpad 52";
            case 70: return "Touchpad 53";
            case 71: return "Touchpad 54";
            case 72: return "Touchpad 55";
            case 73: return "Touchpad 56";
            case 74: return "Touchpad 57";
            case 75: return "Touchpad 58";
            case 76: return "Touchpad 59";
            case 77: return "Touchpad 60";
            default: return "Unknown";
        }
    } else {
        // USB report format
        switch (offset) {
            case 0:  return "Report ID";
            case 1:  return "Buttons 1";
            case 2:  return "Buttons 2";
            case 3:  return "D-Pad";
            case 4:  return "Left Stick X (low)";
            case 5:  return "Left Stick X (high)";
            case 6:  return "Left Stick Y (low)";
            case 7:  return "Left Stick Y (high)";
            case 8:  return "Right Stick X (low)";
            case 9:  return "Right Stick X (high)";
            case 10: return "Right Stick Y (low)";
            case 11: return "Right Stick Y (high)";
            case 12: return "Left Trigger (low)";
            case 13: return "Left Trigger (high)";
            case 14: return "Right Trigger (low)";
            case 15: return "Right Trigger (high)";
            case 16: return "Counter";
            case 17: return "Battery";
            case 18: return "Touchpad 1";
            case 19: return "Touchpad 2";
            case 20: return "Touchpad 3";
            case 21: return "Touchpad 4";
            case 22: return "Touchpad 5";
            case 23: return "Touchpad 6";
            case 24: return "Touchpad 7";
            case 25: return "Touchpad 8";
            case 26: return "Touchpad 9";
            case 27: return "Touchpad 10";
            case 28: return "Touchpad 11";
            case 29: return "Touchpad 12";
            case 30: return "Touchpad 13";
            case 31: return "Touchpad 14";
            case 32: return "Touchpad 15";
            case 33: return "Touchpad 16";
            case 34: return "Touchpad 17";
            case 35: return "Touchpad 18";
            case 36: return "Touchpad 19";
            case 37: return "Touchpad 20";
            case 38: return "Touchpad 21";
            case 39: return "Touchpad 22";
            case 40: return "Touchpad 23";
            case 41: return "Touchpad 24";
            case 42: return "Touchpad 25";
            case 43: return "Touchpad 26";
            case 44: return "Touchpad 27";
            case 45: return "Touchpad 28";
            case 46: return "Touchpad 29";
            case 47: return "Touchpad 30";
            case 48: return "Touchpad 31";
            case 49: return "Touchpad 32";
            case 50: return "Touchpad 33";
            case 51: return "Touchpad 34";
            case 52: return "Touchpad 35";
            case 53: return "Touchpad 36";
            case 54: return "Touchpad 37";
            case 55: return "Touchpad 38";
            case 56: return "Touchpad 39";
            case 57: return "Touchpad 40";
            case 58: return "Touchpad 41";
            case 59: return "Touchpad 42";
            case 60: return "Touchpad 43";
            case 61: return "Touchpad 44";
            case 62: return "Touchpad 45";
            case 63: return "Touchpad 46";
            default: return "Unknown";
        }
    }
}

std::string DualSenseWidget::GetByteValue(const std::vector<BYTE>& inputReport, int offset,
                                          const std::string& connectionType) const {
    if (offset >= inputReport.size()) return "N/A";

    BYTE value = inputReport[offset];

    // Special handling for certain bytes
    if (offset == 0) {
        return std::to_string(value) + " (0x" + std::to_string(value) + ")";
    } else if (offset >= 4 && offset <= 11) {
        // Stick values - show as 16-bit
        if (offset % 2 == 0 && offset + 1 < inputReport.size()) {
            SHORT stickValue = static_cast<SHORT>((inputReport[offset + 1] << 8) | inputReport[offset]);
            return std::to_string(stickValue);
        }
    } else if (offset >= 12 && offset <= 15) {
        // Trigger values - show as 16-bit
        if (offset % 2 == 0 && offset + 1 < inputReport.size()) {
            USHORT triggerValue = static_cast<USHORT>((inputReport[offset + 1] << 8) | inputReport[offset]);
            return std::to_string(triggerValue);
        }
    } else if (offset == 17) {
        // Battery level
        return std::to_string(value) + "%";
    }

    return std::to_string(value);
}

std::string DualSenseWidget::GetByteNotes(int offset, const std::string& connectionType) const {
    if (offset == 0) {
        return connectionType == "Bluetooth" ? "Should be 0x31" : "Should be 0x01";
    } else if (offset == 1) {
        return "Square, Cross, Circle, Triangle, L1, R1, L2, R2";
    } else if (offset == 2) {
        return "Share, Options, L3, R3, PS, Touchpad";
    } else if (offset == 3) {
        return "D-Pad direction";
    } else if (offset >= 4 && offset <= 11) {
        return "Stick data (16-bit signed)";
    } else if (offset >= 12 && offset <= 15) {
        return "Trigger data (16-bit unsigned)";
    } else if (offset == 16) {
        return "Packet counter";
    } else if (offset == 17) {
        return "Battery level (0-100)";
    } else if (offset >= 18) {
        return "Touchpad data";
    }

    return "";
}

void DualSenseWidget::DrawRawButtonStates(display_commander::ui::IImGuiWrapper& imgui,
                                           const DualSenseDeviceInfo& device) {
    if (!imgui.CollapsingHeader("Raw Buttons (Special-K Format)", display_commander::ui::wrapper_flags::TreeNodeFlags_DefaultOpen)) {
        return;
    }

    if (!device.hid_device || device.hid_device->input_report.size() == 0) {
        imgui.TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "No input report data available");
        return;
    }

    // Use XInput state (converted from Special-K DualSense data)
    WORD buttons = device.current_state.Gamepad.wButtons;

    // Display buttons
    const struct {
        WORD mask;
        const char* name;
    } button_list[] = {
        {XINPUT_GAMEPAD_A, "A"},
        {XINPUT_GAMEPAD_B, "B"},
        {XINPUT_GAMEPAD_X, "X"},
        {XINPUT_GAMEPAD_Y, "Y"},
        {XINPUT_GAMEPAD_LEFT_SHOULDER, "L1"},
        {XINPUT_GAMEPAD_RIGHT_SHOULDER, "R1"},
        {XINPUT_GAMEPAD_BACK, "Share"},
        {XINPUT_GAMEPAD_START, "Options"},
        {XINPUT_GAMEPAD_LEFT_THUMB, "L3"},
        {XINPUT_GAMEPAD_RIGHT_THUMB, "R3"},
        {0x0400, "PS"},
    };

    imgui.Columns(3, "RawButtonColumns", false);
    for (const auto& button : button_list) {
        bool pressed = IsButtonPressed(buttons, button.mask);
        imgui.TextColored(pressed ? ImVec4(0.0f, 1.0f, 0.0f, 1.0f) : ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "%s: %s",
                           button.name, pressed ? "PRESSED" : "Released");
        imgui.NextColumn();
    }
    imgui.Columns(1);

    // D-Pad
    imgui.Text("D-Pad:");
    const char* dpad_directions[] = {"None", "Up",        "Up-Right", "Right",  "Down-Right",
                                     "Down", "Down-Left", "Left",     "Up-Left"};
    imgui.Text("Direction: %s", dpad_directions[buttons & 0x0F]);
}

void DualSenseWidget::DrawRawStickStates(display_commander::ui::IImGuiWrapper& imgui,
                                          const DualSenseDeviceInfo& device) {
    if (!imgui.CollapsingHeader("Raw Analog Sticks (Special-K Format)", display_commander::ui::wrapper_flags::TreeNodeFlags_DefaultOpen)) {
        return;
    }

    if (!device.hid_device || device.hid_device->input_report.size() == 0) {
        imgui.TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "No input report data available");
        return;
    }

    // Use XInput state (converted from Special-K DualSense data)
    SHORT leftX = device.current_state.Gamepad.sThumbLX;
    SHORT leftY = device.current_state.Gamepad.sThumbLY;

    // Left stick
    imgui.Text("Left Stick:");

    float lx = ShortToFloat(leftX);
    float ly = ShortToFloat(leftY);
    imgui.Text("X: %.3f (Raw: %d)", lx, leftX);
    imgui.Text("Y: %.3f (Raw: %d)", ly, leftY);

    // Visual representation for left stick
    imgui.Text("Position:");
    ImVec2 canvas_pos = imgui.GetCursorScreenPos();
    display_commander::ui::IImDrawList* draw_list = imgui.GetWindowDrawList();
    ImVec2 canvas_size = ImVec2(100, 100);

    // Draw circle
    ImVec2 center = ImVec2(canvas_pos.x + canvas_size.x * 0.5f, canvas_pos.y + canvas_size.y * 0.5f);
    draw_list->AddCircle(center, canvas_size.x * 0.4f, ImColor(100, 100, 100, 255), 32, 2.0f);

    // Draw crosshairs
    draw_list->AddLine(ImVec2(canvas_pos.x, center.y), ImVec2(canvas_pos.x + canvas_size.x, center.y),
                       ImColor(100, 100, 100, 255), 1.0f);
    draw_list->AddLine(ImVec2(center.x, canvas_pos.y), ImVec2(center.x, canvas_pos.y + canvas_size.y),
                       ImColor(100, 100, 100, 255), 1.0f);

    // Draw stick position
    ImVec2 stick_pos = ImVec2(center.x + lx * canvas_size.x * 0.4f, center.y - ly * canvas_size.y * 0.4f);
    draw_list->AddCircleFilled(stick_pos, 5.0f, ImColor(0, 255, 0, 255));

    imgui.Dummy(canvas_size);

    // Right stick
    imgui.Text("Right Stick:");
    SHORT rightX = device.current_state.Gamepad.sThumbRX;
    SHORT rightY = device.current_state.Gamepad.sThumbRY;

    float rx = ShortToFloat(rightX);
    float ry = ShortToFloat(rightY);
    imgui.Text("X: %.3f (Raw: %d)", rx, rightX);
    imgui.Text("Y: %.3f (Raw: %d)", ry, rightY);

    // Visual representation for right stick
    imgui.Text("Position:");
    canvas_pos = imgui.GetCursorScreenPos();
    draw_list = imgui.GetWindowDrawList();

    // Draw circle
    center = ImVec2(canvas_pos.x + canvas_size.x * 0.5f, canvas_pos.y + canvas_size.y * 0.5f);
    draw_list->AddCircle(center, canvas_size.x * 0.4f, ImColor(100, 100, 100, 255), 32, 2.0f);

    // Draw crosshairs
    draw_list->AddLine(ImVec2(canvas_pos.x, center.y), ImVec2(canvas_pos.x + canvas_size.x, center.y),
                       ImColor(100, 100, 100, 255), 1.0f);
    draw_list->AddLine(ImVec2(center.x, canvas_pos.y), ImVec2(center.x, canvas_pos.y + canvas_size.y),
                       ImColor(100, 100, 100, 255), 1.0f);

    // Draw stick position
    stick_pos = ImVec2(center.x + rx * canvas_size.x * 0.4f, center.y - ry * canvas_size.y * 0.4f);
    draw_list->AddCircleFilled(stick_pos, 5.0f, ImColor(0, 255, 0, 255));

    imgui.Dummy(canvas_size);
}

void DualSenseWidget::DrawRawTriggerStates(display_commander::ui::IImGuiWrapper& imgui,
                                           const DualSenseDeviceInfo& device) {
    if (!imgui.CollapsingHeader("Raw Triggers (Special-K Format)", display_commander::ui::wrapper_flags::TreeNodeFlags_DefaultOpen)) {
        return;
    }

    if (!device.hid_device || device.hid_device->input_report.size() == 0) {
        imgui.TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "No input report data available");
        return;
    }

    // Use XInput state (converted from Special-K DualSense data)
    USHORT leftTrigger = device.current_state.Gamepad.bLeftTrigger;

    imgui.Text("Left Trigger: %u/65535 (%.1f%%)", leftTrigger, (static_cast<float>(leftTrigger) / 65535.0f) * 100.0f);

    // Right trigger
    USHORT rightTrigger = device.current_state.Gamepad.bRightTrigger;

    imgui.Text("Right Trigger: %u/65535 (%.1f%%)", rightTrigger,
                (static_cast<float>(rightTrigger) / 65535.0f) * 100.0f);
}

void DualSenseWidget::DrawSpecialKData(display_commander::ui::IImGuiWrapper& imgui,
                                        const DualSenseDeviceInfo& device) {
    if (!imgui.CollapsingHeader("Special-K DualSense Data", display_commander::ui::wrapper_flags::TreeNodeFlags_DefaultOpen)) {
        return;
    }

    const auto& sk_data = device.sk_dualsense_data;

    // Basic input data
    if (imgui.CollapsingHeader("Input Data", display_commander::ui::wrapper_flags::TreeNodeFlags_DefaultOpen)) {
        imgui.Columns(2, "SKInputColumns", false);

        // Sticks
        imgui.Text("Left Stick: X=%d, Y=%d", sk_data.LeftStickX, sk_data.LeftStickY);
        imgui.NextColumn();
        imgui.Text("Right Stick: X=%d, Y=%d", sk_data.RightStickX, sk_data.RightStickY);
        imgui.NextColumn();

        // Triggers
        imgui.Text("Left Trigger: %d", sk_data.TriggerLeft);
        imgui.NextColumn();
        imgui.Text("Right Trigger: %d", sk_data.TriggerRight);
        imgui.NextColumn();

        // D-pad
        const char* dpad_names[] = {"Up",        "Up-Right", "Right",   "Down-Right", "Down",
                                    "Down-Left", "Left",     "Up-Left", "None"};
        imgui.Text("D-Pad: %s", dpad_names[static_cast<int>(sk_data.DPad)]);
        imgui.NextColumn();
        imgui.Text("Sequence: %d", sk_data.SeqNo);
        imgui.NextColumn();

        imgui.Columns(1);
    }

    // Button states
    if (imgui.CollapsingHeader("Button States", display_commander::ui::wrapper_flags::TreeNodeFlags_DefaultOpen)) {
        imgui.Columns(3, "SKButtonColumns", false);

        // Face buttons
        imgui.Text("Square: %s", sk_data.ButtonSquare ? "PRESSED" : "Released");
        imgui.NextColumn();
        imgui.Text("Cross: %s", sk_data.ButtonCross ? "PRESSED" : "Released");
        imgui.NextColumn();
        imgui.Text("Circle: %s", sk_data.ButtonCircle ? "PRESSED" : "Released");
        imgui.NextColumn();
        imgui.Text("Triangle: %s", sk_data.ButtonTriangle ? "PRESSED" : "Released");
        imgui.NextColumn();

        // Shoulder buttons
        imgui.Text("L1: %s", sk_data.ButtonL1 ? "PRESSED" : "Released");
        imgui.NextColumn();
        imgui.Text("R1: %s", sk_data.ButtonR1 ? "PRESSED" : "Released");
        imgui.NextColumn();
        imgui.Text("L2: %s", sk_data.ButtonL2 ? "PRESSED" : "Released");
        imgui.NextColumn();
        imgui.Text("R2: %s", sk_data.ButtonR2 ? "PRESSED" : "Released");
        imgui.NextColumn();

        // System buttons
        imgui.Text("Create: %s", sk_data.ButtonCreate ? "PRESSED" : "Released");
        imgui.NextColumn();
        imgui.Text("Options: %s", sk_data.ButtonOptions ? "PRESSED" : "Released");
        imgui.NextColumn();
        imgui.Text("L3: %s", sk_data.ButtonL3 ? "PRESSED" : "Released");
        imgui.NextColumn();
        imgui.Text("R3: %s", sk_data.ButtonR3 ? "PRESSED" : "Released");
        imgui.NextColumn();
        imgui.Text("Home: %s", sk_data.ButtonHome ? "PRESSED" : "Released");
        imgui.NextColumn();
        imgui.Text("Touchpad: %s", sk_data.ButtonPad ? "PRESSED" : "Released");
        imgui.NextColumn();
        imgui.Text("Mute: %s", sk_data.ButtonMute ? "PRESSED" : "Released");
        imgui.NextColumn();

        // Edge controller buttons
        if (sk_data.ButtonLeftFunction || sk_data.ButtonRightFunction || sk_data.ButtonLeftPaddle
            || sk_data.ButtonRightPaddle) {
            imgui.Text("Left Function: %s", sk_data.ButtonLeftFunction ? "PRESSED" : "Released");
            imgui.NextColumn();
            imgui.Text("Right Function: %s", sk_data.ButtonRightFunction ? "PRESSED" : "Released");
            imgui.NextColumn();
            imgui.Text("Left Paddle: %s", sk_data.ButtonLeftPaddle ? "PRESSED" : "Released");
            imgui.NextColumn();
            imgui.Text("Right Paddle: %s", sk_data.ButtonRightPaddle ? "PRESSED" : "Released");
            imgui.NextColumn();
        }

        imgui.Columns(1);
    }

    // Motion sensors
    if (imgui.CollapsingHeader("Motion Sensors")) {
        imgui.Columns(2, "SKMotionColumns", false);

        imgui.Text("Angular Velocity X: %d", sk_data.AngularVelocityX);
        imgui.NextColumn();
        imgui.Text("Angular Velocity Y: %d", sk_data.AngularVelocityY);
        imgui.NextColumn();
        imgui.Text("Angular Velocity Z: %d", sk_data.AngularVelocityZ);
        imgui.NextColumn();
        imgui.Text("Accelerometer X: %d", sk_data.AccelerometerX);
        imgui.NextColumn();
        imgui.Text("Accelerometer Y: %d", sk_data.AccelerometerY);
        imgui.NextColumn();
        imgui.Text("Accelerometer Z: %d", sk_data.AccelerometerZ);
        imgui.NextColumn();
        imgui.Text("Temperature: %d°C", sk_data.Temperature);
        imgui.NextColumn();
        imgui.Text("Sensor Timestamp: %u", sk_data.SensorTimestamp);
        imgui.NextColumn();

        imgui.Columns(1);
    }

    // Battery and power
    if (imgui.CollapsingHeader("Battery & Power")) {
        imgui.Columns(2, "SKPowerColumns", false);

        imgui.Text("Battery: %d%%", sk_data.PowerPercent * 10);
        imgui.NextColumn();
        const char* power_state_names[] = {"Unknown", "Charging", "Discharging", "Not Charging", "Full"};
        imgui.Text("Power State: %s", power_state_names[static_cast<int>(sk_data.PowerState)]);
        imgui.NextColumn();
        imgui.Text("USB Data: %s", sk_data.PluggedUsbData ? "Yes" : "No");
        imgui.NextColumn();
        imgui.Text("USB Power: %s", sk_data.PluggedUsbPower ? "Yes" : "No");
        imgui.NextColumn();
        imgui.Text("Headphones: %s", sk_data.PluggedHeadphones ? "Yes" : "No");
        imgui.NextColumn();
        imgui.Text("Microphone: %s", sk_data.PluggedMic ? "Yes" : "No");
        imgui.NextColumn();
        imgui.Text("External Mic: %s", sk_data.PluggedExternalMic ? "Yes" : "No");
        imgui.NextColumn();
        imgui.Text("Mic Muted: %s", sk_data.MicMuted ? "Yes" : "No");
        imgui.NextColumn();
        imgui.Text("Haptic Filter: %s", sk_data.HapticLowPassFilter ? "On" : "Off");
        imgui.NextColumn();

        imgui.Columns(1);
    }

    // Adaptive triggers
    if (imgui.CollapsingHeader("Adaptive Triggers")) {
        imgui.Columns(2, "SKTriggerColumns", false);

        imgui.Text("Left Trigger Status: %d", sk_data.TriggerLeftStatus);
        imgui.NextColumn();
        imgui.Text("Right Trigger Status: %d", sk_data.TriggerRightStatus);
        imgui.NextColumn();
        imgui.Text("Left Stop Location: %d", sk_data.TriggerLeftStopLocation);
        imgui.NextColumn();
        imgui.Text("Right Stop Location: %d", sk_data.TriggerRightStopLocation);
        imgui.NextColumn();
        imgui.Text("Left Effect: %d", sk_data.TriggerLeftEffect);
        imgui.NextColumn();
        imgui.Text("Right Effect: %d", sk_data.TriggerRightEffect);
        imgui.NextColumn();

        imgui.Columns(1);
    }

    // Timestamps
    if (imgui.CollapsingHeader("Timestamps")) {
        imgui.Text("Host Timestamp: %u", sk_data.HostTimestamp);
        imgui.Text("Device Timestamp: %u", sk_data.DeviceTimeStamp);
        imgui.Text("Sensor Timestamp: %u", sk_data.SensorTimestamp);
    }

    // Touch data
    if (imgui.CollapsingHeader("Touch Data")) {
        imgui.Text("Touch Data: ");
        for (int i = 0; i < 9; i++) {
            imgui.SameLine();
            imgui.Text("%02X ", sk_data.TouchData.data[i]);
        }
    }

    // Debug info
    if (imgui.CollapsingHeader("Debug Info")) {
        imgui.Text("Unknown Counter: %u", sk_data.UNK_COUNTER);
        imgui.Text("Unknown 1: %d", sk_data.UNK1);
        imgui.Text("Unknown 2: %d", sk_data.UNK2);
        imgui.Text("Unknown 3: %d", sk_data.PluggedUnk1);
        imgui.Text("Unknown 4: %d", sk_data.PluggedUnk3);

        imgui.Text("AES CMAC: ");
        for (int i = 0; i < 8; i++) {
            imgui.SameLine();
            imgui.Text("%02X ", sk_data.AesCmac[i]);
        }
    }
}

// Special-K debug helper functions
void DualSenseWidget::DrawSpecialKFieldRow(display_commander::ui::IImGuiWrapper& imgui, const char* fieldName,
                                           int offset, int size, const std::vector<BYTE>& inputReport,
                                           const DualSenseDeviceInfo& device, const char* description) {
    if (offset + size > inputReport.size()) {
        return;  // Not enough data
    }

    imgui.TableNextRow();

    // Field Name
    imgui.TableSetColumnIndex(0);
    imgui.Text("%s", fieldName);

    // Offset
    imgui.TableSetColumnIndex(1);
    imgui.Text("%d", offset);

    // Size
    imgui.TableSetColumnIndex(2);
    imgui.Text("%d byte%s", size, size > 1 ? "s" : "");

    // Raw Value
    imgui.TableSetColumnIndex(3);
    if (size == 1) {
        imgui.Text("0x%02X (%d)", inputReport[offset], inputReport[offset]);
    } else if (size == 2) {
        uint16_t value = *reinterpret_cast<const uint16_t*>(&inputReport[offset]);
        imgui.Text("0x%04X (%d)", value, value);
    } else if (size == 4) {
        uint32_t value = *reinterpret_cast<const uint32_t*>(&inputReport[offset]);
        imgui.Text("0x%08X (%u)", value, value);
    } else {
        imgui.Text("Multi-byte");
    }

    // Interpreted Value
    imgui.TableSetColumnIndex(4);
    if (size == 1) {
        imgui.Text("%d", inputReport[offset]);
    } else if (size == 2) {
        int16_t value = *reinterpret_cast<const int16_t*>(&inputReport[offset]);
        imgui.Text("%d", value);
    } else if (size == 4) {
        int32_t value = *reinterpret_cast<const int32_t*>(&inputReport[offset]);
        imgui.Text("%d", value);
    } else {
        imgui.Text("N/A");
    }

    // Description
    imgui.TableSetColumnIndex(5);
    imgui.Text("%s", description ? description : "Special-K field");
}

void DualSenseWidget::DrawSpecialKBitFieldRow(display_commander::ui::IImGuiWrapper& imgui, const char* fieldName,
                                              int byteOffset, int bitOffset, int bitCount,
                                              const std::vector<BYTE>& inputReport, const DualSenseDeviceInfo& device,
                                              const char* description) {
    if (byteOffset >= inputReport.size()) {
        return;  // Not enough data
    }

    imgui.TableNextRow();

    // Field Name
    imgui.TableSetColumnIndex(0);
    imgui.Text("%s", fieldName);

    // Offset
    imgui.TableSetColumnIndex(1);
    imgui.Text("%d.%d", byteOffset, bitOffset);

    // Size
    imgui.TableSetColumnIndex(2);
    imgui.Text("%d bit%s", bitCount, bitCount > 1 ? "s" : "");

    // Raw Value
    imgui.TableSetColumnIndex(3);
    uint8_t byteValue = inputReport[byteOffset];
    uint8_t mask = (1 << bitCount) - 1;
    uint8_t fieldValue = (byteValue >> bitOffset) & mask;
    imgui.Text("0x%02X (bit %d-%d)", fieldValue, bitOffset, bitOffset + bitCount - 1);

    // Interpreted Value
    imgui.TableSetColumnIndex(4);
    if (bitCount == 1) {
        imgui.Text("%s", fieldValue ? "ON" : "OFF");
    } else {
        imgui.Text("%d", fieldValue);
    }

    // Description
    imgui.TableSetColumnIndex(5);
    imgui.Text("%s", description ? description : "Special-K bit field");
}

// Global functions for device enumeration
void EnumerateDualSenseDevices() { display_commander::dualsense::EnumerateDualSenseDevices(); }

void UpdateDualSenseDeviceStates() {
    if (g_dualsense_widget) {
        g_dualsense_widget->UpdateDeviceStates();
    }
}

}  // namespace display_commander::widgets::dualsense_widget
