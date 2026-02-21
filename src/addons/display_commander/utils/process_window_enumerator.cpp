#include "process_window_enumerator.hpp"
#include <windows.h>
#include <psapi.h>
#include <tlhelp32.h>
#include <string>
#include <vector>
#include "../hooks/api_hooks.hpp"
#include "../utils/logging.hpp"
#include "overlay_window_detector.hpp"

namespace display_commander::utils {

// Callback for EnumWindows to collect windows for a specific process
struct WindowEnumData {
    DWORD process_id;
    std::vector<HWND> windows;
};

BOOL CALLBACK EnumWindowsProc(HWND hwnd, LPARAM lParam) {
    WindowEnumData* data = reinterpret_cast<WindowEnumData*>(lParam);
    DWORD window_pid = 0;
    GetWindowThreadProcessId(hwnd, &window_pid);

    if (window_pid == data->process_id) {
        data->windows.push_back(hwnd);
    }

    return TRUE;  // Continue enumeration
}

std::wstring GetProcessFullPath(DWORD process_id) {
    HANDLE h_process = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, process_id);
    if (h_process == nullptr) {
        return L"";
    }

    wchar_t process_path[MAX_PATH] = {};
    DWORD size = MAX_PATH;

    if (QueryFullProcessImageNameW(h_process, 0, process_path, &size)) {
        CloseHandle(h_process);
        return std::wstring(process_path);
    }

    CloseHandle(h_process);
    return L"";
}

void LogAllProcessesAndWindows() {
    LogInfo("=== Starting Process and Window Enumeration ===");

    // Create process snapshot
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        LogError("Failed to create process snapshot: %lu", GetLastError());
        return;
    }

    PROCESSENTRY32W process_entry = {};
    process_entry.dwSize = sizeof(PROCESSENTRY32W);

    if (!Process32FirstW(snapshot, &process_entry)) {
        LogError("Failed to get first process: %lu", GetLastError());
        CloseHandle(snapshot);
        return;
    }

    int process_count = 0;
    int window_count = 0;

    do {
        process_count++;
        DWORD pid = process_entry.th32ProcessID;

        // Get process full path
        std::wstring process_path = GetProcessFullPath(pid);
        std::wstring process_name(process_entry.szExeFile);

        // Log process information
        if (!process_path.empty()) {
            LogInfo("Process [%lu]: %ls", pid, process_path.c_str());
        } else {
            LogInfo("Process [%lu]: %ls (path unavailable)", pid, process_name.c_str());
        }

        // Enumerate windows for this process
        WindowEnumData enum_data = {};
        enum_data.process_id = pid;
        EnumWindows(EnumWindowsProc, reinterpret_cast<LPARAM>(&enum_data));

        if (!enum_data.windows.empty()) {
            LogInfo("  Windows for PID %lu:", pid);
            for (HWND hwnd : enum_data.windows) {
                window_count++;

                // Get window information (using existing GetWindowTitle from overlay_window_detector)
                std::wstring title = GetWindowTitle(hwnd);
                if (title.empty()) {
                    title = L"(No Title)";
                }
                bool is_visible = display_commanderhooks::IsWindowVisible_direct(hwnd);

                RECT window_rect = {};
                bool has_rect = GetWindowRect(hwnd, &window_rect) != FALSE;

                LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
                LONG_PTR ex_style = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);

                // Log window information
                if (has_rect) {
                    LogInfo(
                        "    HWND: 0x%p | Title: %ls | Visible: %s | Rect: (%ld,%ld)-(%ld,%ld) | Style: 0x%08lX | "
                        "ExStyle: 0x%08lX",
                        hwnd, title.c_str(), is_visible ? "Yes" : "No", window_rect.left, window_rect.top,
                        window_rect.right, window_rect.bottom, static_cast<unsigned long>(style),
                        static_cast<unsigned long>(ex_style));
                } else {
                    LogInfo("    HWND: 0x%p | Title: %ls | Visible: %s | Style: 0x%08lX | ExStyle: 0x%08lX", hwnd,
                            title.c_str(), is_visible ? "Yes" : "No", static_cast<unsigned long>(style),
                            static_cast<unsigned long>(ex_style));
                }
            }
        }

    } while (Process32NextW(snapshot, &process_entry));

    CloseHandle(snapshot);

    LogInfo("=== Enumeration Complete: %d processes, %d windows ===", process_count, window_count);
}

}  // namespace display_commander::utils
