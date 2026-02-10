#pragma once

#include "../ui/new_ui/settings_wrapper.hpp"

#include <vector>

namespace settings {

// Bring setting types into scope
using ui::new_ui::BoolSetting;
using ui::new_ui::SettingBase;

// Settings manager for hook suppression
// Hooks with suppress_xxx_hooks = true are blacklisted by default
// Currently blacklisted: DEBUG_OUTPUT (can be noisy), HID_SUPPRESSION (experimental)
class HookSuppressionSettings {
   public:
    HookSuppressionSettings();
    ~HookSuppressionSettings() = default;

    // Load all settings from ReShade config
    void LoadAll();

    // Get all settings for loading
    std::vector<SettingBase*> GetAllSettings();

    // Hook suppression settings
    BoolSetting suppress_dxgi_factory_hooks;
    BoolSetting suppress_dxgi_swapchain_hooks;
    BoolSetting suppress_d3d11_device_hooks;
    BoolSetting suppress_d3d12_device_hooks;
    BoolSetting suppress_xinput_hooks;
    BoolSetting suppress_dinput_hooks;
    BoolSetting suppress_streamline_hooks;
    BoolSetting suppress_ngx_hooks;
    BoolSetting suppress_windows_gaming_input_hooks;
    BoolSetting suppress_hid_hooks;
    BoolSetting suppress_api_hooks;
    BoolSetting suppress_window_api_hooks;
    BoolSetting suppress_sleep_hooks;
    BoolSetting suppress_timeslowdown_hooks;
    BoolSetting suppress_debug_output_hooks;
    BoolSetting suppress_loadlibrary_hooks;
    BoolSetting suppress_display_settings_hooks;
    BoolSetting suppress_windows_message_hooks;
    BoolSetting suppress_opengl_hooks;
    BoolSetting suppress_hid_suppression_hooks;
    BoolSetting suppress_nvapi_hooks;
    BoolSetting suppress_process_exit_hooks;
    BoolSetting suppress_window_proc_hooks;
    BoolSetting suppress_dbghelp_hooks;

    // Auto-detection settings (set to 1 when hooks are successfully installed)
    BoolSetting dxgi_factory_hooks_installed;
    BoolSetting dxgi_swapchain_hooks_installed;
    BoolSetting d3d11_device_hooks_installed;
    BoolSetting d3d12_device_hooks_installed;
    BoolSetting xinput_hooks_installed;
    BoolSetting dinput_hooks_installed;
    BoolSetting streamline_hooks_installed;
    BoolSetting ngx_hooks_installed;
    BoolSetting windows_gaming_input_hooks_installed;
    BoolSetting hid_hooks_installed;
    BoolSetting api_hooks_installed;
    BoolSetting window_api_hooks_installed;
    BoolSetting sleep_hooks_installed;
    BoolSetting timeslowdown_hooks_installed;
    BoolSetting debug_output_hooks_installed;
    BoolSetting loadlibrary_hooks_installed;
    BoolSetting display_settings_hooks_installed;
    BoolSetting windows_message_hooks_installed;
    BoolSetting opengl_hooks_installed;
    BoolSetting hid_suppression_hooks_installed;
    BoolSetting nvapi_hooks_installed;
    BoolSetting process_exit_hooks_installed;
    BoolSetting window_proc_hooks_installed;
    BoolSetting dbghelp_hooks_installed;

   private:
    std::vector<SettingBase*> all_settings_;
};

// Global instance
extern HookSuppressionSettings g_hook_suppression_settings;

}  // namespace settings
