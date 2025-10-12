#include "hid_hooks.hpp"
#include "../utils.hpp"
#include "../settings/experimental_tab_settings.hpp"
#include "../globals.hpp"
#include <MinHook.h>
#include <algorithm>
#include <winioctl.h>
#include <setupapi.h>
#include <devguid.h>
#include <hidsdi.h>
#include <hidpi.h>

// Define missing GUIDs if not available in headers
#ifndef GUID_DEVINTERFACE_HID
static const GUID GUID_DEVINTERFACE_HID = {0x4d1e55b2, 0xf16f, 0x11cf, {0x88, 0xcb, 0x00, 0x11, 0x11, 0x00, 0x00, 0x30}};
#endif
#ifndef GUID_DEVINTERFACE_KEYBOARD
static const GUID GUID_DEVINTERFACE_KEYBOARD = {0x884b96c3, 0x56ef, 0x11d1, {0xbc, 0x8c, 0x00, 0xa0, 0xc9, 0x14, 0x05, 0xdd}};
#endif
#ifndef GUID_DEVINTERFACE_MOUSE
static const GUID GUID_DEVINTERFACE_MOUSE = {0x378de44c, 0x56ef, 0x11d1, {0xbc, 0x8c, 0x00, 0xa0, 0xc9, 0x14, 0x05, 0xdd}};
#endif

