#include "hid_suppression_hooks.hpp"
#include <MinHook.h>
#include <algorithm>
#include <atomic>
#include <string>
#include "../../globals.hpp"
#include "../../settings/experimental_tab_settings.hpp"
#include "../../utils/detour_call_tracker.hpp"
#include "../../utils/logging.hpp"
#include "../../utils/srwlock_registry.hpp"
#include "../../utils/srwlock_wrapper.hpp"
#include "../../widgets/xinput_widget/xinput_widget.hpp"
#include "hid_statistics.hpp"
#include "../windows_hooks/windows_message_hooks.hpp"

namespace renodx::hooks {

// Original function pointers
ReadFile_pfn ReadFile_Original = nullptr;
ReadFileEx_pfn ReadFileEx_Original = nullptr;
ReadFileScatter_pfn ReadFileScatter_Original = nullptr;
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
    return (vendorId == SONY_VENDOR_ID)
           && (productId == DUALSENSE_PRODUCT_ID || productId == DUALSENSE_EDGE_PRODUCT_ID);
}

bool ShouldSuppressHIDInput() { return settings::g_experimentalTabSettings.hid_suppression_enabled.GetValue(); }

void SetHIDSuppressionEnabled(bool enabled) {
    utils::SRWLockExclusive lock(utils::g_hid_suppression_mutex);
    settings::g_experimentalTabSettings.hid_suppression_enabled.SetValue(enabled);
    LogInfo("HID suppression %s", enabled ? "enabled" : "disabled");
}

BOOL WINAPI ReadFile_Direct(HANDLE hFile, LPVOID lpBuffer, DWORD nNumberOfBytesToRead, LPDWORD lpNumberOfBytesRead,
                            LPOVERLAPPED lpOverlapped) {
    // Call original function
    return ReadFile_Original
               ? ReadFile_Original(hFile, lpBuffer, nNumberOfBytesToRead, lpNumberOfBytesRead, lpOverlapped)
               : ReadFile(hFile, lpBuffer, nNumberOfBytesToRead, lpNumberOfBytesRead, lpOverlapped);
}

static bool LooksLikeHIDRead(HANDLE hFile, DWORD nNumberOfBytesToRead) {
    return nNumberOfBytesToRead > 0 && nNumberOfBytesToRead <= 100 && GetFileType(hFile) == FILE_TYPE_UNKNOWN;
}

// Hooked ReadFile function - suppresses HID input reading for games
BOOL WINAPI ReadFile_Detour(HANDLE hFile, LPVOID lpBuffer, DWORD nNumberOfBytesToRead, LPDWORD lpNumberOfBytesRead,
                            LPOVERLAPPED lpOverlapped) {
    CALL_GUARD_NO_TS();
    display_commanderhooks::g_hook_stats[display_commanderhooks::HOOK_HID_ReadFile].increment_total();
    display_commanderhooks::UpdateHookLastCallTime(display_commanderhooks::HOOK_HID_ReadFile);

    if (ShouldSuppressHIDInput() && settings::g_experimentalTabSettings.hid_suppression_block_readfile.GetValue()) {
        if (LooksLikeHIDRead(hFile, nNumberOfBytesToRead)) {
            if (lpNumberOfBytesRead) {
                *lpNumberOfBytesRead = 0;
            }
            SetLastError(ERROR_DEVICE_NOT_CONNECTED);
            LogInfo("HID suppression: Blocked ReadFile operation on potential HID device");
            return FALSE;
        }
    }

    display_commanderhooks::g_hook_stats[display_commanderhooks::HOOK_HID_ReadFile].increment_unsuppressed();
    // Call original function
    return ReadFile_Original
               ? ReadFile_Original(hFile, lpBuffer, nNumberOfBytesToRead, lpNumberOfBytesRead, lpOverlapped)
               : ReadFile(hFile, lpBuffer, nNumberOfBytesToRead, lpNumberOfBytesRead, lpOverlapped);
}

