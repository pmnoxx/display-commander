// Source Code <Display Commander>

// Group 1 — Source Code (Display Commander)
#pragma once

// Group 2 — ReShade / ImGui
// (none)

// Group 3 — Standard C++
#include <string>
#include <vector>

// Group 4 — Windows.h
#include <windows.h>

// Group 5 — Other Windows SDK
// (none)

namespace display_commander::utils {

// Enumerate all running processes and their windows, logging to file
void LogAllProcessesAndWindows();

// Basic info about a running game process that has Display Commander loaded.
struct RunningGameInfo {
    DWORD pid;
    std::wstring exe_path;
    std::wstring display_title;
    HWND main_window;
    bool can_terminate;
};

// Register current process as a Display Commander-managed game via named mutex.
void RegisterCurrentProcessWithDisplayCommanderMutex();

// Discover all running processes that have registered the Display Commander mutex.
// Must be called only from the dedicated monitoring thread (e.g. continuous monitoring);
// UI and other threads must use the cache API below.
void GetRunningGamesWithDisplayCommander(std::vector<RunningGameInfo>& out_games);

// --- Running games cache (updated only from the continuous monitoring thread) ---

// Refresh the running-games cache (OpenMutexW + enumeration). Call only from the
// continuous monitoring / dedicated worker thread, not from the UI thread.
void RefreshRunningGamesCache();

// Copy current cached list into out_games. Safe to call from any thread (e.g. UI).
void GetRunningGamesCache(std::vector<RunningGameInfo>& out_games);

// Request an immediate refresh on the next monitoring loop iteration (e.g. after
// user clicks Refresh or Kill). Safe to call from any thread.
void RequestRunningGamesRefresh();

// True if a refresh was requested (monitoring thread should call RefreshRunningGamesCache).
bool RunningGamesRefreshRequested();

}  // namespace display_commander::utils

