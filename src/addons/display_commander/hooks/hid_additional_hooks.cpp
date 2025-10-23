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
    MH_STATUS init_status = SafeInitializeMinHook();
    if (init_status != MH_OK && init_status != MH_ERROR_ALREADY_INITIALIZED) {
        LogError("Failed to initialize MinHook for additional HID hooks - Status: %d", init_status);
        return false;
    }

    if (init_status == MH_ERROR_ALREADY_INITIALIZED) {
        LogInfo("MinHook already initialized, proceeding with additional HID hooks");
    } else {
        LogInfo("MinHook initialized successfully for additional HID hooks");
    }

    // Track hook installation success/failure
    int successful_hooks = 0;
    int total_hooks = 0;

    // Hook WriteFile
    total_hooks++;
    if (CreateAndEnableHook(WriteFile, WriteFile_Detour, (LPVOID*)&WriteFile_Original, "WriteFile")) {
        successful_hooks++;
    } else {
        LogWarn("Failed to install WriteFile hook, continuing with other hooks");
    }

    // Hook DeviceIoControl
    total_hooks++;
    if (CreateAndEnableHook(DeviceIoControl, DeviceIoControl_Detour, (LPVOID*)&DeviceIoControl_Original, "DeviceIoControl")) {
        successful_hooks++;
    } else {
        LogWarn("Failed to install DeviceIoControl hook, continuing with other hooks");
    }

    // Hook HidD functions using CreateAndEnableHook for consistency
    total_hooks++;
    if (CreateAndEnableHook(HidD_GetPreparsedData, HidD_GetPreparsedData_Detour, (LPVOID*)&HidD_GetPreparsedData_Original, "HidD_GetPreparsedData")) {
        successful_hooks++;
    } else {
        LogWarn("Failed to install HidD_GetPreparsedData hook, continuing with other hooks");
    }

    total_hooks++;
    if (CreateAndEnableHook(HidD_FreePreparsedData, HidD_FreePreparsedData_Detour, (LPVOID*)&HidD_FreePreparsedData_Original, "HidD_FreePreparsedData")) {
        successful_hooks++;
    } else {
        LogWarn("Failed to install HidD_FreePreparsedData hook, continuing with other hooks");
    }

    total_hooks++;
    if (CreateAndEnableHook(HidP_GetCaps, HidP_GetCaps_Detour, (LPVOID*)&HidP_GetCaps_Original, "HidP_GetCaps")) {
        successful_hooks++;
    } else {
        LogWarn("Failed to install HidP_GetCaps hook, continuing with other hooks");
    }

    total_hooks++;
    if (CreateAndEnableHook(HidD_GetManufacturerString, HidD_GetManufacturerString_Detour, (LPVOID*)&HidD_GetManufacturerString_Original, "HidD_GetManufacturerString")) {
        successful_hooks++;
    } else {
        LogWarn("Failed to install HidD_GetManufacturerString hook, continuing with other hooks");
    }

    total_hooks++;
    if (CreateAndEnableHook(HidD_GetProductString, HidD_GetProductString_Detour, (LPVOID*)&HidD_GetProductString_Original, "HidD_GetProductString")) {
        successful_hooks++;
    } else {
        LogWarn("Failed to install HidD_GetProductString hook, continuing with other hooks");
    }

    total_hooks++;
    if (CreateAndEnableHook(HidD_GetSerialNumberString, HidD_GetSerialNumberString_Detour, (LPVOID*)&HidD_GetSerialNumberString_Original, "HidD_GetSerialNumberString")) {
        successful_hooks++;
    } else {
        LogWarn("Failed to install HidD_GetSerialNumberString hook, continuing with other hooks");
    }

    total_hooks++;
    if (CreateAndEnableHook(HidD_GetNumInputBuffers, HidD_GetNumInputBuffers_Detour, (LPVOID*)&HidD_GetNumInputBuffers_Original, "HidD_GetNumInputBuffers")) {
        successful_hooks++;
    } else {
        LogWarn("Failed to install HidD_GetNumInputBuffers hook, continuing with other hooks");
    }

    total_hooks++;
    if (CreateAndEnableHook(HidD_SetNumInputBuffers, HidD_SetNumInputBuffers_Detour, (LPVOID*)&HidD_SetNumInputBuffers_Original, "HidD_SetNumInputBuffers")) {
        successful_hooks++;
    } else {
        LogWarn("Failed to install HidD_SetNumInputBuffers hook, continuing with other hooks");
    }

    total_hooks++;
    if (CreateAndEnableHook(HidD_GetFeature, HidD_GetFeature_Detour, (LPVOID*)&HidD_GetFeature_Original, "HidD_GetFeature")) {
        successful_hooks++;
    } else {
        LogWarn("Failed to install HidD_GetFeature hook, continuing with other hooks");
    }

    total_hooks++;
    if (CreateAndEnableHook(HidD_SetFeature, HidD_SetFeature_Detour, (LPVOID*)&HidD_SetFeature_Original, "HidD_SetFeature")) {
        successful_hooks++;
    } else {
        LogWarn("Failed to install HidD_SetFeature hook, continuing with other hooks");
    }

    // Mark as installed if at least one hook succeeded
    if (successful_hooks > 0) {
        g_additional_hid_hooks_installed.store(true);
        LogInfo("Successfully installed %d/%d additional HID hooks", successful_hooks, total_hooks);
        if (successful_hooks < total_hooks) {
            LogWarn("Some HID hooks failed to install, but continuing with available functionality");
        }
        return true;
    } else {
        LogError("Failed to install any additional HID hooks");
        return false;
    }
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

} // namespace display_commanderhooks
