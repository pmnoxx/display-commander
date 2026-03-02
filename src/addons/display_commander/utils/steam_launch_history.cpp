// Source Code <Display Commander>

// Group 1 — Source Code (Display Commander)
#include "steam_launch_history.hpp"
#include "logging.hpp"

// Group 2 — ReShade / ImGui
// (none)

// Group 3 — Standard C++
#include <ctime>
#include <string>

// Group 4 — Windows.h
#include <windows.h>

// Group 5 — Other Windows SDK
// (none)

namespace display_commander::steam_launch_history {

namespace {

constexpr const wchar_t* kBaseKey = L"Software\\Display Commander\\SteamLaunchHistory";

}  // namespace

void RecordSteamLaunch(uint32_t app_id) {
    if (app_id == 0) return;

    HKEY hKey = nullptr;
    LSTATUS st = RegCreateKeyExW(HKEY_CURRENT_USER, kBaseKey, 0, nullptr, REG_OPTION_NON_VOLATILE,
                                 KEY_READ | KEY_WRITE, nullptr, &hKey, nullptr);
    if (st != ERROR_SUCCESS || !hKey) {
        LogInfo("Steam launch history: failed to open key, error %ld", (long)st);
        return;
    }

    std::string appIdStr = std::to_string(app_id);
    std::wstring appIdW;
    appIdW.reserve(appIdStr.size() + 1);
    for (char c : appIdStr) appIdW += (wchar_t)(unsigned char)c;

    int64_t now = static_cast<int64_t>(time(nullptr));
    st = RegSetValueExW(hKey, appIdW.c_str(), 0, REG_QWORD, reinterpret_cast<const BYTE*>(&now), sizeof(now));
    RegCloseKey(hKey);
    if (st != ERROR_SUCCESS) {
        LogInfo("Steam launch history: failed to write app_id %u, error %ld", app_id, (long)st);
    }
}

int64_t GetSteamLaunchTimestamp(uint32_t app_id) {
    if (app_id == 0) return 0;

    HKEY hKey = nullptr;
    LSTATUS st = RegOpenKeyExW(HKEY_CURRENT_USER, kBaseKey, 0, KEY_READ, &hKey);
    if (st != ERROR_SUCCESS || !hKey) return 0;

    std::string appIdStr = std::to_string(app_id);
    std::wstring appIdW;
    appIdW.reserve(appIdStr.size() + 1);
    for (char c : appIdStr) appIdW += (wchar_t)(unsigned char)c;

    int64_t timestamp = 0;
    DWORD size = sizeof(timestamp);
    st = RegQueryValueExW(hKey, appIdW.c_str(), nullptr, nullptr, reinterpret_cast<LPBYTE>(&timestamp), &size);
    RegCloseKey(hKey);
    return (st == ERROR_SUCCESS) ? timestamp : 0;
}

}  // namespace display_commander::steam_launch_history
