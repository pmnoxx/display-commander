#include "xinput_hooks.hpp"
#include "hook_suppression_manager.hpp"
#include "../utils/logging.hpp"
#include "../settings/developer_tab_settings.hpp"
#include "dualsense_hooks.hpp"
#include "../input_remapping/input_remapping.hpp"
#include "../utils/general_utils.hpp"
#include "../utils/timing.hpp"
#include "../widgets/xinput_widget/xinput_widget.hpp"
#include "../swapchain_events.hpp"
#include "windows_hooks/windows_message_hooks.hpp"
#include "../globals.hpp"
#include <MinHook.h>
#include <array>
#include <string>
#include <vector>
#include <cmath>


namespace display_commanderhooks {


// XInput function pointers for direct calls
XInputGetState_pfn XInputGetState_Direct = nullptr;
XInputGetStateEx_pfn XInputGetStateEx_Direct = nullptr;
XInputSetState_pfn XInputSetState_Direct = nullptr;
XInputGetBatteryInformation_pfn XInputGetBatteryInformation_Direct = nullptr;

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
                               float left_center_x, float left_center_y, float right_center_x, float right_center_y) {
    if (!pState)
        return;

    // Process left stick using radial processing (preserves direction)
    float lx = ShortToFloat(pState->Gamepad.sThumbLX);
    float ly = ShortToFloat(pState->Gamepad.sThumbLY);

    // Apply center calibration (recenter the stick)
    lx = recenter(lx, left_center_x);
    ly = recenter(ly, left_center_y);

    ProcessStickInputRadial(lx, ly, left_deadzone, left_max_input, left_min_output);

    // Convert back to SHORT with correct scaling
    pState->Gamepad.sThumbLX = FloatToShort(lx);
    pState->Gamepad.sThumbLY = FloatToShort(ly);

    // Process right stick using radial processing (preserves direction)
    float rx = ShortToFloat(pState->Gamepad.sThumbRX);
    float ry = ShortToFloat(pState->Gamepad.sThumbRY);

    // Apply center calibration (recenter the stick)
    rx = recenter(rx, right_center_x);
    ry = recenter(ry, right_center_y);

    ProcessStickInputRadial(rx, ry, right_deadzone, right_max_input, right_min_output);

    // Convert back to SHORT with correct scaling
    pState->Gamepad.sThumbRX = FloatToShort(rx);
    pState->Gamepad.sThumbRY = FloatToShort(ry);
}

