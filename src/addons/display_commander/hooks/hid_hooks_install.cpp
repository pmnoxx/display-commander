#include "hid_hooks_install.hpp"
#include <MinHook.h>
#include <atomic>
#include "../utils/general_utils.hpp"
#include "../utils/logging.hpp"
#include "hid_additional_hooks.hpp"
#include "hid_suppression_hooks.hpp"
#include "hook_suppression_manager.hpp"

namespace display_commanderhooks {

namespace {

static std::atomic<bool> g_hid_kernel32_hooks_installed{false};
static std::atomic<bool> g_hid_d_hooks_installed{false};

}  // namespace

bool InstallHIDKernel32Hooks(HMODULE hModule) {
    if (hModule == nullptr) {
        return false;
    }
    if (g_hid_kernel32_hooks_installed.load()) {
        LogInfo("HID kernel32 hooks already installed");
        return true;
    }

    // const bool suppress_suppression =
    //     HookSuppressionManager::GetInstance().ShouldSuppressHook(HookType::HID_SUPPRESSION);
    const bool suppress_hid = HookSuppressionManager::GetInstance().ShouldSuppressHook(HookType::HID_KERNEL32);

    MH_STATUS init_status = SafeInitializeMinHook(HookType::HID_SUPPRESSION);
    if (init_status != MH_OK && init_status != MH_ERROR_ALREADY_INITIALIZED) {
        LogError("Failed to initialize MinHook for HID kernel32 hooks - Status: %d", init_status);
        return false;
    }

    int installed = 0;

    {
        if (CreateAndEnableHookFromModule(hModule, "ReadFile", reinterpret_cast<LPVOID>(renodx::hooks::ReadFile_Detour),
                                          reinterpret_cast<LPVOID*>(&renodx::hooks::ReadFile_Original), "ReadFile")) {
            installed++;
        } else {
            LogError("Failed to install ReadFile hook (kernel32)");
            return false;
        }
        if (CreateAndEnableHookFromModule(
                hModule, "ReadFileEx", reinterpret_cast<LPVOID>(renodx::hooks::ReadFileEx_Detour),
                reinterpret_cast<LPVOID*>(&renodx::hooks::ReadFileEx_Original), "ReadFileEx")) {
            installed++;
        } else {
            LogWarn("Failed to install ReadFileEx hook (kernel32), continuing");
        }
        if (CreateAndEnableHookFromModule(
                hModule, "ReadFileScatter", reinterpret_cast<LPVOID>(renodx::hooks::ReadFileScatter_Detour),
                reinterpret_cast<LPVOID*>(&renodx::hooks::ReadFileScatter_Original), "ReadFileScatter")) {
            installed++;
        } else {
            LogWarn("Failed to install ReadFileScatter hook (kernel32), continuing");
        }
        if (CreateAndEnableHookFromModule(
                hModule, "CreateFileA", reinterpret_cast<LPVOID>(renodx::hooks::CreateFileA_Detour),
                reinterpret_cast<LPVOID*>(&renodx::hooks::CreateFileA_Original), "CreateFileA")) {
            installed++;
        } else {
            LogError("Failed to install CreateFileA hook (kernel32)");
            return false;
        }
        if (CreateAndEnableHookFromModule(
                hModule, "CreateFileW", reinterpret_cast<LPVOID>(renodx::hooks::CreateFileW_Detour),
                reinterpret_cast<LPVOID*>(&renodx::hooks::CreateFileW_Original), "CreateFileW")) {
            installed++;
        } else {
            LogError("Failed to install CreateFileW hook (kernel32)");
            return false;
        }
    }

    if (!suppress_hid) {
        if (CreateAndEnableHookFromModule(hModule, "WriteFile", reinterpret_cast<LPVOID>(WriteFile_Detour),
                                          reinterpret_cast<LPVOID*>(&WriteFile_Original), "WriteFile")) {
            installed++;
        } else {
            LogWarn("Failed to install WriteFile hook (kernel32), continuing");
        }
        if (CreateAndEnableHookFromModule(
                hModule, "WriteFileEx", reinterpret_cast<LPVOID>(WriteFileEx_Detour),
                reinterpret_cast<LPVOID*>(&WriteFileEx_Original), "WriteFileEx")) {
            installed++;
        } else {
            LogWarn("Failed to install WriteFileEx hook (kernel32), continuing");
        }
        if (CreateAndEnableHookFromModule(hModule, "DeviceIoControl", reinterpret_cast<LPVOID>(DeviceIoControl_Detour),
                                          reinterpret_cast<LPVOID*>(&DeviceIoControl_Original), "DeviceIoControl")) {
            installed++;
        } else {
            LogWarn("Failed to install DeviceIoControl hook (kernel32), continuing");
        }
    }

    if (installed > 0) {
        g_hid_kernel32_hooks_installed.store(true);
        //    if (!suppress_suppression)
        {
            HookSuppressionManager::GetInstance().MarkHookInstalled(HookType::HID_SUPPRESSION);
            renodx::hooks::MarkHIDSuppressionHooksInstalled(true);
        }
        if (!suppress_hid && (WriteFile_Original != nullptr || WriteFileEx_Original != nullptr || DeviceIoControl_Original != nullptr)) {
            HookSuppressionManager::GetInstance().MarkHookInstalled(HookType::HID_KERNEL32);
            MarkAdditionalHIDHooksInstalled(true);
        }
        LogInfo("HID kernel32 hooks installed: %d hooks", installed);
        return true;
    }
    return false;
}

bool InstallHIDDHooks(HMODULE hModule) {
    if (hModule == nullptr) {
        return false;
    }
    if (g_hid_d_hooks_installed.load()) {
        LogInfo("HID (hid.dll) hooks already installed");
        return true;
    }

    const bool suppress_hid = HookSuppressionManager::GetInstance().ShouldSuppressHook(HookType::HID_HID_DLL);

    MH_STATUS init_status = SafeInitializeMinHook(HookType::HID_HID_DLL);
    if (init_status != MH_OK && init_status != MH_ERROR_ALREADY_INITIALIZED) {
        LogError("Failed to initialize MinHook for HID (hid.dll) hooks - Status: %d", init_status);
        return false;
    }

    int installed = 0;

    if (CreateAndEnableHookFromModule(
            hModule, "HidD_GetInputReport", reinterpret_cast<LPVOID>(renodx::hooks::HidD_GetInputReport_Detour),
            reinterpret_cast<LPVOID*>(&renodx::hooks::HidD_GetInputReport_Original), "HidD_GetInputReport")) {
        installed++;
    } else {
        LogWarn("Failed to install HidD_GetInputReport hook (hid.dll), continuing");
    }
    if (CreateAndEnableHookFromModule(
            hModule, "HidD_GetAttributes", reinterpret_cast<LPVOID>(renodx::hooks::HidD_GetAttributes_Detour),
            reinterpret_cast<LPVOID*>(&renodx::hooks::HidD_GetAttributes_Original), "HidD_GetAttributes")) {
        installed++;
    } else {
        LogWarn("Failed to install HidD_GetAttributes hook (hid.dll), continuing");
    }

    if (!suppress_hid) {
        const char* hid_procs[] = {"HidD_GetPreparsedData",
                                   "HidD_FreePreparsedData",
                                   "HidP_GetCaps",
                                   "HidD_GetManufacturerString",
                                   "HidD_GetProductString",
                                   "HidD_GetSerialNumberString",
                                   "HidD_GetNumInputBuffers",
                                   "HidD_SetNumInputBuffers",
                                   "HidD_GetFeature",
                                   "HidD_SetFeature"};
        using detour_t = LPVOID;
        detour_t detours[] = {
            reinterpret_cast<LPVOID>(HidD_GetPreparsedData_Detour),
            reinterpret_cast<LPVOID>(HidD_FreePreparsedData_Detour),
            reinterpret_cast<LPVOID>(HidP_GetCaps_Detour),
            reinterpret_cast<LPVOID>(HidD_GetManufacturerString_Detour),
            reinterpret_cast<LPVOID>(HidD_GetProductString_Detour),
            reinterpret_cast<LPVOID>(HidD_GetSerialNumberString_Detour),
            reinterpret_cast<LPVOID>(HidD_GetNumInputBuffers_Detour),
            reinterpret_cast<LPVOID>(HidD_SetNumInputBuffers_Detour),
            reinterpret_cast<LPVOID>(HidD_GetFeature_Detour),
            reinterpret_cast<LPVOID>(HidD_SetFeature_Detour),
        };
        LPVOID* originals[] = {
            reinterpret_cast<LPVOID*>(&HidD_GetPreparsedData_Original),
            reinterpret_cast<LPVOID*>(&HidD_FreePreparsedData_Original),
            reinterpret_cast<LPVOID*>(&HidP_GetCaps_Original),
            reinterpret_cast<LPVOID*>(&HidD_GetManufacturerString_Original),
            reinterpret_cast<LPVOID*>(&HidD_GetProductString_Original),
            reinterpret_cast<LPVOID*>(&HidD_GetSerialNumberString_Original),
            reinterpret_cast<LPVOID*>(&HidD_GetNumInputBuffers_Original),
            reinterpret_cast<LPVOID*>(&HidD_SetNumInputBuffers_Original),
            reinterpret_cast<LPVOID*>(&HidD_GetFeature_Original),
            reinterpret_cast<LPVOID*>(&HidD_SetFeature_Original),
        };
        constexpr size_t n = sizeof(hid_procs) / sizeof(hid_procs[0]);
        for (size_t i = 0; i < n; ++i) {
            if (CreateAndEnableHookFromModule(hModule, hid_procs[i], detours[i], originals[i], hid_procs[i])) {
                installed++;
            } else {
                LogWarn("Failed to install %s hook (hid.dll), continuing", hid_procs[i]);
            }
        }
    }

    if (installed > 0) {
        g_hid_d_hooks_installed.store(true);
        if ((renodx::hooks::HidD_GetInputReport_Original != nullptr
             || renodx::hooks::HidD_GetAttributes_Original != nullptr)) {
            HookSuppressionManager::GetInstance().MarkHookInstalled(HookType::HID_SUPPRESSION);
            renodx::hooks::MarkHIDSuppressionHooksInstalled(true);
        }
        if (!suppress_hid) {
            HookSuppressionManager::GetInstance().MarkHookInstalled(HookType::HID_HID_DLL);
            MarkAdditionalHIDHooksInstalled(true);
        }
        LogInfo("HID (hid.dll) hooks installed: %d hooks", installed);
        return true;
    }
    return false;
}

}  // namespace display_commanderhooks
