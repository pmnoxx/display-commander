#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Game launcher registry: when Display Commander runs inside a game, it records the game in
// HKCU\Software\Display Commander\Games so the Installer UI can list games and offer Start/Stop/Update DC.

namespace display_commander::game_launcher_registry {

struct GameEntry {
    std::wstring key;          // Registry subkey (hash of path)
    std::wstring path;         // Full exe path
    std::wstring name;         // Exe filename (e.g. game.exe)
    std::wstring window_title; // Main window title from game's HWND when recorded
    std::wstring arguments;    // Launch arguments (command line after exe path)
    int64_t last_run;          // Unix timestamp when DC last ran with this game
};

// Record that Display Commander is running with the given game exe path, optional launch arguments,
// and optional window title (from the game's main HWND via GetWindowTextW).
// Creates/updates HKCU\Software\Display Commander\Games\<key> with Path, Name, WindowTitle, Arguments, LastRun.
void RecordGameRun(const wchar_t* game_exe_path, const wchar_t* launch_arguments,
                   const wchar_t* window_title);

// Enumerate all games recorded in the registry (for Installer UI).
// Entries are appended to out; out is not cleared.
void EnumerateGames(std::vector<GameEntry>& out);

// Get central Display Commander addon directory (same as installer script: %LOCALAPPDATA%\Programs\Display Commander).
std::wstring GetCentralAddonDir();

}  // namespace display_commander::game_launcher_registry