BOOL WINAPI ReadFileEx_Detour(HANDLE hFile, LPVOID lpBuffer, DWORD nNumberOfBytesToRead, LPOVERLAPPED lpOverlapped,
                              LPOVERLAPPED_COMPLETION_ROUTINE lpCompletionRoutine) {
    CALL_GUARD_NO_TS();
    display_commanderhooks::g_hook_stats[display_commanderhooks::HOOK_HID_ReadFileEx].increment_total();
    display_commanderhooks::UpdateHookLastCallTime(display_commanderhooks::HOOK_HID_ReadFileEx);

    if (ShouldSuppressHIDInput() && settings::g_experimentalTabSettings.hid_suppression_block_readfile.GetValue()) {
        if (LooksLikeHIDRead(hFile, nNumberOfBytesToRead)) {
            SetLastError(ERROR_DEVICE_NOT_CONNECTED);
            LogInfo("HID suppression: Blocked ReadFileEx operation on potential HID device");
            return FALSE;
        }
    }

    display_commanderhooks::g_hook_stats[display_commanderhooks::HOOK_HID_ReadFileEx].increment_unsuppressed();
    return ReadFileEx_Original
               ? ReadFileEx_Original(hFile, lpBuffer, nNumberOfBytesToRead, lpOverlapped, lpCompletionRoutine)
               : ReadFileEx(hFile, lpBuffer, nNumberOfBytesToRead, lpOverlapped, lpCompletionRoutine);
}

BOOL WINAPI ReadFileScatter_Detour(HANDLE hFile, FILE_SEGMENT_ELEMENT aSegmentArray[], DWORD nNumberOfBytesToRead,
                                   LPDWORD lpReserved, LPOVERLAPPED lpOverlapped) {
    CALL_GUARD_NO_TS();
    display_commanderhooks::g_hook_stats[display_commanderhooks::HOOK_HID_ReadFileScatter].increment_total();
    display_commanderhooks::UpdateHookLastCallTime(display_commanderhooks::HOOK_HID_ReadFileScatter);

    if (ShouldSuppressHIDInput() && settings::g_experimentalTabSettings.hid_suppression_block_readfile.GetValue()) {
        if (LooksLikeHIDRead(hFile, nNumberOfBytesToRead)) {
            SetLastError(ERROR_DEVICE_NOT_CONNECTED);
            LogInfo("HID suppression: Blocked ReadFileScatter operation on potential HID device");
            return FALSE;
        }
    }

    display_commanderhooks::g_hook_stats[display_commanderhooks::HOOK_HID_ReadFileScatter].increment_unsuppressed();
    return ReadFileScatter_Original
               ? ReadFileScatter_Original(hFile, aSegmentArray, nNumberOfBytesToRead, lpReserved, lpOverlapped)
               : ReadFileScatter(hFile, aSegmentArray, nNumberOfBytesToRead, lpReserved, lpOverlapped);
}

BOOLEAN __stdcall HidD_GetInputReport_Direct(HANDLE HidDeviceObject, PVOID ReportBuffer, ULONG ReportBufferLength) {
    CALL_GUARD_NO_TS();
    return HidD_GetInputReport_Original
               ? HidD_GetInputReport_Original(HidDeviceObject, ReportBuffer, ReportBufferLength)
               : HidD_GetInputReport(HidDeviceObject, ReportBuffer, ReportBufferLength);
}

// Hooked HidD_GetInputReport function - suppresses HID input report reading
BOOLEAN __stdcall HidD_GetInputReport_Detour(HANDLE HidDeviceObject, PVOID ReportBuffer, ULONG ReportBufferLength) {
    CALL_GUARD_NO_TS();
    display_commanderhooks::g_hook_stats[display_commanderhooks::HOOK_HIDD_GetInputReport].increment_total();
    display_commanderhooks::UpdateHookLastCallTime(display_commanderhooks::HOOK_HIDD_GetInputReport);
    // Check if HID suppression is enabled and GetInputReport blocking is enabled
    if (ShouldSuppressHIDInput()
        && settings::g_experimentalTabSettings.hid_suppression_block_getinputreport.GetValue()) {
        // Suppress input report reading for games
        if (ReportBuffer) {
            memset(ReportBuffer, 0, ReportBufferLength);
        }
        LogInfo("HID suppression: Blocked HidD_GetInputReport operation");
        return FALSE;
    }

    // Call original function
    display_commanderhooks::g_hook_stats[display_commanderhooks::HOOK_HIDD_GetInputReport].increment_unsuppressed();
    return HidD_GetInputReport_Original
               ? HidD_GetInputReport_Original(HidDeviceObject, ReportBuffer, ReportBufferLength)
               : HidD_GetInputReport(HidDeviceObject, ReportBuffer, ReportBufferLength);
}

BOOLEAN __stdcall HidD_GetAttributes_Direct(HANDLE HidDeviceObject, PHIDD_ATTRIBUTES Attributes) {
    return HidD_GetAttributes_Original ? HidD_GetAttributes_Original(HidDeviceObject, Attributes)
                                       : HidD_GetAttributes(HidDeviceObject, Attributes);
}

