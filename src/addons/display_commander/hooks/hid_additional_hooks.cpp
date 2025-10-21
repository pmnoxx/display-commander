#include "hid_additional_hooks.hpp"
#include "hid_statistics.hpp"
#include "../utils.hpp"
#include "../utils/general_utils.hpp"
#include <MinHook.h>
#include <atomic>

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
BOOL WINAPI WriteFile_Detour(HANDLE hFile, LPCVOID lpBuffer, DWORD nNumberOfBytesToWrite, LPDWORD lpNumberOfBytesWritten, LPOVERLAPPED lpOverlapped) {
    // Increment HID statistics
    auto& stats = g_hid_api_stats[HID_WRITEFILE];
    stats.increment_total();

    // Call original function
    BOOL result = WriteFile_Original ?
        WriteFile_Original(hFile, lpBuffer, nNumberOfBytesToWrite, lpNumberOfBytesWritten, lpOverlapped) :
        WriteFile(hFile, lpBuffer, nNumberOfBytesToWrite, lpNumberOfBytesWritten, lpOverlapped);

    // Update statistics based on result
    if (result) {
        stats.increment_successful();
    } else {
        stats.increment_failed();
    }

    return result;
}

// Hooked DeviceIoControl function
BOOL WINAPI DeviceIoControl_Detour(HANDLE hDevice, DWORD dwIoControlCode, LPVOID lpInBuffer, DWORD nInBufferSize, LPVOID lpOutBuffer, DWORD nOutBufferSize, LPDWORD lpBytesReturned, LPOVERLAPPED lpOverlapped) {
    // Increment HID statistics
    auto& stats = g_hid_api_stats[HID_DEVICEIOCONTROL];
    stats.increment_total();

    // Call original function
    BOOL result = DeviceIoControl_Original ?
        DeviceIoControl_Original(hDevice, dwIoControlCode, lpInBuffer, nInBufferSize, lpOutBuffer, nOutBufferSize, lpBytesReturned, lpOverlapped) :
        DeviceIoControl(hDevice, dwIoControlCode, lpInBuffer, nInBufferSize, lpOutBuffer, nOutBufferSize, lpBytesReturned, lpOverlapped);

    // Update statistics based on result
    if (result) {
        stats.increment_successful();
    } else {
        stats.increment_failed();
    }

    return result;
}

// Hooked HidD_GetPreparsedData function
BOOLEAN __stdcall HidD_GetPreparsedData_Detour(HANDLE HidDeviceObject, PHIDP_PREPARSED_DATA* PreparsedData) {
    // Increment HID statistics
    auto& stats = g_hid_api_stats[HID_HIDD_GETPREPARSEDDATA];
    stats.increment_total();

    // Call original function
    BOOLEAN result = HidD_GetPreparsedData_Original ?
        HidD_GetPreparsedData_Original(HidDeviceObject, PreparsedData) :
        HidD_GetPreparsedData(HidDeviceObject, PreparsedData);

    // Update statistics based on result
    if (result) {
        stats.increment_successful();
    } else {
        stats.increment_failed();
    }

    return result;
}

// Hooked HidD_FreePreparsedData function
BOOLEAN __stdcall HidD_FreePreparsedData_Detour(PHIDP_PREPARSED_DATA PreparsedData) {
    // Increment HID statistics
    auto& stats = g_hid_api_stats[HID_HIDD_FREEPREPARSEDDATA];
    stats.increment_total();

    // Call original function
    BOOLEAN result = HidD_FreePreparsedData_Original ?
        HidD_FreePreparsedData_Original(PreparsedData) :
        HidD_FreePreparsedData(PreparsedData);

    // Update statistics based on result
    if (result) {
        stats.increment_successful();
    } else {
        stats.increment_failed();
    }

    return result;
}

// Hooked HidP_GetCaps function
BOOLEAN __stdcall HidP_GetCaps_Detour(PHIDP_PREPARSED_DATA PreparsedData, PHIDP_CAPS Capabilities) {
    // Increment HID statistics
    auto& stats = g_hid_api_stats[HID_HIDD_GETCAPS];
    stats.increment_total();

    // Call original function
    BOOLEAN result = HidP_GetCaps_Original ?
        HidP_GetCaps_Original(PreparsedData, Capabilities) :
        HidP_GetCaps(PreparsedData, Capabilities);

    // Update statistics based on result
    if (result) {
        stats.increment_successful();
    } else {
        stats.increment_failed();
    }

    return result;
}

