#include "steam_library.hpp"
#include <windows.h>
#include <algorithm>
#include <cctype>
#include <fstream>

namespace display_commander::steam_library {

namespace {

constexpr wchar_t kRegPathSteamHkcu[] = L"SOFTWARE\\Valve\\Steam";
constexpr wchar_t kRegPathSteamHklm[] = L"SOFTWARE\\Valve\\Steam";
constexpr wchar_t kValueSteamPath[] = L"SteamPath";
constexpr wchar_t kValueInstallPath[] = L"InstallPath";

std::wstring GetSteamPathFromRegistry(HKEY root, const wchar_t* valueName) {
    wchar_t buf[MAX_PATH + 2] = {};
    DWORD len = (DWORD)std::size(buf);
    DWORD flags = RRF_RT_REG_SZ;
    if (root == HKEY_LOCAL_MACHINE) flags |= RRF_SUBKEY_WOW6432KEY;
    LSTATUS st = RegGetValueW(root, kRegPathSteamHklm, valueName, flags, nullptr, buf, &len);
    if (st != ERROR_SUCCESS) return {};
    std::wstring path(buf);
    std::replace(path.begin(), path.end(), L'/', L'\\');
    if (!path.empty() && path.back() == L'\\') path.pop_back();
    return path;
}

// Scan for "path" key followed by quoted string (path value) in libraryfolders.vdf.
void ParseLibraryFoldersVdfSimple(const std::string& content, std::vector<std::wstring>& out_paths) {
    const std::string pathKey = "\"path\"";
    size_t pos = 0;
    for (;;) {
        pos = content.find(pathKey, pos);
        if (pos == std::string::npos) break;
        pos += pathKey.size();
        while (pos < content.size() && (content[pos] == '\t' || content[pos] == ' ' || content[pos] == '\r' || content[pos] == '\n')) ++pos;
        if (pos < content.size() && content[pos] == '"') {
            ++pos;
            size_t start = pos;
            while (pos < content.size() && content[pos] != '"') {
                if (content[pos] == '\\' && pos + 1 < content.size()) ++pos;
                ++pos;
            }
            if (pos > start) {
                std::string pathUtf8(content.substr(start, pos - start));
                std::wstring wpath;
                wpath.reserve(pathUtf8.size() + 1);
                for (unsigned char c : pathUtf8) wpath += (wchar_t)c;
                std::replace(wpath.begin(), wpath.end(), L'/', L'\\');
                if (!wpath.empty() && wpath.back() == L'\\') wpath.pop_back();
                if (wpath.size() >= 3 && wpath[1] == L':' && (wpath[0] >= L'A' && wpath[0] <= L'Z' || wpath[0] >= L'a' && wpath[0] <= L'z'))
                    out_paths.push_back(wpath);
            }
        }
    }
}

// Parse appmanifest_*.acf for "appid", "name", "installdir". Simple quoted-key quoted-value.
bool ParseAppManifest(const std::string& content, uint32_t& app_id, std::string& name, std::string& installdir) {
    app_id = 0;
    name.clear();
    installdir.clear();
    auto getValue = [&content](const char* key, std::string& out) -> bool {
        std::string search = std::string("\"") + key + "\"";
        size_t pos = content.find(search);
        if (pos == std::string::npos) return false;
        pos += search.size();
        while (pos < content.size() && (content[pos] == '\t' || content[pos] == ' ' || content[pos] == '\r' || content[pos] == '\n')) ++pos;
        if (pos >= content.size() || content[pos] != '"') return false;
        ++pos;
        size_t start = pos;
        while (pos < content.size() && content[pos] != '"') {
            if (content[pos] == '\\' && pos + 1 < content.size()) ++pos;
            ++pos;
        }
        if (pos > start) { out = content.substr(start, pos - start); return true; }
        return false;
    };
    std::string appidStr;
    if (!getValue("appid", appidStr)) return false;
    app_id = (uint32_t)strtoul(appidStr.c_str(), nullptr, 10);
    if (app_id == 0) return false;
    getValue("name", name);
    getValue("installdir", installdir);
    return true;
}

}  // namespace

std::wstring GetSteamInstallPath() {
    wchar_t buf[MAX_PATH + 2] = {};
    DWORD len = (DWORD)std::size(buf);
    LSTATUS st = RegGetValueW(HKEY_CURRENT_USER, kRegPathSteamHkcu, kValueSteamPath, RRF_RT_REG_SZ, nullptr, buf, &len);
    if (st == ERROR_SUCCESS && buf[0]) {
        std::wstring path(buf);
        std::replace(path.begin(), path.end(), L'/', L'\\');
        if (!path.empty() && path.back() == L'\\') path.pop_back();
        return path;
    }
    std::wstring path = GetSteamPathFromRegistry(HKEY_LOCAL_MACHINE, kValueInstallPath);
    if (!path.empty()) return path;
    return GetSteamPathFromRegistry(HKEY_LOCAL_MACHINE, kValueSteamPath);
}

void GetLibraryPaths(std::vector<std::wstring>& out) {
    out.clear();
    std::wstring steamPath = GetSteamInstallPath();
    if (steamPath.empty()) return;
    out.push_back(steamPath);

    std::wstring vdfPath = steamPath + L"\\config\\libraryfolders.vdf";
    if (GetFileAttributesW(vdfPath.c_str()) == INVALID_FILE_ATTRIBUTES)
        vdfPath = steamPath + L"\\steamapps\\libraryfolders.vdf";
    if (GetFileAttributesW(vdfPath.c_str()) == INVALID_FILE_ATTRIBUTES) return;

    std::ifstream f(vdfPath);
    if (!f) return;
    std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    f.close();
    ParseLibraryFoldersVdfSimple(content, out);
    // Remove duplicate of default path when it also appeared in vdf (keep out[0] = default)
    auto samePath = [](const std::wstring& a, const std::wstring& b) {
        if (a.size() != b.size()) return false;
        for (size_t i = 0; i < a.size(); ++i) {
            wchar_t x = a[i], y = b[i];
            if (x >= L'A' && x <= L'Z') x += 32;
            if (y >= L'A' && y <= L'Z') y += 32;
            if (x != y) return false;
        }
        return true;
    };
    for (size_t i = out.size(); i > 1; --i) {
        if (samePath(out[i - 1], steamPath))
            out.erase(out.begin() + (long)(i - 1));
    }
}

void GetInstalledGames(std::vector<SteamGame>& out) {
    out.clear();
    std::vector<std::wstring> libPaths;
    GetLibraryPaths(libPaths);
    for (const std::wstring& lib : libPaths) {
        std::wstring steamapps = lib + L"\\steamapps";
        WIN32_FIND_DATAW fd = {};
        std::wstring search = steamapps + L"\\appmanifest_*.acf";
        HANDLE h = FindFirstFileW(search.c_str(), &fd);
        if (h == INVALID_HANDLE_VALUE) continue;
        do {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            std::wstring fullPath = steamapps + L"\\" + fd.cFileName;
            HANDLE hFile = CreateFileW(fullPath.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
            if (hFile == INVALID_HANDLE_VALUE) continue;
            DWORD size = GetFileSize(hFile, nullptr);
            if (size == 0 || size > 1024 * 1024) { CloseHandle(hFile); continue; }
            std::string content(size, 0);
            DWORD read = 0;
            if (!ReadFile(hFile, &content[0], size, &read, nullptr) || read != size) { CloseHandle(hFile); continue; }
            CloseHandle(hFile);
            uint32_t appId = 0;
            std::string name, installdir;
            if (!ParseAppManifest(content, appId, name, installdir)) continue;
            if (installdir.empty()) continue;
            std::wstring installdirW;
            installdirW.reserve(installdir.size() + 1);
            for (unsigned char c : installdir) installdirW += (wchar_t)c;
            std::wstring installPath = steamapps + L"\\common\\" + installdirW;
            DWORD att = GetFileAttributesW(installPath.c_str());
            if (att == INVALID_FILE_ATTRIBUTES || !(att & FILE_ATTRIBUTE_DIRECTORY)) continue;
            SteamGame game;
            game.app_id = appId;
            game.name = name;
            game.install_dir = installPath;
            out.push_back(game);
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
}

std::wstring FindMainExeInDir(const std::wstring& install_dir) {
    WIN32_FIND_DATAW fd = {};
    std::wstring search = install_dir + L"\\*.exe";
    HANDLE h = FindFirstFileW(search.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return {};
    std::wstring firstExe;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        std::wstring name = fd.cFileName;
        for (auto& c : name) if (c >= L'A' && c <= L'Z') c += 32;
        if (name.find(L"uninstall") == 0) continue;
        if (name.find(L"unins") == 0) continue;
        if (name == L"setup.exe") continue;
        if (name == L"install.exe") continue;
        std::wstring full = install_dir + L"\\" + fd.cFileName;
        if (firstExe.empty()) firstExe = full;
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return firstExe;
}

}  // namespace display_commander::steam_library
