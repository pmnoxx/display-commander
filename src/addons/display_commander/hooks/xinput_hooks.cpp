#include "xinput_hooks.hpp"
#include "../input_remapping/input_remapping.hpp"
#include "../utils.hpp"
#include "../widgets/xinput_widget/xinput_widget.hpp"
#include "windows_hooks/windows_message_hooks.hpp"
#include <MinHook.h>
#include <array>
#include <string>
#include <vector>


namespace display_commanderhooks {


// XInput function pointers for direct calls
XInputGetStateEx_pfn XInputGetStateEx_Direct = nullptr;
XInputSetState_pfn XInputSetState_Direct = nullptr;
XInputGetBatteryInformation_pfn XInputGetBatteryInformation_Direct = nullptr;

// Hook state
static std::atomic<bool> g_xinput_hooks_installed{false};

// Track which modules have been hooked
std::array<bool, 5> hooked_modules = {};
const char *xinput_modules[] = {
    "xinput1_4.dll", "xinput1_3.dll", "xinput1_2.dll", "xinput1_1.dll", "xinput9_1_0.dll",
};


// Initialize XInput function pointers for direct calls
static void InitializeXInputDirectFunctions() {
    if (XInputGetStateEx_Direct != nullptr && XInputSetState_Direct != nullptr && XInputGetBatteryInformation_Direct != nullptr) {
        return; // Already initialized
    }
    for (const char *module_name : xinput_modules) {
        HMODULE xinput_module = LoadLibraryA(module_name);
        if (xinput_module != nullptr) {
            LogInfo("Found XInput module: %s at 0x%p", module_name, xinput_module);
            //XInputGetStateEx_Direct = (XInputGetStateEx_pfn)GetProcAddress(xinput_module, (LPCSTR)100);
            XInputSetState_Direct = (XInputSetState_pfn)GetProcAddress(xinput_module, "XInputSetState");
            XInputGetBatteryInformation_Direct = (XInputGetBatteryInformation_pfn)GetProcAddress(xinput_module, "XInputGetBatteryInformation");
            break;
        } else {
            LogInfo("XInput module: %s not found", module_name);
        }
    }
}

// Previous state for change detection
static XINPUT_STATE g_previous_states[XUSER_MAX_COUNT] = {};
static bool g_previous_states_valid[XUSER_MAX_COUNT] = {};

// Track hooked modules and their function addresses
struct HookedXInputModule {
    HMODULE module;
    FARPROC function;
    std::string module_name;
};

// Helper function to apply max input, min output, and deadzone to thumbstick values
void ApplyThumbstickProcessing(XINPUT_STATE *pState, float left_max_input, float right_max_input, float left_min_output,
                               float right_min_output, float left_deadzone, float right_deadzone) {
    if (!pState)
        return;

    // Process left stick using unified function with correct scaling
    float lx = ShortToFloat(pState->Gamepad.sThumbLX);
    float ly = ShortToFloat(pState->Gamepad.sThumbLY);

    lx = ProcessStickInput(lx, left_deadzone, left_max_input, left_min_output);
    ly = ProcessStickInput(ly, left_deadzone, left_max_input, left_min_output);

    // Convert back to SHORT with correct scaling
    pState->Gamepad.sThumbLX = FloatToShort(lx);
    pState->Gamepad.sThumbLY = FloatToShort(ly);

    // Process right stick using unified function with correct scaling
    float rx = ShortToFloat(pState->Gamepad.sThumbRX);
    float ry = ShortToFloat(pState->Gamepad.sThumbRY);

    rx = ProcessStickInput(rx, right_deadzone, right_max_input, right_min_output);
    ry = ProcessStickInput(ry, right_deadzone, right_max_input, right_min_output);

    // Convert back to SHORT with correct scaling
    pState->Gamepad.sThumbRX = FloatToShort(rx);
    pState->Gamepad.sThumbRY = FloatToShort(ry);
}

// Helper function to detect changes in XInput state
void LogXInputChanges(DWORD dwUserIndex, const XINPUT_STATE *pState) {
    if (true) {
        return;
    }
    if (dwUserIndex >= XUSER_MAX_COUNT || pState == nullptr) {
        return;
    }

    // Check if we have a previous state to compare against
    if (g_previous_states_valid[dwUserIndex]) {
        const XINPUT_STATE &prev = g_previous_states[dwUserIndex];
        const XINPUT_STATE &curr = *pState;

        // Check for changes in gamepad data
        if (prev.Gamepad.wButtons != curr.Gamepad.wButtons) {
            LogError("XXX XInput Controller %lu: Button state changed from 0x%04X to 0x%04X", dwUserIndex,
                     prev.Gamepad.wButtons, curr.Gamepad.wButtons);

            // Check for Guide button specifically
            bool prev_guide = (prev.Gamepad.wButtons & XINPUT_GAMEPAD_GUIDE) != 0;
            bool curr_guide = (curr.Gamepad.wButtons & XINPUT_GAMEPAD_GUIDE) != 0;
            if (prev_guide != curr_guide) {
                LogInfo("XXX XInput Controller %lu: Guide button %s", dwUserIndex, curr_guide ? "PRESSED" : "RELEASED");
            }

            // Debug: Always log Guide button state for testing
            LogInfo("XXX XInput Controller %lu: Guide button state = %s (0x%04X)", dwUserIndex,
                    curr_guide ? "PRESSED" : "NOT PRESSED", curr.Gamepad.wButtons);
        }

        if (prev.Gamepad.bLeftTrigger != curr.Gamepad.bLeftTrigger) {
            LogError("XXX XInput Controller %lu: Left trigger changed from %u to %u", dwUserIndex,
                     prev.Gamepad.bLeftTrigger, curr.Gamepad.bLeftTrigger);
        }

        if (prev.Gamepad.bRightTrigger != curr.Gamepad.bRightTrigger) {
            LogError("XXX XInput Controller %lu: Right trigger changed from %u to %u", dwUserIndex,
                     prev.Gamepad.bRightTrigger, curr.Gamepad.bRightTrigger);
        }

        if (prev.Gamepad.sThumbLX != curr.Gamepad.sThumbLX) {
            LogError("XXX XInput Controller %lu: Left stick X changed from %d to %d", dwUserIndex,
                     prev.Gamepad.sThumbLX, curr.Gamepad.sThumbLX);
        }

        if (prev.Gamepad.sThumbLY != curr.Gamepad.sThumbLY) {
            LogError("XXX XInput Controller %lu: Left stick Y changed from %d to %d", dwUserIndex,
                     prev.Gamepad.sThumbLY, curr.Gamepad.sThumbLY);
        }

        if (prev.Gamepad.sThumbRX != curr.Gamepad.sThumbRX) {
            LogError("XXX XInput Controller %lu: Right stick X changed from %d to %d", dwUserIndex,
                     prev.Gamepad.sThumbRX, curr.Gamepad.sThumbRX);
        }

        if (prev.Gamepad.sThumbRY != curr.Gamepad.sThumbRY) {
            LogError("XXX XInput Controller %lu: Right stick Y changed from %d to %d", dwUserIndex,
                     prev.Gamepad.sThumbRY, curr.Gamepad.sThumbRY);
        }

        if (prev.dwPacketNumber != curr.dwPacketNumber) {
            LogError("XXX XInput Controller %lu: Packet number changed from %lu to %lu", dwUserIndex,
                     prev.dwPacketNumber, curr.dwPacketNumber);
        }
    }

    // Store current state for next comparison
    g_previous_states[dwUserIndex] = *pState;
    g_previous_states_valid[dwUserIndex] = true;
}

// Hooked XInputGetState function
DWORD WINAPI XInputGetState_Detour(DWORD dwUserIndex, XINPUT_STATE *pState) {
    if (pState == nullptr) {
        return ERROR_INVALID_PARAMETER;
    }
    LogInfo("XXX XInputGetState called for controller %lu", dwUserIndex);

    // Track hook call statistics
    g_hook_stats[HOOK_XInputGetState].increment_total();

    // Call original function - prefer XInputGetStateEx_Original for Guide button support
    DWORD result = XInputGetStateEx_Direct != nullptr ? XInputGetStateEx_Direct(dwUserIndex, pState) : ERROR_DEVICE_NOT_CONNECTED;

    // Apply A/B button swapping if enabled
    if (result == ERROR_SUCCESS) {
        auto shared_state = display_commander::widgets::xinput_widget::XInputWidget::GetSharedState();

        // Process chord detection first to check for input suppression
        display_commander::widgets::xinput_widget::ProcessChordDetection(dwUserIndex, pState->Gamepad.wButtons);

        // Store original state for UI tracking (before any modifications)
        XINPUT_STATE original_state = *pState;

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

            // Apply max input, min output, and deadzone processing
            if (shared_state) {
                float left_max_input = shared_state->left_stick_max_input.load();
                float right_max_input = shared_state->right_stick_max_input.load();
                float left_min_output = shared_state->left_stick_min_output.load();
                float right_min_output = shared_state->right_stick_min_output.load();
                float left_deadzone =
                    shared_state->left_stick_deadzone.load() / 100.0f; // Convert percentage to decimal
                float right_deadzone =
                    shared_state->right_stick_deadzone.load() / 100.0f; // Convert percentage to decimal

                ApplyThumbstickProcessing(pState, left_max_input, right_max_input, left_min_output, right_min_output,
                                          left_deadzone, right_deadzone);
            }

            // Process input remapping before updating state
            display_commander::input_remapping::process_gamepad_input_for_remapping(dwUserIndex, pState);

            // Track unsuppressed call (input was processed)
            g_hook_stats[HOOK_XInputGetState].increment_unsuppressed();
        }

        // Always update the UI state with the original state (before suppression/modifications)
        // This ensures the UI shows the actual controller state regardless of suppression
        display_commander::widgets::xinput_widget::UpdateXInputState(dwUserIndex, &original_state);

        // Update battery status periodically
        display_commander::widgets::xinput_widget::UpdateBatteryStatus(dwUserIndex);

        LogXInputChanges(dwUserIndex, &original_state);
    } else {
        // Mark controller as disconnected in shared state
        if (dwUserIndex < XUSER_MAX_COUNT) {
            auto shared_state = display_commander::widgets::xinput_widget::XInputWidget::GetSharedState();
            if (shared_state) {
                shared_state->controller_connected[dwUserIndex] = false;
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
    LogInfo("XXX XInputGetStateEx called for controller %lu", dwUserIndex);

    // Track hook call statistics
    g_hook_stats[HOOK_XInputGetStateEx].increment_total();

    // Call original function - always use XInputGetStateEx_Original if available
    DWORD result = XInputGetStateEx_Direct != nullptr ? XInputGetStateEx_Direct(dwUserIndex, pState) : ERROR_DEVICE_NOT_CONNECTED;

    // Apply A/B button swapping if enabled
    if (result == ERROR_SUCCESS) {
        auto shared_state = display_commander::widgets::xinput_widget::XInputWidget::GetSharedState();

        // Process chord detection first to check for input suppression
        display_commander::widgets::xinput_widget::ProcessChordDetection(dwUserIndex, pState->Gamepad.wButtons);

        // Store original state for UI tracking (before any modifications)
        XINPUT_STATE original_state = *pState;

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

            // Apply max input, min output, and deadzone processing
            if (shared_state) {
                float left_max_input = shared_state->left_stick_max_input.load();
                float right_max_input = shared_state->right_stick_max_input.load();
                float left_min_output = shared_state->left_stick_min_output.load();
                float right_min_output = shared_state->right_stick_min_output.load();
                float left_deadzone =
                    shared_state->left_stick_deadzone.load() / 100.0f; // Convert percentage to decimal
                float right_deadzone =
                    shared_state->right_stick_deadzone.load() / 100.0f; // Convert percentage to decimal

                ApplyThumbstickProcessing(pState, left_max_input, right_max_input, left_min_output, right_min_output,
                                          left_deadzone, right_deadzone);
            }

            // Process input remapping before updating state
            display_commander::input_remapping::process_gamepad_input_for_remapping(dwUserIndex, pState);

            // Track unsuppressed call (input was processed)
            g_hook_stats[HOOK_XInputGetStateEx].increment_unsuppressed();
        }

        // Always update the UI state with the original state (before suppression/modifications)
        // This ensures the UI shows the actual controller state regardless of suppression
        display_commander::widgets::xinput_widget::UpdateXInputState(dwUserIndex, &original_state);

        // Update battery status periodically
        display_commander::widgets::xinput_widget::UpdateBatteryStatus(dwUserIndex);

        LogXInputChanges(dwUserIndex, &original_state);
    } else {
        // Mark controller as disconnected in shared state
        if (dwUserIndex < XUSER_MAX_COUNT) {
            auto shared_state = display_commander::widgets::xinput_widget::XInputWidget::GetSharedState();
            if (shared_state) {
                shared_state->controller_connected[dwUserIndex] = false;
            }
        }
        LogError("XXX XInput Controller %lu: GetStateEx failed with error %lu", dwUserIndex, result);
    }

    return result;
}

bool InstallXInputHooks() {
    // Initialize direct function pointers first
    InitializeXInputDirectFunctions();

    bool any_success = false;

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

        // Hook XInputGetState
        FARPROC xinput_get_state_proc = GetProcAddress(xinput_module, "XInputGetState");
        if (xinput_get_state_proc != nullptr) {
            LogInfo("Found XInputGetState in %s at: 0x%p", module_name, xinput_get_state_proc);

            LPVOID *original_fn_ptr = nullptr;
            if (MH_CreateHook(xinput_get_state_proc, XInputGetState_Detour, (LPVOID *)&original_fn_ptr) ==
                MH_OK) {
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

            LPVOID *original_xinput_get_state_ex_proc = nullptr;
            if (MH_CreateHook(xinput_get_state_ex_proc, XInputGetStateEx_Detour,
                                (LPVOID *)&original_xinput_get_state_ex_proc) == MH_OK) {
                if (MH_EnableHook(xinput_get_state_ex_proc) == MH_OK) {
                    if (XInputGetStateEx_Direct == nullptr) {
                        XInputGetStateEx_Direct = (XInputGetStateEx_pfn)original_xinput_get_state_ex_proc;
                    }
                    LogInfo("Successfully hooked XInputGetStateEx (ordinal 100) in %s", module_name);
                } else {
                    MH_RemoveHook(xinput_get_state_ex_proc);
                }
            }
        }
        any_success = true;
    }
    if (any_success) {
        g_xinput_hooks_installed.store(true);
    }

    return any_success;
}

bool AreXInputHooksInstalled() { return g_xinput_hooks_installed.load(); }

} // namespace display_commanderhooks
