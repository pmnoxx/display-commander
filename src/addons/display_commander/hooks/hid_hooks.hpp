#pragma once

#include <atomic>
#include <string>
#include <unordered_map>
#include <mutex>
#include <chrono>
#include <windows.h>

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
};

// HID hook call statistics
struct HidHookStats {
    std::atomic<uint64_t> total_readfileex_calls{0};
    std::atomic<uint64_t> total_files_tracked{0};
    std::atomic<uint64_t> total_bytes_read{0};

    void IncrementReadFileEx() { total_readfileex_calls.fetch_add(1); }
    void IncrementFilesTracked() { total_files_tracked.fetch_add(1); }
    void AddBytesRead(uint64_t bytes) { total_bytes_read.fetch_add(bytes); }
    void Reset() {
        total_readfileex_calls.store(0);
        total_files_tracked.store(0);
        total_bytes_read.store(0);
    }
};

// Function pointer types for HID hooks
using ReadFileEx_pfn = BOOL(WINAPI *)(HANDLE, LPVOID, DWORD, LPOVERLAPPED, LPOVERLAPPED_COMPLETION_ROUTINE);

// Original function pointers
extern ReadFileEx_pfn ReadFileEx_Original;

// Hooked functions
BOOL WINAPI ReadFileEx_Detour(HANDLE h_file, LPVOID lp_buffer, DWORD n_number_of_bytes_to_read,
                              LPOVERLAPPED lp_overlapped, LPOVERLAPPED_COMPLETION_ROUTINE lp_completion_routine);

// Hook management
bool InstallHidHooks();
void UninstallHidHooks();
bool AreHidHooksInstalled();

// Statistics and file tracking
const std::unordered_map<std::string, HidFileReadStats>& GetHidFileStats();
const HidHookStats& GetHidHookStats();
void ResetHidStatistics();
void ClearHidFileHistory();

// Helper functions
bool IsHidDevice(HANDLE h_file);
std::string GetDevicePath(HANDLE h_file);

} // namespace display_commanderhooks