namespace display_commanderhooks {

// Original function pointers
ReadFileEx_pfn ReadFileEx_Original = nullptr;
CreateFileW_pfn CreateFileW_Original = nullptr;
CreateFileA_pfn CreateFileA_Original = nullptr;
OpenFile_pfn OpenFile_Original = nullptr;
SetupDiGetClassDevs_pfn SetupDiGetClassDevs_Original = nullptr;
SetupDiEnumDeviceInterfaces_pfn SetupDiEnumDeviceInterfaces_Original = nullptr;
SetupDiGetDeviceInterfaceDetail_pfn SetupDiGetDeviceInterfaceDetail_Original = nullptr;
SetupDiEnumDeviceInfo_pfn SetupDiEnumDeviceInfo_Original = nullptr;
SetupDiGetDeviceRegistryProperty_pfn SetupDiGetDeviceRegistryProperty_Original = nullptr;
HidD_GetHidGuid_pfn HidD_GetHidGuid_Original = nullptr;
HidD_GetAttributes_pfn HidD_GetAttributes_Original = nullptr;
HidD_GetPreparsedData_pfn HidD_GetPreparsedData_Original = nullptr;
HidD_FreePreparsedData_pfn HidD_FreePreparsedData_Original = nullptr;

namespace {
// Hook state
std::atomic<bool> g_hid_hooks_installed{false};

// Statistics and file tracking
HidHookStats g_hid_hook_stats;
std::unordered_map<std::string, HidFileReadStats> g_hid_file_stats;
SRWLOCK g_hid_stats_lock = SRWLOCK_INIT;

// HID suppression state
std::atomic<uint64_t> g_hid_suppressed_calls{0};
} // anonymous namespace

// Helper function to check if a file handle is likely a HID device
bool IsHidDevice(HANDLE h_file) {
    if (h_file == INVALID_HANDLE_VALUE || h_file == nullptr) {
        return false;
    }

    // Get file type - HID devices typically have FILE_TYPE_UNKNOWN
    DWORD file_type = GetFileType(h_file);
    LogDebug("IsHidDevice: handle=%p, file_type=%lu", h_file, file_type);

    if (file_type != FILE_TYPE_UNKNOWN) {
        LogDebug("IsHidDevice: Not a device file (file_type=%lu)", file_type);
        return false; // Not a device file
    }

    // Try to get device path
    std::string device_path = GetDevicePath(h_file);
    LogDebug("IsHidDevice: device_path='%s'", device_path.c_str());

    if (device_path.empty() || device_path == "Unknown Device") {
        // If we can't get the device path, we can't determine if it's HID
        // But we can still track it as a potential device file
        LogDebug("IsHidDevice: Assuming HID device due to FILE_TYPE_UNKNOWN");
        return true; // Assume it might be a HID device if it's FILE_TYPE_UNKNOWN
    }

    // Check if it's a HID device path
    std::string lower_path = device_path;
    std::transform(lower_path.begin(), lower_path.end(), lower_path.begin(), ::tolower);
    bool is_hid = lower_path.find("hid") != std::string::npos ||
                  lower_path.find("usb") != std::string::npos ||
                  lower_path.find("input") != std::string::npos ||
                  lower_path.find("\\device\\") != std::string::npos; // Generic device path

    LogDebug("IsHidDevice: is_hid=%s, lower_path='%s'", is_hid ? "true" : "false", lower_path.c_str());
    return is_hid;
}

// Helper function to check if a file path is a HID device
bool IsHidDevicePath(const std::string& path) {
    if (path.empty()) {
        return false;
    }

    std::string lower_path = path;
    std::transform(lower_path.begin(), lower_path.end(), lower_path.begin(), ::tolower);

    return lower_path.find("hid") != std::string::npos ||
           lower_path.find("usb") != std::string::npos ||
           lower_path.find("input") != std::string::npos ||
           lower_path.find("\\device\\") != std::string::npos ||
           lower_path.find("\\??\\") != std::string::npos;
}

// Helper function to check if a wide file path is a HID device
bool IsHidDevicePath(const std::wstring& path) {
    if (path.empty()) {
        return false;
    }

    std::wstring lower_path = path;
    std::transform(lower_path.begin(), lower_path.end(), lower_path.begin(), ::towlower);

    return lower_path.find(L"hid") != std::wstring::npos ||
           lower_path.find(L"usb") != std::wstring::npos ||
           lower_path.find(L"input") != std::wstring::npos ||
           lower_path.find(L"\\device\\") != std::wstring::npos ||
           lower_path.find(L"\\??\\") != std::wstring::npos;
}

// Helper function to check if a GUID is HID-related
bool IsHidGuid(const GUID* guid) {
    if (guid == nullptr) {
        return false;
    }

    // Check against known HID GUIDs
    bool is_hid = (IsEqualGUID(*guid, GUID_DEVCLASS_HIDCLASS) != 0) ||
                  (IsEqualGUID(*guid, GUID_DEVINTERFACE_HID) != 0) ||
                  (IsEqualGUID(*guid, GUID_DEVINTERFACE_KEYBOARD) != 0) ||
                  (IsEqualGUID(*guid, GUID_DEVINTERFACE_MOUSE) != 0);

    if (is_hid) {
        LogDebug("IsHidGuid: HID GUID detected");
    } else {
        LogDebug("IsHidGuid: Non-HID GUID");
    }

    return is_hid;
}

// Helper function to get device path from handle
std::string GetDevicePath(HANDLE h_file) {
    if (h_file == INVALID_HANDLE_VALUE || h_file == nullptr) {
        return "";
    }

    // Try to get the final path name by handle
    char path_buffer[MAX_PATH];
    DWORD path_length = GetFinalPathNameByHandleA(h_file, path_buffer, MAX_PATH, VOLUME_NAME_DOS);

    if (path_length > 0 && path_length < MAX_PATH) {
        return std::string(path_buffer);
    }

    // If GetFinalPathNameByHandle fails, try to get basic info
    BY_HANDLE_FILE_INFORMATION file_info;
    if (GetFileInformationByHandle(h_file, &file_info) != 0) {
        // Create a more descriptive device identifier
        if ((file_info.dwFileAttributes & FILE_ATTRIBUTE_DEVICE) != 0) {
            // Create a unique identifier based on file attributes and handle
            char unique_id[64];
            snprintf(unique_id, sizeof(unique_id), "\\Device\\HID_Device_0x%p_%lu",
                    h_file, file_info.nFileIndexHigh);
            return std::string(unique_id);
        }
    }

    // Try to get more information about the handle
    DWORD file_type = GetFileType(h_file);

    // Try to get device information through other means
    DWORD device_type = 0;
    DWORD device_number = 0;

    // Try to get device information (this might work for some devices)
    if (DeviceIoControl(h_file, IOCTL_STORAGE_GET_DEVICE_NUMBER, nullptr, 0,
                       &device_number, sizeof(device_number), &device_type, nullptr) != 0) {
        char device_id[64];
        snprintf(device_id, sizeof(device_id), "\\Device\\Storage_Device_0x%p_Type%lu_Dev%lu",
                h_file, device_type, device_number);
        return std::string(device_id);
    }

    // Fallback to basic handle information
    char type_id[64];
    snprintf(type_id, sizeof(type_id), "\\Device\\Unknown_Device_0x%p_Type%lu", h_file, file_type);
    return std::string(type_id);
}

// Hooked ReadFileEx function
BOOL WINAPI ReadFileEx_Detour(HANDLE h_file, LPVOID lp_buffer, DWORD n_number_of_bytes_to_read,
                              LPOVERLAPPED lp_overlapped, LPOVERLAPPED_COMPLETION_ROUTINE lp_completion_routine) {

    // Increment total call counter
    g_hid_hook_stats.IncrementReadFileEx();

    LogDebug("ReadFileEx_Detour: handle=%p, bytes=%u", h_file, n_number_of_bytes_to_read);

    // Check if this is a HID device
    if (IsHidDevice(h_file)) {
        // Check if HID suppression is enabled from settings
        if (settings::g_experimentalTabSettings.suppress_hid_devices.GetValue()) {
            // Suppress the HID device access
            g_hid_suppressed_calls.fetch_add(1);
            LogDebug("HID ReadFileEx suppressed: handle=%p, bytes=%u", h_file, n_number_of_bytes_to_read);

            // Return FALSE to indicate failure (device access blocked)
            if (lp_overlapped != nullptr) {
                // For overlapped I/O, we need to set the error and call the completion routine
                lp_overlapped->Internal = ERROR_ACCESS_DENIED;
                lp_overlapped->InternalHigh = 0;
                if (lp_completion_routine != nullptr) {
                    lp_completion_routine(ERROR_ACCESS_DENIED, 0, lp_overlapped);
                }
            }
            return FALSE;
        }

        std::string device_path = GetDevicePath(h_file);

        // Thread-safe access to file stats
        {
            AcquireSRWLockExclusive(&g_hid_stats_lock);

            // Find existing entry or create new one
            auto it = g_hid_file_stats.find(device_path);

            if (it != g_hid_file_stats.end()) {
                // Update existing entry
                it->second.read_count.fetch_add(1);
                it->second.bytes_read.fetch_add(n_number_of_bytes_to_read);
                it->second.last_read = std::chrono::steady_clock::now();
            } else {
                // Create new entry
                auto [new_it, inserted] = g_hid_file_stats.try_emplace(device_path, device_path);
                auto& new_stats = new_it->second;
                new_stats.read_count.store(1);
                new_stats.bytes_read.store(n_number_of_bytes_to_read);
                g_hid_hook_stats.IncrementFilesTracked();

                // Log when we first discover a new device
                LogInfo("New HID device discovered: %s", device_path.c_str());
            }

            g_hid_hook_stats.AddBytesRead(n_number_of_bytes_to_read);

            ReleaseSRWLockExclusive(&g_hid_stats_lock);
        }

        LogDebug("HID ReadFileEx: %s, %u bytes", device_path.c_str(), n_number_of_bytes_to_read);
    } else {
        // Log non-HID ReadFileEx calls for debugging
        DWORD file_type = GetFileType(h_file);
        LogDebug("Non-HID ReadFileEx: handle=%p, file_type=%lu, bytes=%u", h_file, file_type, n_number_of_bytes_to_read);
    }

    // Call original function
    if (ReadFileEx_Original != nullptr) {
        return ReadFileEx_Original(h_file, lp_buffer, n_number_of_bytes_to_read, lp_overlapped, lp_completion_routine);
    }

    // Fallback to system ReadFileEx
    return ::ReadFileEx(h_file, lp_buffer, n_number_of_bytes_to_read, lp_overlapped, lp_completion_routine);
}

// Hooked CreateFileW function
HANDLE WINAPI CreateFileW_Detour(LPCWSTR lp_file_name, DWORD dw_desired_access, DWORD dw_share_mode,
                                 LPSECURITY_ATTRIBUTES lp_security_attributes, DWORD dw_creation_disposition,
                                 DWORD dw_flags_and_attributes, HANDLE h_template_file) {

    // Check if this is a HID device path
    if (lp_file_name != nullptr && IsHidDevicePath(std::wstring(lp_file_name))) {
        // Check if HID suppression is enabled from settings
        if (settings::g_experimentalTabSettings.suppress_hid_devices.GetValue()) {
            // Suppress the HID device creation
            g_hid_suppressed_calls.fetch_add(1);
            LogDebug("HID CreateFileW suppressed: %S", lp_file_name);

            // Return INVALID_HANDLE_VALUE to indicate failure
            SetLastError(ERROR_ACCESS_DENIED);
            return INVALID_HANDLE_VALUE;
        }
    }

    // Call original function
    if (CreateFileW_Original != nullptr) {
        return CreateFileW_Original(lp_file_name, dw_desired_access, dw_share_mode, lp_security_attributes,
                                   dw_creation_disposition, dw_flags_and_attributes, h_template_file);
    }

    // Fallback to system CreateFileW
    return ::CreateFileW(lp_file_name, dw_desired_access, dw_share_mode, lp_security_attributes,
                        dw_creation_disposition, dw_flags_and_attributes, h_template_file);
}

// Hooked CreateFileA function
HANDLE WINAPI CreateFileA_Detour(LPCSTR lp_file_name, DWORD dw_desired_access, DWORD dw_share_mode,
                                 LPSECURITY_ATTRIBUTES lp_security_attributes, DWORD dw_creation_disposition,
                                 DWORD dw_flags_and_attributes, HANDLE h_template_file) {

    // Check if this is a HID device path
    if (lp_file_name != nullptr && IsHidDevicePath(std::string(lp_file_name))) {
        // Check if HID suppression is enabled from settings
        if (settings::g_experimentalTabSettings.suppress_hid_devices.GetValue()) {
            // Suppress the HID device creation
            g_hid_suppressed_calls.fetch_add(1);
            LogDebug("HID CreateFileA suppressed: %s", lp_file_name);

            // Return INVALID_HANDLE_VALUE to indicate failure
            SetLastError(ERROR_ACCESS_DENIED);
            return INVALID_HANDLE_VALUE;
        }
    }

    // Call original function
    if (CreateFileA_Original != nullptr) {
        return CreateFileA_Original(lp_file_name, dw_desired_access, dw_share_mode, lp_security_attributes,
                                   dw_creation_disposition, dw_flags_and_attributes, h_template_file);
    }

    // Fallback to system CreateFileA
    return ::CreateFileA(lp_file_name, dw_desired_access, dw_share_mode, lp_security_attributes,
                        dw_creation_disposition, dw_flags_and_attributes, h_template_file);
}

// Hooked OpenFile function
HFILE WINAPI OpenFile_Detour(LPCSTR lp_file_name, LPOFSTRUCT lp_reopen_buff, UINT u_style) {

    // Check if this is a HID device path
    if (lp_file_name != nullptr && IsHidDevicePath(std::string(lp_file_name))) {
        // Check if HID suppression is enabled from settings
        if (settings::g_experimentalTabSettings.suppress_hid_devices.GetValue()) {
            // Suppress the HID device opening
            g_hid_suppressed_calls.fetch_add(1);
            LogDebug("HID OpenFile suppressed: %s", lp_file_name);

            // Return HFILE_ERROR to indicate failure
            if (lp_reopen_buff != nullptr) {
                lp_reopen_buff->cBytes = sizeof(OFSTRUCT);
                lp_reopen_buff->fFixedDisk = 0;
                lp_reopen_buff->nErrCode = ERROR_ACCESS_DENIED;
                lp_reopen_buff->Reserved1 = 0;
                lp_reopen_buff->Reserved2 = 0;
                lp_reopen_buff->szPathName[0] = '\0';
            }
            return HFILE_ERROR;
        }
    }

    // Call original function
    if (OpenFile_Original != nullptr) {
        return OpenFile_Original(lp_file_name, lp_reopen_buff, u_style);
    }

    // Fallback to system OpenFile
    return ::OpenFile(lp_file_name, lp_reopen_buff, u_style);
}

// Hooked SetupDiGetClassDevs function
HDEVINFO WINAPI SetupDiGetClassDevs_Detour(const GUID* class_guid, PCSTR enumerator, HWND hwnd_parent, DWORD flags) {

    // Always increment call counter
    g_hid_hook_stats.IncrementSetupDiGetClassDevs();

    // Log the GUID for debugging
    if (class_guid != nullptr) {
        LogDebug("SetupDiGetClassDevs called with GUID: {%08lX-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}",
                 class_guid->Data1, class_guid->Data2, class_guid->Data3,
                 class_guid->Data4[0], class_guid->Data4[1], class_guid->Data4[2], class_guid->Data4[3],
                 class_guid->Data4[4], class_guid->Data4[5], class_guid->Data4[6], class_guid->Data4[7]);
    }

    // Check if HID suppression is enabled from settings
    if (settings::g_experimentalTabSettings.suppress_hid_devices.GetValue()) {
        // Suppress ALL device enumeration when HID suppression is enabled (regardless of device type)
        g_hid_suppressed_calls.fetch_add(1);
        g_hid_hook_stats.IncrementSetupDiGetClassDevsSuppressed();
        LogDebug("SetupDiGetClassDevs: Suppressing ALL device class enumeration due to HID suppression");

        // Return INVALID_HANDLE_VALUE to indicate failure
        SetLastError(ERROR_ACCESS_DENIED);
        return INVALID_HANDLE_VALUE;
    }

    // Call original function
    if (SetupDiGetClassDevs_Original != nullptr) {
        return SetupDiGetClassDevs_Original(class_guid, enumerator, hwnd_parent, flags);
    }

    // Fallback to system SetupDiGetClassDevs
    return ::SetupDiGetClassDevs(class_guid, enumerator, hwnd_parent, flags);
}

// Hooked SetupDiEnumDeviceInterfaces function
BOOL WINAPI SetupDiEnumDeviceInterfaces_Detour(HDEVINFO device_info_set, PSP_DEVINFO_DATA device_info_data,
                                               const GUID* interface_class_guid, DWORD member_index,
                                               PSP_DEVICE_INTERFACE_DATA device_interface_data) {

    // Always increment call counter
    g_hid_hook_stats.IncrementSetupDiEnumDeviceInterfaces();

    // Log the GUID for debugging
    if (interface_class_guid != nullptr) {
        LogDebug("SetupDiEnumDeviceInterfaces called with GUID: {%08lX-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}",
                 interface_class_guid->Data1, interface_class_guid->Data2, interface_class_guid->Data3,
                 interface_class_guid->Data4[0], interface_class_guid->Data4[1], interface_class_guid->Data4[2], interface_class_guid->Data4[3],
                 interface_class_guid->Data4[4], interface_class_guid->Data4[5], interface_class_guid->Data4[6], interface_class_guid->Data4[7]);
    }

    // Check if HID suppression is enabled from settings
    if (settings::g_experimentalTabSettings.suppress_hid_devices.GetValue()) {
        // Suppress ALL device interface enumeration when HID suppression is enabled (regardless of device type)
        g_hid_suppressed_calls.fetch_add(1);
        g_hid_hook_stats.IncrementSetupDiEnumDeviceInterfacesSuppressed();
        LogDebug("SetupDiEnumDeviceInterfaces: Suppressing ALL device interface enumeration due to HID suppression");

        // Return FALSE to indicate no more devices
        SetLastError(ERROR_NO_MORE_ITEMS);
        return FALSE;
    }

    // Call original function
    if (SetupDiEnumDeviceInterfaces_Original != nullptr) {
        return SetupDiEnumDeviceInterfaces_Original(device_info_set, device_info_data, interface_class_guid,
                                                   member_index, device_interface_data);
    }

    // Fallback to system SetupDiEnumDeviceInterfaces
    return ::SetupDiEnumDeviceInterfaces(device_info_set, device_info_data, interface_class_guid,
                                        member_index, device_interface_data);
}

// Hooked HidD_GetHidGuid function
void WINAPI HidD_GetHidGuid_Detour(LPGUID hid_guid) {
    // Always increment call counter
    g_hid_hook_stats.IncrementHidDGetHidGuid();

    // Check if HID suppression is enabled from settings
    if (settings::g_experimentalTabSettings.suppress_hid_devices.GetValue()) {
        // Suppress ALL HID GUID retrieval when HID suppression is enabled (regardless of device type)
        g_hid_suppressed_calls.fetch_add(1);
        g_hid_hook_stats.IncrementHidDGetHidGuidSuppressed();
        LogDebug("HidD_GetHidGuid: Suppressing ALL HID GUID retrieval due to HID suppression");

        // Return a dummy GUID or leave it uninitialized
        if (hid_guid != nullptr) {
            ZeroMemory(hid_guid, sizeof(GUID));
        }
        return;
    }

    // Call original function
    if (HidD_GetHidGuid_Original != nullptr) {
        HidD_GetHidGuid_Original(hid_guid);
    } else {
        ::HidD_GetHidGuid(hid_guid);
    }
}

// Hooked HidD_GetAttributes function
BOOLEAN WINAPI HidD_GetAttributes_Detour(HANDLE hid_device_object, PHIDD_ATTRIBUTES attributes) {
    // Always increment call counter
    g_hid_hook_stats.IncrementHidDGetAttributes();

    // Check if HID suppression is enabled from settings
    if (settings::g_experimentalTabSettings.suppress_hid_devices.GetValue()) {
        // Suppress ALL HID attributes retrieval when HID suppression is enabled (regardless of device type)
        g_hid_suppressed_calls.fetch_add(1);
        g_hid_hook_stats.IncrementHidDGetAttributesSuppressed();
        LogDebug("HidD_GetAttributes: Suppressing ALL HID attributes retrieval due to HID suppression");

        // Return FALSE to indicate failure
        return FALSE;
    }

    // Call original function
    if (HidD_GetAttributes_Original != nullptr) {
        return HidD_GetAttributes_Original(hid_device_object, attributes);
    }

    return ::HidD_GetAttributes(hid_device_object, attributes);
}

// Hooked HidD_GetPreparsedData function
BOOLEAN WINAPI HidD_GetPreparsedData_Detour(HANDLE hid_device_object, PVOID* preparsed_data) {
    // Always increment call counter
    g_hid_hook_stats.IncrementHidDGetPreparsedData();

    // Check if HID suppression is enabled from settings
    if (settings::g_experimentalTabSettings.suppress_hid_devices.GetValue()) {
        // Suppress ALL HID preparsed data retrieval when HID suppression is enabled (regardless of device type)
        g_hid_suppressed_calls.fetch_add(1);
        g_hid_hook_stats.IncrementHidDGetPreparsedDataSuppressed();
        LogDebug("HidD_GetPreparsedData: Suppressing ALL HID preparsed data retrieval due to HID suppression");

        // Return FALSE to indicate failure
        return FALSE;
    }

    // Call original function
    if (HidD_GetPreparsedData_Original != nullptr) {
        return HidD_GetPreparsedData_Original(hid_device_object, preparsed_data);
    }

    return ::HidD_GetPreparsedData(hid_device_object, reinterpret_cast<PHIDP_PREPARSED_DATA*>(preparsed_data));
}

// Hooked HidD_FreePreparsedData function
BOOLEAN WINAPI HidD_FreePreparsedData_Detour(PVOID preparsed_data) {
    // Always increment call counter
    g_hid_hook_stats.IncrementHidDFreePreparsedData();

    // This function is safe to call even when suppression is enabled
    // Call original function
    if (HidD_FreePreparsedData_Original != nullptr) {
        return HidD_FreePreparsedData_Original(preparsed_data);
    }

    return ::HidD_FreePreparsedData(reinterpret_cast<PHIDP_PREPARSED_DATA>(preparsed_data));
}

// Hooked SetupDiGetDeviceInterfaceDetail function
BOOL WINAPI SetupDiGetDeviceInterfaceDetail_Detour(HDEVINFO device_info_set, PSP_DEVICE_INTERFACE_DATA device_interface_data,
                                                   PSP_DEVICE_INTERFACE_DETAIL_DATA device_interface_detail_data, DWORD device_interface_detail_data_size,
                                                   PDWORD required_size, PSP_DEVINFO_DATA device_info_data) {

    // Always increment call counter
    g_hid_hook_stats.IncrementSetupDiGetDeviceInterfaceDetail();

    // Check if HID suppression is enabled from settings
    if (settings::g_experimentalTabSettings.suppress_hid_devices.GetValue()) {
        // Suppress ALL device interface detail retrieval when HID suppression is enabled (regardless of device type)
        g_hid_suppressed_calls.fetch_add(1);
        g_hid_hook_stats.IncrementSetupDiGetDeviceInterfaceDetailSuppressed();
        LogDebug("SetupDiGetDeviceInterfaceDetail: Suppressing ALL device interface detail retrieval due to HID suppression");

        // Return FALSE to indicate failure
        SetLastError(ERROR_ACCESS_DENIED);
        return FALSE;
    }

    // Call original function
    if (SetupDiGetDeviceInterfaceDetail_Original != nullptr) {
        return SetupDiGetDeviceInterfaceDetail_Original(device_info_set, device_interface_data, device_interface_detail_data,
                                                       device_interface_detail_data_size, required_size, device_info_data);
    }

    return ::SetupDiGetDeviceInterfaceDetail(device_info_set, device_interface_data, device_interface_detail_data,
                                            device_interface_detail_data_size, required_size, device_info_data);
}

// Hooked SetupDiEnumDeviceInfo function
BOOL WINAPI SetupDiEnumDeviceInfo_Detour(HDEVINFO device_info_set, DWORD member_index, PSP_DEVINFO_DATA device_info_data) {

    // Always increment call counter
    g_hid_hook_stats.IncrementSetupDiEnumDeviceInfo();

    // Check if HID suppression is enabled from settings
    if (settings::g_experimentalTabSettings.suppress_hid_devices.GetValue()) {
        // Suppress ALL device info enumeration when HID suppression is enabled (regardless of device type)
        g_hid_suppressed_calls.fetch_add(1);
        g_hid_hook_stats.IncrementSetupDiEnumDeviceInfoSuppressed();
        LogDebug("SetupDiEnumDeviceInfo: Suppressing ALL device info enumeration due to HID suppression");

        // Return FALSE to indicate no more devices
        SetLastError(ERROR_NO_MORE_ITEMS);
        return FALSE;
    }

    // Call original function
    if (SetupDiEnumDeviceInfo_Original != nullptr) {
        return SetupDiEnumDeviceInfo_Original(device_info_set, member_index, device_info_data);
    }

    return ::SetupDiEnumDeviceInfo(device_info_set, member_index, device_info_data);
}

// Hooked SetupDiGetDeviceRegistryProperty function
BOOL WINAPI SetupDiGetDeviceRegistryProperty_Detour(HDEVINFO device_info_set, PSP_DEVINFO_DATA device_info_data,
                                                    DWORD property, PDWORD property_reg_data_type, PBYTE property_buffer,
                                                    DWORD property_buffer_size, PDWORD required_size) {

    // Always increment call counter
    g_hid_hook_stats.IncrementSetupDiGetDeviceRegistryProperty();

    // Check if HID suppression is enabled from settings
    if (settings::g_experimentalTabSettings.suppress_hid_devices.GetValue()) {
        // Suppress ALL device registry property retrieval when HID suppression is enabled (regardless of device type)
        g_hid_suppressed_calls.fetch_add(1);
        g_hid_hook_stats.IncrementSetupDiGetDeviceRegistryPropertySuppressed();
        LogDebug("SetupDiGetDeviceRegistryProperty: Suppressing ALL device registry property retrieval due to HID suppression");

        // Return FALSE to indicate failure
        SetLastError(ERROR_ACCESS_DENIED);
        return FALSE;
    }

    // Call original function
    if (SetupDiGetDeviceRegistryProperty_Original != nullptr) {
        return SetupDiGetDeviceRegistryProperty_Original(device_info_set, device_info_data, property, property_reg_data_type,
                                                        property_buffer, property_buffer_size, required_size);
    }

    return ::SetupDiGetDeviceRegistryProperty(device_info_set, device_info_data, property, property_reg_data_type,
                                             property_buffer, property_buffer_size, required_size);
}

// Hook management functions
bool InstallHidHooks() {
    if (g_hid_hooks_installed.load()) {
        LogInfo("HID hooks already installed");
        return true;
    }

    // Initialize MinHook (only if not already initialized)
    MH_STATUS init_status = MH_Initialize();
    if (init_status != MH_OK && init_status != MH_ERROR_ALREADY_INITIALIZED) {
        LogError("Failed to initialize MinHook for HID hooks - Status: %d", init_status);
        return false;
    }

    if (init_status == MH_ERROR_ALREADY_INITIALIZED) {
        LogInfo("MinHook already initialized, proceeding with HID hooks");
    } else {
        LogInfo("MinHook initialized successfully for HID hooks");
    }

    // Hook ReadFileEx
    LogInfo("Creating ReadFileEx hook...");
    if (MH_CreateHook(ReadFileEx, ReadFileEx_Detour, reinterpret_cast<LPVOID*>(&ReadFileEx_Original)) != MH_OK) {
        LogError("Failed to create ReadFileEx hook");
        return false;
    }

    // Hook CreateFileW
    LogInfo("Creating CreateFileW hook...");
    if (MH_CreateHook(CreateFileW, CreateFileW_Detour, reinterpret_cast<LPVOID*>(&CreateFileW_Original)) != MH_OK) {
        LogError("Failed to create CreateFileW hook");
        return false;
    }

    // Hook CreateFileA
    LogInfo("Creating CreateFileA hook...");
    if (MH_CreateHook(CreateFileA, CreateFileA_Detour, reinterpret_cast<LPVOID*>(&CreateFileA_Original)) != MH_OK) {
        LogError("Failed to create CreateFileA hook");
        return false;
    }

    // Hook OpenFile
    LogInfo("Creating OpenFile hook...");
    if (MH_CreateHook(OpenFile, OpenFile_Detour, reinterpret_cast<LPVOID*>(&OpenFile_Original)) != MH_OK) {
        LogError("Failed to create OpenFile hook");
        return false;
    }

    // Hook SetupDiGetClassDevs
    LogInfo("Creating SetupDiGetClassDevs hook...");
    if (MH_CreateHook(SetupDiGetClassDevs, SetupDiGetClassDevs_Detour, reinterpret_cast<LPVOID*>(&SetupDiGetClassDevs_Original)) != MH_OK) {
        LogError("Failed to create SetupDiGetClassDevs hook");
        return false;
    }

    // Hook SetupDiEnumDeviceInterfaces
    LogInfo("Creating SetupDiEnumDeviceInterfaces hook...");
    if (MH_CreateHook(SetupDiEnumDeviceInterfaces, SetupDiEnumDeviceInterfaces_Detour, reinterpret_cast<LPVOID*>(&SetupDiEnumDeviceInterfaces_Original)) != MH_OK) {
        LogError("Failed to create SetupDiEnumDeviceInterfaces hook");
        return false;
    }

    // Hook SetupDiGetDeviceInterfaceDetail
    LogInfo("Creating SetupDiGetDeviceInterfaceDetail hook...");
    if (MH_CreateHook(SetupDiGetDeviceInterfaceDetail, SetupDiGetDeviceInterfaceDetail_Detour, reinterpret_cast<LPVOID*>(&SetupDiGetDeviceInterfaceDetail_Original)) != MH_OK) {
        LogError("Failed to create SetupDiGetDeviceInterfaceDetail hook");
        return false;
    }

    // Hook SetupDiEnumDeviceInfo
    LogInfo("Creating SetupDiEnumDeviceInfo hook...");
    if (MH_CreateHook(SetupDiEnumDeviceInfo, SetupDiEnumDeviceInfo_Detour, reinterpret_cast<LPVOID*>(&SetupDiEnumDeviceInfo_Original)) != MH_OK) {
        LogError("Failed to create SetupDiEnumDeviceInfo hook");
        return false;
    }

    // Hook SetupDiGetDeviceRegistryProperty
    LogInfo("Creating SetupDiGetDeviceRegistryProperty hook...");
    if (MH_CreateHook(SetupDiGetDeviceRegistryProperty, SetupDiGetDeviceRegistryProperty_Detour, reinterpret_cast<LPVOID*>(&SetupDiGetDeviceRegistryProperty_Original)) != MH_OK) {
        LogError("Failed to create SetupDiGetDeviceRegistryProperty hook");
        return false;
    }

    // Hook HidD_GetHidGuid
    LogInfo("Creating HidD_GetHidGuid hook...");
    if (MH_CreateHook(HidD_GetHidGuid, HidD_GetHidGuid_Detour, reinterpret_cast<LPVOID*>(&HidD_GetHidGuid_Original)) != MH_OK) {
        LogError("Failed to create HidD_GetHidGuid hook");
        return false;
    }

    // Hook HidD_GetAttributes
    LogInfo("Creating HidD_GetAttributes hook...");
    if (MH_CreateHook(HidD_GetAttributes, HidD_GetAttributes_Detour, reinterpret_cast<LPVOID*>(&HidD_GetAttributes_Original)) != MH_OK) {
        LogError("Failed to create HidD_GetAttributes hook");
        return false;
    }

    // Hook HidD_GetPreparsedData
    LogInfo("Creating HidD_GetPreparsedData hook...");
    if (MH_CreateHook(HidD_GetPreparsedData, HidD_GetPreparsedData_Detour, reinterpret_cast<LPVOID*>(&HidD_GetPreparsedData_Original)) != MH_OK) {
        LogError("Failed to create HidD_GetPreparsedData hook");
        return false;
    }

    // Hook HidD_FreePreparsedData
    LogInfo("Creating HidD_FreePreparsedData hook...");
    if (MH_CreateHook(HidD_FreePreparsedData, HidD_FreePreparsedData_Detour, reinterpret_cast<LPVOID*>(&HidD_FreePreparsedData_Original)) != MH_OK) {
        LogError("Failed to create HidD_FreePreparsedData hook");
        return false;
    }

    // Enable all hooks
    LogInfo("Enabling all HID hooks...");
    if (MH_EnableHook(ReadFileEx) != MH_OK) {
        LogError("Failed to enable ReadFileEx hook");
        return false;
    }
    if (MH_EnableHook(CreateFileW) != MH_OK) {
        LogError("Failed to enable CreateFileW hook");
        return false;
    }
    if (MH_EnableHook(CreateFileA) != MH_OK) {
        LogError("Failed to enable CreateFileA hook");
        return false;
    }
    if (MH_EnableHook(OpenFile) != MH_OK) {
        LogError("Failed to enable OpenFile hook");
        return false;
    }
    if (MH_EnableHook(SetupDiGetClassDevs) != MH_OK) {
        LogError("Failed to enable SetupDiGetClassDevs hook");
        return false;
    }
    if (MH_EnableHook(SetupDiEnumDeviceInterfaces) != MH_OK) {
        LogError("Failed to enable SetupDiEnumDeviceInterfaces hook");
        return false;
    }
    if (MH_EnableHook(SetupDiGetDeviceInterfaceDetail) != MH_OK) {
        LogError("Failed to enable SetupDiGetDeviceInterfaceDetail hook");
        return false;
    }
    if (MH_EnableHook(SetupDiEnumDeviceInfo) != MH_OK) {
        LogError("Failed to enable SetupDiEnumDeviceInfo hook");
        return false;
    }
    if (MH_EnableHook(SetupDiGetDeviceRegistryProperty) != MH_OK) {
        LogError("Failed to enable SetupDiGetDeviceRegistryProperty hook");
        return false;
    }
    if (MH_EnableHook(HidD_GetHidGuid) != MH_OK) {
        LogError("Failed to enable HidD_GetHidGuid hook");
        return false;
    }
    if (MH_EnableHook(HidD_GetAttributes) != MH_OK) {
        LogError("Failed to enable HidD_GetAttributes hook");
        return false;
    }
    if (MH_EnableHook(HidD_GetPreparsedData) != MH_OK) {
        LogError("Failed to enable HidD_GetPreparsedData hook");
        return false;
    }
    if (MH_EnableHook(HidD_FreePreparsedData) != MH_OK) {
        LogError("Failed to enable HidD_FreePreparsedData hook");
        return false;
    }

    g_hid_hooks_installed.store(true);
    LogInfo("HID hooks installed successfully - All HID device access and enumeration APIs are now hooked");
    return true;
}

void UninstallHidHooks() {
    if (!g_hid_hooks_installed.load()) {
        LogInfo("HID hooks not installed");
        return;
    }

    // Disable all hooks
    LogInfo("Disabling all HID hooks...");
    if (MH_DisableHook(ReadFileEx) != MH_OK) {
        LogError("Failed to disable ReadFileEx hook");
    }
    if (MH_DisableHook(CreateFileW) != MH_OK) {
        LogError("Failed to disable CreateFileW hook");
    }
    if (MH_DisableHook(CreateFileA) != MH_OK) {
        LogError("Failed to disable CreateFileA hook");
    }
    if (MH_DisableHook(OpenFile) != MH_OK) {
        LogError("Failed to disable OpenFile hook");
    }
    if (MH_DisableHook(SetupDiGetClassDevs) != MH_OK) {
        LogError("Failed to disable SetupDiGetClassDevs hook");
    }
    if (MH_DisableHook(SetupDiEnumDeviceInterfaces) != MH_OK) {
        LogError("Failed to disable SetupDiEnumDeviceInterfaces hook");
    }
    if (MH_DisableHook(SetupDiGetDeviceInterfaceDetail) != MH_OK) {
        LogError("Failed to disable SetupDiGetDeviceInterfaceDetail hook");
    }
    if (MH_DisableHook(SetupDiEnumDeviceInfo) != MH_OK) {
        LogError("Failed to disable SetupDiEnumDeviceInfo hook");
    }
    if (MH_DisableHook(SetupDiGetDeviceRegistryProperty) != MH_OK) {
        LogError("Failed to disable SetupDiGetDeviceRegistryProperty hook");
    }
    if (MH_DisableHook(HidD_GetHidGuid) != MH_OK) {
        LogError("Failed to disable HidD_GetHidGuid hook");
    }
    if (MH_DisableHook(HidD_GetAttributes) != MH_OK) {
        LogError("Failed to disable HidD_GetAttributes hook");
    }
    if (MH_DisableHook(HidD_GetPreparsedData) != MH_OK) {
        LogError("Failed to disable HidD_GetPreparsedData hook");
    }
    if (MH_DisableHook(HidD_FreePreparsedData) != MH_OK) {
        LogError("Failed to disable HidD_FreePreparsedData hook");
    }

    g_hid_hooks_installed.store(false);
    LogInfo("HID hooks uninstalled");
}

bool AreHidHooksInstalled() {
    return g_hid_hooks_installed.load();
}

// Statistics access functions
std::unordered_map<std::string, HidFileReadStats> GetHidFileStats() {
    AcquireSRWLockShared(&g_hid_stats_lock);
    auto copy = g_hid_file_stats;
    ReleaseSRWLockShared(&g_hid_stats_lock);
    return copy;
}

const HidHookStats& GetHidHookStats() {
    return g_hid_hook_stats;
}

void ResetHidStatistics() {
    AcquireSRWLockExclusive(&g_hid_stats_lock);
    g_hid_hook_stats.Reset();
    g_hid_file_stats.clear();
    ReleaseSRWLockExclusive(&g_hid_stats_lock);
    LogInfo("HID statistics reset");
}

void ClearHidFileHistory() {
    AcquireSRWLockExclusive(&g_hid_stats_lock);
    g_hid_file_stats.clear();
    ReleaseSRWLockExclusive(&g_hid_stats_lock);
    LogInfo("HID file history cleared");
}

// HID suppression functions
uint64_t GetHidSuppressedCallsCount() {
    return g_hid_suppressed_calls.load();
}

void ResetHidSuppressionStats() {
    g_hid_suppressed_calls.store(0);
    LogInfo("HID suppression statistics reset");
}

} // namespace display_commanderhooks