// Hooked HidD_GetManufacturerString function
BOOLEAN __stdcall HidD_GetManufacturerString_Detour(HANDLE HidDeviceObject, PVOID Buffer, ULONG BufferLength) {
    // Increment HID statistics
    auto& stats = g_hid_api_stats[HID_HIDD_GETMANUFACTURERSTRING];
    stats.increment_total();

    // Call original function
    BOOLEAN result = HidD_GetManufacturerString_Original ?
        HidD_GetManufacturerString_Original(HidDeviceObject, Buffer, BufferLength) :
        HidD_GetManufacturerString(HidDeviceObject, Buffer, BufferLength);

    // Update statistics based on result
    if (result) {
        stats.increment_successful();
    } else {
        stats.increment_failed();
    }

    return result;
}

// Hooked HidD_GetProductString function
BOOLEAN __stdcall HidD_GetProductString_Detour(HANDLE HidDeviceObject, PVOID Buffer, ULONG BufferLength) {
    // Increment HID statistics
    auto& stats = g_hid_api_stats[HID_HIDD_GETPRODUCTSTRING];
    stats.increment_total();

    // Call original function
    BOOLEAN result = HidD_GetProductString_Original ?
        HidD_GetProductString_Original(HidDeviceObject, Buffer, BufferLength) :
        HidD_GetProductString(HidDeviceObject, Buffer, BufferLength);

    // Update statistics based on result
    if (result) {
        stats.increment_successful();
    } else {
        stats.increment_failed();
    }

    return result;
}

// Hooked HidD_GetSerialNumberString function
BOOLEAN __stdcall HidD_GetSerialNumberString_Detour(HANDLE HidDeviceObject, PVOID Buffer, ULONG BufferLength) {
    // Increment HID statistics
    auto& stats = g_hid_api_stats[HID_HIDD_GETSERIALNUMBERSTRING];
    stats.increment_total();

    // Call original function
    BOOLEAN result = HidD_GetSerialNumberString_Original ?
        HidD_GetSerialNumberString_Original(HidDeviceObject, Buffer, BufferLength) :
        HidD_GetSerialNumberString(HidDeviceObject, Buffer, BufferLength);

    // Update statistics based on result
    if (result) {
        stats.increment_successful();
    } else {
        stats.increment_failed();
    }

    return result;
}

// Hooked HidD_GetNumInputBuffers function
BOOLEAN __stdcall HidD_GetNumInputBuffers_Detour(HANDLE HidDeviceObject, PULONG NumberBuffers) {
    // Increment HID statistics
    auto& stats = g_hid_api_stats[HID_HIDD_GETNUMINPUTBUFFERS];
    stats.increment_total();

    // Call original function
    BOOLEAN result = HidD_GetNumInputBuffers_Original ?
        HidD_GetNumInputBuffers_Original(HidDeviceObject, NumberBuffers) :
        HidD_GetNumInputBuffers(HidDeviceObject, NumberBuffers);

    // Update statistics based on result
    if (result) {
        stats.increment_successful();
    } else {
        stats.increment_failed();
    }

    return result;
}

// Hooked HidD_SetNumInputBuffers function
BOOLEAN __stdcall HidD_SetNumInputBuffers_Detour(HANDLE HidDeviceObject, ULONG NumberBuffers) {
    // Increment HID statistics
    auto& stats = g_hid_api_stats[HID_HIDD_SETNUMINPUTBUFFERS];
    stats.increment_total();

    // Call original function
    BOOLEAN result = HidD_SetNumInputBuffers_Original ?
        HidD_SetNumInputBuffers_Original(HidDeviceObject, NumberBuffers) :
        HidD_SetNumInputBuffers(HidDeviceObject, NumberBuffers);

    // Update statistics based on result
    if (result) {
        stats.increment_successful();
    } else {
        stats.increment_failed();
    }

    return result;
}

