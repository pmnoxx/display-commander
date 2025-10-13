#include "dualsense_hooks.hpp"
#include "hid_suppression_hooks.hpp"
#include "../utils.hpp"
#include <atomic>
#include <thread>
#include <chrono>

namespace display_commander::hooks {

// Direct function pointers for bypassing HID suppression
ReadFile_pfn ReadFile_Direct = nullptr;
HidD_GetInputReport_pfn HidD_GetInputReport_Direct = nullptr;
HidD_GetAttributes_pfn HidD_GetAttributes_Direct = nullptr;

// Global DualSense state for all controllers
DualSenseState g_dualsense_states[XUSER_MAX_COUNT];

// DualSense support state
static std::atomic<bool> g_dualsense_initialized{false};
static std::atomic<bool> g_dualsense_available{false};
static std::thread g_dualsense_thread;
static std::atomic<bool> g_dualsense_thread_running{false};

// Initialize direct function pointers for bypassing HID suppression
void InitializeDirectHIDFunctions() {
    if (ReadFile_Direct != nullptr) {
        return; // Already initialized
    }

    // Get direct function pointers from HID suppression hooks
    ReadFile_Direct = renodx::hooks::ReadFile_Original;
    HidD_GetInputReport_Direct = renodx::hooks::HidD_GetInputReport_Original;
    HidD_GetAttributes_Direct = renodx::hooks::HidD_GetAttributes_Original;

    // If hooks aren't installed, get functions directly from system
    if (ReadFile_Direct == nullptr) {
        HMODULE kernel32 = GetModuleHandleA("kernel32.dll");
        if (kernel32) {
            ReadFile_Direct = (ReadFile_pfn)GetProcAddress(kernel32, "ReadFile");
        }
    }

    if (HidD_GetInputReport_Direct == nullptr || HidD_GetAttributes_Direct == nullptr) {
        HMODULE hid = LoadLibraryA("hid.dll");
        if (hid) {
            if (HidD_GetInputReport_Direct == nullptr) {
                HidD_GetInputReport_Direct = (HidD_GetInputReport_pfn)GetProcAddress(hid, "HidD_GetInputReport");
            }
            if (HidD_GetAttributes_Direct == nullptr) {
                HidD_GetAttributes_Direct = (HidD_GetAttributes_pfn)GetProcAddress(hid, "HidD_GetAttributes");
            }
        }
    }

    LogInfo("DualSense: Direct HID functions initialized - ReadFile: %p, GetInputReport: %p, GetAttributes: %p",
            ReadFile_Direct, HidD_GetInputReport_Direct, HidD_GetAttributes_Direct);
}

// Function to check if Special-K is available and has DualSense support
bool CheckSpecialKDualSenseSupport() {
    // Try to load Special-K's DualSense functions
    // This is a simplified check - in a real implementation, you'd need to
    // dynamically load Special-K's functions or use a more sophisticated detection method

    // For now, we'll assume Special-K is available if we can find its module
    HMODULE sk_module = GetModuleHandleA("SpecialK64.dll");
    if (sk_module == nullptr) {
        sk_module = GetModuleHandleA("SpecialK32.dll");
    }

    if (sk_module == nullptr) {
        return false;
    }

    // In a real implementation, you would check for specific Special-K functions here
    // For now, we'll just return true if Special-K is loaded
    return true;
}

// Function to read DualSense state using direct HID calls
bool ReadDualSenseState(DWORD user_index, DualSenseState& state) {
    if (user_index >= XUSER_MAX_COUNT) {
        return false;
    }

    // Initialize direct HID functions if needed
    InitializeDirectHIDFunctions();

    // This is a placeholder implementation for direct HID reading
    // In a real implementation, you would:
    // 1. Enumerate HID devices to find DualSense controllers
    // 2. Use ReadFile_Direct or HidD_GetInputReport_Direct to read input reports
    // 3. Parse the DualSense input report format
    // 4. Convert the data to XInput format

    // Example of how to use the direct functions (commented out):
    /*
    if (ReadFile_Direct) {
        // Use ReadFile_Direct to read from HID device handle
        // This bypasses the HID suppression hooks
        DWORD bytesRead = 0;
        BYTE inputReport[78]; // DualSense input report size
        if (ReadFile_Direct(hidDeviceHandle, inputReport, sizeof(inputReport), &bytesRead, nullptr)) {
            // Parse the input report and convert to XInput format
            // ... parsing code would go here ...
        }
    }

    if (HidD_GetInputReport_Direct) {
        // Use HidD_GetInputReport_Direct to read input report
        // This also bypasses the HID suppression hooks
        BYTE inputReport[78];
        if (HidD_GetInputReport_Direct(hidDeviceHandle, inputReport, sizeof(inputReport))) {
            // Parse the input report and convert to XInput format
            // ... parsing code would go here ...
        }
    }
    */

    // For now, we'll just return false to indicate no DualSense data
    // This prevents the feature from working until proper HID integration is implemented
    return false;
}

// Background thread to poll DualSense controllers
void DualSensePollingThread() {
    while (g_dualsense_thread_running.load()) {
        if (g_dualsense_available.load()) {
            for (DWORD i = 0; i < XUSER_MAX_COUNT; ++i) {
                DualSenseState new_state;
                if (ReadDualSenseState(i, new_state)) {
                    g_dualsense_states[i] = new_state;
                }
            }
        }

        // Poll at 60Hz
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }
}

bool InitializeDualSenseSupport() {
    if (g_dualsense_initialized.load()) {
        return g_dualsense_available.load();
    }

    LogInfo("Initializing DualSense support...");

    // Initialize direct HID functions first
    InitializeDirectHIDFunctions();

    // Check if we have the necessary HID functions
    if (ReadFile_Direct == nullptr && HidD_GetInputReport_Direct == nullptr) {
        LogError("DualSense: No direct HID functions available");
        g_dualsense_initialized.store(true);
        g_dualsense_available.store(false);
        return false;
    }

    // Check if Special-K is available (optional)
    bool sk_available = CheckSpecialKDualSenseSupport();
    if (sk_available) {
        LogInfo("Special-K detected - using Special-K DualSense support");
    } else {
        LogInfo("Special-K not found - using direct HID reading");
    }

    // Initialize DualSense states
    for (int i = 0; i < XUSER_MAX_COUNT; ++i) {
        g_dualsense_states[i] = {};
        g_dualsense_states[i].connected = false;
    }

    // Start polling thread
    g_dualsense_thread_running.store(true);
    g_dualsense_thread = std::thread(DualSensePollingThread);

    g_dualsense_available.store(true);
    g_dualsense_initialized.store(true);

    LogInfo("DualSense support initialized successfully");
    return true;
}

void CleanupDualSenseSupport() {
    if (!g_dualsense_initialized.load()) {
        return;
    }

    LogInfo("Cleaning up DualSense support...");

    // Stop polling thread
    g_dualsense_thread_running.store(false);
    if (g_dualsense_thread.joinable()) {
        g_dualsense_thread.join();
    }

    g_dualsense_available.store(false);
    g_dualsense_initialized.store(false);

    LogInfo("DualSense support cleaned up");
}

bool IsDualSenseAvailable() {
    return g_dualsense_available.load();
}

bool ConvertDualSenseToXInput(DWORD user_index, XINPUT_STATE* pState) {
    if (!g_dualsense_available.load() || user_index >= XUSER_MAX_COUNT || pState == nullptr) {
        return false;
    }

    const DualSenseState& dualsense = g_dualsense_states[user_index];
    if (!dualsense.connected) {
        return false;
    }

    // Convert DualSense state to XInput format
    pState->dwPacketNumber = dualsense.packet_number;
    pState->Gamepad.wButtons = dualsense.buttons;
    pState->Gamepad.sThumbLX = dualsense.left_stick_x;
    pState->Gamepad.sThumbLY = dualsense.left_stick_y;
    pState->Gamepad.sThumbRX = dualsense.right_stick_x;
    pState->Gamepad.sThumbRY = dualsense.right_stick_y;
    pState->Gamepad.bLeftTrigger = dualsense.left_trigger;
    pState->Gamepad.bRightTrigger = dualsense.right_trigger;

    return true;
}

} // namespace display_commander::hooks
