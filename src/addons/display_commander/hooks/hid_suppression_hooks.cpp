#include "hid_suppression_hooks.hpp"
#include "../globals.hpp"
#include "../utils.hpp"
#include "../settings/experimental_tab_settings.hpp"
#include <MinHook.h>
#include <atomic>
#include <mutex>

namespace renodx::hooks {

// Original function pointers
ReadFile_pfn ReadFile_Original = nullptr;
HidD_GetInputReport_pfn HidD_GetInputReport_Original = nullptr;
HidD_GetAttributes_pfn HidD_GetAttributes_Original = nullptr;

// Hook state
static std::atomic<bool> g_hid_suppression_hooks_installed{false};
static std::mutex g_hid_suppression_mutex;

// DualSense device identifiers
constexpr USHORT SONY_VENDOR_ID = 0x054c;
constexpr USHORT DUALSENSE_PRODUCT_ID = 0x0ce6;
constexpr USHORT DUALSENSE_EDGE_PRODUCT_ID = 0x0df2;

bool IsDualSenseDevice(USHORT vendorId, USHORT productId) {
    return (vendorId == SONY_VENDOR_ID) &&
           (productId == DUALSENSE_PRODUCT_ID || productId == DUALSENSE_EDGE_PRODUCT_ID);
}

bool ShouldSuppressHIDInput() {
    return settings::g_experimentalTabSettings.hid_suppression_enabled.GetValue();
}

void SetHIDSuppressionEnabled(bool enabled) {
    std::lock_guard<std::mutex> lock(g_hid_suppression_mutex);
    settings::g_experimentalTabSettings.hid_suppression_enabled.SetValue(enabled);
    LogInfo("HID suppression %s", enabled ? "enabled" : "disabled");
}

bool IsHIDSuppressionEnabled() {
    return settings::g_experimentalTabSettings.hid_suppression_enabled.GetValue();
}

// Hooked ReadFile function - suppresses HID input reading for games
BOOL WINAPI ReadFile_Detour(HANDLE hFile, LPVOID lpBuffer, DWORD nNumberOfBytesToRead, LPDWORD lpNumberOfBytesRead, LPOVERLAPPED lpOverlapped) {
    // Check if HID suppression is enabled and ReadFile blocking is enabled
    if (ShouldSuppressHIDInput() && settings::g_experimentalTabSettings.hid_suppression_block_readfile.GetValue()) {
        // Check if this looks like a HID device read operation
        // HID input reports are typically small (1-78 bytes for DualSense)
        if (nNumberOfBytesToRead > 0 && nNumberOfBytesToRead <= 100) {
            // Get file type to determine if this is a HID device
            DWORD fileType = GetFileType(hFile);
            if (fileType == FILE_TYPE_UNKNOWN) {
                // This could be a HID device, suppress the read
                if (lpNumberOfBytesRead) {
                    *lpNumberOfBytesRead = 0;
                }
                SetLastError(ERROR_DEVICE_NOT_CONNECTED);
                LogInfo("HID suppression: Blocked ReadFile operation on potential HID device");
                return FALSE;
            }
        }
    }

    // Call original function
    return ReadFile_Original ?
        ReadFile_Original(hFile, lpBuffer, nNumberOfBytesToRead, lpNumberOfBytesRead, lpOverlapped) :
        ReadFile(hFile, lpBuffer, nNumberOfBytesToRead, lpNumberOfBytesRead, lpOverlapped);
}

// Hooked HidD_GetInputReport function - suppresses HID input report reading
BOOLEAN __stdcall HidD_GetInputReport_Detour(HANDLE HidDeviceObject, PVOID ReportBuffer, ULONG ReportBufferLength) {
    // Check if HID suppression is enabled and GetInputReport blocking is enabled
    if (ShouldSuppressHIDInput() && settings::g_experimentalTabSettings.hid_suppression_block_getinputreport.GetValue()) {
        // Suppress input report reading for games
        if (ReportBuffer) {
            memset(ReportBuffer, 0, ReportBufferLength);
        }
        LogInfo("HID suppression: Blocked HidD_GetInputReport operation");
        return FALSE;
    }

    // Call original function
    return HidD_GetInputReport_Original ?
        HidD_GetInputReport_Original(HidDeviceObject, ReportBuffer, ReportBufferLength) :
        HidD_GetInputReport(HidDeviceObject, ReportBuffer, ReportBufferLength);
}

// Hooked HidD_GetAttributes function - returns error when detecting DualSense
BOOLEAN __stdcall HidD_GetAttributes_Detour(HANDLE HidDeviceObject, PHIDD_ATTRIBUTES Attributes) {
    // Call original function first to get the actual attributes
    BOOLEAN result = HidD_GetAttributes_Original ?
        HidD_GetAttributes_Original(HidDeviceObject, Attributes) :
        HidD_GetAttributes(HidDeviceObject, Attributes);

    // Check if HID suppression is enabled and GetAttributes blocking is enabled
    if (ShouldSuppressHIDInput() && settings::g_experimentalTabSettings.hid_suppression_block_getattributes.GetValue() && result && Attributes) {
        // Check if we should only block DualSense devices or all HID devices
        bool shouldBlock = false;
        if (settings::g_experimentalTabSettings.hid_suppression_dualsense_only.GetValue()) {
            // Only block DualSense devices
            shouldBlock = IsDualSenseDevice(Attributes->VendorID, Attributes->ProductID);
        } else {
            // Block all HID devices
            shouldBlock = true;
        }

        if (shouldBlock) {
            LogInfo("HID suppression: Detected %s device (VID:0x%04X PID:0x%04X), returning error",
                   settings::g_experimentalTabSettings.hid_suppression_dualsense_only.GetValue() ? "DualSense" : "HID",
                   Attributes->VendorID, Attributes->ProductID);
            return FALSE; // Return error to prevent game from detecting the device
        }
    }

    return result;
}

bool InstallHIDSuppressionHooks() {
    if (g_hid_suppression_hooks_installed.load()) {
        LogInfo("HID suppression hooks already installed");
        return true;
    }

    // Initialize MinHook (only if not already initialized)
    MH_STATUS init_status = MH_Initialize();
    if (init_status != MH_OK && init_status != MH_ERROR_ALREADY_INITIALIZED) {
        LogError("Failed to initialize MinHook for HID suppression hooks - Status: %d", init_status);
        return false;
    }

    if (init_status == MH_ERROR_ALREADY_INITIALIZED) {
        LogInfo("MinHook already initialized, proceeding with HID suppression hooks");
    } else {
        LogInfo("MinHook initialized successfully for HID suppression hooks");
    }

    // Hook ReadFile
    if (MH_CreateHook(ReadFile, ReadFile_Detour, (LPVOID*)&ReadFile_Original) != MH_OK) {
        LogError("Failed to create ReadFile hook for HID suppression");
        return false;
    }

    // Hook HidD_GetInputReport
    if (MH_CreateHookApi(L"hid.dll", "HidD_GetInputReport", HidD_GetInputReport_Detour, (LPVOID*)&HidD_GetInputReport_Original) != MH_OK) {
        LogError("Failed to create HidD_GetInputReport hook for HID suppression");
        return false;
    }

    // Hook HidD_GetAttributes
    if (MH_CreateHookApi(L"hid.dll", "HidD_GetAttributes", HidD_GetAttributes_Detour, (LPVOID*)&HidD_GetAttributes_Original) != MH_OK) {
        LogError("Failed to create HidD_GetAttributes hook for HID suppression");
        return false;
    }

    // Enable individual hooks
    if (MH_EnableHook(ReadFile) != MH_OK) {
        LogError("Failed to enable ReadFile hook for HID suppression");
        return false;
    }

    if (MH_EnableHook(HidD_GetInputReport) != MH_OK) {
        LogError("Failed to enable HidD_GetInputReport hook for HID suppression");
        return false;
    }

    if (MH_EnableHook(HidD_GetAttributes) != MH_OK) {
        LogError("Failed to enable HidD_GetAttributes hook for HID suppression");
        return false;
    }

    g_hid_suppression_hooks_installed.store(true);
    LogInfo("HID suppression hooks installed successfully");

    return true;
}

void UninstallHIDSuppressionHooks() {
    if (!g_hid_suppression_hooks_installed.load()) {
        LogInfo("HID suppression hooks not installed");
        return;
    }

    // Disable individual hooks
    MH_DisableHook(ReadFile);
    MH_DisableHook(HidD_GetInputReport);
    MH_DisableHook(HidD_GetAttributes);

    // Remove individual hooks
    MH_RemoveHook(ReadFile);
    MH_RemoveHook(HidD_GetInputReport);
    MH_RemoveHook(HidD_GetAttributes);

    // Clean up
    ReadFile_Original = nullptr;
    HidD_GetInputReport_Original = nullptr;
    HidD_GetAttributes_Original = nullptr;

    g_hid_suppression_hooks_installed.store(false);
    LogInfo("HID suppression hooks uninstalled successfully");
}

bool AreHIDSuppressionHooksInstalled() {
    return g_hid_suppression_hooks_installed.load();
}

} // namespace renodx::hooks

