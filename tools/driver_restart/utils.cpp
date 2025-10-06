#include "driver_restart.h"
#include <shellapi.h>

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "advapi32.lib")

namespace DriverRestart {

bool Utils::IsWow64() {
    BOOL result = FALSE;
    if (IsWow64Process(GetCurrentProcess(), &result) == 0) {
        return false;
    }
    return result != FALSE;
}

bool Utils::WaitForDesktop() {
    for (int passes = 1; passes <= DESKTOP_WAIT_TIMEOUT; passes++) {
        HDESK desktop = OpenInputDesktop(0, FALSE, MAXIMUM_ALLOWED);
        if (desktop != nullptr) {
            CloseDesktop(desktop);
            return true;
        }

        if (passes == DESKTOP_WAIT_TIMEOUT) {
            return false;
        }

        Sleep(DESKTOP_WAIT_INTERVAL);
    }

    return false;
}

void Utils::ShowError(const std::wstring& message) {
    MessageBoxW(nullptr, message.c_str(), L"Driver Restart - Error", MB_ICONERROR | MB_OK);
}

void Utils::ShowInfo(const std::wstring& message) {
    MessageBoxW(nullptr, message.c_str(), L"Driver Restart - Information", MB_ICONINFORMATION | MB_OK);
}

bool Utils::IsRunningAsAdmin() {
    BOOL is_admin = FALSE;
    PSID admin_group = nullptr;

    // Create a SID for the Administrators group
    SID_IDENTIFIER_AUTHORITY nt_authority = SECURITY_NT_AUTHORITY;
    if (AllocateAndInitializeSid(&nt_authority, 2, SECURITY_BUILTIN_DOMAIN_RID,
                                 DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0, &admin_group)) {
        CheckTokenMembership(nullptr, admin_group, &is_admin);
        FreeSid(admin_group);
    }

    return is_admin != FALSE;
}

bool Utils::RequestAdminPrivileges() {
    if (IsRunningAsAdmin()) {
        return true;
    }

    // Get the current executable path
    wchar_t exe_path[MAX_PATH];
    if (GetModuleFileNameW(nullptr, exe_path, MAX_PATH) == 0) {
        return false;
    }

    // Get command line arguments
    wchar_t* cmd_line = GetCommandLineW();
    if (!cmd_line) {
        return false;
    }

    // Extract just the arguments (skip the executable name)
    wchar_t* args = wcschr(cmd_line, L' ');
    if (!args) {
        args = L""; // No arguments
    } else {
        args++; // Skip the space
    }

    // Launch with UAC elevation using cmd to ensure console output is visible
    std::wstring cmd_args = L"/c \"\"";
    cmd_args += exe_path;
    cmd_args += L"\" ";
    cmd_args += args;
    cmd_args += L" & pause\"";

    SHELLEXECUTEINFOW sei = {};
    sei.cbSize = sizeof(sei);
    sei.lpVerb = L"runas";
    sei.lpFile = L"cmd.exe";
    sei.lpParameters = cmd_args.c_str();
    sei.nShow = SW_SHOW;
    sei.fMask = SEE_MASK_NOCLOSEPROCESS;

    if (ShellExecuteExW(&sei)) {
        if (sei.hProcess) {
            // Wait for the elevated process to complete
            WaitForSingleObject(sei.hProcess, INFINITE);
            CloseHandle(sei.hProcess);
        }
        // Exit current process since elevated version will run
        ExitProcess(0);
    }

    return false;
}

bool Utils::EnablePrivilege(const std::wstring& privilege_name) {
    HANDLE token;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token) == 0) {
        return false;
    }

    LUID luid;
    if (LookupPrivilegeValueW(nullptr, privilege_name.c_str(), &luid) == 0) {
        CloseHandle(token);
        return false;
    }

    TOKEN_PRIVILEGES tp;
    tp.PrivilegeCount = 1;
    tp.Privileges[0].Luid = luid;
    tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

    bool result = AdjustTokenPrivileges(token, FALSE, &tp, sizeof(tp), nullptr, nullptr) != 0;
    CloseHandle(token);

    return result;
}

} // namespace DriverRestart