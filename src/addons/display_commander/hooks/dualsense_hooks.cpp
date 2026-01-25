#include "dualsense_hooks.hpp"
#include <atomic>
#include <chrono>
#include <thread>
#include "../dualsense/dualsense_hid_wrapper.hpp"
#include "../utils/logging.hpp"
#include "../utils/srwlock_wrapper.hpp"
#include "../widgets/xinput_widget/xinput_widget.hpp"
#include "hid_suppression_hooks.hpp"

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
        return;  // Already initialized
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

// Function to read DualSense state using direct HID calls
bool ReadDualSenseState(DWORD user_index, DualSenseState& state) {
    if (user_index >= XUSER_MAX_COUNT) {
        return false;
    }
    LogInfo("[DUALSENSE] ReadDualSenseState called for controller %lu", user_index);

    // Check if DualSense HID wrapper is available
    if (!display_commander::dualsense::g_dualsense_hid_wrapper) {
        return false;
    }
    LogInfo("[DUALSENSE] DualSense HID wrapper available");

    // Update all device states from HID
    display_commander::dualsense::g_dualsense_hid_wrapper->UpdateDeviceStates();

    // Get the device from HID wrapper
    auto* device = display_commander::dualsense::g_dualsense_hid_wrapper->GetDevice(user_index);
    if (device == nullptr || !device->is_connected) {
        state.connected = false;
        return false;
    }
    LogInfo("[DUALSENSE] DualSense device available");
    // Convert the device state to our DualSenseState format
    state.connected = device->is_connected;
    state.buttons = device->current_state.Gamepad.wButtons;
    state.left_stick_x = device->current_state.Gamepad.sThumbLX;
    state.left_stick_y = device->current_state.Gamepad.sThumbLY;
    state.right_stick_x = device->current_state.Gamepad.sThumbRX;
    state.right_stick_y = device->current_state.Gamepad.sThumbRY;
    state.left_trigger = device->current_state.Gamepad.bLeftTrigger;
    state.right_trigger = device->current_state.Gamepad.bRightTrigger;
    state.packet_number = device->current_state.dwPacketNumber;
    LogInfo("[DUALSENSE] DualSense state converted to XInput format state.connected %d", (int)state.connected);
    return state.connected;
}

void DualSensePollingOnce() {
    if (g_dualsense_available.load()) {
        for (DWORD i = 0; i < 1; ++i) {
            DualSenseState new_state;
            if (ReadDualSenseState(i, new_state)) {
                g_dualsense_states[i] = new_state;
            }
        }
    }
}

// Background thread to poll DualSense controllers
void DualSensePollingThread() {
    /*
    while (g_dualsense_thread_running.load()) {
        if (!display_commander::widgets::xinput_widget::XInputWidget::GetSharedState()->enable_dualsense_xinput.load())
    { std::this_thread::sleep_for(std::chrono::milliseconds(1000)); continue;
        }
        //LogInfo("[DUALSENSE] DualSense polling thread running");
        if (g_dualsense_available.load()) {
            for (DWORD i = 0; i < 1; ++i) {
                //LogInfo("[DUALSENSE] DualSense polling thread reading state for controller %lu", i);
                DualSenseState new_state;
                if (ReadDualSenseState(i, new_state)) {
                    g_dualsense_states[i] = new_state;
                }
            }
        }

        // Poll at 60Hz
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }*/
}

bool InitializeDualSenseSupport() {
    if (g_dualsense_initialized.load()) {
        return g_dualsense_available.load();
    }

    LogInfo("Initializing DualSense support...");

    // Initialize DualSense HID wrapper first
    display_commander::dualsense::InitializeDualSenseHID();

    // Check if HID wrapper is available
    if (!display_commander::dualsense::g_dualsense_hid_wrapper) {
        LogError("DualSense: Failed to initialize HID wrapper");
        g_dualsense_initialized.store(true);
        g_dualsense_available.store(false);
        return false;
    }

    // Initialize direct HID functions for fallback
    InitializeDirectHIDFunctions();

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

    // Cleanup HID wrapper
    display_commander::dualsense::CleanupDualSenseHID();

    g_dualsense_available.store(false);
    g_dualsense_initialized.store(false);

    LogInfo("DualSense support cleaned up");
}

bool IsDualSenseAvailable() { return g_dualsense_available.load(); }

bool ConvertDualSenseToXInput(DWORD user_index, XINPUT_STATE* state) {
    if (!g_dualsense_available.load() || user_index >= XUSER_MAX_COUNT || state == nullptr) {
        return false;
    }
    DualSensePollingOnce();

    const DualSenseState& dualsense = g_dualsense_states[user_index];
    if (!dualsense.connected) {
        return false;
    }

    // Convert DualSense state to XInput format
    state->dwPacketNumber = dualsense.packet_number;
    state->Gamepad.wButtons = dualsense.buttons;
    state->Gamepad.sThumbLX = dualsense.left_stick_x;
    state->Gamepad.sThumbLY = dualsense.left_stick_y;
    state->Gamepad.sThumbRX = dualsense.right_stick_x;
    state->Gamepad.sThumbRY = dualsense.right_stick_y;
    state->Gamepad.bLeftTrigger = dualsense.left_trigger;
    state->Gamepad.bRightTrigger = dualsense.right_trigger;

    // Update XInput UI structures for proper display
    auto shared_state = display_commander::widgets::xinput_widget::XInputWidget::GetSharedState();
    if (shared_state) {
        // Thread-safe update
        utils::SRWLockExclusive lock(shared_state->state_lock);

        // Update controller state for UI display
        shared_state->controller_states[user_index] = *state;
        shared_state->controller_connected[user_index] =
            display_commander::widgets::xinput_widget::ControllerState::Connected;
        shared_state->last_packet_numbers[user_index] = state->dwPacketNumber;
        shared_state->last_update_times[user_index] = GetTickCount64();

        // Increment event counter
        shared_state->total_events.fetch_add(1);
    }

    return true;
}

}  // namespace display_commander::hooks
