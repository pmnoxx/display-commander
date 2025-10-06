#include "driver_restart.h"
#include <iostream>
#include <fstream>

namespace DriverRestart {

void LogToFile(const std::wstring& message) {
    std::wofstream log_file(L"driver_restart_log.txt", std::ios::app);
    if (log_file.is_open()) {
        log_file << message << std::endl;
        log_file.close();
    }
}

int DriverManager::SetDriverState(DWORD state) {
    // Ensure we have the required privileges
    if (!Utils::IsRunningAsAdmin()) {
        std::wcout << L"Error: Not running as administrator\n";
        return 0;
    }

    HDEVINFO devices = SetupDiGetClassDevs(&GUID_DEVCLASS_DISPLAY, nullptr, nullptr, DIGCF_PRESENT);
    if (devices == INVALID_HANDLE_VALUE) {
        std::wcout << L"Error: Failed to get display device info list. Error: " << GetLastError() << L"\n";
        return 0;
    }

    int result = 0;
    DWORD index = 0;
    SP_DEVINFO_DATA device = {};
    device.cbSize = sizeof(device);

    std::wcout << L"Enumerating display devices...\n";
    LogToFile(L"Enumerating display devices...");

    while (SetupDiEnumDeviceInfo(devices, index, &device) != 0) {
        // Get device description
        wchar_t device_desc[256] = {0};
        DWORD desc_size = sizeof(device_desc);
        SetupDiGetDeviceRegistryPropertyW(devices, &device, SPDRP_DEVICEDESC, nullptr,
                                         (PBYTE)device_desc, desc_size, &desc_size);

        std::wcout << L"Found device: " << device_desc << L"\n";

        SP_PROPCHANGE_PARAMS params = {};
        params.ClassInstallHeader.cbSize = sizeof(params.ClassInstallHeader);
        params.ClassInstallHeader.InstallFunction = DIF_PROPERTYCHANGE;
        params.StateChange = state;
        params.Scope = DICS_FLAG_GLOBAL;
        params.HwProfile = 0;

        if (SetupDiSetClassInstallParams(devices, &device, &params.ClassInstallHeader, sizeof(params)) != 0) {
            if (SetupDiCallClassInstaller(DIF_PROPERTYCHANGE, devices, &device) != 0) {
                std::wcout << L"Successfully changed state for: " << device_desc << L"\n";
                result++;
            } else {
                DWORD error = GetLastError();
                std::wcout << L"Failed to change state for: " << device_desc << L" (CallClassInstaller failed, Error: " << error << L")\n";
            }
        } else {
            DWORD error = GetLastError();
            std::wcout << L"Failed to set install params for: " << device_desc << L" (SetClassInstallParams failed, Error: " << error << L")\n";
        }

        index++;
    }

    if (index == 0) {
        std::wcout << L"Warning: No display devices found\n";
    }

    std::wcout << L"Total devices processed: " << index << L", Successfully changed: " << result << L"\n";

    SetupDiDestroyDeviceInfoList(devices);
    return result;
}

bool DriverManager::StopDriver() {
    std::wcout << L"Starting driver stop process...\n";

    WindowManager::SaveWindows();
    std::wcout << L"Window positions saved.\n";

    int disabled_count = SetDriverState(DICS_DISABLE);
    if (disabled_count == 0) {
        std::wcout << L"Error: Failed to disable any display drivers\n";
        return false;
    }

    std::wcout << L"Disabled " << disabled_count << L" display drivers.\n";

    StopGraphicsProcesses();
    std::wcout << L"Stopped graphics control processes.\n";

    Sleep(SLEEP_AFTER_DISABLE);
    std::wcout << L"Waiting " << SLEEP_AFTER_DISABLE << L"ms after disable...\n";

    RefreshNotifyIcons();
    std::wcout << L"Refreshed notification icons.\n";

    return true;
}

bool DriverManager::StartDriver() {
    std::wcout << L"Starting driver enable process...\n";

    int enabled_count = SetDriverState(DICS_ENABLE);
    if (enabled_count == 0) {
        std::wcout << L"Error: Failed to enable any display drivers\n";
        return false;
    }

    std::wcout << L"Enabled " << enabled_count << L" display drivers.\n";

    FixTaskbar();
    std::wcout << L"Fixed taskbar components.\n";

    Sleep(SLEEP_AFTER_ENABLE);
    std::wcout << L"Waiting " << SLEEP_AFTER_ENABLE << L"ms after enable...\n";

    WindowManager::RestoreWindows();
    std::wcout << L"Restored window positions.\n";

    StartGraphicsProcesses();
    std::wcout << L"Started graphics control processes.\n";

    Sleep(SLEEP_AFTER_DISABLE);
    std::wcout << L"Final wait completed.\n";

    return true;
}

bool DriverManager::RestartDriver() {
    std::wcout << L"Starting driver restart process...\n";
    LogToFile(L"Starting driver restart process...");

    if (!StopDriver()) {
        std::wcout << L"StopDriver failed, trying alternative method...\n";
        LogToFile(L"StopDriver failed, trying alternative method...");
        return RestartDriverAlternative();
    }

    if (!StartDriver()) {
        std::wcout << L"StartDriver failed, trying alternative method...\n";
        LogToFile(L"StartDriver failed, trying alternative method...");
        return RestartDriverAlternative();
    }

    std::wcout << L"Driver restart completed successfully.\n";
    LogToFile(L"Driver restart completed successfully.");
    return true;
}

bool DriverManager::RestartDriverAlternative() {
    std::wcout << L"Trying alternative driver restart methods...\n";

    // Try method 1: Direct disable/enable approach
    std::wcout << L"\n--- Method 1: Direct Disable/Enable ---\n";
    if (RestartDisplayAdapters()) {
        std::wcout << L"Method 1 succeeded.\n";
        return true;
    }

    // Try method 2: WMI approach (currently falls back to SetupAPI)
    std::wcout << L"\n--- Method 2: WMI Approach ---\n";
    if (RestartDisplayAdaptersWMI()) {
        std::wcout << L"Method 2 succeeded.\n";
        return true;
    }

    // Try method 3: Force restart using devcon-like approach
    std::wcout << L"\n--- Method 3: Force Restart ---\n";
    std::wcout << L"Attempting force restart of display adapters...\n";

    HDEVINFO devices = SetupDiGetClassDevs(&GUID_DEVCLASS_DISPLAY, nullptr, nullptr, DIGCF_PRESENT);
    if (devices != INVALID_HANDLE_VALUE) {
        int restarted = 0;
        DWORD index = 0;
        SP_DEVINFO_DATA device = {};
        device.cbSize = sizeof(device);

        while (SetupDiEnumDeviceInfo(devices, index, &device) != 0) {
            // Try to restart using CM_REMOVE and CM_REENUMERATE
            SP_PROPCHANGE_PARAMS params = {};
            params.ClassInstallHeader.cbSize = sizeof(params.ClassInstallHeader);
            params.ClassInstallHeader.InstallFunction = DIF_PROPERTYCHANGE;
            params.StateChange = DICS_PROPCHANGE;
            params.Scope = DICS_FLAG_GLOBAL;
            params.HwProfile = 0;

            if (SetupDiSetClassInstallParams(devices, &device, &params.ClassInstallHeader, sizeof(params)) != 0) {
                if (SetupDiCallClassInstaller(DIF_PROPERTYCHANGE, devices, &device) != 0) {
                    std::wcout << L"Force restart succeeded for device " << index << L"\n";
                    restarted++;
                } else {
                    DWORD error = GetLastError();
                    std::wcout << L"Force restart failed for device " << index << L" (Error: " << error << L")\n";
                }
            }

            index++;
        }

        SetupDiDestroyDeviceInfoList(devices);

        if (restarted > 0) {
            std::wcout << L"Method 3 succeeded - restarted " << restarted << L" devices.\n";
            return true;
        }
    }

    std::wcout << L"All restart methods failed.\n";
    return false;
}

bool DriverManager::RestartDisplayAdapters() {
    std::wcout << L"Attempting to restart display adapters using alternative method...\n";

    // Try to find and restart display adapters using a more direct approach
    HDEVINFO devices = SetupDiGetClassDevs(&GUID_DEVCLASS_DISPLAY, nullptr, nullptr, DIGCF_PRESENT);
    if (devices == INVALID_HANDLE_VALUE) {
        std::wcout << L"Failed to get display device info list. Error: " << GetLastError() << L"\n";
        return false;
    }

    int restarted = 0;
    DWORD index = 0;
    SP_DEVINFO_DATA device = {};
    device.cbSize = sizeof(device);

    while (SetupDiEnumDeviceInfo(devices, index, &device) != 0) {
        // Get device instance ID
        wchar_t instance_id[256] = {0};
        DWORD id_size = sizeof(instance_id);
        if (SetupDiGetDeviceInstanceIdW(devices, &device, instance_id, id_size, &id_size) != 0) {
            std::wcout << L"Found device instance: " << instance_id << L"\n";

            // Try to disable the device first
            SP_PROPCHANGE_PARAMS params = {};
            params.ClassInstallHeader.cbSize = sizeof(params.ClassInstallHeader);
            params.ClassInstallHeader.InstallFunction = DIF_PROPERTYCHANGE;
            params.StateChange = DICS_DISABLE;
            params.Scope = DICS_FLAG_GLOBAL;
            params.HwProfile = 0;

            std::wcout << L"Attempting to disable device: " << instance_id << L"\n";
            if (SetupDiSetClassInstallParams(devices, &device, &params.ClassInstallHeader, sizeof(params)) != 0) {
                if (SetupDiCallClassInstaller(DIF_PROPERTYCHANGE, devices, &device) != 0) {
                    std::wcout << L"Successfully disabled device: " << instance_id << L"\n";

                    // Wait for disable to take effect
                    Sleep(2000);

                    // Now enable it
                    params.StateChange = DICS_ENABLE;
                    std::wcout << L"Attempting to enable device: " << instance_id << L"\n";
                    if (SetupDiSetClassInstallParams(devices, &device, &params.ClassInstallHeader, sizeof(params)) != 0) {
                        if (SetupDiCallClassInstaller(DIF_PROPERTYCHANGE, devices, &device) != 0) {
                            std::wcout << L"Successfully restarted device: " << instance_id << L"\n";
                            restarted++;
                        } else {
                            DWORD error = GetLastError();
                            std::wcout << L"Failed to enable device: " << instance_id << L" (Error: " << error << L")\n";
                        }
                    } else {
                        DWORD error = GetLastError();
                        std::wcout << L"Failed to set enable params for: " << instance_id << L" (Error: " << error << L")\n";
                    }
                } else {
                    DWORD error = GetLastError();
                    std::wcout << L"Failed to disable device: " << instance_id << L" (Error: " << error << L")\n";
                }
            } else {
                DWORD error = GetLastError();
                std::wcout << L"Failed to set disable params for: " << instance_id << L" (Error: " << error << L")\n";
            }
        }

        index++;
    }

    SetupDiDestroyDeviceInfoList(devices);

    std::wcout << L"Restarted " << restarted << L" display adapters.\n";
    return restarted > 0;
}

bool DriverManager::RestartDisplayAdaptersWMI() {
    std::wcout << L"Attempting to restart display adapters using WMI method...\n";

    // This is a placeholder for WMI implementation
    // WMI would require additional COM initialization and is more complex
    // For now, we'll use the SetupAPI method with better error handling

    std::wcout << L"WMI method not implemented yet, falling back to SetupAPI method.\n";
    return RestartDisplayAdapters();
}

bool DriverManager::StopGraphicsProcesses() {
    // Stop common graphics control panel processes
    ProcessManager::KillProcess(L"MOM.exe");      // AMD Catalyst Control Center
    ProcessManager::KillProcess(L"CCC.exe");      // AMD Catalyst Control Center
    ProcessManager::KillProcess(L"RadeonSoftware.exe");  // AMD Radeon Software
    ProcessManager::KillProcess(L"RadeonSettings.exe");  // AMD Radeon Settings
    ProcessManager::KillProcess(L"cnext.exe");    // AMD Radeon Settings
    return true;
}

bool DriverManager::StartGraphicsProcesses() {
    // Try to restart AMD Radeon Software if available
    std::wstring program_files = L"C:\\Program Files\\AMD\\CNext\\CNext";
    if (GetFileAttributesW(program_files.c_str()) != INVALID_FILE_ATTRIBUTES) {
        ProcessManager::RunAsUser(L"cncmd.exe restart");
    }

    // Try to restart AMD Catalyst Control Center if available
    program_files = L"C:\\Program Files (x86)\\AMD\\ATI.ACE\\Core-Static";
    if (GetFileAttributesW(program_files.c_str()) != INVALID_FILE_ATTRIBUTES) {
        ProcessManager::RunAsUser(L"CLI.exe start");
    }

    return true;
}

bool DriverManager::FixTaskbar() {
    // Restart Windows shell components that might be affected
    ProcessManager::KillProcess(L"ShellExperienceHost.exe");
    ProcessManager::KillProcess(L"SearchUI.exe");
    return true;
}

bool DriverManager::RefreshNotifyIcons() {
    HWND parent = FindWindowW(L"Shell_TrayWnd", nullptr);
    if (parent == nullptr) {
        return false;
    }

    parent = FindWindowExW(parent, nullptr, L"TrayNotifyWnd", nullptr);
    if (parent == nullptr) {
        return false;
    }

    parent = FindWindowExW(parent, nullptr, L"SysPager", nullptr);
    if (parent == nullptr) {
        return false;
    }

    HWND window = FindWindowExW(parent, nullptr, L"ToolbarWindow32", L"Notification Area");
    if (window != nullptr) {
        RefreshNotifyWindow(window);
    }

    window = FindWindowExW(parent, nullptr, L"ToolbarWindow32", L"User Promoted Notification Area");
    if (window != nullptr) {
        RefreshNotifyWindow(window);
    }

    parent = FindWindowW(L"NotifyIconOverflowWindow", nullptr);
    if (parent != nullptr) {
        window = FindWindowExW(parent, nullptr, L"ToolbarWindow32", L"Overflow Notification Area");
        if (window != nullptr) {
            RefreshNotifyWindow(window);
        }
    }

    return true;
}

bool DriverManager::RefreshNotifyWindow(HWND window) {
    RECT rect;
    if (GetClientRect(window, &rect) == 0) {
        return false;
    }

    for (int y = 0; y < rect.bottom; y += 4) {
        for (int x = 0; x < rect.right; x += 4) {
            PostMessageW(window, WM_MOUSEMOVE, 0, (y << 16) | (x & 65535));
        }
    }

    return true;
}

} // namespace DriverRestart