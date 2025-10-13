#pragma once

#include <windows.h>
#include <xinput.h>
#include <vector>
#include <memory>
#include <atomic>
#include <string>

// Forward declarations for XInput_HID types
using GetInputReport_pfn = bool (*)(void*);

// Type definitions for Special-K DualSense data
enum class Direction : uint8_t {
    Up = 0, UpRight = 1, Right = 2, DownRight = 3,
    Down = 4, DownLeft = 5, Left = 6, UpLeft = 7, None = 8
};

enum class PowerState : uint8_t {
    Unknown = 0, Charging = 1, Discharging = 2, NotCharging = 3, Full = 4
};

// TouchData structure (9 bytes)
struct TouchData {
    uint8_t data[9];
};

// Special-K DualSense HID data structure format
struct SK_HID_DualSense_GetStateData // 63 bytes
{
/* 0  */ uint8_t    LeftStickX;
/* 1  */ uint8_t    LeftStickY;
/* 2  */ uint8_t    RightStickX;
/* 3  */ uint8_t    RightStickY;
/* 4  */ uint8_t    TriggerLeft;
/* 5  */ uint8_t    TriggerRight;
/* 6  */ uint8_t    SeqNo;                   // always 0x01 on BT
/* 7.0*/ Direction  DPad                : 4;
/* 7.4*/ uint8_t    ButtonSquare        : 1;
/* 7.5*/ uint8_t    ButtonCross         : 1;
/* 7.6*/ uint8_t    ButtonCircle        : 1;
/* 7.7*/ uint8_t    ButtonTriangle      : 1;
/* 8.0*/ uint8_t    ButtonL1            : 1;
/* 8.1*/ uint8_t    ButtonR1            : 1;
/* 8.2*/ uint8_t    ButtonL2            : 1;
/* 8.3*/ uint8_t    ButtonR2            : 1;
/* 8.4*/ uint8_t    ButtonCreate        : 1;
/* 8.5*/ uint8_t    ButtonOptions       : 1;
/* 8.6*/ uint8_t    ButtonL3            : 1;
/* 8.7*/ uint8_t    ButtonR3            : 1;
/* 9.0*/ uint8_t    ButtonHome          : 1;
/* 9.1*/ uint8_t    ButtonPad           : 1;
/* 9.2*/ uint8_t    ButtonMute          : 1;
/* 9.3*/ uint8_t    UNK1                : 1; // appears unused
/* 9.4*/ uint8_t    ButtonLeftFunction  : 1; // DualSense Edge
/* 9.5*/ uint8_t    ButtonRightFunction : 1; // DualSense Edge
/* 9.6*/ uint8_t    ButtonLeftPaddle    : 1; // DualSense Edge
/* 9.7*/ uint8_t    ButtonRightPaddle   : 1; // DualSense Edge
/*10  */ uint8_t    UNK2;                    // appears unused
/*11  */ uint32_t   UNK_COUNTER;             // Linux driver calls this reserved, tools leak calls the 2 high bytes "random"
/*15  */ int16_t    AngularVelocityX;
/*17  */ int16_t    AngularVelocityZ;
/*19  */ int16_t    AngularVelocityY;
/*21  */ int16_t    AccelerometerX;
/*23  */ int16_t    AccelerometerY;
/*25  */ int16_t    AccelerometerZ;
/*27  */ uint32_t   SensorTimestamp;
/*31  */ int8_t     Temperature;                  // reserved2 in Linux driver
/*32  */ TouchData  TouchData;                   // TouchData structure (9 bytes)
/*41.0*/ uint8_t    TriggerRightStopLocation : 4; // trigger stop can be a range from 0 to 9 (F/9.0 for Apple interface)
/*41.4*/ uint8_t    TriggerRightStatus       : 4;
/*42.0*/ uint8_t    TriggerLeftStopLocation  : 4;
/*42.4*/ uint8_t    TriggerLeftStatus        : 4; // 0 feedbackNoLoad
                                                  // 1 feedbackLoadApplied
                                                  // 0 weaponReady
                                                  // 1 weaponFiring
                                                  // 2 weaponFired
                                                  // 0 vibrationNotVibrating
                                                  // 1 vibrationIsVibrating
/*43  */ uint32_t   HostTimestamp;                // mirrors data from report write
/*47.0*/ uint8_t    TriggerRightEffect       : 4; // Active trigger effect, previously we thought this was status max
/*47.4*/ uint8_t    TriggerLeftEffect        : 4; // 0 for reset and all other effects
                                                  // 1 for feedback effect
                                                  // 2 for weapon effect
                                                  // 3 for vibration
/*48  */ uint32_t   DeviceTimeStamp;
/*52.0*/ uint8_t    PowerPercent             : 4; // 0x00-0x0A
/*52.4*/ PowerState PowerState               : 4;
/*53.0*/ uint8_t    PluggedHeadphones        : 1;
/*53.1*/ uint8_t    PluggedMic               : 1;
/*53.2*/ uint8_t    MicMuted                 : 1; // Mic muted by powersave/mute command
/*53.3*/ uint8_t    PluggedUsbData           : 1;
/*53.4*/ uint8_t    PluggedUsbPower          : 1;
/*53.5*/ uint8_t    PluggedUnk1              : 3;
/*54.0*/ uint8_t    PluggedExternalMic       : 1; // Is external mic active (automatic in mic auto mode)
/*54.1*/ uint8_t    HapticLowPassFilter      : 1; // Is the Haptic Low-Pass-Filter active?
/*54.2*/ uint8_t    PluggedUnk3              : 6;
/*55  */ uint8_t    AesCmac[8];
};

