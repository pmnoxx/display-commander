#include "driver_restart.h"
#include <iostream>
#include <string>
#include <fstream>

void PrintUsage() {
    std::wcout << L"Driver Restart Tool\n";
    std::wcout << L"Usage: driver_restart.exe [options]\n\n";
    std::wcout << L"Options:\n";
    std::wcout << L"  /q, -q     Quiet mode - minimal output\n";
    std::wcout << L"  /h, -h     Show this help message\n";
    std::wcout << L"  /v, -v     Verbose mode - show detailed output\n";
    std::wcout << L"  /t, -t     Test mode - detect devices without restarting\n\n";
    std::wcout << L"This tool will automatically request administrator privileges if needed.\n";
    std::wcout << L"Without options, the tool will restart the graphics driver.\n";
}

bool ParseCommandLine(int argc, wchar_t* argv[], bool& quiet, bool& verbose, bool& test_mode) {
    quiet = false;
    verbose = false;
    test_mode = false;

    for (int i = 1; i < argc; i++) {
        std::wstring arg = argv[i];

        if (arg == L"/q" || arg == L"-q") {
            quiet = true;
        }
        else if (arg == L"/h" || arg == L"-h" || arg == L"/?" || arg == L"-?") {
            PrintUsage();
            return false;
        }
        else if (arg == L"/v" || arg == L"-v") {
            verbose = true;
        }
        else if (arg == L"/t" || arg == L"-t") {
            test_mode = true;
        }
        else {
            std::wcout << L"Unknown option: " << arg << L"\n";
            PrintUsage();
            return false;
        }
    }

    return true;
}

int wmain(int argc, wchar_t* argv[]) {
    bool quiet = false;
    bool verbose = false;
    bool test_mode = false;

    if (!ParseCommandLine(argc, argv, quiet, verbose, test_mode)) {
        return 1;
    }

    if (test_mode) {
        std::wcout << L"Driver Restart Tool - Test Mode\n";
        std::wcout << L"===============================\n\n";
        std::wcout << L"Detecting display devices (no restart will be performed)...\n\n";

        // Test device detection without requiring admin privileges
        HDEVINFO devices = SetupDiGetClassDevs(&GUID_DEVCLASS_DISPLAY, nullptr, nullptr, DIGCF_PRESENT);
        if (devices == INVALID_HANDLE_VALUE) {
            std::wcout << L"Error: Failed to get display device info list. Error: " << GetLastError() << L"\n";
            return 1;
        }

        int device_count = 0;
        DWORD index = 0;
        SP_DEVINFO_DATA device = {};
        device.cbSize = sizeof(device);

        while (SetupDiEnumDeviceInfo(devices, index, &device) != 0) {
            wchar_t device_desc[256] = {0};
            DWORD desc_size = sizeof(device_desc);
            SetupDiGetDeviceRegistryPropertyW(devices, &device, SPDRP_DEVICEDESC, nullptr,
                                             (PBYTE)device_desc, desc_size, &desc_size);

            wchar_t instance_id[256] = {0};
            DWORD id_size = sizeof(instance_id);
            SetupDiGetDeviceInstanceIdW(devices, &device, instance_id, id_size, &id_size);

            std::wcout << L"Device " << (device_count + 1) << L": " << device_desc << L"\n";
            std::wcout << L"  Instance ID: " << instance_id << L"\n";

            device_count++;
            index++;
        }

        SetupDiDestroyDeviceInfoList(devices);

        std::wcout << L"\nFound " << device_count << L" display devices.\n";
        std::wcout << L"Test mode completed.\n";
        return 0;
    }

    if (verbose) {
        std::wcout << L"Driver Restart Tool - Verbose Mode\n";
        std::wcout << L"==================================\n\n";

        // Create a log file for the elevated process
        std::wofstream log_file(L"driver_restart_log.txt");
        if (log_file.is_open()) {
            log_file << L"Driver Restart Tool - Verbose Mode\n";
            log_file << L"==================================\n\n";
            log_file.close();
        }
    }

    // Check if running on WOW64 (32-bit on 64-bit Windows)
    if (DriverRestart::Utils::IsWow64()) {
        if (verbose) {
            std::wcout << L"Running on WOW64, attempting to launch 64-bit version...\n";
        }

        // Try to launch the 64-bit version
        std::wstring command = L"driver_restart64.exe";
        for (int i = 1; i < argc; i++) {
            command += L" ";
            command += argv[i];
        }

        if (!DriverRestart::ProcessManager::RunAsUser(command)) {
            DriverRestart::Utils::ShowError(L"Failed to launch 64-bit version of driver_restart.exe");
            return 1;
        }

        return 0;
    }

    if (verbose) {
        std::wcout << L"Waiting for desktop to be ready...\n";
    }

    if (!DriverRestart::Utils::WaitForDesktop()) {
        DriverRestart::Utils::ShowError(L"Failed to access desktop. Please ensure you're running from an interactive session.");
        return 1;
    }

    if (verbose) {
        std::wcout << L"Desktop ready. Starting driver restart process...\n";
    }

    // Check for administrator privileges and request if needed
    if (!DriverRestart::Utils::IsRunningAsAdmin()) {
        if (verbose) {
            std::wcout << L"Administrator privileges required. Requesting elevation...\n";
        }

        if (!DriverRestart::Utils::RequestAdminPrivileges()) {
            DriverRestart::Utils::ShowError(L"Failed to obtain administrator privileges. Please run as administrator.");
            return 1;
        }

        // This point should not be reached as RequestAdminPrivileges exits the process
        return 1;
    }

    if (verbose) {
        std::wcout << L"Running with administrator privileges.\n";
    }

    // Enable required privileges for driver operations
    if (!DriverRestart::Utils::EnablePrivilege(L"SeLoadDriverPrivilege")) {
        if (verbose) {
            std::wcout << L"Warning: Could not enable SE_LOAD_DRIVER_NAME privilege.\n";
        }
    }

    if (!DriverRestart::Utils::EnablePrivilege(L"SeSystemEnvironmentPrivilege")) {
        if (verbose) {
            std::wcout << L"Warning: Could not enable SE_SYSTEM_ENVIRONMENT_NAME privilege.\n";
        }
    }

    if (verbose) {
        std::wcout << L"Stopping graphics driver...\n";
    }

    if (!DriverRestart::DriverManager::RestartDriver()) {
        if (verbose) {
            std::wcout << L"Error: Failed to restart graphics driver.\n";
        }
        return 1;
    }

    if (verbose) {
        std::wcout << L"Driver restart completed successfully.\n";
    } else if (!quiet) {
        std::wcout << L"Graphics driver restarted successfully.\n";
    }

    return 0;
}