#pragma once

#include <deps/imgui/imgui.h>
#include <memory>
#include <atomic>
#include <string>
#include <vector>
#include <windows.h>
#include <xinput.h>
#include "../../dualsense/dualsense_hid_wrapper.hpp"

namespace display_commander::widgets::dualsense_widget {

// Use the HID wrapper's device structure
using DualSenseDeviceInfo = display_commander::dualsense::DualSenseDevice;

// Thread-safe shared state for DualSense data
struct DualSenseSharedState {
    // Device enumeration
    std::vector<DualSenseDeviceInfo> devices;
    std::atomic<bool> enumeration_in_progress{false};
    std::atomic<uint64_t> last_enumeration_time{0};

    // Event counters
    std::atomic<uint64_t> total_events{0};
    std::atomic<uint64_t> button_events{0};
    std::atomic<uint64_t> stick_events{0};
    std::atomic<uint64_t> trigger_events{0};

    // Debug settings
    std::atomic<uint64_t> touchpad_events{0};

    // Settings
    std::atomic<bool> enable_dualsense_detection{true};
    std::atomic<bool> show_device_ids{true};
    std::atomic<bool> show_connection_type{true};
    std::atomic<bool> show_battery_info{true};
    std::atomic<bool> show_advanced_features{false};

    // HID device selection
    std::atomic<int> selected_hid_type{0}; // 0 = Auto, 1 = DualSense Regular, 2 = DualSense Edge, 3 = DualShock 4, 4 = All Sony

    // Thread safety
    mutable std::atomic<bool> is_updating{false};
};

// DualSense widget class
class DualSenseWidget {
public:
    DualSenseWidget();
    ~DualSenseWidget() = default;

    // Main draw function - call this from the main tab
    void OnDraw();

    // Initialize the widget (call once at startup)
    void Initialize();

    // Cleanup the widget (call at shutdown)
    void Cleanup();

    // Get the shared state (thread-safe)
    static std::shared_ptr<DualSenseSharedState> GetSharedState();

private:
    // UI state
    bool is_initialized_ = false;
    int selected_device_ = 0;

    // UI helper functions
    void DrawDeviceSelector();
    void DrawDeviceList();
    void DrawDeviceInfo();
    void DrawSettings();
    void DrawEventCounters();
    void DrawDeviceDetails(const DualSenseDeviceInfo& device);
    void DrawButtonStates(const DualSenseDeviceInfo& device);
    void DrawStickStates(const DualSenseDeviceInfo& device);
    void DrawTriggerStates(const DualSenseDeviceInfo& device);
    void DrawBatteryStatus(const DualSenseDeviceInfo& device);
    void DrawAdvancedFeatures(const DualSenseDeviceInfo& device);
    void DrawInputReport(const DualSenseDeviceInfo& device);

    // Raw input report parsing with debug offset
    void DrawRawButtonStates(const DualSenseDeviceInfo& device);
    void DrawRawStickStates(const DualSenseDeviceInfo& device);
    void DrawRawTriggerStates(const DualSenseDeviceInfo& device);
    void DrawSpecialKData(const DualSenseDeviceInfo& device);

    // Helper functions
    std::string GetButtonName(WORD button) const;
    std::string GetDeviceStatus(const DualSenseDeviceInfo& device) const;
    bool IsButtonPressed(WORD buttons, WORD button) const;

    // Special-K debug helper functions
    void DrawSpecialKFieldRow(const char* fieldName, int offset, int size, const std::vector<BYTE>& inputReport, const DualSenseDeviceInfo& device, const char* description = nullptr);
    void DrawSpecialKBitFieldRow(const char* fieldName, int byteOffset, int bitOffset, int bitCount, const std::vector<BYTE>& inputReport, const DualSenseDeviceInfo& device, const char* description = nullptr);
    std::string GetConnectionTypeString(const DualSenseDeviceInfo& device) const;
    std::string GetDeviceTypeString(const DualSenseDeviceInfo& device) const;
    std::string GetHIDTypeString(int hid_type) const;
    bool IsDeviceTypeEnabled(USHORT product_id) const;

    // Input report debug helpers
    std::string GetByteDescription(int offset, const std::string& connectionType) const;
    std::string GetByteValue(const std::vector<BYTE>& inputReport, int offset, const std::string& connectionType) const;
    std::string GetByteNotes(int offset, const std::string& connectionType) const;

    // Settings management
    void LoadSettings();
    void SaveSettings();

public:
    // Device enumeration
    void UpdateDeviceStates();

private:

    // Global shared state
    static std::shared_ptr<DualSenseSharedState> g_shared_state;
};

// Global widget instance
extern std::unique_ptr<DualSenseWidget> g_dualsense_widget;

// Global functions for integration
void InitializeDualSenseWidget();
void CleanupDualSenseWidget();
void DrawDualSenseWidget();

// Global functions for device enumeration
void EnumerateDualSenseDevices();
void UpdateDualSenseDeviceStates();

} // namespace display_commander::widgets::dualsense_widget