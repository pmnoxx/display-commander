#include "vulkan_hooks.hpp"
#include <MinHook.h>
#include <vulkan/vulkan.h>
#include <array>
#include <atomic>
#include "../utils.hpp"
#include "../utils/general_utils.hpp"
#include "../utils/logging.hpp"
#include "hook_suppression_manager.hpp"

// Original function pointers
PFN_vkCreateInstance vkCreateInstance_Original = nullptr;
PFN_vkCreateDevice vkCreateDevice_Original = nullptr;
PFN_vkCreateSwapchainKHR vkCreateSwapchainKHR_Original = nullptr;
PFN_vkQueuePresentKHR vkQueuePresentKHR_Original = nullptr;
PFN_vkAcquireNextImageKHR vkAcquireNextImageKHR_Original = nullptr;
PFN_vkQueueSubmit vkQueueSubmit_Original = nullptr;
PFN_vkQueueSubmit2 vkQueueSubmit2_Original = nullptr;

// Hook installation state
static std::atomic<bool> g_vulkan_hooks_installed{false};

// Call counters for tracking first 5 calls
static constexpr int MAX_LOGGED_CALLS = 5;
static std::atomic<int> g_vkCreateInstance_call_count{0};
static std::atomic<int> g_vkCreateDevice_call_count{0};
static std::atomic<int> g_vkCreateSwapchainKHR_call_count{0};
static std::atomic<int> g_vkQueuePresentKHR_call_count{0};
static std::atomic<int> g_vkAcquireNextImageKHR_call_count{0};
static std::atomic<int> g_vkQueueSubmit_call_count{0};
static std::atomic<int> g_vkQueueSubmit2_call_count{0};

// Helper function to log first N calls
template <typename... Args>
void LogVulkanCall(const char* functionName, std::atomic<int>& callCount, Args... args) {
    int count = callCount.fetch_add(1) + 1;
    if (count <= MAX_LOGGED_CALLS) {
        LogInfo("[Vulkan] %s call #%d", functionName, count);
        // Log additional details for first call
        if (count == 1) {
            LogInfo("[Vulkan] %s - First call details logged", functionName);
        }
    }
}

// Hook detour functions
VkResult VKAPI_CALL vkCreateInstance_Detour(const VkInstanceCreateInfo* pCreateInfo,
                                            const VkAllocationCallbacks* pAllocator, VkInstance* pInstance) {
    LogVulkanCall("vkCreateInstance", g_vkCreateInstance_call_count);

    if (pCreateInfo) {
        int count = g_vkCreateInstance_call_count.load();
        if (count <= MAX_LOGGED_CALLS) {
            LogInfo("[Vulkan] vkCreateInstance - enabledExtensionCount: %u, enabledLayerCount: %u",
                    pCreateInfo->enabledExtensionCount, pCreateInfo->enabledLayerCount);
        }
    }

    VkResult result = vkCreateInstance_Original(pCreateInfo, pAllocator, pInstance);

    int count = g_vkCreateInstance_call_count.load();
    if (count <= MAX_LOGGED_CALLS) {
        LogInfo("[Vulkan] vkCreateInstance - Result: %d, Instance: %p", result, pInstance ? *pInstance : nullptr);
    }

    return result;
}

VkResult VKAPI_CALL vkCreateDevice_Detour(VkPhysicalDevice physicalDevice, const VkDeviceCreateInfo* pCreateInfo,
                                          const VkAllocationCallbacks* pAllocator, VkDevice* pDevice) {
    LogVulkanCall("vkCreateDevice", g_vkCreateDevice_call_count);

    if (pCreateInfo) {
        int count = g_vkCreateDevice_call_count.load();
        if (count <= MAX_LOGGED_CALLS) {
            LogInfo("[Vulkan] vkCreateDevice - enabledExtensionCount: %u, queueCreateInfoCount: %u",
                    pCreateInfo->enabledExtensionCount, pCreateInfo->queueCreateInfoCount);
        }
    }

    VkResult result = vkCreateDevice_Original(physicalDevice, pCreateInfo, pAllocator, pDevice);

    int count = g_vkCreateDevice_call_count.load();
    if (count <= MAX_LOGGED_CALLS) {
        LogInfo("[Vulkan] vkCreateDevice - Result: %d, Device: %p", result, pDevice ? *pDevice : nullptr);
    }

    return result;
}