// Hooked HidD_GetAttributes function - returns error when detecting DualSense
BOOLEAN __stdcall HidD_GetAttributes_Detour(HANDLE HidDeviceObject, PHIDD_ATTRIBUTES Attributes) {
    CALL_GUARD_NO_TS();
    display_commanderhooks::g_hook_stats[display_commanderhooks::HOOK_HIDD_GetAttributes].increment_total();
    display_commanderhooks::UpdateHookLastCallTime(display_commanderhooks::HOOK_HIDD_GetAttributes);
    // Call original function first to get the actual attributes
    BOOLEAN result = HidD_GetAttributes_Original ? HidD_GetAttributes_Original(HidDeviceObject, Attributes)
                                                 : HidD_GetAttributes(HidDeviceObject, Attributes);

    // Check if HID suppression is enabled and GetAttributes blocking is enabled
    if (ShouldSuppressHIDInput() && settings::g_experimentalTabSettings.hid_suppression_block_getattributes.GetValue()
        && result && Attributes) {
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
            return FALSE;  // Return error to prevent game from detecting the device
        }
    }

    display_commanderhooks::g_hook_stats[display_commanderhooks::HOOK_HIDD_GetAttributes].increment_unsuppressed();
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
    return (lowerPath.find("vid_054c") != std::string::npos
            && (lowerPath.find("pid_0ce6") != std::string::npos ||  // DualSense Controller (Regular)
                lowerPath.find("pid_0df2") != std::string::npos));  // DualSense Edge Controller
}

bool IsDualSenseDevicePath(const std::wstring& path) {
    // Convert to lowercase for case-insensitive comparison
    std::wstring lowerPath = path;
    std::transform(lowerPath.begin(), lowerPath.end(), lowerPath.begin(), ::towlower);

    // Check for DualSense device path patterns
    // Look for Sony vendor ID (054c) and DualSense product IDs (0ce6, 0df2)
    return (lowerPath.find(L"vid_054c") != std::wstring::npos
            && (lowerPath.find(L"pid_0ce6") != std::wstring::npos ||  // DualSense Controller (Regular)
                lowerPath.find(L"pid_0df2") != std::wstring::npos));  // DualSense Edge Controller
}

// Direct CreateFileA function (calls original)
HANDLE WINAPI CreateFileA_Direct(LPCSTR lpFileName, DWORD dwDesiredAccess, DWORD dwShareMode,
                                 LPSECURITY_ATTRIBUTES lpSecurityAttributes, DWORD dwCreationDisposition,
                                 DWORD dwFlagsAndAttributes, HANDLE hTemplateFile) {
    return CreateFileA_Original ? CreateFileA_Original(lpFileName, dwDesiredAccess, dwShareMode, lpSecurityAttributes,
                                                       dwCreationDisposition, dwFlagsAndAttributes, hTemplateFile)
                                : CreateFileA(lpFileName, dwDesiredAccess, dwShareMode, lpSecurityAttributes,
                                              dwCreationDisposition, dwFlagsAndAttributes, hTemplateFile);
}

// Hooked CreateFileA function - blocks HID device access
HANDLE WINAPI CreateFileA_Detour(LPCSTR lpFileName, DWORD dwDesiredAccess, DWORD dwShareMode,
                                 LPSECURITY_ATTRIBUTES lpSecurityAttributes, DWORD dwCreationDisposition,
                                 DWORD dwFlagsAndAttributes, HANDLE hTemplateFile) {
    CALL_GUARD_NO_TS();
    display_commanderhooks::g_hook_stats[display_commanderhooks::HOOK_HID_CreateFileA].increment_total();
    display_commanderhooks::UpdateHookLastCallTime(display_commanderhooks::HOOK_HID_CreateFileA);

    // Check if this is a HID device access and increment device type counters
    if (ShouldSuppressHIDInput() && lpFileName && IsHIDDevicePath(std::string(lpFileName))) {
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
            SetLastError(ERROR_ACCESS_DENIED);
            return INVALID_HANDLE_VALUE;
        }
    }

    display_commanderhooks::g_hook_stats[display_commanderhooks::HOOK_HID_CreateFileA].increment_unsuppressed();
    // Call original function
    return CreateFileA_Original ? CreateFileA_Original(lpFileName, dwDesiredAccess, dwShareMode, lpSecurityAttributes,
                                                       dwCreationDisposition, dwFlagsAndAttributes, hTemplateFile)
                                : CreateFileA(lpFileName, dwDesiredAccess, dwShareMode, lpSecurityAttributes,
                                              dwCreationDisposition, dwFlagsAndAttributes, hTemplateFile);
}

