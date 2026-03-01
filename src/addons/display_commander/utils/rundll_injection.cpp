// RunDLL entry points for process injection: StartAndInject and WaitAndInject.
// Moved from main_entry.cpp to simplify main_entry and to make future injection refactors easier.

#include "config/display_commander_config.hpp"
#include "utils/rundll_injection_helpers.hpp"

#include <windows.h>
#include <cstring>
#include <filesystem>
#include <format>
#include <iostream>
#include <string>
#include <vector>

#ifndef UNREFERENCED_PARAMETER
#define UNREFERENCED_PARAMETER(x) (void)(x)
#endif

// RunDLL entry point to start a game and inject into it
// Allows calling: rundll32.exe zzz_display_commander.addon64,StartAndInject "C:\Path\To\game.exe"
// The exe path should be passed as a command line argument
extern "C" __declspec(dllexport) void CALLBACK StartAndInject(HWND hwnd, HINSTANCE hInst, LPSTR lpszCmdLine,
                                                              int nCmdShow) {
    UNREFERENCED_PARAMETER(hwnd);
    UNREFERENCED_PARAMETER(hInst);
    UNREFERENCED_PARAMETER(nCmdShow);

    // Initialize config system for logging
    display_commander::config::DisplayCommanderConfigManager::GetInstance().Initialize();
    display_commander::config::DisplayCommanderConfigManager::GetInstance().SetAutoFlushLogs(true);

    // Parse exe path from command line
    std::string exe_path_ansi;
    if (lpszCmdLine != nullptr && strlen(lpszCmdLine) > 0) {
        // Remove quotes if present
        exe_path_ansi = lpszCmdLine;
        if (exe_path_ansi.length() >= 2 && exe_path_ansi.front() == '"' && exe_path_ansi.back() == '"') {
            exe_path_ansi = exe_path_ansi.substr(1, exe_path_ansi.length() - 2);
        }
    }

    if (exe_path_ansi.empty()) {
        OutputDebugStringA(
            "StartAndInject: No exe path provided. Usage: rundll32.exe zzz_display_commander.addon64,StartAndInject "
            "\"C:\\Path\\To\\game.exe\"");
        return;
    }

    // Convert to wide string
    int size_needed = MultiByteToWideChar(CP_ACP, 0, exe_path_ansi.c_str(), -1, nullptr, 0);
    if (size_needed <= 0) {
        OutputDebugStringA("StartAndInject: Failed to convert exe path to wide string");
        return;
    }

    std::vector<wchar_t> exe_path_wide(size_needed);
    MultiByteToWideChar(CP_ACP, 0, exe_path_ansi.c_str(), -1, exe_path_wide.data(), size_needed);
    std::wstring exe_path(exe_path_wide.data());

    // Check if file exists
    if (!std::filesystem::exists(exe_path)) {
        OutputDebugStringA(std::format("StartAndInject: Exe file not found: {}", exe_path_ansi).c_str());
        return;
    }

    OutputDebugStringA(std::format("StartAndInject: Starting process: {}", exe_path_ansi).c_str());

    // Start the process
    STARTUPINFOW si = {};
    PROCESS_INFORMATION pi = {};
    si.cb = sizeof(si);

    std::wstring command_line = L"\"" + exe_path + L"\"";

    if (!CreateProcessW(nullptr, command_line.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
        DWORD error = GetLastError();
        OutputDebugStringA(
            std::format("StartAndInject: Failed to start process: Error {} (0x{:X})", error, error).c_str());
        return;
    }

    // Close thread handle (we only need process handle)
    CloseHandle(pi.hThread);

    OutputDebugStringA(std::format("StartAndInject: Process started (PID {})", pi.dwProcessId).c_str());

    // Wait a bit for the process to initialize
    Sleep(500);

    // Check process architecture
    BOOL is_wow64 = FALSE;
    IsWow64Process(pi.hProcess, &is_wow64);

    // Get ReShade DLL path
    std::wstring dll_path = GetReShadeDllPath(is_wow64 != FALSE);
    if (dll_path.empty()) {
        OutputDebugStringA("StartAndInject: Failed to find ReShade DLL path");
        CloseHandle(pi.hProcess);
        return;
    }

    // Inject into the process
    bool success = InjectIntoProcess(pi.dwProcessId, dll_path);

    CloseHandle(pi.hProcess);

    if (success) {
        OutputDebugStringA(
            std::format("StartAndInject: Successfully started and injected into process (PID {})", pi.dwProcessId)
                .c_str());
    } else {
        OutputDebugStringA(
            std::format("StartAndInject: Failed to inject into process (PID {})", pi.dwProcessId).c_str());
    }
}

// RunDLL entry point to wait for process and inject
// Allows calling: rundll32.exe zzz_display_commander.addon64,WaitAndInject "game.exe"
// The exe name should be passed as a command line argument
extern "C" __declspec(dllexport) void CALLBACK WaitAndInject(HWND hwnd, HINSTANCE hInst, LPSTR lpszCmdLine,
                                                             int nCmdShow) {
    UNREFERENCED_PARAMETER(hwnd);
    UNREFERENCED_PARAMETER(hInst);
    UNREFERENCED_PARAMETER(nCmdShow);

    // Initialize config system for logging
    display_commander::config::DisplayCommanderConfigManager::GetInstance().Initialize();
    display_commander::config::DisplayCommanderConfigManager::GetInstance().SetAutoFlushLogs(true);

    // Parse exe name from command line
    std::string exe_name_ansi;
    if (lpszCmdLine != nullptr && strlen(lpszCmdLine) > 0) {
        // Remove quotes if present
        exe_name_ansi = lpszCmdLine;
        if (exe_name_ansi.length() >= 2 && exe_name_ansi.front() == '"' && exe_name_ansi.back() == '"') {
            exe_name_ansi = exe_name_ansi.substr(1, exe_name_ansi.length() - 2);
        }
    }

    if (exe_name_ansi.empty()) {
        OutputDebugStringA(
            "WaitAndInject: No exe name provided. Usage: rundll32.exe zzz_display_commander.addon64,WaitAndInject "
            "\"game.exe\"");
        return;
    }

    // Convert to wide string
    int size_needed = MultiByteToWideChar(CP_ACP, 0, exe_name_ansi.c_str(), -1, nullptr, 0);
    if (size_needed <= 0) {
        OutputDebugStringA("WaitAndInject: Failed to convert exe name to wide string");
        return;
    }

    std::vector<wchar_t> exe_name_wide(size_needed);
    MultiByteToWideChar(CP_ACP, 0, exe_name_ansi.c_str(), -1, exe_name_wide.data(), size_needed);
    std::wstring exe_name(exe_name_wide.data());

    // Extract just the filename if a path was provided (e.g., ".\BPSR_STREAM.exe" -> "BPSR_STREAM.exe")
    std::filesystem::path exe_path(exe_name);
    std::wstring exe_name_only = exe_path.filename().wstring();

    OutputDebugStringA(std::format("WaitAndInject: Waiting for process: {} (comparing against: {})", exe_name_ansi,
                                   std::string(exe_name_only.begin(), exe_name_only.end()))
                           .c_str());
    std::cout << "WaitAndInject: Waiting for process: " << exe_name_ansi
              << " (comparing against: " << std::string(exe_name_only.begin(), exe_name_only.end()) << ")" << std::endl;

    // Wait forever and inject into every new process that starts
    WaitForProcessAndInject(exe_name_only);
}
