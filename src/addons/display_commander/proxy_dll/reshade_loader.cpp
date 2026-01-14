/*
 * ReShade DLL Loader
 * Loads ReShade64.dll or ReShade32.dll from the game directory when Display Commander is in proxy mode
 */

#include "reshade_loader.hpp"
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>


// Helper function to write to DisplayCommander.log before the logger is initialized
namespace {
void WriteToLogFile(const std::string& message, const char* level = "INFO") {
    try {
        // Get the game executable path to determine log file location
        WCHAR exe_path[MAX_PATH];
        if (GetModuleFileNameW(nullptr, exe_path, MAX_PATH) == 0) {
            OutputDebugStringA("DisplayCommander: Failed to get executable path for logging\n");
            return;
        }

        std::filesystem::path exe_dir = std::filesystem::path(exe_path).parent_path();
        std::filesystem::path log_path = exe_dir / "DisplayCommander.log";

        // Get current time
        SYSTEMTIME time;
        GetLocalTime(&time);

        // Format timestamp
        std::ostringstream timestamp;
        timestamp << std::setfill('0') << std::setw(2) << time.wHour << ":" << std::setw(2) << time.wMinute << ":"
                  << std::setw(2) << time.wSecond << "." << std::setw(3) << time.wMilliseconds;

        // Format log entry
        std::ostringstream log_entry;
        log_entry << "[" << timestamp.str() << "] [" << level << "] " << message << "\n";

        // Write to log file
        std::ofstream log_file(log_path, std::ios::app);
        if (log_file.is_open()) {
            log_file << log_entry.str();
            log_file.flush();
            log_file.close();
        }
    } catch (...) {
        // Ignore errors to prevent crashes during logging
        OutputDebugStringA("DisplayCommander: Error writing to log file\n");
    }
}
}  // anonymous namespace

HMODULE LoadReShadeDll() {
    // Get the game executable path (the process that loaded us)
    WCHAR exe_path[MAX_PATH];
    if (GetModuleFileNameW(nullptr, exe_path, MAX_PATH) == 0) {
        WriteToLogFile("Failed to get game executable path", "ERROR");
        return nullptr;
    }

    std::filesystem::path exe_dir = std::filesystem::path(exe_path).parent_path();

    // Determine which ReShade DLL to load based on architecture
#ifdef _WIN64
    std::filesystem::path reshade_path = exe_dir / L"ReShade64.dll";
#else
    std::filesystem::path reshade_path = exe_dir / L"ReShade32.dll";
#endif

    // Check if ReShade DLL exists
    if (!std::filesystem::exists(reshade_path)) {
        std::ostringstream msg;
        msg << "ReShade DLL not found at: " << reshade_path.string();
        WriteToLogFile(msg.str(), "ERROR");
        return nullptr;
    }

    // Set environment variable to disable ReShade loading check
    SetEnvironmentVariableW(L"RESHADE_DISABLE_LOADING_CHECK", L"1");

    // Load the ReShade DLL
    HMODULE reshade_module = LoadLibraryW(reshade_path.c_str());
    if (reshade_module == nullptr) {
        DWORD error = GetLastError();
        std::ostringstream msg;
        msg << "Failed to load ReShade DLL from " << reshade_path.string() << " (error: " << error << ")";
        WriteToLogFile(msg.str(), "ERROR");
        return nullptr;
    }

    std::ostringstream msg;
    msg << "Successfully loaded ReShade DLL from: " << reshade_path.string();
    WriteToLogFile(msg.str(), "INFO");
    return reshade_module;
}
