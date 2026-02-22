#include "hid_suppression_hooks.hpp"
#include "hid_statistics.hpp"
#include "../globals.hpp"
#include "../utils.hpp"
#include "../utils/general_utils.hpp"
#include "../utils/logging.hpp"
#include "../utils/srwlock_wrapper.hpp"
#include "../settings/experimental_tab_settings.hpp"
#include "../widgets/xinput_widget/xinput_widget.hpp"
#include "hook_suppression_manager.hpp"
#include <MinHook.h>
#include <atomic>
#include <algorithm>
#include <string>

namespace renodx::hooks {

// Original function pointers
ReadFile_pfn ReadFile_Original = nullptr;
HidD_GetInputReport_pfn HidD_GetInputReport_Original = nullptr;
HidD_GetAttributes_pfn HidD_GetAttributes_Original = nullptr;
CreateFileA_pfn CreateFileA_Original = nullptr;
CreateFileW_pfn CreateFileW_Original = nullptr;

// Hook state
static std::atomic<bool> g_hid_suppression_hooks_installed{false};

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
    utils::SRWLockExclusive lock(utils::g_hid_suppression_mutex);
    settings::g_experimentalTabSettings.hid_suppression_enabled.SetValue(enabled);
    LogInfo("HID suppression %s", enabled ? "enabled" : "disabled");
}

bool IsHIDSuppressionEnabled() {
    return settings::g_experimentalTabSettings.hid_suppression_enabled.GetValue();
}

BOOL WINAPI ReadFile_Direct(HANDLE hFile, LPVOID lpBuffer, DWORD nNumberOfBytesToRead, LPDWORD lpNumberOfBytesRead, LPOVERLAPPED lpOverlapped) {
    // Call original function
    return ReadFile_Original ?
        ReadFile_Original(hFile, lpBuffer, nNumberOfBytesToRead, lpNumberOfBytesRead, lpOverlapped) :
        ReadFile(hFile, lpBuffer, nNumberOfBytesToRead, lpNumberOfBytesRead, lpOverlapped);
}


// Hooked ReadFile function - suppresses HID input reading for games
BOOL WINAPI ReadFile_Detour(HANDLE hFile, LPVOID lpBuffer, DWORD nNumberOfBytesToRead, LPDWORD lpNumberOfBytesRead, LPOVERLAPPED lpOverlapped) {
    // Increment HID statistics
    auto& stats = display_commanderhooks::g_hid_api_stats[display_commanderhooks::HID_READFILE];
    stats.increment_total();

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
                stats.increment_blocked();
                LogInfo("HID suppression: Blocked ReadFile operation on potential HID device");
                return FALSE;
            }
        }
    }

    // Call original function
    BOOL result = ReadFile_Original ?
        ReadFile_Original(hFile, lpBuffer, nNumberOfBytesToRead, lpNumberOfBytesRead, lpOverlapped) :
        ReadFile(hFile, lpBuffer, nNumberOfBytesToRead, lpNumberOfBytesRead, lpOverlapped);

    // Update statistics based on result
    if (result) {
        stats.increment_successful();
    } else {
        stats.increment_failed();
    }

    return result;
}


