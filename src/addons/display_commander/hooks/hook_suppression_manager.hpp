#pragma once

#include <string>

namespace display_commanderhooks {

// Hook types that can be suppressed
enum class HookType {
    DXGI_FACTORY,
    DXGI_SWAPCHAIN,
    SL_PROXY_DXGI_SWAPCHAIN,
    D3D11_DEVICE,
    D3D12_DEVICE,
    XINPUT,
    DINPUT,   // dinput.dll (DirectInputCreateA/W)
    DINPUT8,  // dinput8.dll (DirectInput8Create)
    STREAMLINE,
    NGX,
    WINDOWS_GAMING_INPUT,
    API,
    WINDOW_API,
    SLEEP,
    TIMESLOWDOWN,
    DEBUG_OUTPUT,
    LOADLIBRARY,
    DISPLAY_SETTINGS,
    WINDOWS_MESSAGE,
    OPENGL,
    NVAPI,
    PROCESS_EXIT,
    WINDOW_PROC,
    DBGHELP,
    D3D9,
    VULKAN_LOADER  // vulkan-1.dll vkGetInstanceProcAddr / vkGetDeviceProcAddr (VK_NV_low_latency2, extensions)
};

// Hook suppression manager
class HookSuppressionManager {
   public:
    static HookSuppressionManager& GetInstance();

    // Check if a specific hook type should be suppressed
    bool ShouldSuppressHook(HookType hookType);

    // Set suppression for a hook type and persist to config. Takes effect on next hook install attempt.
    void SetSuppressHook(HookType hookType, bool suppress);

    // Mark a hook as successfully installed
    void MarkHookInstalled(HookType hookType);

    // Get the human-readable name for a hook type
    std::string GetHookTypeName(HookType hookType);

    // Check if a hook type is currently marked as installed (from config DisplayCommander.HooksInstalled)
    bool IsHookInstalled(HookType hookType);

    // Number of hook types (for UI iteration)
    static constexpr int kHookTypeCount = 26;

    // Get hook type by index (0 .. kHookTypeCount-1). Valid only for display iteration.
    static HookType GetHookTypeByIndex(int index);

   private:
    HookSuppressionManager() = default;
    ~HookSuppressionManager() = default;
    HookSuppressionManager(const HookSuppressionManager&) = delete;
    HookSuppressionManager& operator=(const HookSuppressionManager&) = delete;
};

}  // namespace display_commanderhooks