// Direct CreateFileW function (calls original)
HANDLE WINAPI CreateFileW_Direct(LPCWSTR lpFileName, DWORD dwDesiredAccess, DWORD dwShareMode,
                                 LPSECURITY_ATTRIBUTES lpSecurityAttributes, DWORD dwCreationDisposition,
                                 DWORD dwFlagsAndAttributes, HANDLE hTemplateFile) {
    return CreateFileW_Original ? CreateFileW_Original(lpFileName, dwDesiredAccess, dwShareMode, lpSecurityAttributes,
                                                       dwCreationDisposition, dwFlagsAndAttributes, hTemplateFile)
                                : CreateFileW(lpFileName, dwDesiredAccess, dwShareMode, lpSecurityAttributes,
                                              dwCreationDisposition, dwFlagsAndAttributes, hTemplateFile);
}

// Hooked CreateFileW function - blocks HID device access
HANDLE WINAPI CreateFileW_Detour(LPCWSTR lpFileName, DWORD dwDesiredAccess, DWORD dwShareMode,
                                 LPSECURITY_ATTRIBUTES lpSecurityAttributes, DWORD dwCreationDisposition,
                                 DWORD dwFlagsAndAttributes, HANDLE hTemplateFile) {
    CALL_GUARD_NO_TS();
    display_commanderhooks::g_hook_stats[display_commanderhooks::HOOK_HID_CreateFileW].increment_total();
    display_commanderhooks::UpdateHookLastCallTime(display_commanderhooks::HOOK_HID_CreateFileW);

    // Check if this is a HID device access and increment device type counters
    if (ShouldSuppressHIDInput() && lpFileName && IsHIDDevicePath(std::wstring(lpFileName))) {
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
            SetLastError(ERROR_ACCESS_DENIED);
            return INVALID_HANDLE_VALUE;
        }
    }

    display_commanderhooks::g_hook_stats[display_commanderhooks::HOOK_HID_CreateFileW].increment_unsuppressed();
    // Call original function
    return CreateFileW_Original ? CreateFileW_Original(lpFileName, dwDesiredAccess, dwShareMode, lpSecurityAttributes,
                                                       dwCreationDisposition, dwFlagsAndAttributes, hTemplateFile)
                                : CreateFileW(lpFileName, dwDesiredAccess, dwShareMode, lpSecurityAttributes,
                                              dwCreationDisposition, dwFlagsAndAttributes, hTemplateFile);
}

void UninstallHIDSuppressionHooks() {
    if (!g_hid_suppression_hooks_installed.load()) {
        LogInfo("HID suppression hooks not installed");
        return;
    }

    // Disable individual hooks
    MH_DisableHook(ReadFile);
    MH_DisableHook(ReadFileEx);
    MH_DisableHook(ReadFileScatter);
    MH_DisableHook(HidD_GetInputReport);
    MH_DisableHook(HidD_GetAttributes);
    MH_DisableHook(CreateFileA);
    MH_DisableHook(CreateFileW);

    // Remove individual hooks
    MH_RemoveHook(ReadFile);
    MH_RemoveHook(ReadFileEx);
    MH_RemoveHook(ReadFileScatter);
    MH_RemoveHook(HidD_GetInputReport);
    MH_RemoveHook(HidD_GetAttributes);
    MH_RemoveHook(CreateFileA);
    MH_RemoveHook(CreateFileW);

    // Clean up
    ReadFile_Original = nullptr;
    ReadFileEx_Original = nullptr;
    ReadFileScatter_Original = nullptr;
    HidD_GetInputReport_Original = nullptr;
    HidD_GetAttributes_Original = nullptr;
    CreateFileA_Original = nullptr;
    CreateFileW_Original = nullptr;

    g_hid_suppression_hooks_installed.store(false);
    LogInfo("HID suppression hooks uninstalled successfully");
}

bool AreHIDSuppressionHooksInstalled() { return g_hid_suppression_hooks_installed.load(); }

void MarkHIDSuppressionHooksInstalled(bool installed) { g_hid_suppression_hooks_installed.store(installed); }

}  // namespace renodx::hooks