// Hooked HidD_GetFeature function
BOOLEAN __stdcall HidD_GetFeature_Detour(HANDLE HidDeviceObject, PVOID ReportBuffer, ULONG ReportBufferLength) {
    // Increment HID statistics
    auto& stats = g_hid_api_stats[HID_HIDD_GETFEATURE];
    stats.increment_total();

    // Call original function
    BOOLEAN result = HidD_GetFeature_Original ?
        HidD_GetFeature_Original(HidDeviceObject, ReportBuffer, ReportBufferLength) :
        HidD_GetFeature(HidDeviceObject, ReportBuffer, ReportBufferLength);

    // Update statistics based on result
    if (result) {
        stats.increment_successful();
    } else {
        stats.increment_failed();
    }

    return result;
}

// Hooked HidD_SetFeature function
BOOLEAN __stdcall HidD_SetFeature_Detour(HANDLE HidDeviceObject, PVOID ReportBuffer, ULONG ReportBufferLength) {
    // Increment HID statistics
    auto& stats = g_hid_api_stats[HID_HIDD_SETFEATURE];
    stats.increment_total();

    // Call original function
    BOOLEAN result = HidD_SetFeature_Original ?
        HidD_SetFeature_Original(HidDeviceObject, ReportBuffer, ReportBufferLength) :
        HidD_SetFeature(HidDeviceObject, ReportBuffer, ReportBufferLength);

    // Update statistics based on result
    if (result) {
        stats.increment_successful();
    } else {
        stats.increment_failed();
    }

    return result;
}

