#include "hid_hooks.hpp"
#include "../utils.hpp"
#include <MinHook.h>
#include <algorithm>
#include <winioctl.h>

namespace display_commanderhooks {

// Original function pointers
ReadFileEx_pfn ReadFileEx_Original = nullptr;

namespace {
// Hook state
std::atomic<bool> g_hid_hooks_installed{false};

// Statistics and file tracking
HidHookStats g_hid_hook_stats;
std::unordered_map<std::string, HidFileReadStats> g_hid_file_stats;
std::mutex g_hid_stats_mutex;
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
        std::string device_path = GetDevicePath(h_file);

        // Thread-safe access to file stats
        {
            std::lock_guard<std::mutex> lock(g_hid_stats_mutex);

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

    LogInfo("Enabling ReadFileEx hook...");
    if (MH_EnableHook(ReadFileEx) != MH_OK) {
        LogError("Failed to enable ReadFileEx hook");
        return false;
    }

    g_hid_hooks_installed.store(true);
    LogInfo("HID hooks installed successfully - ReadFileEx hook is now active");
    return true;
}

void UninstallHidHooks() {
    if (!g_hid_hooks_installed.load()) {
        LogInfo("HID hooks not installed");
        return;
    }

    // Disable ReadFileEx hook
    if (MH_DisableHook(ReadFileEx) != MH_OK) {
        LogError("Failed to disable ReadFileEx hook");
    }

    g_hid_hooks_installed.store(false);
    LogInfo("HID hooks uninstalled");
}

bool AreHidHooksInstalled() {
    return g_hid_hooks_installed.load();
}

// Statistics access functions
const std::unordered_map<std::string, HidFileReadStats>& GetHidFileStats() {
    return g_hid_file_stats;
}

const HidHookStats& GetHidHookStats() {
    return g_hid_hook_stats;
}

void ResetHidStatistics() {
    std::lock_guard<std::mutex> lock(g_hid_stats_mutex);
    g_hid_hook_stats.Reset();
    g_hid_file_stats.clear();
    LogInfo("HID statistics reset");
}

void ClearHidFileHistory() {
    std::lock_guard<std::mutex> lock(g_hid_stats_mutex);
    g_hid_file_stats.clear();
    LogInfo("HID file history cleared");
}

} // namespace display_commanderhooks