// Hooked XInputGetState function
DWORD WINAPI XInputGetState_Detour(DWORD dwUserIndex, XINPUT_STATE *pState) {
    if (pState == nullptr) {
        return ERROR_INVALID_PARAMETER;
    }
    if (XInputGetStateEx_Direct == nullptr || XInputGetState_Direct == nullptr) {
        return ERROR_DEVICE_NOT_CONNECTED;
    }
  //  LogInfo("XXX XInputGetState called for controller %lu", dwUserIndex);

    // Track hook call statistics
    g_hook_stats[HOOK_XInputGetState].increment_total();

    // Measure timing for smooth call rate calculation
    auto shared_state = display_commander::widgets::xinput_widget::XInputWidget::GetSharedState();
    if (shared_state && dwUserIndex == 0) {
        uint64_t current_time_ns = utils::get_now_ns();
        uint64_t last_call_time = shared_state->last_xinput_call_time_ns.load();

        if (last_call_time > 0) {
            uint64_t time_since_last_call_ns = current_time_ns - last_call_time;
            // Only update if time since last call is reasonable (ignore if > 1000ms)
            if (time_since_last_call_ns < 1 * utils::SEC_TO_NS) { // 1 second in nanoseconds
                uint64_t old_update_ns = shared_state->xinput_getstate_update_ns.load();
                uint64_t new_update_ns = UpdateRollingAverage(time_since_last_call_ns, old_update_ns);
                shared_state->xinput_getstate_update_ns.store(new_update_ns);
            }
        }
        shared_state->last_xinput_call_time_ns.store(current_time_ns);
    }
    static bool tried_get_state_ex = false;
    static bool use_get_state_ex = false;

    // Check if override state is set - if so, spoof controller connection for controller 0
    bool should_spoof_connection = false;
    if (shared_state && dwUserIndex == 0) {
        float override_ly = shared_state->override_state.left_stick_y.load();
        WORD override_buttons = shared_state->override_state.buttons_pressed_mask.load();
        // If any override is active, we need to spoof connection
        if (!std::isinf(override_ly) || override_buttons != 0 ||
            !std::isinf(shared_state->override_state.left_stick_x.load()) ||
            !std::isinf(shared_state->override_state.right_stick_x.load()) ||
            !std::isinf(shared_state->override_state.right_stick_y.load())) {
            should_spoof_connection = true;
        }
    }

    // Check if DualSense to XInput conversion is enabled
    bool dualsense_enabled = shared_state && shared_state->enable_dualsense_xinput.load();

    DWORD result = ERROR_DEVICE_NOT_CONNECTED;

    // Try DualSense conversion first if enabled
    if (dualsense_enabled && display_commander::hooks::IsDualSenseAvailable()) {
        if (display_commander::hooks::ConvertDualSenseToXInput(dwUserIndex, pState)) {
            result = ERROR_SUCCESS;
        }
    }

    // Fall back to original XInput if DualSense conversion failed or is disabled
    if (result != ERROR_SUCCESS) {
        result = use_get_state_ex ? XInputGetStateEx_Direct(dwUserIndex, pState) : XInputGetState_Direct(dwUserIndex, pState);
        if (result == ERROR_SUCCESS) {
            if (!tried_get_state_ex) {
                tried_get_state_ex = true;
                use_get_state_ex = XInputGetStateEx_Direct(dwUserIndex, pState) == ERROR_SUCCESS;
            }
        }
    }

    // If controller is not connected but we need to spoof for override, create fake state
    if (result != ERROR_SUCCESS && should_spoof_connection) {
        // Initialize fake gamepad state (all zeros)
        ZeroMemory(&pState->Gamepad, sizeof(XINPUT_GAMEPAD));
        pState->dwPacketNumber = 0;
        result = ERROR_SUCCESS; // Spoof as connected

        // Mark controller as connected in shared state
        if (shared_state && dwUserIndex < XUSER_MAX_COUNT) {
            shared_state->controller_connected[dwUserIndex] = display_commander::widgets::xinput_widget::ControllerState::Connected;
        }

        LogInfo("XXX Spoofing controller 0 connection for override state");
    }

    // Apply A/B button swapping if enabled
    if (result == ERROR_SUCCESS) {
        // Store the frame ID when XInput is successfully detected
        uint64_t current_frame_id = g_global_frame_id.load();
        g_last_xinput_detected_frame_id.store(current_frame_id);

        auto shared_state = display_commander::widgets::xinput_widget::XInputWidget::GetSharedState();

        // Process chord detection first to check for input suppression
        display_commander::widgets::xinput_widget::ProcessChordDetection(dwUserIndex, pState->Gamepad.wButtons);

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

        // Check if input should be suppressed due to chord being pressed
        if (shared_state && shared_state->suppress_input.load()) {
            // Suppress all input by zeroing out the gamepad state
            ZeroMemory(&pState->Gamepad, sizeof(XINPUT_GAMEPAD));
            LogInfo("XXX Input suppressed due to chord being pressed (Controller %lu)", dwUserIndex);
            // Don't increment unsuppressed - input was suppressed
        } else {
            // Input is not suppressed, process normally
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

                ApplyThumbstickProcessing(pState, left_max_input, right_max_input, left_min_output, right_min_output,
                                          left_deadzone, right_deadzone, left_center_x, left_center_y, right_center_x, right_center_y);
            }

            // Process input remapping before updating state
            display_commander::input_remapping::process_gamepad_input_for_remapping(dwUserIndex, pState);

            // Process autofire
            display_commander::widgets::xinput_widget::ProcessAutofire(dwUserIndex, pState);

            // Track unsuppressed call (input was processed)
            g_hook_stats[HOOK_XInputGetState].increment_unsuppressed();
        }

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
            LogError("XXX XInput Controller %lu: GetState failed with error %lu", dwUserIndex, result);
        }
    }

    return result;
}