bool InstallAdditionalHIDHooks() {
    if (g_additional_hid_hooks_installed.load()) {
        LogInfo("Additional HID hooks already installed");
        return true;
    }

    // Initialize MinHook (only if not already initialized)
    MH_STATUS init_status = MH_Initialize();
    if (init_status != MH_OK && init_status != MH_ERROR_ALREADY_INITIALIZED) {
        LogError("Failed to initialize MinHook for additional HID hooks - Status: %d", init_status);
        return false;
    }

    if (init_status == MH_ERROR_ALREADY_INITIALIZED) {
        LogInfo("MinHook already initialized, proceeding with additional HID hooks");
    } else {
        LogInfo("MinHook initialized successfully for additional HID hooks");
    }

    // Hook WriteFile
    if (!CreateAndEnableHook(WriteFile, WriteFile_Detour, (LPVOID*)&WriteFile_Original, "WriteFile")) {
        LogError("Failed to create and enable WriteFile hook");
        return false;
    }

    // Hook DeviceIoControl
    if (!CreateAndEnableHook(DeviceIoControl, DeviceIoControl_Detour, (LPVOID*)&DeviceIoControl_Original, "DeviceIoControl")) {
        LogError("Failed to create and enable DeviceIoControl hook");
        return false;
    }

    // Hook HidD functions
    if (MH_CreateHookApi(L"hid.dll", "HidD_GetPreparsedData", HidD_GetPreparsedData_Detour, (LPVOID*)&HidD_GetPreparsedData_Original) != MH_OK) {
        LogError("Failed to create HidD_GetPreparsedData hook");
        return false;
    }
    if (MH_EnableHook(HidD_GetPreparsedData) != MH_OK) {
        LogError("Failed to enable HidD_GetPreparsedData hook");
        return false;
    }

    if (MH_CreateHookApi(L"hid.dll", "HidD_FreePreparsedData", HidD_FreePreparsedData_Detour, (LPVOID*)&HidD_FreePreparsedData_Original) != MH_OK) {
        LogError("Failed to create HidD_FreePreparsedData hook");
        return false;
    }
    if (MH_EnableHook(HidD_FreePreparsedData) != MH_OK) {
        LogError("Failed to enable HidD_FreePreparsedData hook");
        return false;
    }

    if (MH_CreateHookApi(L"hid.dll", "HidP_GetCaps", HidP_GetCaps_Detour, (LPVOID*)&HidP_GetCaps_Original) != MH_OK) {
        LogError("Failed to create HidP_GetCaps hook");
        return false;
    }
    if (MH_EnableHook(HidP_GetCaps) != MH_OK) {
        LogError("Failed to enable HidP_GetCaps hook");
        return false;
    }

    if (MH_CreateHookApi(L"hid.dll", "HidD_GetManufacturerString", HidD_GetManufacturerString_Detour, (LPVOID*)&HidD_GetManufacturerString_Original) != MH_OK) {
        LogError("Failed to create HidD_GetManufacturerString hook");
        return false;
    }
    if (MH_EnableHook(HidD_GetManufacturerString) != MH_OK) {
        LogError("Failed to enable HidD_GetManufacturerString hook");
        return false;
    }

    if (MH_CreateHookApi(L"hid.dll", "HidD_GetProductString", HidD_GetProductString_Detour, (LPVOID*)&HidD_GetProductString_Original) != MH_OK) {
        LogError("Failed to create HidD_GetProductString hook");
        return false;
    }
    if (MH_EnableHook(HidD_GetProductString) != MH_OK) {
        LogError("Failed to enable HidD_GetProductString hook");
        return false;
    }

    if (MH_CreateHookApi(L"hid.dll", "HidD_GetSerialNumberString", HidD_GetSerialNumberString_Detour, (LPVOID*)&HidD_GetSerialNumberString_Original) != MH_OK) {
        LogError("Failed to create HidD_GetSerialNumberString hook");
        return false;
    }
    if (MH_EnableHook(HidD_GetSerialNumberString) != MH_OK) {
        LogError("Failed to enable HidD_GetSerialNumberString hook");
        return false;
    }

    if (MH_CreateHookApi(L"hid.dll", "HidD_GetNumInputBuffers", HidD_GetNumInputBuffers_Detour, (LPVOID*)&HidD_GetNumInputBuffers_Original) != MH_OK) {
        LogError("Failed to create HidD_GetNumInputBuffers hook");
        return false;
    }
    if (MH_EnableHook(HidD_GetNumInputBuffers) != MH_OK) {
        LogError("Failed to enable HidD_GetNumInputBuffers hook");
        return false;
    }

    if (MH_CreateHookApi(L"hid.dll", "HidD_SetNumInputBuffers", HidD_SetNumInputBuffers_Detour, (LPVOID*)&HidD_SetNumInputBuffers_Original) != MH_OK) {
        LogError("Failed to create HidD_SetNumInputBuffers hook");
        return false;
    }
    if (MH_EnableHook(HidD_SetNumInputBuffers) != MH_OK) {
        LogError("Failed to enable HidD_SetNumInputBuffers hook");
        return false;
    }

    if (MH_CreateHookApi(L"hid.dll", "HidD_GetFeature", HidD_GetFeature_Detour, (LPVOID*)&HidD_GetFeature_Original) != MH_OK) {
        LogError("Failed to create HidD_GetFeature hook");
        return false;
    }
    if (MH_EnableHook(HidD_GetFeature) != MH_OK) {
        LogError("Failed to enable HidD_GetFeature hook");
        return false;
    }

    if (MH_CreateHookApi(L"hid.dll", "HidD_SetFeature", HidD_SetFeature_Detour, (LPVOID*)&HidD_SetFeature_Original) != MH_OK) {
        LogError("Failed to create HidD_SetFeature hook");
        return false;
    }
    if (MH_EnableHook(HidD_SetFeature) != MH_OK) {
        LogError("Failed to enable HidD_SetFeature hook");
        return false;
    }

    g_additional_hid_hooks_installed.store(true);
    LogInfo("Successfully installed additional HID hooks");
    return true;
}

void UninstallAdditionalHIDHooks() {
    if (!g_additional_hid_hooks_installed.load()) {
        LogInfo("Additional HID hooks not installed");
        return;
    }

    // Disable all hooks
    MH_DisableHook(WriteFile);
    MH_DisableHook(DeviceIoControl);
    MH_DisableHook(HidD_GetPreparsedData);
    MH_DisableHook(HidD_FreePreparsedData);
    MH_DisableHook(HidP_GetCaps);
    MH_DisableHook(HidD_GetManufacturerString);
    MH_DisableHook(HidD_GetProductString);
    MH_DisableHook(HidD_GetSerialNumberString);
    MH_DisableHook(HidD_GetNumInputBuffers);
    MH_DisableHook(HidD_SetNumInputBuffers);
    MH_DisableHook(HidD_GetFeature);
    MH_DisableHook(HidD_SetFeature);

    g_additional_hid_hooks_installed.store(false);
    LogInfo("Successfully uninstalled additional HID hooks");
}

} // namespace display_commanderhooks
