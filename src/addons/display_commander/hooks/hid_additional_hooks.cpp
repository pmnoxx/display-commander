#include "hid_additional_hooks.hpp"
#include <MinHook.h>
#include <atomic>
#include "../utils/logging.hpp"
#include "hid_hooks_install.hpp"
#include "hook_suppression_manager.hpp"
#include "windows_hooks/windows_message_hooks.hpp"


namespace display_commanderhooks {

// Additional HID hook function pointers
WriteFile_pfn WriteFile_Original = nullptr;
DeviceIoControl_pfn DeviceIoControl_Original = nullptr;
HidD_GetPreparsedData_pfn HidD_GetPreparsedData_Original = nullptr;
HidD_FreePreparsedData_pfn HidD_FreePreparsedData_Original = nullptr;
HidP_GetCaps_pfn HidP_GetCaps_Original = nullptr;
HidD_GetManufacturerString_pfn HidD_GetManufacturerString_Original = nullptr;
HidD_GetProductString_pfn HidD_GetProductString_Original = nullptr;
HidD_GetSerialNumberString_pfn HidD_GetSerialNumberString_Original = nullptr;
HidD_GetNumInputBuffers_pfn HidD_GetNumInputBuffers_Original = nullptr;
HidD_SetNumInputBuffers_pfn HidD_SetNumInputBuffers_Original = nullptr;
HidD_GetFeature_pfn HidD_GetFeature_Original = nullptr;
HidD_SetFeature_pfn HidD_SetFeature_Original = nullptr;

// Hook installation status
static std::atomic<bool> g_additional_hid_hooks_installed{false};

// Hooked WriteFile function
BOOL WINAPI WriteFile_Detour(HANDLE hFile, LPCVOID lpBuffer, DWORD nNumberOfBytesToWrite,
                             LPDWORD lpNumberOfBytesWritten, LPOVERLAPPED lpOverlapped) {
    g_hook_stats[HOOK_HID_WriteFile].increment_total();

    BOOL result = WriteFile_Original
                      ? WriteFile_Original(hFile, lpBuffer, nNumberOfBytesToWrite, lpNumberOfBytesWritten, lpOverlapped)
                      : WriteFile(hFile, lpBuffer, nNumberOfBytesToWrite, lpNumberOfBytesWritten, lpOverlapped);

    g_hook_stats[HOOK_HID_WriteFile].increment_unsuppressed();
    return result;
}

// Hooked DeviceIoControl function
BOOL WINAPI DeviceIoControl_Detour(HANDLE hDevice, DWORD dwIoControlCode, LPVOID lpInBuffer, DWORD nInBufferSize,
                                   LPVOID lpOutBuffer, DWORD nOutBufferSize, LPDWORD lpBytesReturned,
                                   LPOVERLAPPED lpOverlapped) {
    g_hook_stats[HOOK_HID_DeviceIoControl].increment_total();

    BOOL result = DeviceIoControl_Original
                      ? DeviceIoControl_Original(hDevice, dwIoControlCode, lpInBuffer, nInBufferSize, lpOutBuffer,
                                                 nOutBufferSize, lpBytesReturned, lpOverlapped)
                      : DeviceIoControl(hDevice, dwIoControlCode, lpInBuffer, nInBufferSize, lpOutBuffer,
                                        nOutBufferSize, lpBytesReturned, lpOverlapped);

    g_hook_stats[HOOK_HID_DeviceIoControl].increment_unsuppressed();
    return result;
}

// Hooked HidD_GetPreparsedData function
BOOLEAN __stdcall HidD_GetPreparsedData_Detour(HANDLE HidDeviceObject, PHIDP_PREPARSED_DATA* PreparsedData) {
    g_hook_stats[HOOK_HIDD_GetPreparsedData].increment_total();

    BOOLEAN result = HidD_GetPreparsedData_Original ? HidD_GetPreparsedData_Original(HidDeviceObject, PreparsedData)
                                                    : HidD_GetPreparsedData(HidDeviceObject, PreparsedData);

    g_hook_stats[HOOK_HIDD_GetPreparsedData].increment_unsuppressed();
    return result;
}

// Hooked HidD_FreePreparsedData function
BOOLEAN __stdcall HidD_FreePreparsedData_Detour(PHIDP_PREPARSED_DATA PreparsedData) {
    g_hook_stats[HOOK_HIDD_FreePreparsedData].increment_total();

    BOOLEAN result = HidD_FreePreparsedData_Original ? HidD_FreePreparsedData_Original(PreparsedData)
                                                     : HidD_FreePreparsedData(PreparsedData);

    g_hook_stats[HOOK_HIDD_FreePreparsedData].increment_unsuppressed();
    return result;
}

// Hooked HidP_GetCaps function
BOOLEAN __stdcall HidP_GetCaps_Detour(PHIDP_PREPARSED_DATA PreparsedData, PHIDP_CAPS Capabilities) {
    g_hook_stats[HOOK_HIDD_GetCaps].increment_total();

    BOOLEAN result = HidP_GetCaps_Original ? HidP_GetCaps_Original(PreparsedData, Capabilities)
                                           : HidP_GetCaps(PreparsedData, Capabilities);

    g_hook_stats[HOOK_HIDD_GetCaps].increment_unsuppressed();
    return result;
}

// Hooked HidD_GetManufacturerString function
BOOLEAN __stdcall HidD_GetManufacturerString_Detour(HANDLE HidDeviceObject, PVOID Buffer, ULONG BufferLength) {
    g_hook_stats[HOOK_HIDD_GetManufacturerString].increment_total();

    BOOLEAN result = HidD_GetManufacturerString_Original
                         ? HidD_GetManufacturerString_Original(HidDeviceObject, Buffer, BufferLength)
                         : HidD_GetManufacturerString(HidDeviceObject, Buffer, BufferLength);

    g_hook_stats[HOOK_HIDD_GetManufacturerString].increment_unsuppressed();
    return result;
}

// Hooked HidD_GetProductString function
BOOLEAN __stdcall HidD_GetProductString_Detour(HANDLE HidDeviceObject, PVOID Buffer, ULONG BufferLength) {
    g_hook_stats[HOOK_HIDD_GetProductString].increment_total();

    BOOLEAN result = HidD_GetProductString_Original
                         ? HidD_GetProductString_Original(HidDeviceObject, Buffer, BufferLength)
                         : HidD_GetProductString(HidDeviceObject, Buffer, BufferLength);

    g_hook_stats[HOOK_HIDD_GetProductString].increment_unsuppressed();
    return result;
}

// Hooked HidD_GetSerialNumberString function
BOOLEAN __stdcall HidD_GetSerialNumberString_Detour(HANDLE HidDeviceObject, PVOID Buffer, ULONG BufferLength) {
    g_hook_stats[HOOK_HIDD_GetSerialNumberString].increment_total();

    BOOLEAN result = HidD_GetSerialNumberString_Original
                         ? HidD_GetSerialNumberString_Original(HidDeviceObject, Buffer, BufferLength)
                         : HidD_GetSerialNumberString(HidDeviceObject, Buffer, BufferLength);

    g_hook_stats[HOOK_HIDD_GetSerialNumberString].increment_unsuppressed();
    return result;
}

// Hooked HidD_GetNumInputBuffers function
BOOLEAN __stdcall HidD_GetNumInputBuffers_Detour(HANDLE HidDeviceObject, PULONG NumberBuffers) {
    g_hook_stats[HOOK_HIDD_GetNumInputBuffers].increment_total();

    BOOLEAN result = HidD_GetNumInputBuffers_Original ? HidD_GetNumInputBuffers_Original(HidDeviceObject, NumberBuffers)
                                                      : HidD_GetNumInputBuffers(HidDeviceObject, NumberBuffers);

    g_hook_stats[HOOK_HIDD_GetNumInputBuffers].increment_unsuppressed();
    return result;
}

// Hooked HidD_SetNumInputBuffers function
BOOLEAN __stdcall HidD_SetNumInputBuffers_Detour(HANDLE HidDeviceObject, ULONG NumberBuffers) {
    g_hook_stats[HOOK_HIDD_SetNumInputBuffers].increment_total();

    BOOLEAN result = HidD_SetNumInputBuffers_Original ? HidD_SetNumInputBuffers_Original(HidDeviceObject, NumberBuffers)
                                                      : HidD_SetNumInputBuffers(HidDeviceObject, NumberBuffers);

    g_hook_stats[HOOK_HIDD_SetNumInputBuffers].increment_unsuppressed();
    return result;
}

// Hooked HidD_GetFeature function
BOOLEAN __stdcall HidD_GetFeature_Detour(HANDLE HidDeviceObject, PVOID ReportBuffer, ULONG ReportBufferLength) {
    g_hook_stats[HOOK_HIDD_GetFeature].increment_total();

    BOOLEAN result = HidD_GetFeature_Original
                         ? HidD_GetFeature_Original(HidDeviceObject, ReportBuffer, ReportBufferLength)
                         : HidD_GetFeature(HidDeviceObject, ReportBuffer, ReportBufferLength);

    g_hook_stats[HOOK_HIDD_GetFeature].increment_unsuppressed();
    return result;
}

// Hooked HidD_SetFeature function
BOOLEAN __stdcall HidD_SetFeature_Detour(HANDLE HidDeviceObject, PVOID ReportBuffer, ULONG ReportBufferLength) {
    g_hook_stats[HOOK_HIDD_SetFeature].increment_total();

    BOOLEAN result = HidD_SetFeature_Original
                         ? HidD_SetFeature_Original(HidDeviceObject, ReportBuffer, ReportBufferLength)
                         : HidD_SetFeature(HidDeviceObject, ReportBuffer, ReportBufferLength);

    g_hook_stats[HOOK_HIDD_SetFeature].increment_unsuppressed();
    return result;
}

void UninstallAdditionalHIDHooks() {
    if (!g_additional_hid_hooks_installed.load()) {
        LogInfo("Additional HID hooks not installed");
        return;
    }

    LogInfo("Uninstalling additional HID hooks...");

    // Disable hooks only if they have original function pointers (indicating successful installation)
    if (WriteFile_Original) {
        MH_DisableHook(WriteFile);
        WriteFile_Original = nullptr;
    }

    if (DeviceIoControl_Original) {
        MH_DisableHook(DeviceIoControl);
        DeviceIoControl_Original = nullptr;
    }

    if (HidD_GetPreparsedData_Original) {
        MH_DisableHook(HidD_GetPreparsedData);
        HidD_GetPreparsedData_Original = nullptr;
    }

    if (HidD_FreePreparsedData_Original) {
        MH_DisableHook(HidD_FreePreparsedData);
        HidD_FreePreparsedData_Original = nullptr;
    }

    if (HidP_GetCaps_Original) {
        MH_DisableHook(HidP_GetCaps);
        HidP_GetCaps_Original = nullptr;
    }

    if (HidD_GetManufacturerString_Original) {
        MH_DisableHook(HidD_GetManufacturerString);
        HidD_GetManufacturerString_Original = nullptr;
    }

    if (HidD_GetProductString_Original) {
        MH_DisableHook(HidD_GetProductString);
        HidD_GetProductString_Original = nullptr;
    }

    if (HidD_GetSerialNumberString_Original) {
        MH_DisableHook(HidD_GetSerialNumberString);
        HidD_GetSerialNumberString_Original = nullptr;
    }

    if (HidD_GetNumInputBuffers_Original) {
        MH_DisableHook(HidD_GetNumInputBuffers);
        HidD_GetNumInputBuffers_Original = nullptr;
    }

    if (HidD_SetNumInputBuffers_Original) {
        MH_DisableHook(HidD_SetNumInputBuffers);
        HidD_SetNumInputBuffers_Original = nullptr;
    }

    if (HidD_GetFeature_Original) {
        MH_DisableHook(HidD_GetFeature);
        HidD_GetFeature_Original = nullptr;
    }

    if (HidD_SetFeature_Original) {
        MH_DisableHook(HidD_SetFeature);
        HidD_SetFeature_Original = nullptr;
    }

    g_additional_hid_hooks_installed.store(false);
    LogInfo("Successfully uninstalled additional HID hooks");
}

void MarkAdditionalHIDHooksInstalled(bool installed) { g_additional_hid_hooks_installed.store(installed); }

}  // namespace display_commanderhooks