// Hooked XInputGetStateEx function
DWORD WINAPI XInputGetStateEx_Detour(DWORD dwUserIndex, XINPUT_STATE *pState) {
    if (pState == nullptr) {
        return ERROR_INVALID_PARAMETER;
    }
   // LogInfo("XXX XInputGetStateEx called for controller %lu", dwUserIndex);

    // Track hook call statistics
    g_hook_stats[HOOK_XInputGetStateEx].increment_total();

    // Measure timing for smooth call rate calculation
    auto shared_state = display_commander::widgets::xinput_widget::XInputWidget::GetSharedState();
    if (shared_state && dwUserIndex == 0) {
        uint64_t current_time_ns = utils::get_now_ns();
        uint64_t last_call_time = shared_state->last_xinput_call_time_ns.load();

        if (last_call_time > 0) {
            uint64_t time_since_last_call_ns = current_time_ns - last_call_time;
            // Only update if time since last call is reasonable (ignore if > 1000ms)
            if (time_since_last_call_ns < 1000000000ULL) { // 1000ms in nanoseconds
                uint64_t old_update_ns = shared_state->xinput_getstateex_update_ns.load();
                uint64_t new_update_ns = UpdateRollingAverage(time_since_last_call_ns, old_update_ns);
                shared_state->xinput_getstateex_update_ns.store(new_update_ns);
            }
        }
        shared_state->last_xinput_call_time_ns.store(current_time_ns);
    }

    // Check if override state is set - if so, spoof controller connection for controller 0
    bool should_spoof_connection = false;
    if (shared_state && dwUserIndex == 0) {
        float override_ly = shared_state->override_state.left_stick_y.load();
        WORD override_buttons = shared_state->override_state.buttons_pressed_mask.load();
        // If any override is active, we need to spoof connection
        if (!std::isinf(override_ly) || override_buttons != 0 ||
            !std::isinf(shared_state->override_state.left_stick_x.load()) ||
            !std::isinf(shared_state->override_state.right_stick_x.load()) ||
            !std::isinf(shared_state->override_state.right_stick_y.load())) {
            should_spoof_connection = true;
        }
    }

    // Check if DualSense to XInput conversion is enabled
    bool dualsense_enabled = shared_state && shared_state->enable_dualsense_xinput.load();

    DWORD result = ERROR_DEVICE_NOT_CONNECTED;

    // Try DualSense conversion first if enabled
    if (dualsense_enabled && display_commander::hooks::IsDualSenseAvailable()) {
        if (display_commander::hooks::ConvertDualSenseToXInput(dwUserIndex, pState)) {
            result = ERROR_SUCCESS;
        }
    }

    // Fall back to original XInput if DualSense conversion failed or is disabled
    if (result != ERROR_SUCCESS) {
        result = XInputGetStateEx_Direct != nullptr ? XInputGetStateEx_Direct(dwUserIndex, pState) : ERROR_DEVICE_NOT_CONNECTED;
    }

    // If controller is not connected but we need to spoof for override, create fake state
    if (result != ERROR_SUCCESS && should_spoof_connection) {
        // Initialize fake gamepad state (all zeros)
        ZeroMemory(&pState->Gamepad, sizeof(XINPUT_GAMEPAD));
        pState->dwPacketNumber = 0;
        result = ERROR_SUCCESS; // Spoof as connected

        // Mark controller as connected in shared state
        if (shared_state && dwUserIndex < XUSER_MAX_COUNT) {
            shared_state->controller_connected[dwUserIndex] = display_commander::widgets::xinput_widget::ControllerState::Connected;
        }

        LogInfo("XXX Spoofing controller 0 connection for override state");
    }

    // Apply A/B button swapping if enabled
    if (result == ERROR_SUCCESS) {
        // Store the frame ID when XInput is successfully detected
        uint64_t current_frame_id = g_global_frame_id.load();
        g_last_xinput_detected_frame_id.store(current_frame_id);

        auto shared_state = display_commander::widgets::xinput_widget::XInputWidget::GetSharedState();

        // Process chord detection first to check for input suppression
        display_commander::widgets::xinput_widget::ProcessChordDetection(dwUserIndex, pState->Gamepad.wButtons);

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

        // Check if input should be suppressed due to chord being pressed
        if (shared_state && shared_state->suppress_input.load()) {
            // Suppress all input by zeroing out the gamepad state
            ZeroMemory(&pState->Gamepad, sizeof(XINPUT_GAMEPAD));
            LogInfo("XXX Input suppressed due to chord being pressed (Controller %lu)", dwUserIndex);
            // Don't increment unsuppressed - input was suppressed
        } else {
            // Input is not suppressed, process normally
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

                ApplyThumbstickProcessing(pState, left_max_input, right_max_input, left_min_output, right_min_output,
                                          left_deadzone, right_deadzone, left_center_x, left_center_y, right_center_x, right_center_y);
            }

            // Process input remapping before updating state
            display_commander::input_remapping::process_gamepad_input_for_remapping(dwUserIndex, pState);

            // Process autofire
            display_commander::widgets::xinput_widget::ProcessAutofire(dwUserIndex, pState);

            // Track unsuppressed call (input was processed)
            g_hook_stats[HOOK_XInputGetStateEx].increment_unsuppressed();
        }

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
        LogError("XXX XInput Controller %lu: GetStateEx failed with error %lu", dwUserIndex, result);
    }

    return result;
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

