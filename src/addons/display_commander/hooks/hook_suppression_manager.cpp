#include "hook_suppression_manager.hpp"
#include "../settings/hook_suppression_settings.hpp"
#include "../utils/logging.hpp"

namespace display_commanderhooks {

HookSuppressionManager& HookSuppressionManager::GetInstance() {
    static HookSuppressionManager instance;
    return instance;
}

bool HookSuppressionManager::ShouldSuppressHook(HookType hookType) {
    switch (hookType) {
        case HookType::DXGI_FACTORY:
            return settings::g_hook_suppression_settings.suppress_dxgi_factory_hooks.GetValue();
        case HookType::DXGI_SWAPCHAIN:
            return settings::g_hook_suppression_settings.suppress_dxgi_swapchain_hooks.GetValue();
        case HookType::D3D_DEVICE:
            return settings::g_hook_suppression_settings.suppress_d3d_device_hooks.GetValue();
        case HookType::XINPUT:
            return settings::g_hook_suppression_settings.suppress_xinput_hooks.GetValue();
        case HookType::DINPUT:
            return settings::g_hook_suppression_settings.suppress_dinput_hooks.GetValue();
        case HookType::STREAMLINE:
            return settings::g_hook_suppression_settings.suppress_streamline_hooks.GetValue();
        case HookType::NGX:
            return settings::g_hook_suppression_settings.suppress_ngx_hooks.GetValue();
        case HookType::WINDOWS_GAMING_INPUT:
            return settings::g_hook_suppression_settings.suppress_windows_gaming_input_hooks.GetValue();
        case HookType::HID:
            return settings::g_hook_suppression_settings.suppress_hid_hooks.GetValue();
        case HookType::API:
            return settings::g_hook_suppression_settings.suppress_api_hooks.GetValue();
        case HookType::WINDOW_API:
            return settings::g_hook_suppression_settings.suppress_window_api_hooks.GetValue();
        case HookType::SLEEP:
            return settings::g_hook_suppression_settings.suppress_sleep_hooks.GetValue();
        case HookType::TIMESLOWDOWN:
            return settings::g_hook_suppression_settings.suppress_timeslowdown_hooks.GetValue();
        case HookType::DEBUG_OUTPUT:
            return settings::g_hook_suppression_settings.suppress_debug_output_hooks.GetValue();
        case HookType::LOADLIBRARY:
            return settings::g_hook_suppression_settings.suppress_loadlibrary_hooks.GetValue();
        case HookType::DISPLAY_SETTINGS:
            return settings::g_hook_suppression_settings.suppress_display_settings_hooks.GetValue();
        case HookType::WINDOWS_MESSAGE:
            return settings::g_hook_suppression_settings.suppress_windows_message_hooks.GetValue();
        case HookType::OPENGL:
            return settings::g_hook_suppression_settings.suppress_opengl_hooks.GetValue();
        case HookType::HID_SUPPRESSION:
            return settings::g_hook_suppression_settings.suppress_hid_suppression_hooks.GetValue();
        case HookType::NVAPI:
            return settings::g_hook_suppression_settings.suppress_nvapi_hooks.GetValue();
        case HookType::PROCESS_EXIT:
            return settings::g_hook_suppression_settings.suppress_process_exit_hooks.GetValue();
        case HookType::WINDOW_PROC:
            return settings::g_hook_suppression_settings.suppress_window_proc_hooks.GetValue();
        default:
            LogError("HookSuppressionManager::ShouldSuppressHook - Invalid hook type: %d", static_cast<int>(hookType));
            return false;
    }
}

void HookSuppressionManager::MarkHookInstalled(HookType hookType) {
    switch (hookType) {
        case HookType::DXGI_FACTORY:
            if (!settings::g_hook_suppression_settings.dxgi_factory_hooks_installed.GetValue()) {
                settings::g_hook_suppression_settings.dxgi_factory_hooks_installed.SetValue(true);
                settings::g_hook_suppression_settings.suppress_dxgi_factory_hooks.SetValue(false);
            }
            break;
        case HookType::DXGI_SWAPCHAIN:
            if (!settings::g_hook_suppression_settings.suppress_dxgi_swapchain_hooks.GetValue()) {
                settings::g_hook_suppression_settings.suppress_dxgi_swapchain_hooks.SetValue(true);
                settings::g_hook_suppression_settings.suppress_dxgi_swapchain_hooks.SetValue(false);
            }
            break;
        case HookType::D3D_DEVICE:
            if (!settings::g_hook_suppression_settings.d3d_device_hooks_installed.GetValue()) {
                settings::g_hook_suppression_settings.d3d_device_hooks_installed.SetValue(true);
                settings::g_hook_suppression_settings.suppress_d3d_device_hooks.SetValue(false);
            }
            break;
        case HookType::XINPUT:
            if (!settings::g_hook_suppression_settings.xinput_hooks_installed.GetValue()) {
                settings::g_hook_suppression_settings.xinput_hooks_installed.SetValue(true);
                settings::g_hook_suppression_settings.suppress_xinput_hooks.SetValue(false);
            }
            break;
        case HookType::DINPUT:
            if (!settings::g_hook_suppression_settings.dinput_hooks_installed.GetValue()) {
                settings::g_hook_suppression_settings.dinput_hooks_installed.SetValue(true);
                settings::g_hook_suppression_settings.suppress_dinput_hooks.SetValue(false);
            }
            break;
        case HookType::STREAMLINE:
            if (!settings::g_hook_suppression_settings.streamline_hooks_installed.GetValue()) {
                settings::g_hook_suppression_settings.streamline_hooks_installed.SetValue(true);
                settings::g_hook_suppression_settings.suppress_streamline_hooks.SetValue(false);
            }
            break;
        case HookType::NGX:
            if (!settings::g_hook_suppression_settings.ngx_hooks_installed.GetValue()) {
                settings::g_hook_suppression_settings.ngx_hooks_installed.SetValue(true);
                settings::g_hook_suppression_settings.suppress_ngx_hooks.SetValue(false);
            }
            break;
        case HookType::WINDOWS_GAMING_INPUT:
            if (!settings::g_hook_suppression_settings.windows_gaming_input_hooks_installed.GetValue()) {
                settings::g_hook_suppression_settings.windows_gaming_input_hooks_installed.SetValue(true);
                settings::g_hook_suppression_settings.suppress_windows_gaming_input_hooks.SetValue(false);
            }
            break;
        case HookType::HID:
            if (!settings::g_hook_suppression_settings.hid_hooks_installed.GetValue()) {
                settings::g_hook_suppression_settings.hid_hooks_installed.SetValue(true);
                settings::g_hook_suppression_settings.suppress_hid_hooks.SetValue(false);
            }
            break;
        case HookType::API:
            if (!settings::g_hook_suppression_settings.api_hooks_installed.GetValue()) {
                settings::g_hook_suppression_settings.api_hooks_installed.SetValue(true);
                settings::g_hook_suppression_settings.suppress_api_hooks.SetValue(false);
            }
            break;
        case HookType::WINDOW_API:
            if (!settings::g_hook_suppression_settings.window_api_hooks_installed.GetValue()) {
                settings::g_hook_suppression_settings.window_api_hooks_installed.SetValue(true);
                settings::g_hook_suppression_settings.suppress_window_api_hooks.SetValue(false);
            }
            break;
        case HookType::SLEEP:
            if (!settings::g_hook_suppression_settings.sleep_hooks_installed.GetValue()) {
                settings::g_hook_suppression_settings.sleep_hooks_installed.SetValue(true);
                settings::g_hook_suppression_settings.suppress_sleep_hooks.SetValue(false);
            }
            break;
        case HookType::TIMESLOWDOWN:
            if (!settings::g_hook_suppression_settings.timeslowdown_hooks_installed.GetValue()) {
                settings::g_hook_suppression_settings.timeslowdown_hooks_installed.SetValue(true);
                settings::g_hook_suppression_settings.suppress_timeslowdown_hooks.SetValue(false);
            }
            break;
        case HookType::DEBUG_OUTPUT:
            if (!settings::g_hook_suppression_settings.debug_output_hooks_installed.GetValue()) {
                settings::g_hook_suppression_settings.debug_output_hooks_installed.SetValue(true);
                settings::g_hook_suppression_settings.suppress_debug_output_hooks.SetValue(false);
            }
            break;
        case HookType::LOADLIBRARY:
            if (!settings::g_hook_suppression_settings.loadlibrary_hooks_installed.GetValue()) {
                settings::g_hook_suppression_settings.loadlibrary_hooks_installed.SetValue(true);
                settings::g_hook_suppression_settings.suppress_loadlibrary_hooks.SetValue(false);
            }
            break;
        case HookType::DISPLAY_SETTINGS:
            if (!settings::g_hook_suppression_settings.display_settings_hooks_installed.GetValue()) {
                settings::g_hook_suppression_settings.display_settings_hooks_installed.SetValue(true);
                settings::g_hook_suppression_settings.suppress_display_settings_hooks.SetValue(false);
            }
            break;
        case HookType::WINDOWS_MESSAGE:
            if (!settings::g_hook_suppression_settings.windows_message_hooks_installed.GetValue()) {
                settings::g_hook_suppression_settings.windows_message_hooks_installed.SetValue(true);
                settings::g_hook_suppression_settings.suppress_windows_message_hooks.SetValue(false);
            }
            break;
        case HookType::OPENGL:
            if (!settings::g_hook_suppression_settings.opengl_hooks_installed.GetValue()) {
                settings::g_hook_suppression_settings.opengl_hooks_installed.SetValue(true);
                settings::g_hook_suppression_settings.suppress_opengl_hooks.SetValue(false);
            }
            break;
        case HookType::HID_SUPPRESSION:
            if (!settings::g_hook_suppression_settings.hid_suppression_hooks_installed.GetValue()) {
                settings::g_hook_suppression_settings.hid_suppression_hooks_installed.SetValue(true);
                settings::g_hook_suppression_settings.suppress_hid_suppression_hooks.SetValue(false);
            }
            break;
        case HookType::NVAPI:
            if (!settings::g_hook_suppression_settings.nvapi_hooks_installed.GetValue()) {
                settings::g_hook_suppression_settings.nvapi_hooks_installed.SetValue(true);
                settings::g_hook_suppression_settings.suppress_nvapi_hooks.SetValue(false);
            }
            break;
        case HookType::PROCESS_EXIT:
            if (!settings::g_hook_suppression_settings.process_exit_hooks_installed.GetValue()) {
                settings::g_hook_suppression_settings.process_exit_hooks_installed.SetValue(true);
                settings::g_hook_suppression_settings.suppress_process_exit_hooks.SetValue(false);
            }
            break;
        case HookType::WINDOW_PROC:
            if (!settings::g_hook_suppression_settings.window_proc_hooks_installed.GetValue()) {
                settings::g_hook_suppression_settings.window_proc_hooks_installed.SetValue(true);
                settings::g_hook_suppression_settings.suppress_window_proc_hooks.SetValue(false);
            }
            break;

        default:

            LogError("HookSuppressionManager::MarkHookInstalled - Invalid hook type: %d", static_cast<int>(hookType));
            break;
    }

    LogInfo("HookSuppressionManager::MarkHookInstalled - Marked %d as installed and set suppression to false", static_cast<int>(hookType));
}

std::string HookSuppressionManager::GetSuppressionSettingName(HookType hookType) {
    switch (hookType) {
        case HookType::DXGI_FACTORY:
            return "SuppressDxgiFactoryHooks";
        case HookType::DXGI_SWAPCHAIN:
            return "SuppressDxgiSwapchainHooks";
        case HookType::D3D_DEVICE:
            return "SuppressD3DDeviceHooks";
        case HookType::XINPUT:
            return "SuppressXInputHooks";
        case HookType::DINPUT:
            return "SuppressDInputHooks";
        case HookType::STREAMLINE:
            return "SuppressStreamlineHooks";
        case HookType::NGX:
            return "SuppressNGXHooks";
        case HookType::WINDOWS_GAMING_INPUT:
            return "SuppressWindowsGamingInputHooks";
        case HookType::HID:
            return "SuppressHidHooks";
        case HookType::API:
            return "SuppressApiHooks";
        case HookType::WINDOW_API:
            return "SuppressWindowApiHooks";
        case HookType::SLEEP:
            return "SuppressSleepHooks";
        case HookType::TIMESLOWDOWN:
            return "SuppressTimeslowdownHooks";
        case HookType::DEBUG_OUTPUT:
            return "SuppressDebugOutputHooks";
        case HookType::LOADLIBRARY:
            return "SuppressLoadLibraryHooks";
        case HookType::DISPLAY_SETTINGS:
            return "SuppressDisplaySettingsHooks";
        case HookType::WINDOWS_MESSAGE:
            return "SuppressWindowsMessageHooks";
        case HookType::OPENGL:
            return "SuppressOpenGLHooks";
        case HookType::HID_SUPPRESSION:
            return "SuppressHidSuppressionHooks";
        case HookType::NVAPI:
            return "SuppressNvapiHooks";
        case HookType::PROCESS_EXIT:
            return "SuppressProcessExitHooks";
        case HookType::WINDOW_PROC:
            return "SuppressWindowProcHooks";
        default:
            LogError("HookSuppressionManager::GetSuppressionSettingName - Invalid hook type: %d", static_cast<int>(hookType));
            return "";
    }
}

std::string HookSuppressionManager::GetInstallationSettingName(HookType hookType) {
    switch (hookType) {
        case HookType::DXGI_FACTORY:
            return "DxgiFactoryHooksInstalled";
        case HookType::DXGI_SWAPCHAIN:
            return "DxgiSwapchainHooksInstalled";
        case HookType::D3D_DEVICE:
            return "D3DDeviceHooksInstalled";
        case HookType::XINPUT:
            return "XInputHooksInstalled";
        case HookType::DINPUT:
            return "DInputHooksInstalled";
        case HookType::STREAMLINE:
            return "StreamlineHooksInstalled";
        case HookType::NGX:
            return "NGXHooksInstalled";
        case HookType::WINDOWS_GAMING_INPUT:
            return "WindowsGamingInputHooksInstalled";
        case HookType::HID:
            return "HidHooksInstalled";
        case HookType::API:
            return "ApiHooksInstalled";
        case HookType::WINDOW_API:
            return "WindowApiHooksInstalled";
        case HookType::SLEEP:
            return "SleepHooksInstalled";
        case HookType::TIMESLOWDOWN:
            return "TimeslowdownHooksInstalled";
        case HookType::DEBUG_OUTPUT:
            return "DebugOutputHooksInstalled";
        case HookType::LOADLIBRARY:
            return "LoadLibraryHooksInstalled";
        case HookType::DISPLAY_SETTINGS:
            return "DisplaySettingsHooksInstalled";
        case HookType::WINDOWS_MESSAGE:
            return "WindowsMessageHooksInstalled";
        case HookType::OPENGL:
            return "OpenGLHooksInstalled";
        case HookType::HID_SUPPRESSION:
            return "HidSuppressionHooksInstalled";
        case HookType::NVAPI:
            return "NvapiHooksInstalled";
        case HookType::PROCESS_EXIT:
            return "ProcessExitHooksInstalled";
        case HookType::WINDOW_PROC:
            return "WindowProcHooksInstalled";
        default:
            LogError("HookSuppressionManager::GetInstallationSettingName - Invalid hook type: %d", static_cast<int>(hookType));
            return "";
    }
}

bool HookSuppressionManager::WasHookInstalled(HookType hookType) {
    switch (hookType) {
        case HookType::DXGI_FACTORY:
            return settings::g_hook_suppression_settings.dxgi_factory_hooks_installed.GetValue();
        case HookType::DXGI_SWAPCHAIN:
            return settings::g_hook_suppression_settings.dxgi_swapchain_hooks_installed.GetValue();
        case HookType::D3D_DEVICE:
            return settings::g_hook_suppression_settings.d3d_device_hooks_installed.GetValue();
        case HookType::XINPUT:
            return settings::g_hook_suppression_settings.xinput_hooks_installed.GetValue();
        case HookType::DINPUT:
            return settings::g_hook_suppression_settings.dinput_hooks_installed.GetValue();
        case HookType::STREAMLINE:
            return settings::g_hook_suppression_settings.streamline_hooks_installed.GetValue();
        case HookType::NGX:
            return settings::g_hook_suppression_settings.ngx_hooks_installed.GetValue();
        case HookType::WINDOWS_GAMING_INPUT:
            return settings::g_hook_suppression_settings.windows_gaming_input_hooks_installed.GetValue();
        case HookType::HID:
            return settings::g_hook_suppression_settings.hid_hooks_installed.GetValue();
        case HookType::API:
            return settings::g_hook_suppression_settings.api_hooks_installed.GetValue();
        case HookType::WINDOW_API:
            return settings::g_hook_suppression_settings.window_api_hooks_installed.GetValue();
        case HookType::SLEEP:
            return settings::g_hook_suppression_settings.sleep_hooks_installed.GetValue();
        case HookType::TIMESLOWDOWN:
            return settings::g_hook_suppression_settings.timeslowdown_hooks_installed.GetValue();
        case HookType::DEBUG_OUTPUT:
            return settings::g_hook_suppression_settings.debug_output_hooks_installed.GetValue();
        case HookType::LOADLIBRARY:
            return settings::g_hook_suppression_settings.loadlibrary_hooks_installed.GetValue();
        case HookType::DISPLAY_SETTINGS:
            return settings::g_hook_suppression_settings.display_settings_hooks_installed.GetValue();
        case HookType::WINDOWS_MESSAGE:
            return settings::g_hook_suppression_settings.windows_message_hooks_installed.GetValue();
        case HookType::OPENGL:
            return settings::g_hook_suppression_settings.opengl_hooks_installed.GetValue();
        case HookType::HID_SUPPRESSION:
            return settings::g_hook_suppression_settings.hid_suppression_hooks_installed.GetValue();
        case HookType::NVAPI:
            return settings::g_hook_suppression_settings.nvapi_hooks_installed.GetValue();
        case HookType::PROCESS_EXIT:
            return settings::g_hook_suppression_settings.process_exit_hooks_installed.GetValue();
        case HookType::WINDOW_PROC:
            return settings::g_hook_suppression_settings.window_proc_hooks_installed.GetValue();
        default:
            LogError("HookSuppressionManager::WasHookInstalled - Invalid hook type: %d", static_cast<int>(hookType));
            return false;
    }
}

std::string HookSuppressionManager::GetHookTypeName(HookType hookType) {
    switch (hookType) {
        case HookType::DXGI_FACTORY:
            return "DXGI Factory";
        case HookType::DXGI_SWAPCHAIN:
            return "DXGI Swapchain";
        case HookType::D3D_DEVICE:
            return "D3D Device";
        case HookType::XINPUT:
            return "XInput";
        case HookType::DINPUT:
            return "DirectInput";
        case HookType::STREAMLINE:
            return "Streamline";
        case HookType::NGX:
            return "NGX";
        case HookType::WINDOWS_GAMING_INPUT:
            return "Windows Gaming Input";
        case HookType::HID:
            return "HID";
        case HookType::API:
            return "API";
        case HookType::WINDOW_API:
            return "Window API";
        case HookType::SLEEP:
            return "Sleep";
        case HookType::TIMESLOWDOWN:
            return "Time Slowdown";
        case HookType::DEBUG_OUTPUT:
            return "Debug Output";
        case HookType::LOADLIBRARY:
            return "LoadLibrary";
        case HookType::DISPLAY_SETTINGS:
            return "Display Settings";
        case HookType::WINDOWS_MESSAGE:
            return "Windows Message";
        case HookType::OPENGL:
            return "OpenGL";
        case HookType::HID_SUPPRESSION:
            return "HID Suppression";
        case HookType::NVAPI:
            return "NVAPI";
        case HookType::PROCESS_EXIT:
            return "Process Exit";
        case HookType::WINDOW_PROC:
            return "Window Procedure";
        default:
            LogError("HookSuppressionManager::GetHookTypeName - Invalid hook type: %d", static_cast<int>(hookType));
            return "Unknown";
    }
}

} // namespace display_commanderhooks
