#include "xinput_hooks.hpp"
#include "../utils.hpp"
#include "../widgets/xinput_widget/xinput_widget.hpp"
#include "../input_remapping/input_remapping.hpp"
#include "windows_hooks/windows_message_hooks.hpp"
#include <MinHook.h>
#include <vector>
#include <string>

// Guide button constant (not defined in standard XInput headers)
#ifndef XINPUT_GAMEPAD_GUIDE
#define XINPUT_GAMEPAD_GUIDE 0x0400
#endif

namespace renodx::hooks {

// Original function pointers
XInputGetState_pfn XInputGetState_Original = nullptr;
XInputGetStateEx_pfn XInputGetStateEx_Original = nullptr;

// Hook state
static std::atomic<bool> g_xinput_hooks_installed{false};

// Previous state for change detection
static XINPUT_STATE g_previous_states[XUSER_MAX_COUNT] = {};
static bool g_previous_states_valid[XUSER_MAX_COUNT] = {};

// Track hooked modules and their function addresses
struct HookedXInputModule {
    HMODULE module;
    FARPROC function;
    std::string module_name;
};
static std::vector<HookedXInputModule> g_hooked_modules;

// Helper function to apply sensitivity and deadzone to thumbstick values
void ApplyThumbstickProcessing(XINPUT_STATE* pState, float left_sensitivity, float right_sensitivity,
                              float left_deadzone, float right_deadzone) {
    if (!pState) return;

    // Process left stick
    float lx = static_cast<float>(pState->Gamepad.sThumbLX) / 32767.0f;
    float ly = static_cast<float>(pState->Gamepad.sThumbLY) / 32767.0f;

    // Apply deadzone processing
    lx = ApplyDeadzone(lx, left_deadzone);
    ly = ApplyDeadzone(ly, left_deadzone);

    // Apply sensitivity
    lx *= left_sensitivity;
    ly *= left_sensitivity;

    // Clamp to valid range and convert back to SHORT
    lx = (std::max)(-1.0f, (std::min)(1.0f, lx));
    ly = (std::max)(-1.0f, (std::min)(1.0f, ly));
    pState->Gamepad.sThumbLX = static_cast<SHORT>(lx * 32767.0f);
    pState->Gamepad.sThumbLY = static_cast<SHORT>(ly * 32767.0f);

    // Process right stick
    float rx = static_cast<float>(pState->Gamepad.sThumbRX) / 32767.0f;
    float ry = static_cast<float>(pState->Gamepad.sThumbRY) / 32767.0f;

    // Apply deadzone processing
    rx = ApplyDeadzone(rx, right_deadzone);
    ry = ApplyDeadzone(ry, right_deadzone);

    // Apply sensitivity
    rx *= right_sensitivity;
    ry *= right_sensitivity;

    // Clamp to valid range and convert back to SHORT
    rx = (std::max)(-1.0f, (std::min)(1.0f, rx));
    ry = (std::max)(-1.0f, (std::min)(1.0f, ry));
    pState->Gamepad.sThumbRX = static_cast<SHORT>(rx * 32767.0f);
    pState->Gamepad.sThumbRY = static_cast<SHORT>(ry * 32767.0f);
}

// Helper function to apply deadzone processing (supports both deadzone and anti-deadzone)
float ApplyDeadzone(float value, float deadzone) {
    if (deadzone == 0.0f) {
        return value; // No deadzone applied
    }

    float abs_value = std::abs(value);
    float sign = (value >= 0.0f) ? 1.0f : -1.0f;

    if (deadzone > 0.0f) {
        // Positive deadzone: traditional deadzone behavior
        if (abs_value < deadzone) {
            return 0.0f;
        }
        // Scale the value to remove the deadzone
        float scaled = (abs_value - deadzone) / (1.0f - deadzone);
        return sign * scaled;
    } else {
        // Negative deadzone: anti-deadzone behavior
        float anti_deadzone = -deadzone; // Convert to positive value
        if (abs_value >= anti_deadzone) {
            float mapped = (abs_value - anti_deadzone) / (1.0f - anti_deadzone);
            return sign * mapped;
        } else {
            // Values below anti_deadzone are scaled down proportionally
            return sign * (abs_value / anti_deadzone) * anti_deadzone;
        }
    }
}

// Helper function to detect changes in XInput state
void LogXInputChanges(DWORD dwUserIndex, const XINPUT_STATE* pState) {
    if (dwUserIndex >= XUSER_MAX_COUNT || pState == nullptr) {
        return;
    }

    // Check if we have a previous state to compare against
    if (g_previous_states_valid[dwUserIndex]) {
        const XINPUT_STATE& prev = g_previous_states[dwUserIndex];
        const XINPUT_STATE& curr = *pState;

        // Check for changes in gamepad data
        if (prev.Gamepad.wButtons != curr.Gamepad.wButtons) {
            LogError("XXX XInput Controller %lu: Button state changed from 0x%04X to 0x%04X",
                     dwUserIndex, prev.Gamepad.wButtons, curr.Gamepad.wButtons);

        // Check for Guide button specifically
        bool prev_guide = (prev.Gamepad.wButtons & XINPUT_GAMEPAD_GUIDE) != 0;
        bool curr_guide = (curr.Gamepad.wButtons & XINPUT_GAMEPAD_GUIDE) != 0;
        if (prev_guide != curr_guide) {
            LogInfo("XXX XInput Controller %lu: Guide button %s", dwUserIndex, curr_guide ? "PRESSED" : "RELEASED");
        }

        // Debug: Always log Guide button state for testing
        LogInfo("XXX XInput Controller %lu: Guide button state = %s (0x%04X)",
                dwUserIndex, curr_guide ? "PRESSED" : "NOT PRESSED", curr.Gamepad.wButtons);
        }

        if (prev.Gamepad.bLeftTrigger != curr.Gamepad.bLeftTrigger) {
            LogError("XXX XInput Controller %lu: Left trigger changed from %u to %u",
                     dwUserIndex, prev.Gamepad.bLeftTrigger, curr.Gamepad.bLeftTrigger);
        }

        if (prev.Gamepad.bRightTrigger != curr.Gamepad.bRightTrigger) {
            LogError("XXX XInput Controller %lu: Right trigger changed from %u to %u",
                     dwUserIndex, prev.Gamepad.bRightTrigger, curr.Gamepad.bRightTrigger);
        }

        if (prev.Gamepad.sThumbLX != curr.Gamepad.sThumbLX) {
            LogError("XXX XInput Controller %lu: Left stick X changed from %d to %d",
                     dwUserIndex, prev.Gamepad.sThumbLX, curr.Gamepad.sThumbLX);
        }

        if (prev.Gamepad.sThumbLY != curr.Gamepad.sThumbLY) {
            LogError("XXX XInput Controller %lu: Left stick Y changed from %d to %d",
                     dwUserIndex, prev.Gamepad.sThumbLY, curr.Gamepad.sThumbLY);
        }

        if (prev.Gamepad.sThumbRX != curr.Gamepad.sThumbRX) {
            LogError("XXX XInput Controller %lu: Right stick X changed from %d to %d",
                     dwUserIndex, prev.Gamepad.sThumbRX, curr.Gamepad.sThumbRX);
        }

        if (prev.Gamepad.sThumbRY != curr.Gamepad.sThumbRY) {
            LogError("XXX XInput Controller %lu: Right stick Y changed from %d to %d",
                     dwUserIndex, prev.Gamepad.sThumbRY, curr.Gamepad.sThumbRY);
        }

        if (prev.dwPacketNumber != curr.dwPacketNumber) {
            LogError("XXX XInput Controller %lu: Packet number changed from %lu to %lu",
                     dwUserIndex, prev.dwPacketNumber, curr.dwPacketNumber);
        }
    }

    // Store current state for next comparison
    g_previous_states[dwUserIndex] = *pState;
    g_previous_states_valid[dwUserIndex] = true;
}

// Hooked XInputGetState function
DWORD WINAPI XInputGetState_Detour(DWORD dwUserIndex, XINPUT_STATE* pState) {
    if (pState == nullptr) {
        return ERROR_INVALID_PARAMETER;
    }

    // Track hook call statistics
    g_hook_stats[HOOK_XInputGetState].increment_total();

    // Call original function - prefer XInputGetStateEx_Original for Guide button support
    DWORD result = XInputGetStateEx_Original ?
        XInputGetStateEx_Original(dwUserIndex, pState) :
        (XInputGetState_Original ? XInputGetState_Original(dwUserIndex, pState) : XInputGetState(dwUserIndex, pState));

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

            // Apply sensitivity and deadzone processing
            if (shared_state) {
                float left_sensitivity = shared_state->left_stick_sensitivity.load();
                float right_sensitivity = shared_state->right_stick_sensitivity.load();
                float left_deadzone = shared_state->left_stick_deadzone.load() / 100.0f; // Convert percentage to decimal
                float right_deadzone = shared_state->right_stick_deadzone.load() / 100.0f; // Convert percentage to decimal

                ApplyThumbstickProcessing(pState, left_sensitivity, right_sensitivity,
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
        LogError("XXX XInput Controller %lu: GetState failed with error %lu", dwUserIndex, result);
    }

    return result;
}

// Hooked XInputGetStateEx function
DWORD WINAPI XInputGetStateEx_Detour(DWORD dwUserIndex, XINPUT_STATE* pState) {
    if (pState == nullptr) {
        return ERROR_INVALID_PARAMETER;
    }

    // Track hook call statistics
    g_hook_stats[HOOK_XInputGetStateEx].increment_total();

    // Call original function - always use XInputGetStateEx_Original if available
    DWORD result = XInputGetStateEx_Original ?
        XInputGetStateEx_Original(dwUserIndex, pState) :
        XInputGetState(dwUserIndex, pState); // Fallback to regular XInputGetState

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

            // Apply sensitivity and deadzone processing
            if (shared_state) {
                float left_sensitivity = shared_state->left_stick_sensitivity.load();
                float right_sensitivity = shared_state->right_stick_sensitivity.load();
                float left_deadzone = shared_state->left_stick_deadzone.load() / 100.0f; // Convert percentage to decimal
                float right_deadzone = shared_state->right_stick_deadzone.load() / 100.0f; // Convert percentage to decimal

                ApplyThumbstickProcessing(pState, left_sensitivity, right_sensitivity,
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
    if (g_xinput_hooks_installed.load()) {
        LogInfo("XInput hooks already installed");
        return true;
    }

    // MinHook is already initialized by API hooks, so we don't need to initialize it again

    // List of all possible XInput module names to check
    const char* xinput_modules[] = {
        "xinput9_1_0.dll",
        "xinput1_4.dll",
        "xinput1_3.dll",
        "xinput1_2.dll",
        "xinput1_1.dll",
    };

    int hooked_count = 0;
    bool any_success = false;

    // Try to hook ALL loaded XInput modules
    for (const char* module_name : xinput_modules) {
        HMODULE xinput_module = GetModuleHandleA(module_name);
        if (xinput_module) {
            LogInfo("XXX Found XInput module: %s at 0x%p", module_name, xinput_module);

            // Try to hook multiple XInput functions from this module
            bool module_hooked = false;

            // Hook XInputGetState
            FARPROC xinput_get_state_proc = GetProcAddress(xinput_module, "XInputGetState");
            if (xinput_get_state_proc) {
                LogInfo("XXX Found XInputGetState in %s at: 0x%p", module_name, xinput_get_state_proc);

                if (MH_CreateHook(xinput_get_state_proc, XInputGetState_Detour, (LPVOID*)&XInputGetState_Original) == MH_OK) {
                    if (MH_EnableHook(xinput_get_state_proc) == MH_OK) {
                        LogInfo("XXX Successfully hooked XInputGetState in %s", module_name);
                        module_hooked = true;
                    } else {
                        LogError("XXX Failed to enable XInputGetState hook in %s", module_name);
                        MH_RemoveHook(xinput_get_state_proc);
                    }
                } else {
                    LogError("XXX Failed to create XInputGetState hook in %s", module_name);
                }
            }
            if (module_hooked) {
                // Store the hooked module info
                HookedXInputModule hooked_module;
                hooked_module.module = xinput_module;
                hooked_module.function = xinput_get_state_proc;
                hooked_module.module_name = module_name;
                g_hooked_modules.push_back(hooked_module);

                hooked_count++;
                any_success = true;
            }
            module_hooked = false;

            // Hook XInputGetStateEx (ordinal 100) - this is what many games actually use
            FARPROC xinput_get_state_ex_proc = GetProcAddress(xinput_module, (LPCSTR)100); // Ordinal 100
            if (xinput_get_state_ex_proc) {
                LogInfo("XXX Found XInputGetStateEx (ordinal 100) in %s at: 0x%p", module_name, xinput_get_state_ex_proc);

                if (MH_CreateHook(xinput_get_state_ex_proc, XInputGetStateEx_Detour, (LPVOID*)&XInputGetStateEx_Original) == MH_OK) {
                    if (MH_EnableHook(xinput_get_state_ex_proc) == MH_OK) {
                        LogInfo("XXX Successfully hooked XInputGetStateEx in %s", module_name);
                        module_hooked = true;
                    } else {
                        LogError("XXX Failed to enable XInputGetStateEx hook in %s", module_name);
                        MH_RemoveHook(xinput_get_state_ex_proc);
                    }
                } else {
                    LogError("XXX Failed to create XInputGetStateEx hook in %s", module_name);
                }
            } else {
                LogWarn("XXX XInputGetStateEx (ordinal 100) not found in %s", module_name);
            }

            if (module_hooked) {
                // Store the hooked module info
                HookedXInputModule hooked_module;
                hooked_module.module = xinput_module;
                hooked_module.function = xinput_get_state_ex_proc;
                hooked_module.module_name = module_name;
                g_hooked_modules.push_back(hooked_module);

                hooked_count++;
                any_success = true;
            }
        }
    }

    // If no modules were found, try to hook the global XInput functions as fallback
    if (!any_success) {
        LogWarn("XXX No XInput modules found - attempting to hook global XInput functions as fallback");

        // Try global XInputGetState
        if (MH_CreateHook(XInputGetState, XInputGetState_Detour, (LPVOID*)&XInputGetState_Original) == MH_OK) {
            if (MH_EnableHook(XInputGetState) == MH_OK) {
                LogInfo("XXX Successfully hooked global XInputGetState");
                any_success = true;
                hooked_count++;
            } else {
                LogError("XXX Failed to enable global XInputGetState hook");
                MH_RemoveHook(XInputGetState);
            }
        } else {
            LogError("XXX Failed to create global XInputGetState hook");
        }

        // Note: We can't easily hook global XInputGetStateEx since it's an ordinal
        // The module-based approach above should catch it
    }

    if (any_success) {
        g_xinput_hooks_installed.store(true);
        LogInfo("XXX XInput hooks installed successfully - hooked %d XInput modules", hooked_count);
        return true;
    } else {
        LogError("XXX Failed to hook any XInput modules");
        return false;
    }
}

void UninstallXInputHooks() {
    if (!g_xinput_hooks_installed.load()) {
        LogInfo("XInput hooks not installed");
        return;
    }

    // Unhook all tracked modules
    for (const auto& hooked_module : g_hooked_modules) {
        LogInfo("XXX Unhooking XInputGetState from %s", hooked_module.module_name.c_str());
        MH_DisableHook(hooked_module.function);
        MH_RemoveHook(hooked_module.function);
    }

    // Also try to unhook global XInputGetState as fallback
    MH_DisableHook(XInputGetState);
    MH_RemoveHook(XInputGetState);

    // Clean up
    XInputGetState_Original = nullptr;
    XInputGetStateEx_Original = nullptr;
    g_hooked_modules.clear();

    // Reset state tracking
    for (int i = 0; i < XUSER_MAX_COUNT; i++) {
        g_previous_states_valid[i] = false;
        ZeroMemory(&g_previous_states[i], sizeof(XINPUT_STATE));
    }

    g_xinput_hooks_installed.store(false);
    LogInfo("XXX XInput hooks uninstalled successfully - unhooked %zu modules", g_hooked_modules.size());
}

bool AreXInputHooksInstalled() {
    return g_xinput_hooks_installed.load();
}

// Test function to manually check XInput state
void TestXInputState() {
    XINPUT_STATE state = {};
    DWORD result = XInputGetState(0, &state);

    if (result == ERROR_SUCCESS) {
        LogInfo("XXX TestXInputState: Controller 0 connected - Buttons: 0x%04X, LStick: (%d,%d), RStick: (%d,%d), LTrigger: %u, RTrigger: %u, Packet: %lu",
                state.Gamepad.wButtons,
                state.Gamepad.sThumbLX, state.Gamepad.sThumbLY,
                state.Gamepad.sThumbRX, state.Gamepad.sThumbRY,
                state.Gamepad.bLeftTrigger, state.Gamepad.bRightTrigger,
                state.dwPacketNumber);
    } else {
        LogInfo("XXX TestXInputState: Controller 0 not connected or error: %lu", result);
    }
}

// Diagnostic function to check what XInput modules are loaded
void DiagnoseXInputModules() {
    LogInfo("XXX Diagnosing XInput modules...");

    const char* xinput_modules[] = {
        "xinput1_4.dll",
        "xinput1_3.dll",
        "xinput1_2.dll",
        "xinput1_1.dll",
        "xinput9_1_0.dll"
    };

    for (const char* module_name : xinput_modules) {
        HMODULE module = GetModuleHandleA(module_name);
        if (module) {
            LogInfo("XXX Found XInput module: %s at 0x%p", module_name, module);

            // Try to get XInputGetState function
            FARPROC proc = GetProcAddress(module, "XInputGetState");
            if (proc) {
                LogInfo("XXX   XInputGetState found at: 0x%p", proc);
            } else {
                LogInfo("XXX   XInputGetState NOT found in %s", module_name);
            }
        } else {
            LogInfo("XXX Module not loaded: %s", module_name);
        }
    }
}

} // namespace renodx::hooks
