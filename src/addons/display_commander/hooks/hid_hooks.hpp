#pragma once

#include <atomic>
#include <string>
#include <unordered_map>
#include <chrono>
#include <windows.h>
#include <setupapi.h>
#include <devguid.h>
#include <hidsdi.h>
#include <hidpi.h>

namespace display_commanderhooks {

// HID file read statistics structure
struct HidFileReadStats {
    std::string file_path;
    std::atomic<uint64_t> read_count{0};
    std::atomic<uint64_t> bytes_read{0};
    std::chrono::steady_clock::time_point first_read;
    std::chrono::steady_clock::time_point last_read;

    // Default constructor
    HidFileReadStats()
        : first_read(std::chrono::steady_clock::now()), last_read(std::chrono::steady_clock::now()) {}

    // Constructor with path
    explicit HidFileReadStats(const std::string& path)
        : file_path(path), first_read(std::chrono::steady_clock::now()), last_read(std::chrono::steady_clock::now()) {}

    // Copy constructor
    HidFileReadStats(const HidFileReadStats& other)
        : file_path(other.file_path)
        , read_count(other.read_count.load())
        , bytes_read(other.bytes_read.load())
        , first_read(other.first_read)
        , last_read(other.last_read) {}

    // Copy assignment operator
    HidFileReadStats& operator=(const HidFileReadStats& other) {
        if (this != &other) {
            file_path = other.file_path;
            read_count.store(other.read_count.load());
            bytes_read.store(other.bytes_read.load());
            first_read = other.first_read;
            last_read = other.last_read;
        }
        return *this;
    }
};

// HID hook call statistics
struct HidHookStats {
    std::atomic<uint64_t> total_readfileex_calls{0};
    std::atomic<uint64_t> total_files_tracked{0};
    std::atomic<uint64_t> total_bytes_read{0};

    // Individual API counters
    std::atomic<uint64_t> setupdi_getclassdevs_calls{0};
    std::atomic<uint64_t> setupdi_enumdeviceinterfaces_calls{0};
    std::atomic<uint64_t> setupdi_getdeviceinterfacedetail_calls{0};
    std::atomic<uint64_t> setupdi_enumdeviceinfo_calls{0};
    std::atomic<uint64_t> setupdi_getdeviceregistryproperty_calls{0};
    std::atomic<uint64_t> hidd_gethidguid_calls{0};
    std::atomic<uint64_t> hidd_getattributes_calls{0};
    std::atomic<uint64_t> hidd_getpreparseddata_calls{0};
    std::atomic<uint64_t> hidd_freepreparseddata_calls{0};

    // Suppression counters for each API
    std::atomic<uint64_t> setupdi_getclassdevs_suppressed{0};
    std::atomic<uint64_t> setupdi_enumdeviceinterfaces_suppressed{0};
    std::atomic<uint64_t> setupdi_getdeviceinterfacedetail_suppressed{0};
    std::atomic<uint64_t> setupdi_enumdeviceinfo_suppressed{0};
    std::atomic<uint64_t> setupdi_getdeviceregistryproperty_suppressed{0};
    std::atomic<uint64_t> hidd_gethidguid_suppressed{0};
    std::atomic<uint64_t> hidd_getattributes_suppressed{0};
    std::atomic<uint64_t> hidd_getpreparseddata_suppressed{0};
    std::atomic<uint64_t> hidd_freepreparseddata_suppressed{0};

    void IncrementReadFileEx() { total_readfileex_calls.fetch_add(1); }
    void IncrementFilesTracked() { total_files_tracked.fetch_add(1); }
    void AddBytesRead(uint64_t bytes) { total_bytes_read.fetch_add(bytes); }

    // Individual API increment methods
    void IncrementSetupDiGetClassDevs() { setupdi_getclassdevs_calls.fetch_add(1); }
    void IncrementSetupDiEnumDeviceInterfaces() { setupdi_enumdeviceinterfaces_calls.fetch_add(1); }
    void IncrementSetupDiGetDeviceInterfaceDetail() { setupdi_getdeviceinterfacedetail_calls.fetch_add(1); }
    void IncrementSetupDiEnumDeviceInfo() { setupdi_enumdeviceinfo_calls.fetch_add(1); }
    void IncrementSetupDiGetDeviceRegistryProperty() { setupdi_getdeviceregistryproperty_calls.fetch_add(1); }
    void IncrementHidDGetHidGuid() { hidd_gethidguid_calls.fetch_add(1); }
    void IncrementHidDGetAttributes() { hidd_getattributes_calls.fetch_add(1); }
    void IncrementHidDGetPreparsedData() { hidd_getpreparseddata_calls.fetch_add(1); }
    void IncrementHidDFreePreparsedData() { hidd_freepreparseddata_calls.fetch_add(1); }

