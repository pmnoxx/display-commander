#include <windows.h>
#include <string>
#include <vector>
#include <iostream>
#include <fstream>
#include <filesystem>

struct SteamGame {
    std::string name;
    uint32_t app_id;
    std::string install_path;
    std::string executable_name;
};

class SteamAPI {
public:
    static bool isSteamInstalled() {
        HKEY hKey;
        LONG result = RegOpenKeyExA(HKEY_LOCAL_MACHINE,
            "SOFTWARE\\WOW6432Node\\Valve\\Steam", 0, KEY_READ, &hKey);

        if (result == ERROR_SUCCESS) {
            RegCloseKey(hKey);
            return true;
        }

        // Try 32-bit registry
        result = RegOpenKeyExA(HKEY_LOCAL_MACHINE,
            "SOFTWARE\\Valve\\Steam", 0, KEY_READ, &hKey);

        if (result == ERROR_SUCCESS) {
            RegCloseKey(hKey);
            return true;
        }

        return false;
    }

    static std::string getSteamInstallPath() {
        HKEY hKey;
        char steamPath[512];
        DWORD pathSize = sizeof(steamPath);

        // Try 64-bit registry first
        LONG result = RegOpenKeyExA(HKEY_LOCAL_MACHINE,
            "SOFTWARE\\WOW6432Node\\Valve\\Steam", 0, KEY_READ, &hKey);

        if (result != ERROR_SUCCESS) {
            // Try 32-bit registry
            result = RegOpenKeyExA(HKEY_LOCAL_MACHINE,
                "SOFTWARE\\Valve\\Steam", 0, KEY_READ, &hKey);
        }

        if (result == ERROR_SUCCESS) {
            result = RegQueryValueExA(hKey, "InstallPath", nullptr, nullptr,
                reinterpret_cast<LPBYTE>(steamPath), &pathSize);
            RegCloseKey(hKey);

            if (result == ERROR_SUCCESS) {
                return std::string(steamPath);
            }
        }

        return "";
    }

    static std::vector<SteamGame> getInstalledGames() {
        std::vector<SteamGame> games;

        if (!isSteamInstalled()) {
            return games;
        }

        std::string steamPath = getSteamInstallPath();
        if (steamPath.empty()) {
            return games;
        }

        // Read libraryfolders.vdf to find all Steam libraries
        std::vector<std::string> libraryPaths;
        libraryPaths.push_back(steamPath); // Default Steam installation

        std::string libraryFoldersPath = steamPath + "\\steamapps\\libraryfolders.vdf";
        std::ifstream file(libraryFoldersPath);
        if (file.is_open()) {
            std::string line;
            while (std::getline(file, line)) {
                // Look for library paths in the VDF file
                size_t pos = line.find("\"path\"");
                if (pos != std::string::npos) {
                    pos = line.find("\"", pos + 6);
                    if (pos != std::string::npos) {
                        size_t endPos = line.find("\"", pos + 1);
                        if (endPos != std::string::npos) {
                            std::string path = line.substr(pos + 1, endPos - pos - 1);
                            // Convert forward slashes to backslashes
                            std::replace(path.begin(), path.end(), '/', '\\');
                            libraryPaths.push_back(path);
                        }
                    }
                }
            }
            file.close();
        }

        // Scan each library for games
        for (const auto& libraryPath : libraryPaths) {
            std::string appsPath = libraryPath + "\\steamapps\\common";
            if (std::filesystem::exists(appsPath)) {
                for (const auto& entry : std::filesystem::directory_iterator(appsPath)) {
                    if (entry.is_directory()) {
                        // Look for appmanifest files
                        std::string manifestPath = entry.path().string() + "\\..\\appmanifest_*.acf";
                        // This is a simplified approach - in reality you'd need to parse the ACF files
                        // to get the actual app ID and executable name
                    }
                }
            }
        }

        return games;
    }

    static bool launchSteamGame(uint32_t appId, const std::string& arguments = "") {
        std::string steamUrl = "steam://run/" + std::to_string(appId);
        if (!arguments.empty()) {
            steamUrl += "//" + arguments;
        }

        HINSTANCE result = ShellExecuteA(nullptr, "open", steamUrl.c_str(), nullptr, nullptr, SW_SHOW);
        return reinterpret_cast<intptr_t>(result) > 32;
    }
};
