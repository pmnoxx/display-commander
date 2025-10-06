#pragma once

#include <windows.h>
#include <devguid.h>
#include <setupapi.h>
#include <tlhelp32.h>
#include <string>
#include <vector>

#pragma comment(lib, "setupapi.lib")

namespace DriverRestart {

// Constants
constexpr int TEXT_SIZE = 256;
constexpr DWORD SLEEP_AFTER_DISABLE = 100;
constexpr DWORD SLEEP_AFTER_ENABLE = 3500;
constexpr DWORD DESKTOP_WAIT_TIMEOUT = 20;
constexpr DWORD DESKTOP_WAIT_INTERVAL = 200;

// Process management
class ProcessManager {
public:
    static bool KillProcess(const std::wstring& process_name);
    static bool RunAsUser(const std::wstring& command);
    static std::vector<DWORD> GetProcessIds(const std::wstring& process_name);
};

// Window position management
class WindowManager {
public:
    static void SaveWindows();
    static void RestoreWindows();
private:
    static BOOL CALLBACK EnumWindowsProc(HWND hwnd, LPARAM l_param);
    struct WindowInfo {
        HWND hwnd;
        WINDOWPLACEMENT placement;
    };
    static std::vector<WindowInfo> saved_windows;
};

// Driver management
class DriverManager {
public:
    static int SetDriverState(DWORD state);
    static bool StopDriver();
    static bool StartDriver();
    static bool RestartDriver();
    static bool RestartDriverAlternative();
private:
    static bool StopGraphicsProcesses();
    static bool StartGraphicsProcesses();
    static bool FixTaskbar();
    static bool RefreshNotifyIcons();
    static bool RefreshNotifyWindow(HWND window);
    static bool RestartDisplayAdapters();
    static bool RestartDisplayAdaptersWMI();
};

// Utility functions
class Utils {
public:
    static bool IsWow64();
    static bool WaitForDesktop();
    static void ShowError(const std::wstring& message);
    static void ShowInfo(const std::wstring& message);
    static bool IsRunningAsAdmin();
    static bool RequestAdminPrivileges();
    static bool EnablePrivilege(const std::wstring& privilege_name);
};

} // namespace DriverRestart