VkResult VKAPI_CALL vkCreateSwapchainKHR_Detour(VkDevice device, const VkSwapchainCreateInfoKHR* pCreateInfo,
                                                const VkAllocationCallbacks* pAllocator, VkSwapchainKHR* pSwapchain) {
    LogVulkanCall("vkCreateSwapchainKHR", g_vkCreateSwapchainKHR_call_count);

    if (pCreateInfo) {
        int count = g_vkCreateSwapchainKHR_call_count.load();
        if (count <= MAX_LOGGED_CALLS) {
            LogInfo("[Vulkan] vkCreateSwapchainKHR - imageExtent: %ux%u, imageFormat: %d, presentMode: %d",
                    pCreateInfo->imageExtent.width, pCreateInfo->imageExtent.height, pCreateInfo->imageFormat,
                    pCreateInfo->presentMode);
        }
    }

    VkResult result = vkCreateSwapchainKHR_Original(device, pCreateInfo, pAllocator, pSwapchain);

    int count = g_vkCreateSwapchainKHR_call_count.load();
    if (count <= MAX_LOGGED_CALLS) {
        LogInfo("[Vulkan] vkCreateSwapchainKHR - Result: %d, Swapchain: %p", result, pSwapchain ? pSwapchain : nullptr);
    }

    return result;
}

VkResult VKAPI_CALL vkQueuePresentKHR_Detour(VkQueue queue, const VkPresentInfoKHR* pPresentInfo) {
    LogVulkanCall("vkQueuePresentKHR", g_vkQueuePresentKHR_call_count);

    if (pPresentInfo) {
        int count = g_vkQueuePresentKHR_call_count.load();
        if (count <= MAX_LOGGED_CALLS) {
            LogInfo("[Vulkan] vkQueuePresentKHR - swapchainCount: %u, waitSemaphoreCount: %u, pNext: %p",
                    pPresentInfo->swapchainCount, pPresentInfo->waitSemaphoreCount, pPresentInfo->pNext);

            // Note: Frame generation detection (VkSetPresentConfigNV) would require NV-specific structures
            // The pNext chain may contain frame generation config if present
        }
    }

    VkResult result = vkQueuePresentKHR_Original(queue, pPresentInfo);

    int count = g_vkQueuePresentKHR_call_count.load();
    if (count <= MAX_LOGGED_CALLS) {
        LogInfo("[Vulkan] vkQueuePresentKHR - Result: %d", result);
    }

    return result;
}

VkResult VKAPI_CALL vkAcquireNextImageKHR_Detour(VkDevice device, VkSwapchainKHR swapchain, uint64_t timeout,
                                                 VkSemaphore semaphore, VkFence fence, uint32_t* pImageIndex) {
    LogVulkanCall("vkAcquireNextImageKHR", g_vkAcquireNextImageKHR_call_count);

    int count = g_vkAcquireNextImageKHR_call_count.load();
    if (count <= MAX_LOGGED_CALLS) {
        LogInfo("[Vulkan] vkAcquireNextImageKHR - timeout: %llu, semaphore: %p, fence: %p", timeout, semaphore, fence);
    }

    VkResult result = vkAcquireNextImageKHR_Original(device, swapchain, timeout, semaphore, fence, pImageIndex);

    if (count <= MAX_LOGGED_CALLS) {
        LogInfo("[Vulkan] vkAcquireNextImageKHR - Result: %d, imageIndex: %u", result, pImageIndex ? *pImageIndex : 0);
    }

    return result;
}

