#pragma once

#include <string>

namespace display_commanderhooks {

// Hook types that can be suppressed
enum class HookType {
    DXGI_FACTORY,
    DXGI_SWAPCHAIN,
    D3D11_DEVICE,
    D3D12_DEVICE,
    XINPUT,
    DINPUT,   // dinput.dll (DirectInputCreateA/W)
    DINPUT8,  // dinput8.dll (DirectInput8Create)
    STREAMLINE,
    NGX,
    WINDOWS_GAMING_INPUT,
    HID_KERNEL32,   // kernel32 ReadFile/CreateFile/WriteFile/DeviceIoControl (InstallHIDKernel32Hooks)
    HID_HID_DLL,    // hid.dll HidD_*/HidP_* (InstallHIDDHooks)
    API,
    WINDOW_API,
    SLEEP,
    TIMESLOWDOWN,
    DEBUG_OUTPUT,
    LOADLIBRARY,
    DISPLAY_SETTINGS,
    WINDOWS_MESSAGE,
    OPENGL,
    HID_SUPPRESSION,
    NVAPI,
    PROCESS_EXIT,
    WINDOW_PROC,
    DBGHELP
};

// Hook suppression manager
class HookSuppressionManager {
   public:
    static HookSuppressionManager& GetInstance();

    // Check if a specific hook type should be suppressed
    bool ShouldSuppressHook(HookType hookType);

    // Mark a hook as successfully installed
    void MarkHookInstalled(HookType hookType);

    // Get the suppression setting name for a hook type
    std::string GetSuppressionSettingName(HookType hookType);

    // Get the installation tracking setting name for a hook type
    std::string GetInstallationSettingName(HookType hookType);

    // Get the human-readable name for a hook type
    std::string GetHookTypeName(HookType hookType);

   private:
    HookSuppressionManager() = default;
    ~HookSuppressionManager() = default;
    HookSuppressionManager(const HookSuppressionManager&) = delete;
    HookSuppressionManager& operator=(const HookSuppressionManager&) = delete;
};

}  // namespace display_commanderhooks