// HID device structure (simplified version for our use)
struct hid_device_file_s {
    wchar_t wszDevicePath[MAX_PATH] = {};
    HANDLE hDeviceFile = INVALID_HANDLE_VALUE;
    bool bConnected = false;
    bool bWireless = false;

    struct {
        USHORT vid = 0;
        USHORT pid = 0;
    } devinfo;

    struct {
        XINPUT_STATE current = {};
        XINPUT_STATE prev = {};
        DWORD input_timestamp = 0;
    } state;

    std::vector<BYTE> input_report;
    GetInputReport_pfn get_input_report = nullptr;

    bool GetInputReport() {
        if (get_input_report) {
            return get_input_report(this);
        }
        return false;
    }
};

namespace display_commander::dualsense {

// DualSense device information
struct DualSenseDevice {
    std::string device_path;
    std::string device_name;
    std::string connection_type; // "USB" or "Bluetooth"
    USHORT vendor_id;
    USHORT product_id;
    bool is_connected;
    bool is_wireless;
    DWORD last_update_time;
    DWORD input_timestamp;

    // Device state
    XINPUT_STATE current_state;
    XINPUT_STATE previous_state;

    // Special-K DualSense HID data
    SK_HID_DualSense_GetStateData sk_dualsense_data;
    SK_HID_DualSense_GetStateData sk_dualsense_data_prev;

    // Device-specific features
    bool has_adaptive_triggers;
    bool has_touchpad;
    bool has_microphone;
    bool has_speaker;

    // Battery information (if available)
    bool battery_info_valid;
    BYTE battery_level;
    BYTE battery_type;

    // XInput_HID integration
    std::shared_ptr<hid_device_file_s> hid_device;
    GetInputReport_pfn get_input_report;

    DualSenseDevice() : vendor_id(0), product_id(0), is_connected(false),
                       is_wireless(false), last_update_time(0), input_timestamp(0),
                       has_adaptive_triggers(false), has_touchpad(false),
                       has_microphone(false), has_speaker(false),
                       battery_info_valid(false), battery_level(0), battery_type(0),
                       hid_device(nullptr), get_input_report(nullptr) {
        ZeroMemory(&current_state, sizeof(XINPUT_STATE));
        ZeroMemory(&previous_state, sizeof(XINPUT_STATE));
        ZeroMemory(&sk_dualsense_data, sizeof(SK_HID_DualSense_GetStateData));
        ZeroMemory(&sk_dualsense_data_prev, sizeof(SK_HID_DualSense_GetStateData));
    }
};

// DualSense HID wrapper class
class DualSenseHIDWrapper {
public:
    DualSenseHIDWrapper();
    ~DualSenseHIDWrapper();

    // Initialize the wrapper
    bool Initialize();

    // Cleanup the wrapper
    void Cleanup();

    // Enumerate connected DualSense devices
    void EnumerateDevices();

    // Update device states (poll input)
    void UpdateDeviceStates();

    // Get list of devices
    const std::vector<DualSenseDevice>& GetDevices() const { return devices_; }

    // Get device by index
    DualSenseDevice* GetDevice(size_t index);

    // Check if device is DualSense
    bool IsDualSenseDevice(USHORT vendor_id, USHORT product_id) const;

    // Check if device matches selected HID type
    bool IsDeviceTypeEnabled(USHORT vendor_id, USHORT product_id, int hid_type) const;

    // Get device type string
    std::string GetDeviceTypeString(USHORT vendor_id, USHORT product_id) const;

    // Set HID device type filter
    void SetHIDTypeFilter(int hid_type) { hid_type_filter_ = hid_type; }

    // Input report processing
    void ProcessUSBInputReport(DualSenseDevice& device, const BYTE* inputReport, DWORD bytesRead);
    void ProcessBluetoothInputReport(DualSenseDevice& device, const BYTE* inputReport, DWORD bytesRead);
    void ParseDualSenseButtons(DualSenseDevice& device, const BYTE* inputReport);
    void ParseDualSenseSticks(DualSenseDevice& device, const BYTE* inputReport);
    void ParseDualSenseTriggers(DualSenseDevice& device, const BYTE* inputReport);

    // Special-K format parsing
    void ParseSpecialKDualSenseData(DualSenseDevice& device, const BYTE* inputReport, DWORD bytesRead);
    void ConvertSpecialKToXInput(DualSenseDevice& device);
    void UpdateSpecialKData(DualSenseDevice& device, const SK_HID_DualSense_GetStateData& new_data);

private:
    // Device management
    std::vector<DualSenseDevice> devices_;
    std::atomic<bool> is_initialized_{false};
    std::atomic<bool> enumeration_in_progress_{false};
    std::atomic<int> hid_type_filter_{0}; // 0 = Auto, 1 = DualSense Regular, 2 = DualSense Edge, 3 = DualShock 4, 4 = All Sony

    // XInput_HID integration
    bool SetupXInputHIDIntegration();
    bool CreateHIDDevice(const std::string& device_path, DualSenseDevice& device);
    void UpdateDeviceFromHID(DualSenseDevice& device);

    // Device enumeration helpers
    void EnumerateHIDDevices();
    bool OpenDevice(const std::string& device_path, HANDLE& handle);
    bool GetDeviceAttributes(HANDLE handle, USHORT& vendor_id, USHORT& product_id);
    bool DetermineConnectionType(const std::string& device_path, bool& is_wireless);
};

// Global instance
extern std::unique_ptr<DualSenseHIDWrapper> g_dualsense_hid_wrapper;

// Global functions
void InitializeDualSenseHID();
void CleanupDualSenseHID();
void EnumerateDualSenseDevices();
void UpdateDualSenseDeviceStates();

} // namespace display_commander::dualsense
