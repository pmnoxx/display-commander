#include "xinput_hooks.hpp"
#include "hook_suppression_manager.hpp"
#include "../utils/logging.hpp"
#include "dualsense_hooks.hpp"
#include "../input_remapping/input_remapping.hpp"
#include "../utils/general_utils.hpp"
#include "../utils/timing.hpp"
#include "../widgets/xinput_widget/xinput_widget.hpp"
#include "../swapchain_events.hpp"
#include "windows_hooks/windows_message_hooks.hpp"
#include "../settings/main_tab_settings.hpp"
#include "../globals.hpp"
#include <MinHook.h>
#include <array>
#include <string>
#include <vector>
#include <cmath>
#include <functional>

// Guide button constant (not defined in standard XInput headers)
#ifndef XINPUT_GAMEPAD_GUIDE
#define XINPUT_GAMEPAD_GUIDE 0x0400
#endif

// Device type and subtype constants (not always defined in standard XInput headers)
#ifndef XINPUT_DEVTYPE_GAMEPAD
#define XINPUT_DEVTYPE_GAMEPAD 0x01
#endif
#ifndef XINPUT_DEVSUBTYPE_GAMEPAD
#define XINPUT_DEVSUBTYPE_GAMEPAD 0x01
#endif

namespace display_commanderhooks {


// XInput function pointers for direct calls
XInputGetState_pfn XInputGetState_Direct = nullptr;
XInputGetStateEx_pfn XInputGetStateEx_Direct = nullptr;
XInputSetState_pfn XInputSetState_Direct = nullptr;
XInputGetBatteryInformation_pfn XInputGetBatteryInformation_Direct = nullptr;
XInputGetCapabilities_pfn XInputGetCapabilities_Direct = nullptr;

// Hook state
static std::atomic<bool> g_xinput_hooks_installed{false};

// Track which modules have been hooked
std::array<bool, 5> hooked_modules = {};
const std::array<const char*, 5> xinput_modules = {
    "xinput1_4.dll", "xinput1_3.dll", "xinput1_2.dll", "xinput1_1.dll", "xinput9_1_0.dll",
};


std::array<XInputGetStateEx_pfn, 5> original_xinput_get_state_ex_procs = {};
std::array<XInputGetState_pfn, 5> original_xinput_get_state_procs = {};
std::array<XInputSetState_pfn, 5> original_xinput_set_state_procs = {};
std::array<XInputGetCapabilities_pfn, 5> original_xinput_get_capabilities_procs = {};

// Packet number tracking for each controller (0-3)
static std::array<DWORD, 4> g_packet_numbers = {};

// Forward declarations for per-module detour helpers
static DWORD WINAPI XInputGetState_Detour_Impl(size_t module_index, DWORD dwUserIndex, XINPUT_STATE *pState);
static DWORD WINAPI XInputGetStateEx_Detour_Impl(size_t module_index, DWORD dwUserIndex, XINPUT_STATE *pState);
static DWORD WINAPI XInputSetState_Detour_Impl(size_t module_index, DWORD dwUserIndex, XINPUT_VIBRATION *pVibration);
static DWORD WINAPI XInputGetCapabilities_Detour_Impl(size_t module_index, DWORD dwUserIndex, DWORD dwFlags,
                                                      XINPUT_CAPABILITIES *pCapabilities);

// Per-module detour entry points (these are what MinHook will install for each DLL)
static DWORD WINAPI XInputGetState_Detour_Module0(DWORD dwUserIndex, XINPUT_STATE *pState) { return XInputGetState_Detour_Impl(0, dwUserIndex, pState); }
static DWORD WINAPI XInputGetState_Detour_Module1(DWORD dwUserIndex, XINPUT_STATE *pState) { return XInputGetState_Detour_Impl(1, dwUserIndex, pState); }
static DWORD WINAPI XInputGetState_Detour_Module2(DWORD dwUserIndex, XINPUT_STATE *pState) { return XInputGetState_Detour_Impl(2, dwUserIndex, pState); }
static DWORD WINAPI XInputGetState_Detour_Module3(DWORD dwUserIndex, XINPUT_STATE *pState) { return XInputGetState_Detour_Impl(3, dwUserIndex, pState); }
static DWORD WINAPI XInputGetState_Detour_Module4(DWORD dwUserIndex, XINPUT_STATE *pState) { return XInputGetState_Detour_Impl(4, dwUserIndex, pState); }

static DWORD WINAPI XInputGetStateEx_Detour_Module0(DWORD dwUserIndex, XINPUT_STATE *pState) { return XInputGetStateEx_Detour_Impl(0, dwUserIndex, pState); }
static DWORD WINAPI XInputGetStateEx_Detour_Module1(DWORD dwUserIndex, XINPUT_STATE *pState) { return XInputGetStateEx_Detour_Impl(1, dwUserIndex, pState); }
static DWORD WINAPI XInputGetStateEx_Detour_Module2(DWORD dwUserIndex, XINPUT_STATE *pState) { return XInputGetStateEx_Detour_Impl(2, dwUserIndex, pState); }
static DWORD WINAPI XInputGetStateEx_Detour_Module3(DWORD dwUserIndex, XINPUT_STATE *pState) { return XInputGetStateEx_Detour_Impl(3, dwUserIndex, pState); }
static DWORD WINAPI XInputGetStateEx_Detour_Module4(DWORD dwUserIndex, XINPUT_STATE *pState) { return XInputGetStateEx_Detour_Impl(4, dwUserIndex, pState); }

static DWORD WINAPI XInputSetState_Detour_Module0(DWORD dwUserIndex, XINPUT_VIBRATION *pVibration) { return XInputSetState_Detour_Impl(0, dwUserIndex, pVibration); }
static DWORD WINAPI XInputSetState_Detour_Module1(DWORD dwUserIndex, XINPUT_VIBRATION *pVibration) { return XInputSetState_Detour_Impl(1, dwUserIndex, pVibration); }
static DWORD WINAPI XInputSetState_Detour_Module2(DWORD dwUserIndex, XINPUT_VIBRATION *pVibration) { return XInputSetState_Detour_Impl(2, dwUserIndex, pVibration); }
static DWORD WINAPI XInputSetState_Detour_Module3(DWORD dwUserIndex, XINPUT_VIBRATION *pVibration) { return XInputSetState_Detour_Impl(3, dwUserIndex, pVibration); }
static DWORD WINAPI XInputSetState_Detour_Module4(DWORD dwUserIndex, XINPUT_VIBRATION *pVibration) { return XInputSetState_Detour_Impl(4, dwUserIndex, pVibration); }

static DWORD WINAPI XInputGetCapabilities_Detour_Module0(DWORD dwUserIndex, DWORD dwFlags, XINPUT_CAPABILITIES *pCapabilities) { return XInputGetCapabilities_Detour_Impl(0, dwUserIndex, dwFlags, pCapabilities); }
static DWORD WINAPI XInputGetCapabilities_Detour_Module1(DWORD dwUserIndex, DWORD dwFlags, XINPUT_CAPABILITIES *pCapabilities) { return XInputGetCapabilities_Detour_Impl(1, dwUserIndex, dwFlags, pCapabilities); }
static DWORD WINAPI XInputGetCapabilities_Detour_Module2(DWORD dwUserIndex, DWORD dwFlags, XINPUT_CAPABILITIES *pCapabilities) { return XInputGetCapabilities_Detour_Impl(2, dwUserIndex, dwFlags, pCapabilities); }
static DWORD WINAPI XInputGetCapabilities_Detour_Module3(DWORD dwUserIndex, DWORD dwFlags, XINPUT_CAPABILITIES *pCapabilities) { return XInputGetCapabilities_Detour_Impl(3, dwUserIndex, dwFlags, pCapabilities); }
static DWORD WINAPI XInputGetCapabilities_Detour_Module4(DWORD dwUserIndex, DWORD dwFlags, XINPUT_CAPABILITIES *pCapabilities) { return XInputGetCapabilities_Detour_Impl(4, dwUserIndex, dwFlags, pCapabilities); }

static const std::array<decltype(&XInputGetState_Detour_Module0), 5> xinput_get_state_detours = {
    XInputGetState_Detour_Module0,
    XInputGetState_Detour_Module1,
    XInputGetState_Detour_Module2,
    XInputGetState_Detour_Module3,
    XInputGetState_Detour_Module4,
};

static const std::array<decltype(&XInputGetStateEx_Detour_Module0), 5> xinput_get_state_ex_detours = {
    XInputGetStateEx_Detour_Module0,
    XInputGetStateEx_Detour_Module1,
    XInputGetStateEx_Detour_Module2,
    XInputGetStateEx_Detour_Module3,
    XInputGetStateEx_Detour_Module4,
};

static const std::array<decltype(&XInputSetState_Detour_Module0), 5> xinput_set_state_detours = {
    XInputSetState_Detour_Module0,
    XInputSetState_Detour_Module1,
    XInputSetState_Detour_Module2,
    XInputSetState_Detour_Module3,
    XInputSetState_Detour_Module4,
};

static const std::array<decltype(&XInputGetCapabilities_Detour_Module0), 5> xinput_get_capabilities_detours = {
    XInputGetCapabilities_Detour_Module0,
    XInputGetCapabilities_Detour_Module1,
    XInputGetCapabilities_Detour_Module2,
    XInputGetCapabilities_Detour_Module3,
    XInputGetCapabilities_Detour_Module4,
};


// Initialize XInput function pointers for direct calls
static void InitializeXInputDirectFunctions() {
    if (XInputSetState_Direct != nullptr || XInputGetBatteryInformation_Direct != nullptr) {
        return; // Already initialized
    }
    for (const char *module_name : xinput_modules) {
        HMODULE xinput_module = LoadLibraryA(module_name);
        if (xinput_module != nullptr) {

            break;
        } else {
            LogInfo("XInput module: %s not found", module_name);
        }
    }
}


float recenter(float value, float center) {
    auto new_value = value - center;

    return new_value / (1 + abs(center));
}

// Helper function to apply max input, min output, deadzone, and center calibration to thumbstick values
void ApplyThumbstickProcessing(XINPUT_STATE *pState, float left_max_input, float right_max_input, float left_min_output,
                               float right_min_output, float left_deadzone, float right_deadzone,
                               float left_center_x, float left_center_y, float right_center_x, float right_center_y,
                               bool left_circular, bool right_circular) {
    if (!pState)
        return;

    // Process left stick
    float lx = ShortToFloat(pState->Gamepad.sThumbLX);
    float ly = ShortToFloat(pState->Gamepad.sThumbLY);

    // Apply center calibration (recenter the stick)
    lx = recenter(lx, left_center_x);
    ly = recenter(ly, left_center_y);

    // Use circular or square processing based on toggle
    if (left_circular) {
        ProcessStickInputRadial(lx, ly, left_deadzone, left_max_input, left_min_output);
    } else {
        ProcessStickInputSquare(lx, ly, left_deadzone, left_max_input, left_min_output);
    }

    // Convert back to SHORT with correct scaling
    pState->Gamepad.sThumbLX = FloatToShort(lx);
    pState->Gamepad.sThumbLY = FloatToShort(ly);

    // Process right stick
    float rx = ShortToFloat(pState->Gamepad.sThumbRX);
    float ry = ShortToFloat(pState->Gamepad.sThumbRY);

    // Apply center calibration (recenter the stick)
    rx = recenter(rx, right_center_x);
    ry = recenter(ry, right_center_y);

    // Use circular or square processing based on toggle
    if (right_circular) {
        ProcessStickInputRadial(rx, ry, right_deadzone, right_max_input, right_min_output);
    } else {
        ProcessStickInputSquare(rx, ry, right_deadzone, right_max_input, right_min_output);
    }

    // Convert back to SHORT with correct scaling
    pState->Gamepad.sThumbRX = FloatToShort(rx);
    pState->Gamepad.sThumbRY = FloatToShort(ry);
}

// Helper function containing shared logic for XInputGetState and XInputGetStateEx
static DWORD ProcessXInputGetState(DWORD dwUserIndex, XINPUT_STATE *pState, HookIndex hook_index,
                                    std::atomic<uint64_t> &update_ns_field, const char *error_function_name,
                                    const std::function<DWORD(DWORD, XINPUT_STATE *)> &call_original_func) {
    // Track hook call statistics
    g_hook_stats[hook_index].increment_total();

    // Measure timing for smooth call rate calculation
    auto shared_state = display_commander::widgets::xinput_widget::XInputWidget::GetSharedState();
    if (shared_state && dwUserIndex == 0) {
        uint64_t current_time_ns = utils::get_now_ns();
        uint64_t last_call_time = shared_state->last_xinput_call_time_ns.load();

        if (last_call_time > 0) {
            uint64_t time_since_last_call_ns = current_time_ns - last_call_time;
            // Only update if time since last call is reasonable (ignore if > 1000ms)
            if (time_since_last_call_ns < 1 * utils::SEC_TO_NS) { // 1 second in nanoseconds
                uint64_t old_update_ns = update_ns_field.load();
                uint64_t new_update_ns = UpdateRollingAverage(time_since_last_call_ns, old_update_ns);
                update_ns_field.store(new_update_ns);
            }
        }
        shared_state->last_xinput_call_time_ns.store(current_time_ns);
    }

    // Check if override state is set - if so, spoof controller connection for controller 0
    bool should_spoof_connection = g_auto_click_enabled.load() && dwUserIndex == 0;

    // Check if DualSense to XInput conversion is enabled
    bool dualsense_enabled = shared_state && shared_state->enable_dualsense_xinput.load();

    DWORD result = ERROR_DEVICE_NOT_CONNECTED;

    // Try DualSense conversion first if enabled
    if (dualsense_enabled && display_commander::hooks::IsDualSenseAvailable()) {
        if (display_commander::hooks::ConvertDualSenseToXInput(dwUserIndex, pState)) {
            result = ERROR_SUCCESS;
        }
    }

    // Track whether we spoofed the connection
    bool did_spoof_connection = false;

    // If controller is not connected but we need to spoof for override, create fake state
    if (should_spoof_connection) {
        // Initialize fake gamepad state (all zeros)
        ZeroMemory(&pState->Gamepad, sizeof(XINPUT_GAMEPAD));
        // Packet number will be set just before return
        result = ERROR_SUCCESS; // Spoof as connected
        did_spoof_connection = true;
    }

    // Fall back to original XInput if DualSense conversion failed or is disabled
    if (result != ERROR_SUCCESS) {
        result = call_original_func(dwUserIndex, pState);
    }


    // Override packet number with our tracked value just before returning
    if (dwUserIndex < 4) {
        // If we spoofed the connection, increment our tracked packet number
        g_packet_numbers[dwUserIndex]++;
        // Always override with our tracked packet number
        pState->dwPacketNumber = g_packet_numbers[dwUserIndex];
    }

    // Apply A/B button swapping if enabled
    if (result == ERROR_SUCCESS) {
        // Mark controller as connected in shared state
        if (shared_state && dwUserIndex < XUSER_MAX_COUNT) {
            shared_state->controller_connected[dwUserIndex] = display_commander::widgets::xinput_widget::ControllerState::Connected;
        }
        // Store the frame ID when XInput is successfully detected
        uint64_t current_frame_id = g_global_frame_id.load();
        g_last_xinput_detected_frame_id.store(current_frame_id);

        auto shared_state = display_commander::widgets::xinput_widget::XInputWidget::GetSharedState();

        // Store original state for UI tracking (before any modifications)
        XINPUT_STATE original_state = *pState;

        // Apply override state if set (check before suppression)
        if (shared_state) {
            float override_lx = shared_state->override_state.left_stick_x.load();
            float override_ly = shared_state->override_state.left_stick_y.load();
            float override_rx = shared_state->override_state.right_stick_x.load();
            float override_ry = shared_state->override_state.right_stick_y.load();
            WORD override_buttons = shared_state->override_state.buttons_pressed_mask.load();

            // Apply stick overrides (INFINITY means not overridden)
            if (!std::isinf(override_lx)) {
                pState->Gamepad.sThumbLX = FloatToShort(override_lx);
            }
            if (!std::isinf(override_ly)) {
                pState->Gamepad.sThumbLY = FloatToShort(override_ly);
            }
            if (!std::isinf(override_rx)) {
                pState->Gamepad.sThumbRX = FloatToShort(override_rx);
            }
            if (!std::isinf(override_ry)) {
                pState->Gamepad.sThumbRY = FloatToShort(override_ry);
            }

            // Apply button override (mask 0 means no override)
            if (override_buttons != 0) {
                pState->Gamepad.wButtons |= override_buttons;
            }
        }

        // Process input normally
        if (shared_state && shared_state->swap_a_b_buttons.load()) {
            // Swap A and B buttons
            WORD original_buttons = pState->Gamepad.wButtons;
            WORD swapped_buttons = original_buttons;

            // If A is pressed, set B instead
            if (original_buttons & XINPUT_GAMEPAD_A) {
                swapped_buttons |= XINPUT_GAMEPAD_B;
                swapped_buttons &= ~XINPUT_GAMEPAD_A;
                LogInfo("XXX A/B Swap: A pressed -> B set (Controller %lu)", dwUserIndex);
            }
            // If B is pressed, set A instead
            if (original_buttons & XINPUT_GAMEPAD_B) {
                swapped_buttons |= XINPUT_GAMEPAD_A;
                swapped_buttons &= ~XINPUT_GAMEPAD_B;
                LogInfo("XXX A/B Swap: B pressed -> A set (Controller %lu)", dwUserIndex);
            }

            pState->Gamepad.wButtons = swapped_buttons;
        }

        // Apply max input, min output, deadzone, and center calibration processing
        if (shared_state) {
            float left_max_input = shared_state->left_stick_max_input.load();
            float right_max_input = shared_state->right_stick_max_input.load();
            float left_min_output = shared_state->left_stick_min_output.load();
            float right_min_output = shared_state->right_stick_min_output.load();
            float left_deadzone =
                shared_state->left_stick_deadzone.load() / 100.0f; // Convert percentage to decimal
            float right_deadzone =
                shared_state->right_stick_deadzone.load() / 100.0f; // Convert percentage to decimal

            float left_center_x = shared_state->left_stick_center_x.load();
            float left_center_y = shared_state->left_stick_center_y.load();
            float right_center_x = shared_state->right_stick_center_x.load();
            float right_center_y = shared_state->right_stick_center_y.load();

            bool left_circular = shared_state->left_stick_circular.load();
            bool right_circular = shared_state->right_stick_circular.load();

            ApplyThumbstickProcessing(pState, left_max_input, right_max_input, left_min_output, right_min_output,
                                      left_deadzone, right_deadzone, left_center_x, left_center_y, right_center_x, right_center_y,
                                      left_circular, right_circular);
        }

        // Process input remapping before updating state
        display_commander::input_remapping::process_gamepad_input_for_remapping(dwUserIndex, pState);

        // Block gamepad input to game when home button is pressed (if enabled)
        // This prevents accidental button presses while using shortcuts
        auto &remapper = display_commander::input_remapping::InputRemapper::get_instance();
        if (remapper.is_block_input_on_home_button() && (pState->Gamepad.wButtons & XINPUT_GAMEPAD_GUIDE) != 0) {
            // Clear all buttons except home button (so remapping can still work)
            WORD home_button_state = pState->Gamepad.wButtons & XINPUT_GAMEPAD_GUIDE;
            pState->Gamepad.wButtons = home_button_state;

            // Clear stick inputs
            pState->Gamepad.sThumbLX = 0;
            pState->Gamepad.sThumbLY = 0;
            pState->Gamepad.sThumbRX = 0;
            pState->Gamepad.sThumbRY = 0;

            // Clear trigger inputs
            pState->Gamepad.bLeftTrigger = 0;
            pState->Gamepad.bRightTrigger = 0;
        }

        // Process autofire
        display_commander::widgets::xinput_widget::ProcessAutofire(dwUserIndex, pState);

        // Track unsuppressed call (input was processed)
        g_hook_stats[hook_index].increment_unsuppressed();

        // Always update the UI state with the original state (before suppression/modifications)
        // This ensures the UI shows the actual controller state regardless of suppression
        display_commander::widgets::xinput_widget::UpdateXInputState(dwUserIndex, &original_state);

        // Update battery status periodically
        display_commander::widgets::xinput_widget::UpdateBatteryStatus(dwUserIndex);
    } else {
        // Mark controller as disconnected in shared state
        if (dwUserIndex < XUSER_MAX_COUNT) {
            auto shared_state = display_commander::widgets::xinput_widget::XInputWidget::GetSharedState();
            if (shared_state) {
                shared_state->controller_connected[dwUserIndex] = display_commander::widgets::xinput_widget::ControllerState::Unconnected;
            }
        }
        if (dwUserIndex == 0) {
            LogErrorThrottled(10, "XXX XInput Controller %lu: %s failed with error %lu (Perhaps disable steam input?)", dwUserIndex, error_function_name, result);
        }
    }

    return result;
}

// Hooked XInputGetState function
DWORD WINAPI XInputGetState_Detour(DWORD dwUserIndex, XINPUT_STATE *pState) {
    if (pState == nullptr) {
        return ERROR_INVALID_PARAMETER;
    }
    if (XInputGetStateEx_Direct == nullptr || XInputGetState_Direct == nullptr) {
        return ERROR_DEVICE_NOT_CONNECTED;
    }

    // Static state for fallback logic
    static bool tried_get_state_ex = false;
    static bool use_get_state_ex = false;

    // Get shared state for timing field access
    auto shared_state = display_commander::widgets::xinput_widget::XInputWidget::GetSharedState();
    if (!shared_state) {
        return ERROR_DEVICE_NOT_CONNECTED;
    }

    // Lambda to call original function with fallback logic
    auto call_original = [&](DWORD user_index, XINPUT_STATE *state) -> DWORD {
        DWORD result = use_get_state_ex ? XInputGetStateEx_Direct(user_index, state) : XInputGetState_Direct(user_index, state);
        if (result == ERROR_SUCCESS) {
            if (!tried_get_state_ex) {
                tried_get_state_ex = true;
                use_get_state_ex = XInputGetStateEx_Direct(user_index, state) == ERROR_SUCCESS;
            }
        }
        return result;
    };

    return ProcessXInputGetState(dwUserIndex, pState, HOOK_XInputGetState,
                                  shared_state->xinput_getstate_update_ns, "GetState", call_original);
}

// Hooked XInputGetStateEx function
DWORD WINAPI XInputGetStateEx_Detour(DWORD dwUserIndex, XINPUT_STATE *pState) {
    if (pState == nullptr) {
        return ERROR_INVALID_PARAMETER;
    }

    // Get shared state for timing field access
    auto shared_state = display_commander::widgets::xinput_widget::XInputWidget::GetSharedState();
    if (!shared_state) {
        return ERROR_DEVICE_NOT_CONNECTED;
    }

    // Lambda to call original function directly
    auto call_original = [](DWORD user_index, XINPUT_STATE *state) -> DWORD {
        return XInputGetStateEx_Direct != nullptr ? XInputGetStateEx_Direct(user_index, state) : ERROR_DEVICE_NOT_CONNECTED;
    };

    return ProcessXInputGetState(dwUserIndex, pState, HOOK_XInputGetStateEx,
                                  shared_state->xinput_getstateex_update_ns, "GetStateEx", call_original);
}

// Hooked XInputSetState function
DWORD WINAPI XInputSetState_Detour(DWORD dwUserIndex, XINPUT_VIBRATION *pVibration) {
    if (pVibration == nullptr) {
        return ERROR_INVALID_PARAMETER;
    }
    if (XInputSetState_Direct == nullptr) {
        return ERROR_DEVICE_NOT_CONNECTED;
    }

    // Track hook call statistics
    g_hook_stats[HOOK_XInputSetState].increment_total();

    // Get vibration amplification setting
    auto shared_state = display_commander::widgets::xinput_widget::XInputWidget::GetSharedState();
    float amplification = 1.0f;
    if (shared_state) {
        amplification = shared_state->vibration_amplification.load();
    }

    // Apply amplification if enabled (amplification != 1.0)
    XINPUT_VIBRATION vibration = *pVibration;
    if (amplification != 1.0f) {
        // Convert WORD (0-65535) to float, apply amplification, clamp, and convert back
        float left_speed = static_cast<float>(vibration.wLeftMotorSpeed);
        float right_speed = static_cast<float>(vibration.wRightMotorSpeed);

        left_speed *= amplification;
        right_speed *= amplification;

        // Clamp to valid range (0-65535)
        left_speed = (left_speed < 0.0f) ? 0.0f : (left_speed > 65535.0f) ? 65535.0f : left_speed;
        right_speed = (right_speed < 0.0f) ? 0.0f : (right_speed > 65535.0f) ? 65535.0f : right_speed;

        vibration.wLeftMotorSpeed = static_cast<WORD>(left_speed);
        vibration.wRightMotorSpeed = static_cast<WORD>(right_speed);
    }

    // Call original function with amplified vibration
    DWORD result = XInputSetState_Direct(dwUserIndex, &vibration);

    if (result == ERROR_SUCCESS) {
        g_hook_stats[HOOK_XInputSetState].increment_unsuppressed();
    }

    return result;
}

// Hooked XInputGetCapabilities function
DWORD WINAPI XInputGetCapabilities_Detour(DWORD dwUserIndex, DWORD dwFlags, XINPUT_CAPABILITIES *pCapabilities) {
    // Track hook call statistics (do this first to verify hook is being called)
    g_hook_stats[HOOK_XInputGetCapabilities].increment_total();

    if (pCapabilities == nullptr) {
        return ERROR_INVALID_PARAMETER;
    }
    if (XInputGetCapabilities_Direct == nullptr) {
        LogErrorThrottled(10, "XInputGetCapabilities_Detour called but XInputGetCapabilities_Direct is nullptr");
        return ERROR_DEVICE_NOT_CONNECTED;
    }

    // Get shared state to check settings
    auto shared_state = display_commander::widgets::xinput_widget::XInputWidget::GetSharedState();

    // Check if override state is set - if so, spoof controller connection for controller 0
    bool should_spoof_connection = g_auto_click_enabled.load() && dwUserIndex == 0;

    // Check if DualSense to XInput conversion is enabled
    bool dualsense_enabled = shared_state && shared_state->enable_dualsense_xinput.load();

    // If DS to XInput is enabled and DualSense is available, or spoofing is enabled, fake connected state
    bool should_fake_connection = false;
    if (dualsense_enabled && display_commander::hooks::IsDualSenseAvailable()) {
        should_fake_connection = true;
    } else if (should_spoof_connection) {
        should_fake_connection = true;
    }

    // If we should fake connection, return fake capabilities
    if (should_fake_connection) {
        // Initialize fake capabilities structure
        ZeroMemory(pCapabilities, sizeof(XINPUT_CAPABILITIES));

        // Set standard gamepad type and subtype
        pCapabilities->Type = XINPUT_DEVTYPE_GAMEPAD;
        pCapabilities->SubType = XINPUT_DEVSUBTYPE_GAMEPAD;
        pCapabilities->Flags = 0; // No special flags

        // Initialize gamepad structure (all buttons available)
        ZeroMemory(&pCapabilities->Gamepad, sizeof(XINPUT_GAMEPAD));

        // Initialize vibration structure (vibration supported)
        pCapabilities->Vibration.wLeftMotorSpeed = 0;
        pCapabilities->Vibration.wRightMotorSpeed = 0;

        g_hook_stats[HOOK_XInputGetCapabilities].increment_unsuppressed();
        return ERROR_SUCCESS;
    }

    // Otherwise, call the original function
    DWORD result = XInputGetCapabilities_Direct(dwUserIndex, dwFlags, pCapabilities);

    if (result == ERROR_SUCCESS) {
        g_hook_stats[HOOK_XInputGetCapabilities].increment_unsuppressed();
    }

    return result;
}

// Per-module implementation helpers used by the per-DLL detours.
// These route calls to the correct original function for the specific xinput DLL.
static DWORD WINAPI XInputGetState_Detour_Impl(size_t module_index, DWORD dwUserIndex, XINPUT_STATE *pState) {
    if (pState == nullptr) {
        return ERROR_INVALID_PARAMETER;
    }

    // Get shared state for timing field access
    auto shared_state = display_commander::widgets::xinput_widget::XInputWidget::GetSharedState();
    if (!shared_state) {
        return ERROR_DEVICE_NOT_CONNECTED;
    }

    // Lambda to call original function for the specific module, with sensible fallbacks
    auto call_original = [module_index](DWORD user_index, XINPUT_STATE *state) -> DWORD {
        XInputGetStateEx_pfn get_state_ex = nullptr;
        XInputGetState_pfn get_state = nullptr;

        if (module_index < original_xinput_get_state_ex_procs.size()) {
            get_state_ex = original_xinput_get_state_ex_procs[module_index];
        }
        if (module_index < original_xinput_get_state_procs.size()) {
            get_state = original_xinput_get_state_procs[module_index];
        }

        // Prefer the module-specific XInputGetStateEx if available
        if (get_state_ex != nullptr) {
            return get_state_ex(user_index, state);
        }

        // Fallback to the module-specific XInputGetState if available
        if (get_state != nullptr) {
            return get_state(user_index, state);
        }

        // Ultimate fallback to global direct pointers (if for some reason module-specific pointers are missing)
        if (XInputGetStateEx_Direct != nullptr) {
            return XInputGetStateEx_Direct(user_index, state);
        }
        if (XInputGetState_Direct != nullptr) {
            return XInputGetState_Direct(user_index, state);
        }

        return ERROR_DEVICE_NOT_CONNECTED;
    };

    return ProcessXInputGetState(dwUserIndex, pState, HOOK_XInputGetState,
                                 shared_state->xinput_getstate_update_ns, "GetState", call_original);
}

static DWORD WINAPI XInputGetStateEx_Detour_Impl(size_t module_index, DWORD dwUserIndex, XINPUT_STATE *pState) {
    if (pState == nullptr) {
        return ERROR_INVALID_PARAMETER;
    }

    // Get shared state for timing field access
    auto shared_state = display_commander::widgets::xinput_widget::XInputWidget::GetSharedState();
    if (!shared_state) {
        return ERROR_DEVICE_NOT_CONNECTED;
    }

    // Lambda to call original function for the specific module, with sensible fallbacks
    auto call_original = [module_index](DWORD user_index, XINPUT_STATE *state) -> DWORD {
        XInputGetStateEx_pfn get_state_ex = nullptr;

        if (module_index < original_xinput_get_state_ex_procs.size()) {
            get_state_ex = original_xinput_get_state_ex_procs[module_index];
        }

        // Prefer the module-specific XInputGetStateEx if available
        if (get_state_ex != nullptr) {
            return get_state_ex(user_index, state);
        }

        // Fallback to the global direct pointer (first discovered module)
        if (XInputGetStateEx_Direct != nullptr) {
            return XInputGetStateEx_Direct(user_index, state);
        }

        return ERROR_DEVICE_NOT_CONNECTED;
    };

    return ProcessXInputGetState(dwUserIndex, pState, HOOK_XInputGetStateEx,
                                 shared_state->xinput_getstateex_update_ns, "GetStateEx", call_original);
}

static DWORD WINAPI XInputSetState_Detour_Impl(size_t module_index, DWORD dwUserIndex, XINPUT_VIBRATION *pVibration) {
    if (pVibration == nullptr) {
        return ERROR_INVALID_PARAMETER;
    }

    // Track hook call statistics
    g_hook_stats[HOOK_XInputSetState].increment_total();

    // Get vibration amplification setting
    auto shared_state = display_commander::widgets::xinput_widget::XInputWidget::GetSharedState();
    float amplification = 1.0f;
    if (shared_state) {
        amplification = shared_state->vibration_amplification.load();
    }

    // Apply amplification if enabled (amplification != 1.0)
    XINPUT_VIBRATION vibration = *pVibration;
    if (amplification != 1.0f) {
        // Convert WORD (0-65535) to float, apply amplification, clamp, and convert back
        float left_speed = static_cast<float>(vibration.wLeftMotorSpeed);
        float right_speed = static_cast<float>(vibration.wRightMotorSpeed);

        left_speed *= amplification;
        right_speed *= amplification;

        // Clamp to valid range (0-65535)
        left_speed = (left_speed < 0.0f) ? 0.0f : (left_speed > 65535.0f) ? 65535.0f : left_speed;
        right_speed = (right_speed < 0.0f) ? 0.0f : (right_speed > 65535.0f) ? 65535.0f : right_speed;

        vibration.wLeftMotorSpeed = static_cast<WORD>(left_speed);
        vibration.wRightMotorSpeed = static_cast<WORD>(right_speed);
    }

    XInputSetState_pfn set_state = nullptr;
    if (module_index < original_xinput_set_state_procs.size()) {
        set_state = original_xinput_set_state_procs[module_index];
    }
    if (set_state == nullptr) {
        set_state = XInputSetState_Direct;
    }

    if (set_state == nullptr) {
        return ERROR_DEVICE_NOT_CONNECTED;
    }

    // Call original function with amplified vibration
    DWORD result = set_state(dwUserIndex, &vibration);

    if (result == ERROR_SUCCESS) {
        g_hook_stats[HOOK_XInputSetState].increment_unsuppressed();
    }

    return result;
}

static DWORD WINAPI XInputGetCapabilities_Detour_Impl(size_t module_index, DWORD dwUserIndex, DWORD dwFlags, XINPUT_CAPABILITIES *pCapabilities) {
    // Track hook statistics (do this first to verify hook is being called)
    g_hook_stats[HOOK_XInputGetCapabilities].increment_total();

    if (pCapabilities == nullptr) {
        return ERROR_INVALID_PARAMETER;
    }

    XInputGetCapabilities_pfn get_capabilities = nullptr;
    if (module_index < original_xinput_get_capabilities_procs.size()) {
        get_capabilities = original_xinput_get_capabilities_procs[module_index];
    }
    if (get_capabilities == nullptr) {
        get_capabilities = XInputGetCapabilities_Direct;
    }

    if (get_capabilities == nullptr) {
        LogErrorThrottled(10, "XInputGetCapabilities_Detour called but no original function pointer is available");
        return ERROR_DEVICE_NOT_CONNECTED;
    }

    // Get shared state to check settings
    auto shared_state = display_commander::widgets::xinput_widget::XInputWidget::GetSharedState();

    // Check if override state is set - if so, spoof controller connection for controller 0
    bool should_spoof_connection = g_auto_click_enabled.load() && dwUserIndex == 0;

    // Check if DualSense to XInput conversion is enabled
    bool dualsense_enabled = shared_state && shared_state->enable_dualsense_xinput.load();

    // If DS to XInput is enabled and DualSense is available, or spoofing is enabled, fake connected state
    bool should_fake_connection = false;
    if (dualsense_enabled && display_commander::hooks::IsDualSenseAvailable()) {
        should_fake_connection = true;
    } else if (should_spoof_connection) {
        should_fake_connection = true;
    }

    // If we should fake connection, return fake capabilities
    if (should_fake_connection) {
        // Initialize fake capabilities structure
        ZeroMemory(pCapabilities, sizeof(XINPUT_CAPABILITIES));

        // Set standard gamepad type and subtype
        pCapabilities->Type = XINPUT_DEVTYPE_GAMEPAD;
        pCapabilities->SubType = XINPUT_DEVSUBTYPE_GAMEPAD;
        pCapabilities->Flags = 0; // No special flags

        // Initialize gamepad structure (all buttons available)
        ZeroMemory(&pCapabilities->Gamepad, sizeof(XINPUT_GAMEPAD));

        // Initialize vibration structure (vibration supported)
        pCapabilities->Vibration.wLeftMotorSpeed = 0;
        pCapabilities->Vibration.wRightMotorSpeed = 0;

        g_hook_stats[HOOK_XInputGetCapabilities].increment_unsuppressed();
        return ERROR_SUCCESS;
    }

    // Otherwise, call the original function
    DWORD result = get_capabilities(dwUserIndex, dwFlags, pCapabilities);

    if (result == ERROR_SUCCESS) {
        g_hook_stats[HOOK_XInputGetCapabilities].increment_unsuppressed();
    }

    return result;
}

bool InstallXInputHooks(HMODULE xinput_module) {
    // Check if XInput hooks should be suppressed
    if (display_commanderhooks::HookSuppressionManager::GetInstance().ShouldSuppressHook(display_commanderhooks::HookType::XINPUT)) {
        LogInfo("XInput hooks installation suppressed by user setting");
        return false;
    }

    if (!g_initialized_with_hwnd.load()) {
        LogInfo("Skipping XInput hooks installation until display commander is initialized");
        return true;
    }

    // Initialize DualSense support if needed
    auto shared_state = display_commander::widgets::xinput_widget::XInputWidget::GetSharedState();
    LogInfo("[DUALSENSE] Initializing1 DualSense support");
    if (shared_state && shared_state->enable_dualsense_xinput.load()) {
        LogInfo("[DUALSENSE] Initializing2 DualSense support");
        display_commander::hooks::InitializeDualSenseSupport();
    }

    bool any_success = false;
    for (size_t idx = 0; idx < std::size(xinput_modules); ++idx) {
        const char *module_name = xinput_modules[idx];
        HMODULE current_module = GetModuleHandleA(module_name);
        if (current_module == nullptr) {
            any_success = true;
            break;
        }
    }
    if (!any_success) {
        LogInfo("No XInput modules found, skipping installation");
        return false;
    }

    //InitializeXInputDirectFunctions();

    static int min_set_value = -1;

    // Try to hook ALL loaded XInput modules
    for (size_t idx = 0; idx < std::size(xinput_modules); ++idx) {
        const char *module_name = xinput_modules[idx];
        HMODULE xinput_module = GetModuleHandleA(module_name);
        if (xinput_module == nullptr) {
            LogInfo("XInput module %s not found", module_name);
            continue;
        }
        if (hooked_modules[idx]) {
            LogInfo("XInput module %s already hooked", module_name);
            any_success = true;
            continue;
        }
        LogInfo("XInput module %s found", module_name);
        hooked_modules[idx] = true;

        bool update = min_set_value == -1 || min_set_value > idx;
        if (update) {
            min_set_value = idx;
        }

        // Hook XInputGetState
        FARPROC xinput_get_state_proc = GetProcAddress(xinput_module, "XInputGetState");
        if (xinput_get_state_proc != nullptr) {
            LogInfo("Found XInputGetState in %s at: 0x%p", module_name, xinput_get_state_proc);

            if (MH_CreateHook(xinput_get_state_proc, xinput_get_state_detours[idx], reinterpret_cast<LPVOID *>(&original_xinput_get_state_procs[idx])) ==
            MH_OK) {
                if (update) {
                    XInputGetState_Direct = original_xinput_get_state_procs[idx];
                }
                if (MH_EnableHook(xinput_get_state_proc) == MH_OK) {
                    LogInfo("Successfully hooked XInputGetState in %s", module_name);
                } else {
                    MH_RemoveHook(xinput_get_state_proc);
                }
            }
        }

        // Hook XInputGetStateEx (ordinal 100) - this is what many games actually use
        FARPROC xinput_get_state_ex_proc = GetProcAddress(xinput_module, (LPCSTR)100); // Ordinal 100
        if (xinput_get_state_ex_proc != nullptr) {
            LogInfo("Found XInputGetStateEx (ordinal 100) in %s at: 0x%p", module_name,
                    xinput_get_state_ex_proc);

            if (MH_CreateHook(xinput_get_state_ex_proc, xinput_get_state_ex_detours[idx],
                                reinterpret_cast<LPVOID *>(&original_xinput_get_state_ex_procs[idx])) == MH_OK) {
                if (MH_EnableHook(xinput_get_state_ex_proc) == MH_OK) {
                    if (update) {
                        XInputGetStateEx_Direct = original_xinput_get_state_ex_procs[idx];
                    }
                    LogInfo("Successfully hooked XInputGetStateEx (ordinal 100) in %s", module_name);
                } else {
                    MH_RemoveHook(xinput_get_state_ex_proc);
                }
            }
        }

        // Hook XInputSetState
        FARPROC xinput_set_state_proc = GetProcAddress(xinput_module, "XInputSetState");
        if (xinput_set_state_proc != nullptr) {
            LogInfo("Found XInputSetState in %s at: 0x%p", module_name, xinput_set_state_proc);

            if (MH_CreateHook(xinput_set_state_proc, xinput_set_state_detours[idx], reinterpret_cast<LPVOID *>(&original_xinput_set_state_procs[idx])) ==
            MH_OK) {
                if (update) {
                    XInputSetState_Direct = original_xinput_set_state_procs[idx];
                }
                if (MH_EnableHook(xinput_set_state_proc) == MH_OK) {
                    LogInfo("Successfully hooked XInputSetState in %s", module_name);
                } else {
                    MH_RemoveHook(xinput_set_state_proc);
                }
            }
        } else if (update) {
            // Fallback: get function pointer directly if hooking failed
            XInputSetState_Direct = (XInputSetState_pfn)GetProcAddress(xinput_module, "XInputSetState");
        }

        if (update) {
            if (XInputSetState_Direct == nullptr) {
                XInputSetState_Direct = (XInputSetState_pfn)GetProcAddress(xinput_module, "XInputSetState");
            }
            XInputGetBatteryInformation_Direct = (XInputGetBatteryInformation_pfn)GetProcAddress(xinput_module, "XInputGetBatteryInformation");
            XInputGetCapabilities_Direct = (XInputGetCapabilities_pfn)GetProcAddress(xinput_module, "XInputGetCapabilities");
        }

        // Hook XInputGetCapabilities
        FARPROC xinput_get_capabilities_proc = GetProcAddress(xinput_module, "XInputGetCapabilities");
        if (xinput_get_capabilities_proc != nullptr) {
            LogInfo("Found XInputGetCapabilities in %s at: 0x%p", module_name, xinput_get_capabilities_proc);

            if (MH_CreateHook(xinput_get_capabilities_proc, xinput_get_capabilities_detours[idx], reinterpret_cast<LPVOID *>(&original_xinput_get_capabilities_procs[idx])) == MH_OK) {
                if (update) {
                    XInputGetCapabilities_Direct = original_xinput_get_capabilities_procs[idx];
                }
                MH_STATUS enable_result = MH_EnableHook(xinput_get_capabilities_proc);
                if (enable_result == MH_OK) {
                    LogInfo("Successfully hooked XInputGetCapabilities in %s", module_name);
                } else {
                    LogError("Failed to enable XInputGetCapabilities hook in %s: %d", module_name, enable_result);
                    MH_RemoveHook(xinput_get_capabilities_proc);
                }
            } else {
                LogError("Failed to create XInputGetCapabilities hook in %s", module_name);
            }
        } else {
            LogInfo("XInputGetCapabilities not found in %s (may not be exported by name)", module_name);
            if (update) {
                // Fallback: try to get function pointer directly (might work if function exists but not exported by name)
                XInputGetCapabilities_Direct = (XInputGetCapabilities_pfn)GetProcAddress(xinput_module, "XInputGetCapabilities");
                if (XInputGetCapabilities_Direct == nullptr) {
                    LogInfo("XInputGetCapabilities function pointer also not available in %s", module_name);
                }
            }
        }

        any_success = true;
    }
    if (any_success) {
        g_xinput_hooks_installed.store(true);

        // Mark XInput hooks as installed
        display_commanderhooks::HookSuppressionManager::GetInstance().MarkHookInstalled(display_commanderhooks::HookType::XINPUT);
    }

    return any_success;
}

} // namespace display_commanderhooks
