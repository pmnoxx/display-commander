#include "game_launcher_registry.hpp"
#include <ctime>
#include "logging.hpp"

#include <windows.h>

namespace display_commander::game_launcher_registry {

namespace {

constexpr const wchar_t* kBaseKey = L"Software\\Display Commander\\Games";
constexpr const wchar_t* kValuePath = L"Path";
constexpr const wchar_t* kValueName = L"Name";
constexpr const wchar_t* kValueWindowTitle = L"WindowTitle";
constexpr const wchar_t* kValueArguments = L"Arguments";
constexpr const wchar_t* kValueLastRun = L"LastRun";

// Stable hash of path string for use as registry subkey (invalid chars removed).
static std::wstring PathToSubkey(const std::wstring& path) {
    if (path.empty()) return L"empty";
    std::wstring norm = path;
    for (auto& c : norm) {
        if (c == L'/')
            c = L'\\';
        else if (c >= L'A' && c <= L'Z')
            c = (wchar_t)(c - L'A' + L'a');
    }
    uint64_t h = 14695981039346656037ULL;  // FNV offset basis
    for (wchar_t c : norm) {
        h ^= (uint64_t)(unsigned short)c;
        h *= 1099511628211ULL;  // FNV prime
    }
    wchar_t hex[17];
    for (int i = 0; i < 16; ++i) {
        unsigned v = (unsigned)(h & 0xF);
        hex[15 - i] = (wchar_t)(v < 10 ? L'0' + v : L'a' + v - 10);
        h >>= 4;
    }
    hex[16] = L'\0';
    return std::wstring(hex);
}

static std::wstring GetExeNameFromPath(const std::wstring& path) {
    if (path.empty()) return L"";
    size_t last = path.find_last_of(L"\\/");
    if (last == std::wstring::npos) return path;
    if (last + 1 >= path.size()) return L"";
    return path.substr(last + 1);
}

}  // namespace

void RecordGameRun(const wchar_t* game_exe_path, const wchar_t* launch_arguments, const wchar_t* window_title) {
    if (!game_exe_path || !game_exe_path[0]) return;
    std::wstring path(game_exe_path);
    std::wstring keyName = PathToSubkey(path);
    std::wstring name = GetExeNameFromPath(path);
    std::wstring arguments(launch_arguments ? launch_arguments : L"");
    std::wstring wtitle(window_title ? window_title : L"");

    HKEY hBase = nullptr;
    LSTATUS st = RegCreateKeyExW(HKEY_CURRENT_USER, kBaseKey, 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_READ | KEY_WRITE,
                                 nullptr, &hBase, nullptr);
    if (st != ERROR_SUCCESS || !hBase) {
        LogInfo("Game launcher registry: failed to open base key, error %ld", (long)st);
        return;
    }

    HKEY hSub = nullptr;
    st = RegCreateKeyExW(hBase, keyName.c_str(), 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_READ | KEY_WRITE, nullptr,
                         &hSub, nullptr);
    RegCloseKey(hBase);
    if (st != ERROR_SUCCESS || !hSub) {
        LogInfo("Game launcher registry: failed to create subkey, error %ld", (long)st);
        return;
    }

    int64_t now = (int64_t)time(nullptr);
    st = RegSetValueExW(hSub, kValuePath, 0, REG_SZ, reinterpret_cast<const BYTE*>(path.c_str()),
                        (DWORD)((path.size() + 1) * sizeof(wchar_t)));
    if (st == ERROR_SUCCESS)
        st = RegSetValueExW(hSub, kValueName, 0, REG_SZ, reinterpret_cast<const BYTE*>(name.c_str()),
                            (DWORD)((name.size() + 1) * sizeof(wchar_t)));
    if (st == ERROR_SUCCESS)
        st = RegSetValueExW(hSub, kValueWindowTitle, 0, REG_SZ, reinterpret_cast<const BYTE*>(wtitle.c_str()),
                            (DWORD)((wtitle.size() + 1) * sizeof(wchar_t)));
    if (st == ERROR_SUCCESS)
        st = RegSetValueExW(hSub, kValueArguments, 0, REG_SZ, reinterpret_cast<const BYTE*>(arguments.c_str()),
                            (DWORD)((arguments.size() + 1) * sizeof(wchar_t)));
    if (st == ERROR_SUCCESS)
        st = RegSetValueExW(hSub, kValueLastRun, 0, REG_QWORD, reinterpret_cast<const BYTE*>(&now), sizeof(now));

    RegCloseKey(hSub);
    if (st != ERROR_SUCCESS) LogInfo("Game launcher registry: failed to write values, error %ld", (long)st);
}

void EnumerateGames(std::vector<GameEntry>& out) {
    HKEY hBase = nullptr;
    LSTATUS st = RegOpenKeyExW(HKEY_CURRENT_USER, kBaseKey, 0, KEY_READ, &hBase);
    if (st != ERROR_SUCCESS || !hBase) return;

    DWORD index = 0;
    wchar_t subkeyName[256];
    DWORD nameLen;

    for (;;) {
        nameLen = (DWORD)std::size(subkeyName);
        st = RegEnumKeyExW(hBase, index, subkeyName, &nameLen, nullptr, nullptr, nullptr, nullptr);
        if (st != ERROR_SUCCESS) break;
        ++index;

        HKEY hSub = nullptr;
        st = RegOpenKeyExW(hBase, subkeyName, 0, KEY_READ, &hSub);
        if (st != ERROR_SUCCESS || !hSub) continue;

        GameEntry entry;
        entry.key = subkeyName;

        wchar_t pathBuf[32768];
        DWORD pathSize = sizeof(pathBuf);
        if (RegQueryValueExW(hSub, kValuePath, nullptr, nullptr, (LPBYTE)pathBuf, &pathSize) == ERROR_SUCCESS)
            entry.path = pathBuf;
        pathSize = sizeof(pathBuf);
        if (RegQueryValueExW(hSub, kValueName, nullptr, nullptr, (LPBYTE)pathBuf, &pathSize) == ERROR_SUCCESS)
            entry.name = pathBuf;
        pathSize = sizeof(pathBuf);
        if (RegQueryValueExW(hSub, kValueWindowTitle, nullptr, nullptr, (LPBYTE)pathBuf, &pathSize) == ERROR_SUCCESS)
            entry.window_title = pathBuf;
        pathSize = sizeof(pathBuf);
        if (RegQueryValueExW(hSub, kValueArguments, nullptr, nullptr, (LPBYTE)pathBuf, &pathSize) == ERROR_SUCCESS)
            entry.arguments = pathBuf;
        DWORD lastRunSize = sizeof(entry.last_run);
        RegQueryValueExW(hSub, kValueLastRun, nullptr, nullptr, (LPBYTE)&entry.last_run, &lastRunSize);

        RegCloseKey(hSub);

        if (!entry.path.empty()) out.push_back(entry);
    }

    RegCloseKey(hBase);
}

void RemoveGame(const wchar_t* game_exe_path) {
    if (!game_exe_path || !game_exe_path[0]) return;
    std::wstring path(game_exe_path);
    std::wstring keyName = PathToSubkey(path);
    std::wstring fullKey = std::wstring(kBaseKey) + L"\\" + keyName;
    LSTATUS st = RegDeleteKeyW(HKEY_CURRENT_USER, fullKey.c_str());
    if (st != ERROR_SUCCESS && st != ERROR_FILE_NOT_FOUND)
        LogInfo("Game launcher registry: failed to delete key, error %ld", (long)st);
}

std::wstring GetCentralAddonDir() {
    wchar_t buf[MAX_PATH];
    DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", buf, (DWORD)std::size(buf));
    if (n == 0 || n >= (DWORD)std::size(buf)) return L"";
    std::wstring path = buf;
    if (!path.empty() && path.back() != L'\\') path += L'\\';
    path += L"Programs\\Display_Commander";
    return path;
}

}  // namespace display_commander::game_launcher_registry
