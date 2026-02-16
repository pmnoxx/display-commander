#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Enumerate Steam libraries and installed games (from libraryfolders.vdf + appmanifest_*.acf).
// Used by the CLI standalone UI "Add Steam game" search. No dependency on Steam API DLLs.

namespace display_commander::steam_library {

struct SteamGame {
    uint32_t app_id{0};
    std::string name;           // From manifest "name"
    std::wstring install_dir;   // e.g. "C:\\SteamLibrary\\steamapps\\common\\GameName"
};

// Get Steam install path from registry (HKCU then HKLM). Returns empty if not found.
std::wstring GetSteamInstallPath();

// Enumerate all Steam library roots: default install path + paths from libraryfolders.vdf.
// Tries config\libraryfolders.vdf first, then steamapps\libraryfolders.vdf.
void GetLibraryPaths(std::vector<std::wstring>& out);

// Enumerate installed Steam games across all libraries. Fills out with app_id, name, install_dir.
// install_dir = library_path + L"\\steamapps\\common\\" + installdir from manifest.
void GetInstalledGames(std::vector<SteamGame>& out);

// Find a main .exe in the given install directory (skips Uninstall, unins*, etc.).
// Returns full path to exe or empty if none found.
std::wstring FindMainExeInDir(const std::wstring& install_dir);

}  // namespace display_commander::steam_library
