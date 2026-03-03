// Source Code <Display Commander>

// Group 1 — Source Code (Display Commander)
#include "steam_favorites.hpp"
#include "logging.hpp"

// Group 2 — ReShade / ImGui
// (none)

// Group 3 — Standard C++
#include <string>

// Group 4 — Windows.h
#include <windows.h>

// Group 5 — Other Windows SDK
// (none)

namespace display_commander::steam_favorites {

namespace {

constexpr const wchar_t* kBaseKey = L"Software\\Display Commander\\SteamFavorites";

void AppIdToWString(uint32_t app_id, std::wstring& out) {
    std::string s = std::to_string(app_id);
    out.clear();
    out.reserve(s.size() + 1);
    for (char c : s) out += static_cast<wchar_t>(static_cast<unsigned char>(c));
}

}  // namespace

void AddSteamGameToFavorites(uint32_t app_id) {
    if (app_id == 0) return;

    HKEY hKey = nullptr;
    LSTATUS st = RegCreateKeyExW(HKEY_CURRENT_USER, kBaseKey, 0, nullptr, REG_OPTION_NON_VOLATILE,
                                 KEY_READ | KEY_WRITE, nullptr, &hKey, nullptr);
    if (st != ERROR_SUCCESS || !hKey) {
        LogInfo("Steam favorites: failed to open key, error %ld", (long)st);
        return;
    }

    std::wstring appIdW;
    AppIdToWString(app_id, appIdW);
    const DWORD one = 1;
    st = RegSetValueExW(hKey, appIdW.c_str(), 0, REG_DWORD, reinterpret_cast<const BYTE*>(&one), sizeof(one));
    RegCloseKey(hKey);
    if (st != ERROR_SUCCESS) {
        LogInfo("Steam favorites: failed to write app_id %u, error %ld", app_id, (long)st);
    }
}

void RemoveSteamGameFromFavorites(uint32_t app_id) {
    if (app_id == 0) return;

    HKEY hKey = nullptr;
    LSTATUS st = RegOpenKeyExW(HKEY_CURRENT_USER, kBaseKey, 0, KEY_READ | KEY_WRITE, &hKey);
    if (st != ERROR_SUCCESS || !hKey) return;

    std::wstring appIdW;
    AppIdToWString(app_id, appIdW);
    RegDeleteValueW(hKey, appIdW.c_str());
    RegCloseKey(hKey);
}

bool IsSteamGameFavorite(uint32_t app_id) {
    if (app_id == 0) return false;

    HKEY hKey = nullptr;
    LSTATUS st = RegOpenKeyExW(HKEY_CURRENT_USER, kBaseKey, 0, KEY_READ, &hKey);
    if (st != ERROR_SUCCESS || !hKey) return false;

    std::wstring appIdW;
    AppIdToWString(app_id, appIdW);
    DWORD value = 0;
    DWORD size = sizeof(value);
    st = RegQueryValueExW(hKey, appIdW.c_str(), nullptr, nullptr, reinterpret_cast<LPBYTE>(&value), &size);
    RegCloseKey(hKey);
    return (st == ERROR_SUCCESS);
}

}  // namespace display_commander::steam_favorites