VkResult VKAPI_CALL vkQueueSubmit_Detour(VkQueue queue, uint32_t submitCount, const VkSubmitInfo* pSubmits,
                                         VkFence fence) {
    LogVulkanCall("vkQueueSubmit", g_vkQueueSubmit_call_count);

    int count = g_vkQueueSubmit_call_count.load();
    if (count <= MAX_LOGGED_CALLS) {
        LogInfo("[Vulkan] vkQueueSubmit - submitCount: %u, fence: %p", submitCount, fence);
    }

    VkResult result = vkQueueSubmit_Original(queue, submitCount, pSubmits, fence);

    if (count <= MAX_LOGGED_CALLS) {
        LogInfo("[Vulkan] vkQueueSubmit - Result: %d", result);
    }

    return result;
}

VkResult VKAPI_CALL vkQueueSubmit2_Detour(VkQueue queue, uint32_t submitCount, const VkSubmitInfo2* pSubmits,
                                          VkFence fence) {
    LogVulkanCall("vkQueueSubmit2", g_vkQueueSubmit2_call_count);

    int count = g_vkQueueSubmit2_call_count.load();
    if (count <= MAX_LOGGED_CALLS) {
        LogInfo("[Vulkan] vkQueueSubmit2 - submitCount: %u, fence: %p", submitCount, fence);
    }

    VkResult result = vkQueueSubmit2_Original(queue, submitCount, pSubmits, fence);

    if (count <= MAX_LOGGED_CALLS) {
        LogInfo("[Vulkan] vkQueueSubmit2 - Result: %d", result);
    }

    return result;
}

