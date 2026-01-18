#pragma once

#include <windows.h>
#include <vulkan/vulkan.h>

// Vulkan function pointer types
typedef VkResult(VKAPI_PTR* PFN_vkCreateInstance)(const VkInstanceCreateInfo* pCreateInfo,
                                                    const VkAllocationCallbacks* pAllocator,
                                                    VkInstance* pInstance);
typedef VkResult(VKAPI_PTR* PFN_vkCreateDevice)(VkPhysicalDevice physicalDevice,
                                                 const VkDeviceCreateInfo* pCreateInfo,
                                                 const VkAllocationCallbacks* pAllocator,
                                                 VkDevice* pDevice);
typedef VkResult(VKAPI_PTR* PFN_vkCreateSwapchainKHR)(VkDevice device,
                                                        const VkSwapchainCreateInfoKHR* pCreateInfo,
                                                        const VkAllocationCallbacks* pAllocator,
                                                        VkSwapchainKHR* pSwapchain);
typedef VkResult(VKAPI_PTR* PFN_vkQueuePresentKHR)(VkQueue queue, const VkPresentInfoKHR* pPresentInfo);
typedef VkResult(VKAPI_PTR* PFN_vkAcquireNextImageKHR)(VkDevice device,
                                                         VkSwapchainKHR swapchain,
                                                         uint64_t timeout,
                                                         VkSemaphore semaphore,
                                                         VkFence fence,
                                                         uint32_t* pImageIndex);
typedef VkResult(VKAPI_PTR* PFN_vkQueueSubmit)(VkQueue queue,
                                                uint32_t submitCount,
                                                const VkSubmitInfo* pSubmits,
                                                VkFence fence);
typedef VkResult(VKAPI_PTR* PFN_vkQueueSubmit2)(VkQueue queue,
                                                 uint32_t submitCount,
                                                 const VkSubmitInfo2* pSubmits,
                                                 VkFence fence);

// Original function pointers
extern PFN_vkCreateInstance vkCreateInstance_Original;
extern PFN_vkCreateDevice vkCreateDevice_Original;
extern PFN_vkCreateSwapchainKHR vkCreateSwapchainKHR_Original;
extern PFN_vkQueuePresentKHR vkQueuePresentKHR_Original;
extern PFN_vkAcquireNextImageKHR vkAcquireNextImageKHR_Original;
extern PFN_vkQueueSubmit vkQueueSubmit_Original;
extern PFN_vkQueueSubmit2 vkQueueSubmit2_Original;

// Hook installation function
bool InstallVulkanHooks(HMODULE vulkan_module);

// Check if Vulkan hooks are installed
bool AreVulkanHooksInstalled();
