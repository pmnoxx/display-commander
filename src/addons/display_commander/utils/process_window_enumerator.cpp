// Source Code <Display Commander>

// Group 1 — Source Code (Display Commander)
#include "process_window_enumerator.hpp"
#include "overlay_window_detector.hpp"
#include "../hooks/api_hooks.hpp"
#include "../utils/logging.hpp"

// Group 2 — ReShade / ImGui
// (none)

// Group 3 — Standard C++
#include <atomic>
#include <memory>
#include <string>
#include <vector>

// Group 4 — Windows.h
#include <windows.h>

// Group 5 — Other Windows SDK
#include <psapi.h>
#include <tlhelp32.h>

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

namespace {

std::wstring BuildDisplayCommanderMutexName(DWORD process_id) {
    wchar_t name[64] = {};
    // Local\ limits to current session; zero-pad PID for stable length
    swprintf_s(name, L"Local\\DisplayCommander_Game_%08lu", static_cast<unsigned long>(process_id));
    return std::wstring(name);
}

HANDLE g_display_commander_process_mutex = nullptr;

}  // namespace

void RegisterCurrentProcessWithDisplayCommanderMutex() {
    if (g_display_commander_process_mutex != nullptr) {
        return;
    }

    DWORD pid = GetCurrentProcessId();
    std::wstring mutex_name = BuildDisplayCommanderMutexName(pid);

    HANDLE h_mutex = CreateMutexW(nullptr, FALSE, mutex_name.c_str());
    if (h_mutex == nullptr) {
        DWORD error = GetLastError();
        LogWarn("RunningGames: CreateMutexW failed for PID %lu (error=%lu)", static_cast<unsigned long>(pid), error);
        return;
    }

    DWORD last_error = GetLastError();
    if (last_error == ERROR_ALREADY_EXISTS) {
        LogInfo("RunningGames: Mutex already existed for PID %lu", static_cast<unsigned long>(pid));
    }

    g_display_commander_process_mutex = h_mutex;
}

void GetRunningGamesWithDisplayCommander(std::vector<RunningGameInfo>& out_games) {
    out_games.clear();

    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        LogError("RunningGames: Failed to create process snapshot: %lu", GetLastError());
        return;
    }

    PROCESSENTRY32W process_entry = {};
    process_entry.dwSize = sizeof(PROCESSENTRY32W);

    if (!Process32FirstW(snapshot, &process_entry)) {
        LogError("RunningGames: Failed to get first process: %lu", GetLastError());
        CloseHandle(snapshot);
        return;
    }

    do {
        DWORD pid = process_entry.th32ProcessID;
        if (pid == 0) {
            continue;
        }

        std::wstring mutex_name = BuildDisplayCommanderMutexName(pid);
        HANDLE h_mutex = OpenMutexW(SYNCHRONIZE, FALSE, mutex_name.c_str());
        if (h_mutex == nullptr) {
            continue;
        }
        CloseHandle(h_mutex);

        RunningGameInfo info{};
        info.pid = pid;

        info.exe_path = GetProcessFullPath(pid);
        if (info.exe_path.empty()) {
            info.exe_path.assign(process_entry.szExeFile);
        }

        WindowEnumData enum_data = {};
        enum_data.process_id = pid;
        EnumWindows(EnumWindowsProc, reinterpret_cast<LPARAM>(&enum_data));

        std::wstring title;
        HWND main_hwnd = nullptr;
        for (HWND hwnd : enum_data.windows) {
            if (!display_commanderhooks::IsWindowVisible_direct(hwnd)) {
                continue;
            }
            title = GetWindowTitle(hwnd);
            if (!title.empty()) {
                main_hwnd = hwnd;
                break;
            }
        }

        if (title.empty()) {
            if (!info.exe_path.empty()) {
                const wchar_t* path_str = info.exe_path.c_str();
                const wchar_t* last_backslash = wcsrchr(path_str, L'\\');
                if (last_backslash != nullptr && last_backslash[1] != L'\0') {
                    title.assign(last_backslash + 1);
                } else {
                    title.assign(info.exe_path);
                }
            } else {
                title.assign(process_entry.szExeFile);
            }
        }
        info.display_title = std::move(title);
        info.main_window = main_hwnd;

        HANDLE h_process =
            OpenProcess(PROCESS_TERMINATE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, process_entry.th32ProcessID);
        if (h_process != nullptr) {
            info.can_terminate = true;
            CloseHandle(h_process);
        } else {
            info.can_terminate = false;
        }

        out_games.push_back(std::move(info));

    } while (Process32NextW(snapshot, &process_entry));

    CloseHandle(snapshot);
}

// --- Running games cache (mutex access only on monitoring thread) ---

namespace {
std::atomic<std::shared_ptr<const std::vector<RunningGameInfo>>> g_running_games_cache{nullptr};
std::atomic<bool> g_running_games_refresh_requested{false};
}  // namespace

void RefreshRunningGamesCache() {
    std::vector<RunningGameInfo> temp;
    GetRunningGamesWithDisplayCommander(temp);
    g_running_games_cache.store(std::make_shared<std::vector<RunningGameInfo>>(std::move(temp)),
                                std::memory_order_release);
    g_running_games_refresh_requested.store(false, std::memory_order_release);
}

void GetRunningGamesCache(std::vector<RunningGameInfo>& out_games) {
    out_games.clear();
    std::shared_ptr<const std::vector<RunningGameInfo>> ptr =
        g_running_games_cache.load(std::memory_order_acquire);
    if (ptr != nullptr) {
        out_games = *ptr;
    }
}

void RequestRunningGamesRefresh() {
    g_running_games_refresh_requested.store(true, std::memory_order_release);
}

bool RunningGamesRefreshRequested() {
    return g_running_games_refresh_requested.load(std::memory_order_acquire);
}

}  // namespace display_commander::utils