BOOLEAN __stdcall HidD_GetInputReport_Direct(HANDLE HidDeviceObject, PVOID ReportBuffer, ULONG ReportBufferLength) {
    return HidD_GetInputReport_Original ?
        HidD_GetInputReport_Original(HidDeviceObject, ReportBuffer, ReportBufferLength) :
        HidD_GetInputReport(HidDeviceObject, ReportBuffer, ReportBufferLength);
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

BOOLEAN __stdcall HidD_GetAttributes_Direct(HANDLE HidDeviceObject, PHIDD_ATTRIBUTES Attributes) {
    return HidD_GetAttributes_Original ?
        HidD_GetAttributes_Original(HidDeviceObject, Attributes) :
        HidD_GetAttributes(HidDeviceObject, Attributes);
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

// Helper function to check if a path is a HID device path
bool IsHIDDevicePath(const std::wstring& path) {
    // Convert to lowercase for case-insensitive comparison
    std::wstring lowerPath = path;
    std::transform(lowerPath.begin(), lowerPath.end(), lowerPath.begin(), ::towlower);

    // Check for HID device path patterns
    return lowerPath.find(L"\\hid") != std::wstring::npos;
}

bool IsHIDDevicePath(const std::string& path) {
    // Convert to lowercase for case-insensitive comparison
    std::string lowerPath = path;
    std::transform(lowerPath.begin(), lowerPath.end(), lowerPath.begin(), ::tolower);

    // Check for HID device path patterns
    return lowerPath.find("\\hid") != std::string::npos;
}

bool IsDualSenseDevicePath(const std::string& path) {
    // Convert to lowercase for case-insensitive comparison
    std::string lowerPath = path;
    std::transform(lowerPath.begin(), lowerPath.end(), lowerPath.begin(), ::tolower);

    // Check for DualSense device path patterns
    // Look for Sony vendor ID (054c) and DualSense product IDs (0ce6, 0df2)
    return (lowerPath.find("vid_054c") != std::string::npos &&
            (lowerPath.find("pid_0ce6") != std::string::npos ||  // DualSense Controller (Regular)
             lowerPath.find("pid_0df2") != std::string::npos));   // DualSense Edge Controller
}

bool IsDualSenseDevicePath(const std::wstring& path) {
    // Convert to lowercase for case-insensitive comparison
    std::wstring lowerPath = path;
    std::transform(lowerPath.begin(), lowerPath.end(), lowerPath.begin(), ::towlower);

    // Check for DualSense device path patterns
    // Look for Sony vendor ID (054c) and DualSense product IDs (0ce6, 0df2)
    return (lowerPath.find(L"vid_054c") != std::wstring::npos &&
            (lowerPath.find(L"pid_0ce6") != std::wstring::npos ||  // DualSense Controller (Regular)
             lowerPath.find(L"pid_0df2") != std::wstring::npos));   // DualSense Edge Controller
}

// Direct CreateFileA function (calls original)
HANDLE WINAPI CreateFileA_Direct(LPCSTR lpFileName, DWORD dwDesiredAccess, DWORD dwShareMode, LPSECURITY_ATTRIBUTES lpSecurityAttributes, DWORD dwCreationDisposition, DWORD dwFlagsAndAttributes, HANDLE hTemplateFile) {
    return CreateFileA_Original ?
        CreateFileA_Original(lpFileName, dwDesiredAccess, dwShareMode, lpSecurityAttributes, dwCreationDisposition, dwFlagsAndAttributes, hTemplateFile) :
        CreateFileA(lpFileName, dwDesiredAccess, dwShareMode, lpSecurityAttributes, dwCreationDisposition, dwFlagsAndAttributes, hTemplateFile);
}

// Hooked CreateFileA function - blocks HID device access
HANDLE WINAPI CreateFileA_Detour(LPCSTR lpFileName, DWORD dwDesiredAccess, DWORD dwShareMode, LPSECURITY_ATTRIBUTES lpSecurityAttributes, DWORD dwCreationDisposition, DWORD dwFlagsAndAttributes, HANDLE hTemplateFile) {
    // Increment HID statistics
    auto& stats = display_commanderhooks::g_hid_api_stats[display_commanderhooks::HID_CREATEFILE_A];
    stats.increment_total();

    // Check if this is a HID device access and increment counters
    if (lpFileName && IsHIDDevicePath(std::string(lpFileName))) {
        // Update device type statistics
        auto& device_stats = display_commanderhooks::g_hid_device_stats;
        device_stats.increment_total();

        if (display_commanderhooks::IsDualSenseDevice(std::string(lpFileName))) {
            device_stats.increment_dualsense();
            LogInfo("HID CreateFile: DualSense device access detected: %s", lpFileName);
        } else if (display_commanderhooks::IsXboxDevice(std::string(lpFileName))) {
            device_stats.increment_xbox();
        } else if (display_commanderhooks::IsHIDDevice(std::string(lpFileName))) {
            device_stats.increment_generic();
        } else {
            device_stats.increment_unknown();
        }

        // Legacy counter for backward compatibility
        auto shared_state = display_commander::widgets::xinput_widget::XInputWidget::GetSharedState();
        if (shared_state) {
            shared_state->hid_createfile_total.fetch_add(1);
            if (IsDualSenseDevicePath(std::string(lpFileName))) {
                shared_state->hid_createfile_dualsense.fetch_add(1);
            }
        }

        LogInfo("HID suppression: CreateFileA access to HID device: %s", lpFileName);
    }

    // Check if HID suppression is enabled and CreateFile blocking is enabled
    if (ShouldSuppressHIDInput() && settings::g_experimentalTabSettings.hid_suppression_block_createfile.GetValue()) {
        if (lpFileName && IsHIDDevicePath(std::string(lpFileName))) {
            LogInfo("HID suppression: Blocked CreateFileA access to HID device: %s", lpFileName);
            stats.increment_blocked();
            SetLastError(ERROR_ACCESS_DENIED);
            return INVALID_HANDLE_VALUE;
        }
    }

    // Call original function
    HANDLE result = CreateFileA_Original ?
        CreateFileA_Original(lpFileName, dwDesiredAccess, dwShareMode, lpSecurityAttributes, dwCreationDisposition, dwFlagsAndAttributes, hTemplateFile) :
        CreateFileA(lpFileName, dwDesiredAccess, dwShareMode, lpSecurityAttributes, dwCreationDisposition, dwFlagsAndAttributes, hTemplateFile);

    // Update statistics based on result
    if (result != INVALID_HANDLE_VALUE) {
        stats.increment_successful();
    } else {
        stats.increment_failed();
    }

    return result;
}

// Direct CreateFileW function (calls original)
HANDLE WINAPI CreateFileW_Direct(LPCWSTR lpFileName, DWORD dwDesiredAccess, DWORD dwShareMode, LPSECURITY_ATTRIBUTES lpSecurityAttributes, DWORD dwCreationDisposition, DWORD dwFlagsAndAttributes, HANDLE hTemplateFile) {
    return CreateFileW_Original ?
        CreateFileW_Original(lpFileName, dwDesiredAccess, dwShareMode, lpSecurityAttributes, dwCreationDisposition, dwFlagsAndAttributes, hTemplateFile) :
        CreateFileW(lpFileName, dwDesiredAccess, dwShareMode, lpSecurityAttributes, dwCreationDisposition, dwFlagsAndAttributes, hTemplateFile);
}

// Hooked CreateFileW function - blocks HID device access
HANDLE WINAPI CreateFileW_Detour(LPCWSTR lpFileName, DWORD dwDesiredAccess, DWORD dwShareMode, LPSECURITY_ATTRIBUTES lpSecurityAttributes, DWORD dwCreationDisposition, DWORD dwFlagsAndAttributes, HANDLE hTemplateFile) {
    // Increment HID statistics
    auto& stats = display_commanderhooks::g_hid_api_stats[display_commanderhooks::HID_CREATEFILE_W];
    stats.increment_total();

    // Check if this is a HID device access and increment counters
    if (lpFileName && IsHIDDevicePath(std::wstring(lpFileName))) {
        // Update device type statistics
        auto& device_stats = display_commanderhooks::g_hid_device_stats;
        device_stats.increment_total();

        if (display_commanderhooks::IsDualSenseDevice(std::wstring(lpFileName))) {
            device_stats.increment_dualsense();
            LogInfo("HID CreateFile: DualSense device access detected: %ls", lpFileName);
        } else if (display_commanderhooks::IsXboxDevice(std::wstring(lpFileName))) {
            device_stats.increment_xbox();
        } else if (display_commanderhooks::IsHIDDevice(std::wstring(lpFileName))) {
            device_stats.increment_generic();
        } else {
            device_stats.increment_unknown();
        }

        // Legacy counter for backward compatibility
        auto shared_state = display_commander::widgets::xinput_widget::XInputWidget::GetSharedState();
        if (shared_state) {
            shared_state->hid_createfile_total.fetch_add(1);
            if (IsDualSenseDevicePath(std::wstring(lpFileName))) {
                shared_state->hid_createfile_dualsense.fetch_add(1);
            }
        }

        LogInfo("HID suppression: CreateFileW access to HID device: %ls", lpFileName);
    }

    // Check if HID suppression is enabled and CreateFile blocking is enabled
    if (ShouldSuppressHIDInput() && settings::g_experimentalTabSettings.hid_suppression_block_createfile.GetValue()) {
        if (lpFileName && IsHIDDevicePath(std::wstring(lpFileName))) {
            LogInfo("HID suppression: Blocked CreateFileW access to HID device: %ls", lpFileName);
            stats.increment_blocked();
            SetLastError(ERROR_ACCESS_DENIED);
            return INVALID_HANDLE_VALUE;
        }
    }

    // Call original function
    HANDLE result = CreateFileW_Original ?
        CreateFileW_Original(lpFileName, dwDesiredAccess, dwShareMode, lpSecurityAttributes, dwCreationDisposition, dwFlagsAndAttributes, hTemplateFile) :
        CreateFileW(lpFileName, dwDesiredAccess, dwShareMode, lpSecurityAttributes, dwCreationDisposition, dwFlagsAndAttributes, hTemplateFile);

    // Update statistics based on result
    if (result != INVALID_HANDLE_VALUE) {
        stats.increment_successful();
    } else {
        stats.increment_failed();
    }

    return result;
}

bool InstallHIDSuppressionHooks() {
    if (g_hid_suppression_hooks_installed.load()) {
        LogInfo("HID suppression hooks already installed");
        return true;
    }

    // Check if HID suppression hooks should be suppressed
    if (display_commanderhooks::HookSuppressionManager::GetInstance().ShouldSuppressHook(display_commanderhooks::HookType::HID_SUPPRESSION)) {
        LogInfo("HID suppression hooks installation suppressed by user setting");
        return false;
    }

    // Initialize MinHook (only if not already initialized)
    MH_STATUS init_status = SafeInitializeMinHook(display_commanderhooks::HookType::HID_SUPPRESSION);
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
    if (!CreateAndEnableHook(ReadFile, ReadFile_Detour, (LPVOID*)&ReadFile_Original, "ReadFile")) {
        LogError("Failed to create and enable ReadFile hook for HID suppression");
        return false;
    }

    // Hook HidD_GetInputReport
//    if (MH_CreateHookApi(L"hid.dll", "HidD_GetInputReport", HidD_GetInputReport_Detour, (LPVOID*)&HidD_GetInputReport_Original) != MH_OK) {
 //       LogError("Failed to create HidD_GetInputReport hook for HID suppression");
  //      return false;
  //  }

    // Hook HidD_GetAttributes
 //   if (MH_CreateHookApi(L"hid.dll", "HidD_GetAttributes", HidD_GetAttributes_Detour, (LPVOID*)&HidD_GetAttributes_Original) != MH_OK) {
 //       LogError("Failed to create HidD_GetAttributes hook for HID suppression");
 //       return false;
 //   }

    // Hook CreateFileA
    if (!CreateAndEnableHook(CreateFileA, CreateFileA_Detour, (LPVOID*)&CreateFileA_Original, "CreateFileA")) {
        LogError("Failed to create and enable CreateFileA hook for HID suppression");
        return false;
    }

    // Hook CreateFileW
    if (!CreateAndEnableHook(CreateFileW, CreateFileW_Detour, (LPVOID*)&CreateFileW_Original, "CreateFileW")) {
        LogError("Failed to create and enable CreateFileW hook for HID suppression");
        return false;
    }

    // Hooks are already enabled by CreateAndEnableHook

    g_hid_suppression_hooks_installed.store(true);
    LogInfo("HID suppression hooks installed successfully");

    // Mark HID suppression hooks as installed
    display_commanderhooks::HookSuppressionManager::GetInstance().MarkHookInstalled(display_commanderhooks::HookType::HID_SUPPRESSION);

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
    MH_DisableHook(CreateFileA);
    MH_DisableHook(CreateFileW);

    // Remove individual hooks
    MH_RemoveHook(ReadFile);
    MH_RemoveHook(HidD_GetInputReport);
    MH_RemoveHook(HidD_GetAttributes);
    MH_RemoveHook(CreateFileA);
    MH_RemoveHook(CreateFileW);

    // Clean up
    ReadFile_Original = nullptr;
    HidD_GetInputReport_Original = nullptr;
    HidD_GetAttributes_Original = nullptr;
    CreateFileA_Original = nullptr;
    CreateFileW_Original = nullptr;

    g_hid_suppression_hooks_installed.store(false);
    LogInfo("HID suppression hooks uninstalled successfully");
}

bool AreHIDSuppressionHooksInstalled() {
    return g_hid_suppression_hooks_installed.load();
}

} // namespace renodx::hooks