// Hook installation function
bool InstallVulkanHooks(HMODULE vulkan_module) {
    if (g_vulkan_hooks_installed.load()) {
        LogInfo("Vulkan hooks already installed");
        return true;
    }

    // Check if Vulkan hooks should be suppressed
    if (display_commanderhooks::HookSuppressionManager::GetInstance().ShouldSuppressHook(
            display_commanderhooks::HookType::VULKAN)) {
        LogInfo("Vulkan hooks installation suppressed by user setting");
        return false;
    }

    if (vulkan_module == nullptr) {
        vulkan_module = GetModuleHandleW(L"vulkan-1.dll");
        if (vulkan_module == nullptr) {
            LogWarn("vulkan-1.dll not loaded, skipping Vulkan hooks");
            return false;
        }
    }

    // Initialize MinHook (only if not already initialized)
    MH_STATUS init_status = SafeInitializeMinHook(display_commanderhooks::HookType::VULKAN);
    if (init_status != MH_OK && init_status != MH_ERROR_ALREADY_INITIALIZED) {
        LogError("Failed to initialize MinHook for Vulkan hooks - Status: %d", init_status);
        return false;
    }

    if (init_status == MH_ERROR_ALREADY_INITIALIZED) {
        LogInfo("MinHook already initialized, proceeding with Vulkan hooks");
    } else {
        LogInfo("MinHook initialized successfully for Vulkan hooks");
    }

    LogInfo("Installing Vulkan hooks...");

    // Get function addresses
    auto vkCreateInstance_proc =
        reinterpret_cast<PFN_vkCreateInstance>(GetProcAddress(vulkan_module, "vkCreateInstance"));
    auto vkCreateDevice_proc = reinterpret_cast<PFN_vkCreateDevice>(GetProcAddress(vulkan_module, "vkCreateDevice"));
    auto vkCreateSwapchainKHR_proc =
        reinterpret_cast<PFN_vkCreateSwapchainKHR>(GetProcAddress(vulkan_module, "vkCreateSwapchainKHR"));
    auto vkQueuePresentKHR_proc =
        reinterpret_cast<PFN_vkQueuePresentKHR>(GetProcAddress(vulkan_module, "vkQueuePresentKHR"));
    auto vkAcquireNextImageKHR_proc =
        reinterpret_cast<PFN_vkAcquireNextImageKHR>(GetProcAddress(vulkan_module, "vkAcquireNextImageKHR"));
    auto vkQueueSubmit_proc = reinterpret_cast<PFN_vkQueueSubmit>(GetProcAddress(vulkan_module, "vkQueueSubmit"));
    auto vkQueueSubmit2_proc = reinterpret_cast<PFN_vkQueueSubmit2>(GetProcAddress(vulkan_module, "vkQueueSubmit2"));

    // Install hooks
    bool success = true;

    if (vkCreateInstance_proc) {
        if (!CreateAndEnableHook(vkCreateInstance_proc, vkCreateInstance_Detour,
                                 reinterpret_cast<LPVOID*>(&vkCreateInstance_Original), "vkCreateInstance")) {
            LogError("Failed to create and enable vkCreateInstance hook");
            success = false;
        }
    } else {
        LogWarn("vkCreateInstance not found in vulkan-1.dll");
    }

    if (vkCreateDevice_proc) {
        if (!CreateAndEnableHook(vkCreateDevice_proc, vkCreateDevice_Detour,
                                 reinterpret_cast<LPVOID*>(&vkCreateDevice_Original), "vkCreateDevice")) {
            LogError("Failed to create and enable vkCreateDevice hook");
            success = false;
        }
    } else {
        LogWarn("vkCreateDevice not found in vulkan-1.dll");
    }

    if (vkCreateSwapchainKHR_proc) {
        if (!CreateAndEnableHook(vkCreateSwapchainKHR_proc, vkCreateSwapchainKHR_Detour,
                                 reinterpret_cast<LPVOID*>(&vkCreateSwapchainKHR_Original), "vkCreateSwapchainKHR")) {
            LogError("Failed to create and enable vkCreateSwapchainKHR hook");
            success = false;
        }
    } else {
        LogWarn("vkCreateSwapchainKHR not found in vulkan-1.dll");
    }

    if (vkQueuePresentKHR_proc) {
        if (!CreateAndEnableHook(vkQueuePresentKHR_proc, vkQueuePresentKHR_Detour,
                                 reinterpret_cast<LPVOID*>(&vkQueuePresentKHR_Original), "vkQueuePresentKHR")) {
            LogError("Failed to create and enable vkQueuePresentKHR hook");
            success = false;
        }
    } else {
        LogWarn("vkQueuePresentKHR not found in vulkan-1.dll");
    }

    if (vkAcquireNextImageKHR_proc) {
        if (!CreateAndEnableHook(vkAcquireNextImageKHR_proc, vkAcquireNextImageKHR_Detour,
                                 reinterpret_cast<LPVOID*>(&vkAcquireNextImageKHR_Original), "vkAcquireNextImageKHR")) {
            LogError("Failed to create and enable vkAcquireNextImageKHR hook");
            success = false;
        }
    } else {
        LogWarn("vkAcquireNextImageKHR not found in vulkan-1.dll");
    }

    if (vkQueueSubmit_proc) {
        if (!CreateAndEnableHook(vkQueueSubmit_proc, vkQueueSubmit_Detour,
                                 reinterpret_cast<LPVOID*>(&vkQueueSubmit_Original), "vkQueueSubmit")) {
            LogError("Failed to create and enable vkQueueSubmit hook");
            success = false;
        }
    } else {
        LogWarn("vkQueueSubmit not found in vulkan-1.dll");
    }

    if (vkQueueSubmit2_proc) {
        if (!CreateAndEnableHook(vkQueueSubmit2_proc, vkQueueSubmit2_Detour,
                                 reinterpret_cast<LPVOID*>(&vkQueueSubmit2_Original), "vkQueueSubmit2")) {
            LogError("Failed to create and enable vkQueueSubmit2 hook");
            success = false;
        }
    } else {
        LogWarn("vkQueueSubmit2 not found in vulkan-1.dll");
    }

    if (success) {
        g_vulkan_hooks_installed.store(true);
        LogInfo("Vulkan hooks installed successfully");

        // Mark Vulkan hooks as installed
        display_commanderhooks::HookSuppressionManager::GetInstance().MarkHookInstalled(
            display_commanderhooks::HookType::VULKAN);
    } else {
        LogError("Some Vulkan hooks failed to install");
    }

    return success;
}

bool AreVulkanHooksInstalled() { return g_vulkan_hooks_installed.load(); }