bool InstallXInputHooks() {
    if (!settings::g_developerTabSettings.load_xinput.GetValue()) {
        LogInfo("XInput hooks not installed - load_xinput is disabled");
        return false;
    }

    // Check if XInput hooks should be suppressed
    if (display_commanderhooks::HookSuppressionManager::GetInstance().ShouldSuppressHook(display_commanderhooks::HookType::XINPUT)) {
        LogInfo("XInput hooks installation suppressed by user setting");
        return false;
    }

    if (!g_initialized_with_hwnd.load()) {
        LogInfo("Skipping XInput hooks installation until display commander is initialized");
        return true;
    }
    // Check if XInput hooks are enabled
    auto shared_state = display_commander::widgets::xinput_widget::XInputWidget::GetSharedState();
    if (shared_state && !shared_state->enable_xinput_hooks.load()) {
        LogInfo("XInput hooks are disabled, skipping installation");
        return true;
    }

    // Initialize DualSense support if needed
    LogInfo("[DUALSENSE] Initializing1 DualSense support");
    if (shared_state && shared_state->enable_dualsense_xinput.load()) {
        LogInfo("[DUALSENSE] Initializing2 DualSense support");
        display_commander::hooks::InitializeDualSenseSupport();
    }

    bool any_success = false;
    for (size_t idx = 0; idx < std::size(xinput_modules); ++idx) {
        const char *module_name = xinput_modules[idx];
        HMODULE xinput_module = GetModuleHandleA(module_name);
        if (xinput_module == nullptr) {
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

            if (MH_CreateHook(xinput_get_state_proc, XInputGetState_Detour, reinterpret_cast<LPVOID *>(&original_xinput_get_state_procs[idx])) ==
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

            if (MH_CreateHook(xinput_get_state_ex_proc, XInputGetStateEx_Detour,
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

            if (MH_CreateHook(xinput_set_state_proc, XInputSetState_Detour, reinterpret_cast<LPVOID *>(&original_xinput_set_state_procs[idx])) ==
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
