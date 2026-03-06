#include "hook_suppression_manager.hpp"
#include "../settings/advanced_tab_settings.hpp"
#include "../settings/hook_suppression_settings.hpp"
#include "../utils/logging.hpp"

#include <set>

namespace display_commanderhooks {

HookSuppressionManager& HookSuppressionManager::GetInstance() {
    static HookSuppressionManager instance;
    return instance;
}

namespace {
// Helper function to get the setting reference for a hook type
ui::new_ui::SettingBase* GetSuppressionSetting(HookType hookType) {
    switch (hookType) {
        case HookType::DXGI_FACTORY:   return &settings::g_hook_suppression_settings.suppress_dxgi_factory_hooks;
        case HookType::DXGI_SWAPCHAIN: return &settings::g_hook_suppression_settings.suppress_dxgi_swapchain_hooks;
        case HookType::D3D11_DEVICE:   return &settings::g_hook_suppression_settings.suppress_d3d11_device_hooks;
        case HookType::D3D12_DEVICE:   return &settings::g_hook_suppression_settings.suppress_d3d12_device_hooks;
        case HookType::XINPUT:         return &settings::g_hook_suppression_settings.suppress_xinput_hooks;
        case HookType::DINPUT:         return &settings::g_hook_suppression_settings.suppress_dinput_hooks;
        case HookType::DINPUT8:        return &settings::g_hook_suppression_settings.suppress_dinput8_hooks;
        case HookType::STREAMLINE:     return &settings::g_hook_suppression_settings.suppress_streamline_hooks;
        case HookType::NGX:            return &settings::g_hook_suppression_settings.suppress_ngx_hooks;
        case HookType::WINDOWS_GAMING_INPUT:
            return &settings::g_hook_suppression_settings.suppress_windows_gaming_input_hooks;
        case HookType::HID_KERNEL32:
            return &settings::g_hook_suppression_settings.suppress_hid_kernel32_hooks;
        case HookType::HID_HID_DLL:
            return &settings::g_hook_suppression_settings.suppress_hid_hid_dll_hooks;
        case HookType::API:              return &settings::g_hook_suppression_settings.suppress_api_hooks;
        case HookType::WINDOW_API:       return &settings::g_hook_suppression_settings.suppress_window_api_hooks;
        case HookType::SLEEP:            return &settings::g_hook_suppression_settings.suppress_sleep_hooks;
        case HookType::TIMESLOWDOWN:     return &settings::g_hook_suppression_settings.suppress_timeslowdown_hooks;
        case HookType::DEBUG_OUTPUT:     return &settings::g_hook_suppression_settings.suppress_debug_output_hooks;
        case HookType::LOADLIBRARY:      return &settings::g_hook_suppression_settings.suppress_loadlibrary_hooks;
        case HookType::DISPLAY_SETTINGS: return &settings::g_hook_suppression_settings.suppress_display_settings_hooks;
        case HookType::WINDOWS_MESSAGE:  return &settings::g_hook_suppression_settings.suppress_windows_message_hooks;
        case HookType::OPENGL:           return &settings::g_hook_suppression_settings.suppress_opengl_hooks;
        case HookType::HID_SUPPRESSION:  return &settings::g_hook_suppression_settings.suppress_hid_suppression_hooks;
        case HookType::NVAPI:            return &settings::g_hook_suppression_settings.suppress_nvapi_hooks;
        case HookType::PROCESS_EXIT:     return &settings::g_hook_suppression_settings.suppress_process_exit_hooks;
        case HookType::WINDOW_PROC:      return &settings::g_hook_suppression_settings.suppress_window_proc_hooks;
        case HookType::DBGHELP:          return &settings::g_hook_suppression_settings.suppress_dbghelp_hooks;
        default:                         return nullptr;
    }
}
}  // anonymous namespace

bool HookSuppressionManager::ShouldSuppressHook(HookType hookType) {
    // Log suppression hint once per hook type (only if setting is not already enabled)
    static std::set<HookType> logged_hook_types;
    if (!logged_hook_types.contains(hookType)) {
        ui::new_ui::SettingBase* setting = GetSuppressionSetting(hookType);
        if (setting != nullptr) {
            // Get current value to check if already suppressed
            bool current_value = false;
            switch (hookType) {
                case HookType::DXGI_FACTORY:
                    current_value = settings::g_hook_suppression_settings.suppress_dxgi_factory_hooks.GetValue();
                    break;
                case HookType::DXGI_SWAPCHAIN:
                    current_value = settings::g_hook_suppression_settings.suppress_dxgi_swapchain_hooks.GetValue();
                    break;
                case HookType::D3D11_DEVICE:
                    current_value = settings::g_hook_suppression_settings.suppress_d3d11_device_hooks.GetValue();
                    break;
                case HookType::D3D12_DEVICE:
                    current_value = settings::g_hook_suppression_settings.suppress_d3d12_device_hooks.GetValue();
                    break;
                case HookType::XINPUT:
                    current_value = settings::g_hook_suppression_settings.suppress_xinput_hooks.GetValue();
                    break;
                case HookType::DINPUT:
                    current_value = settings::g_hook_suppression_settings.suppress_dinput_hooks.GetValue();
                    break;
                case HookType::DINPUT8:
                    current_value = settings::g_hook_suppression_settings.suppress_dinput8_hooks.GetValue();
                    break;
                case HookType::STREAMLINE:
                    current_value = settings::g_hook_suppression_settings.suppress_streamline_hooks.GetValue();
                    break;
                case HookType::NGX:
                    current_value = settings::g_hook_suppression_settings.suppress_ngx_hooks.GetValue();
                    break;
                case HookType::WINDOWS_GAMING_INPUT:
                    current_value =
                        settings::g_hook_suppression_settings.suppress_windows_gaming_input_hooks.GetValue();
                    break;
                case HookType::HID_KERNEL32:
                    current_value = settings::g_hook_suppression_settings.suppress_hid_kernel32_hooks.GetValue();
                    break;
                case HookType::HID_HID_DLL:
                    current_value = settings::g_hook_suppression_settings.suppress_hid_hid_dll_hooks.GetValue();
                    break;
                case HookType::API:
                    current_value = settings::g_hook_suppression_settings.suppress_api_hooks.GetValue();
                    break;
                case HookType::WINDOW_API:
                    current_value = settings::g_hook_suppression_settings.suppress_window_api_hooks.GetValue();
                    break;
                case HookType::SLEEP:
                    current_value = settings::g_hook_suppression_settings.suppress_sleep_hooks.GetValue();
                    break;
                case HookType::TIMESLOWDOWN:
                    current_value = settings::g_hook_suppression_settings.suppress_timeslowdown_hooks.GetValue();
                    break;
                case HookType::DEBUG_OUTPUT:
                    current_value = settings::g_hook_suppression_settings.suppress_debug_output_hooks.GetValue();
                    break;
                case HookType::LOADLIBRARY:
                    current_value = settings::g_hook_suppression_settings.suppress_loadlibrary_hooks.GetValue();
                    break;
                case HookType::DISPLAY_SETTINGS:
                    current_value = settings::g_hook_suppression_settings.suppress_display_settings_hooks.GetValue();
                    break;
                case HookType::WINDOWS_MESSAGE:
                    current_value = settings::g_hook_suppression_settings.suppress_windows_message_hooks.GetValue();
                    break;
                case HookType::OPENGL:
                    current_value = settings::g_hook_suppression_settings.suppress_opengl_hooks.GetValue();
                    break;
                case HookType::HID_SUPPRESSION:
                    current_value = settings::g_hook_suppression_settings.suppress_hid_suppression_hooks.GetValue();
                    break;
                case HookType::NVAPI:
                    current_value = settings::g_hook_suppression_settings.suppress_nvapi_hooks.GetValue();
                    break;
                case HookType::PROCESS_EXIT:
                    current_value = settings::g_hook_suppression_settings.suppress_process_exit_hooks.GetValue();
                    break;
                case HookType::WINDOW_PROC:
                    current_value = settings::g_hook_suppression_settings.suppress_window_proc_hooks.GetValue();
                    break;
                case HookType::DBGHELP:
                    current_value = settings::g_hook_suppression_settings.suppress_dbghelp_hooks.GetValue();
                    break;
                default: break;
            }

            if (!current_value) {
                LogInfo("To suppress %s hooks, set %s=1 in [%s] section of DisplayCommander.toml",
                        GetHookTypeName(hookType).c_str(), setting->GetKey().c_str(), setting->GetSection().c_str());
            }
            logged_hook_types.insert(hookType);
        }
    }

    switch (hookType) {
        case HookType::DXGI_FACTORY:
            return settings::g_hook_suppression_settings.suppress_dxgi_factory_hooks.GetValue();
        case HookType::DXGI_SWAPCHAIN:
            return settings::g_hook_suppression_settings.suppress_dxgi_swapchain_hooks.GetValue();
        case HookType::D3D11_DEVICE:
            return !settings::g_advancedTabSettings.enable_dx11_hooks.GetValue() ||
                   settings::g_hook_suppression_settings.suppress_d3d11_device_hooks.GetValue();
        case HookType::D3D12_DEVICE:
            return settings::g_hook_suppression_settings.suppress_d3d12_device_hooks.GetValue();
        case HookType::XINPUT:     return settings::g_hook_suppression_settings.suppress_xinput_hooks.GetValue();
        case HookType::DINPUT:     return settings::g_hook_suppression_settings.suppress_dinput_hooks.GetValue();
        case HookType::DINPUT8:    return settings::g_hook_suppression_settings.suppress_dinput8_hooks.GetValue();
        case HookType::STREAMLINE: return settings::g_hook_suppression_settings.suppress_streamline_hooks.GetValue();
        case HookType::NGX:        return settings::g_hook_suppression_settings.suppress_ngx_hooks.GetValue();
        case HookType::WINDOWS_GAMING_INPUT:
            return settings::g_hook_suppression_settings.suppress_windows_gaming_input_hooks.GetValue();
        case HookType::HID_KERNEL32:
            return settings::g_hook_suppression_settings.suppress_hid_kernel32_hooks.GetValue();
        case HookType::HID_HID_DLL:
            return settings::g_hook_suppression_settings.suppress_hid_hid_dll_hooks.GetValue();
        case HookType::API:        return settings::g_hook_suppression_settings.suppress_api_hooks.GetValue();
        case HookType::WINDOW_API: return settings::g_hook_suppression_settings.suppress_window_api_hooks.GetValue();
        case HookType::SLEEP:      return settings::g_hook_suppression_settings.suppress_sleep_hooks.GetValue();
        case HookType::TIMESLOWDOWN:
            return settings::g_hook_suppression_settings.suppress_timeslowdown_hooks.GetValue();
        case HookType::DEBUG_OUTPUT:
            return settings::g_hook_suppression_settings.suppress_debug_output_hooks.GetValue();
        case HookType::LOADLIBRARY: return settings::g_hook_suppression_settings.suppress_loadlibrary_hooks.GetValue();
        case HookType::DISPLAY_SETTINGS:
            return settings::g_hook_suppression_settings.suppress_display_settings_hooks.GetValue();
        case HookType::WINDOWS_MESSAGE:
            return settings::g_hook_suppression_settings.suppress_windows_message_hooks.GetValue();
        case HookType::OPENGL: return settings::g_hook_suppression_settings.suppress_opengl_hooks.GetValue();
        case HookType::HID_SUPPRESSION:
            return settings::g_hook_suppression_settings.suppress_hid_suppression_hooks.GetValue();
        case HookType::NVAPI: return settings::g_hook_suppression_settings.suppress_nvapi_hooks.GetValue();
        case HookType::PROCESS_EXIT:
            return settings::g_hook_suppression_settings.suppress_process_exit_hooks.GetValue();
        case HookType::WINDOW_PROC: return settings::g_hook_suppression_settings.suppress_window_proc_hooks.GetValue();
        case HookType::DBGHELP: return settings::g_hook_suppression_settings.suppress_dbghelp_hooks.GetValue();
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
            if (!settings::g_hook_suppression_settings.dxgi_swapchain_hooks_installed.GetValue()) {
                settings::g_hook_suppression_settings.dxgi_swapchain_hooks_installed.SetValue(true);
                settings::g_hook_suppression_settings.suppress_dxgi_swapchain_hooks.SetValue(false);
            }
            break;
        case HookType::D3D11_DEVICE:
            if (!settings::g_hook_suppression_settings.d3d11_device_hooks_installed.GetValue()) {
                settings::g_hook_suppression_settings.d3d11_device_hooks_installed.SetValue(true);
                settings::g_hook_suppression_settings.suppress_d3d11_device_hooks.SetValue(false);
            }
            break;
        case HookType::D3D12_DEVICE:
            if (!settings::g_hook_suppression_settings.d3d12_device_hooks_installed.GetValue()) {
                settings::g_hook_suppression_settings.d3d12_device_hooks_installed.SetValue(true);
                settings::g_hook_suppression_settings.suppress_d3d12_device_hooks.SetValue(false);
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
        case HookType::DINPUT8:
            if (!settings::g_hook_suppression_settings.dinput8_hooks_installed.GetValue()) {
                settings::g_hook_suppression_settings.dinput8_hooks_installed.SetValue(true);
                settings::g_hook_suppression_settings.suppress_dinput8_hooks.SetValue(false);
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
        case HookType::HID_KERNEL32:
            if (!settings::g_hook_suppression_settings.hid_kernel32_hooks_installed.GetValue()) {
                settings::g_hook_suppression_settings.hid_kernel32_hooks_installed.SetValue(true);
                settings::g_hook_suppression_settings.suppress_hid_kernel32_hooks.SetValue(false);
            }
            break;
        case HookType::HID_HID_DLL:
            if (!settings::g_hook_suppression_settings.hid_hid_dll_hooks_installed.GetValue()) {
                settings::g_hook_suppression_settings.hid_hid_dll_hooks_installed.SetValue(true);
                settings::g_hook_suppression_settings.suppress_hid_hid_dll_hooks.SetValue(false);
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
        case HookType::DBGHELP:
            if (!settings::g_hook_suppression_settings.dbghelp_hooks_installed.GetValue()) {
                settings::g_hook_suppression_settings.dbghelp_hooks_installed.SetValue(true);
                settings::g_hook_suppression_settings.suppress_dbghelp_hooks.SetValue(false);
            }
            break;

        default:

            LogError("HookSuppressionManager::MarkHookInstalled - Invalid hook type: %d", static_cast<int>(hookType));
            break;
    }

    LogInfo("HookSuppressionManager::MarkHookInstalled - Marked %d as installed and set suppression to false",
            static_cast<int>(hookType));
}

std::string HookSuppressionManager::GetHookTypeName(HookType hookType) {
    switch (hookType) {
        case HookType::DXGI_FACTORY:         return "DXGI Factory";
        case HookType::DXGI_SWAPCHAIN:       return "DXGI Swapchain";
        case HookType::D3D11_DEVICE:         return "D3D11 Device";
        case HookType::D3D12_DEVICE:         return "D3D12 Device";
        case HookType::XINPUT:               return "XInput";
        case HookType::DINPUT:               return "DirectInput";
        case HookType::DINPUT8:              return "DirectInput 8";
        case HookType::STREAMLINE:           return "Streamline";
        case HookType::NGX:                  return "NGX";
        case HookType::WINDOWS_GAMING_INPUT: return "Windows Gaming Input";
        case HookType::HID_KERNEL32:         return "HID (kernel32)";
        case HookType::HID_HID_DLL:         return "HID (hid.dll)";
        case HookType::API:                  return "API";
        case HookType::WINDOW_API:           return "Window API";
        case HookType::SLEEP:                return "Sleep";
        case HookType::TIMESLOWDOWN:         return "Time Slowdown";
        case HookType::DEBUG_OUTPUT:         return "Debug Output";
        case HookType::LOADLIBRARY:          return "LoadLibrary";
        case HookType::DISPLAY_SETTINGS:     return "Display Settings";
        case HookType::WINDOWS_MESSAGE:      return "Windows Message";
        case HookType::OPENGL:               return "OpenGL";
        case HookType::HID_SUPPRESSION:      return "HID Suppression";
        case HookType::NVAPI:                return "NVAPI";
        case HookType::PROCESS_EXIT:         return "Process Exit";
        case HookType::WINDOW_PROC:          return "Window Procedure";
        case HookType::DBGHELP:              return "DbgHelp";
        default:
            LogError("HookSuppressionManager::GetHookTypeName - Invalid hook type: %d", static_cast<int>(hookType));
            return "Unknown";
    }
}

}  // namespace display_commanderhooks
