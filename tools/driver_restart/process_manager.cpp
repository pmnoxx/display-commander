#include "driver_restart.h"
#include <psapi.h>

#pragma comment(lib, "psapi.lib")

namespace DriverRestart {

bool ProcessManager::KillProcess(const std::wstring& process_name) {
    auto process_ids = GetProcessIds(process_name);
    bool success = true;

    for (DWORD pid : process_ids) {
        HANDLE h_process = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
        if (h_process != nullptr) {
            if (TerminateProcess(h_process, 0) == 0) {
                success = false;
            }
            CloseHandle(h_process);
        } else {
            success = false;
        }
    }

    return success;
}

bool ProcessManager::RunAsUser(const std::wstring& command) {
    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi = {};

    // Create a copy of the command string since CreateProcessW can modify it
    std::wstring cmd_copy = command;

    if (CreateProcessW(nullptr, cmd_copy.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi) == 0) {
        return false;
    }

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return true;
}

std::vector<DWORD> ProcessManager::GetProcessIds(const std::wstring& process_name) {
    std::vector<DWORD> process_ids;

    HANDLE h_snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (h_snapshot == INVALID_HANDLE_VALUE) {
        return process_ids;
    }

    PROCESSENTRY32W pe32;
    pe32.dwSize = sizeof(PROCESSENTRY32W);

    if (Process32FirstW(h_snapshot, &pe32) != 0) {
        do {
            if (_wcsicmp(pe32.szExeFile, process_name.c_str()) == 0) {
                process_ids.push_back(pe32.th32ProcessID);
            }
        } while (Process32NextW(h_snapshot, &pe32) != 0);
    }

    CloseHandle(h_snapshot);
    return process_ids;
}

} // namespace DriverRestart