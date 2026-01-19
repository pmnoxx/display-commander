#pragma once

#include <string>

namespace display_commanderhooks {

// Hook types that can be suppressed
enum class HookType {
    DXGI_FACTORY,
    DXGI_SWAPCHAIN,
    D3D_DEVICE,
    XINPUT,
    DINPUT,
    STREAMLINE,
    NGX,
    WINDOWS_GAMING_INPUT,
    HID,
    API,
    WINDOW_API,
    SLEEP,
    TIMESLOWDOWN,
    DEBUG_OUTPUT,
    LOADLIBRARY,
    DISPLAY_SETTINGS,
    WINDOWS_MESSAGE,
    OPENGL,
    VULKAN,
    HID_SUPPRESSION,
    NVAPI,
    PROCESS_EXIT,
    WINDOW_PROC
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

    // Check if a hook was previously installed
    bool WasHookInstalled(HookType hookType);

    // Get the human-readable name for a hook type
    std::string GetHookTypeName(HookType hookType);

   private:
    HookSuppressionManager() = default;
    ~HookSuppressionManager() = default;
    HookSuppressionManager(const HookSuppressionManager&) = delete;
    HookSuppressionManager& operator=(const HookSuppressionManager&) = delete;
};

}  // namespace display_commanderhooks