    // Suppression increment methods
    void IncrementSetupDiGetClassDevsSuppressed() { setupdi_getclassdevs_suppressed.fetch_add(1); }
    void IncrementSetupDiEnumDeviceInterfacesSuppressed() { setupdi_enumdeviceinterfaces_suppressed.fetch_add(1); }
    void IncrementSetupDiGetDeviceInterfaceDetailSuppressed() { setupdi_getdeviceinterfacedetail_suppressed.fetch_add(1); }
    void IncrementSetupDiEnumDeviceInfoSuppressed() { setupdi_enumdeviceinfo_suppressed.fetch_add(1); }
    void IncrementSetupDiGetDeviceRegistryPropertySuppressed() { setupdi_getdeviceregistryproperty_suppressed.fetch_add(1); }
    void IncrementHidDGetHidGuidSuppressed() { hidd_gethidguid_suppressed.fetch_add(1); }
    void IncrementHidDGetAttributesSuppressed() { hidd_getattributes_suppressed.fetch_add(1); }
    void IncrementHidDGetPreparsedDataSuppressed() { hidd_getpreparseddata_suppressed.fetch_add(1); }
    void IncrementHidDFreePreparsedDataSuppressed() { hidd_freepreparseddata_suppressed.fetch_add(1); }

    void Reset() {
        total_readfileex_calls.store(0);
        total_files_tracked.store(0);
        total_bytes_read.store(0);
        setupdi_getclassdevs_calls.store(0);
        setupdi_enumdeviceinterfaces_calls.store(0);
        setupdi_getdeviceinterfacedetail_calls.store(0);
        setupdi_enumdeviceinfo_calls.store(0);
        setupdi_getdeviceregistryproperty_calls.store(0);
        hidd_gethidguid_calls.store(0);
        hidd_getattributes_calls.store(0);
        hidd_getpreparseddata_calls.store(0);
        hidd_freepreparseddata_calls.store(0);
        setupdi_getclassdevs_suppressed.store(0);
        setupdi_enumdeviceinterfaces_suppressed.store(0);
        setupdi_getdeviceinterfacedetail_suppressed.store(0);
        setupdi_enumdeviceinfo_suppressed.store(0);
        setupdi_getdeviceregistryproperty_suppressed.store(0);
        hidd_gethidguid_suppressed.store(0);
        hidd_getattributes_suppressed.store(0);
        hidd_getpreparseddata_suppressed.store(0);
        hidd_freepreparseddata_suppressed.store(0);
    }
};

// Function pointer types for HID hooks
using ReadFileEx_pfn = BOOL(WINAPI *)(HANDLE, LPVOID, DWORD, LPOVERLAPPED, LPOVERLAPPED_COMPLETION_ROUTINE);
using CreateFileW_pfn = HANDLE(WINAPI *)(LPCWSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
using CreateFileA_pfn = HANDLE(WINAPI *)(LPCSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
using OpenFile_pfn = HFILE(WINAPI *)(LPCSTR, LPOFSTRUCT, UINT);
using SetupDiGetClassDevs_pfn = HDEVINFO(WINAPI *)(const GUID*, PCSTR, HWND, DWORD);
using SetupDiEnumDeviceInterfaces_pfn = BOOL(WINAPI *)(HDEVINFO, PSP_DEVINFO_DATA, const GUID*, DWORD, PSP_DEVICE_INTERFACE_DATA);
using SetupDiGetDeviceInterfaceDetail_pfn = BOOL(WINAPI *)(HDEVINFO, PSP_DEVICE_INTERFACE_DATA, PSP_DEVICE_INTERFACE_DETAIL_DATA, DWORD, PDWORD, PSP_DEVINFO_DATA);
using SetupDiEnumDeviceInfo_pfn = BOOL(WINAPI *)(HDEVINFO, DWORD, PSP_DEVINFO_DATA);
using SetupDiGetDeviceRegistryProperty_pfn = BOOL(WINAPI *)(HDEVINFO, PSP_DEVINFO_DATA, DWORD, PDWORD, PBYTE, DWORD, PDWORD);
using HidD_GetHidGuid_pfn = void(WINAPI *)(LPGUID);
using HidD_GetAttributes_pfn = BOOLEAN(WINAPI *)(HANDLE, PHIDD_ATTRIBUTES);
using HidD_GetPreparsedData_pfn = BOOLEAN(WINAPI *)(HANDLE, PVOID*);
using HidD_FreePreparsedData_pfn = BOOLEAN(WINAPI *)(PVOID);

// Original function pointers
extern ReadFileEx_pfn ReadFileEx_Original;
extern CreateFileW_pfn CreateFileW_Original;
extern CreateFileA_pfn CreateFileA_Original;
extern OpenFile_pfn OpenFile_Original;
extern SetupDiGetClassDevs_pfn SetupDiGetClassDevs_Original;
extern SetupDiEnumDeviceInterfaces_pfn SetupDiEnumDeviceInterfaces_Original;
extern SetupDiGetDeviceInterfaceDetail_pfn SetupDiGetDeviceInterfaceDetail_Original;
extern SetupDiEnumDeviceInfo_pfn SetupDiEnumDeviceInfo_Original;
extern SetupDiGetDeviceRegistryProperty_pfn SetupDiGetDeviceRegistryProperty_Original;
extern HidD_GetHidGuid_pfn HidD_GetHidGuid_Original;
extern HidD_GetAttributes_pfn HidD_GetAttributes_Original;
extern HidD_GetPreparsedData_pfn HidD_GetPreparsedData_Original;
extern HidD_FreePreparsedData_pfn HidD_FreePreparsedData_Original;

// Hooked functions
BOOL WINAPI ReadFileEx_Detour(HANDLE h_file, LPVOID lp_buffer, DWORD n_number_of_bytes_to_read,
                              LPOVERLAPPED lp_overlapped, LPOVERLAPPED_COMPLETION_ROUTINE lp_completion_routine);
HANDLE WINAPI CreateFileW_Detour(LPCWSTR lp_file_name, DWORD dw_desired_access, DWORD dw_share_mode,
                                 LPSECURITY_ATTRIBUTES lp_security_attributes, DWORD dw_creation_disposition,
                                 DWORD dw_flags_and_attributes, HANDLE h_template_file);
HANDLE WINAPI CreateFileA_Detour(LPCSTR lp_file_name, DWORD dw_desired_access, DWORD dw_share_mode,
                                 LPSECURITY_ATTRIBUTES lp_security_attributes, DWORD dw_creation_disposition,
                                 DWORD dw_flags_and_attributes, HANDLE h_template_file);
HFILE WINAPI OpenFile_Detour(LPCSTR lp_file_name, LPOFSTRUCT lp_reopen_buff, UINT u_style);
HDEVINFO WINAPI SetupDiGetClassDevs_Detour(const GUID* class_guid, PCSTR enumerator, HWND hwnd_parent, DWORD flags);
BOOL WINAPI SetupDiEnumDeviceInterfaces_Detour(HDEVINFO device_info_set, PSP_DEVINFO_DATA device_info_data,
                                               const GUID* interface_class_guid, DWORD member_index,
                                               PSP_DEVICE_INTERFACE_DATA device_interface_data);
BOOL WINAPI SetupDiGetDeviceInterfaceDetail_Detour(HDEVINFO device_info_set, PSP_DEVICE_INTERFACE_DATA device_interface_data,
                                                   PSP_DEVICE_INTERFACE_DETAIL_DATA device_interface_detail_data, DWORD device_interface_detail_data_size,
                                                   PDWORD required_size, PSP_DEVINFO_DATA device_info_data);
BOOL WINAPI SetupDiEnumDeviceInfo_Detour(HDEVINFO device_info_set, DWORD member_index, PSP_DEVINFO_DATA device_info_data);
BOOL WINAPI SetupDiGetDeviceRegistryProperty_Detour(HDEVINFO device_info_set, PSP_DEVINFO_DATA device_info_data,
                                                    DWORD property, PDWORD property_reg_data_type, PBYTE property_buffer,
                                                    DWORD property_buffer_size, PDWORD required_size);
void WINAPI HidD_GetHidGuid_Detour(LPGUID hid_guid);
BOOLEAN WINAPI HidD_GetAttributes_Detour(HANDLE hid_device_object, PHIDD_ATTRIBUTES attributes);
BOOLEAN WINAPI HidD_GetPreparsedData_Detour(HANDLE hid_device_object, PVOID* preparsed_data);
BOOLEAN WINAPI HidD_FreePreparsedData_Detour(PVOID preparsed_data);

// Hook management
bool InstallHidHooks();
void UninstallHidHooks();
bool AreHidHooksInstalled();

// Statistics and file tracking
std::unordered_map<std::string, HidFileReadStats> GetHidFileStats();
const HidHookStats& GetHidHookStats();
void ResetHidStatistics();
void ClearHidFileHistory();

// HID suppression functions
uint64_t GetHidSuppressedCallsCount();
void ResetHidSuppressionStats();

// Helper functions
bool IsHidDevice(HANDLE h_file);
std::string GetDevicePath(HANDLE h_file);
bool IsHidDevicePath(const std::string& path);
bool IsHidDevicePath(const std::wstring& path);
bool IsHidGuid(const GUID* guid);

} // namespace display_commanderhooks
