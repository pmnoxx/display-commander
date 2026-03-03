// Standalone UIs: RunStandaloneSettingsUI (no-ReShade settings), RunStandaloneGamesOnlyUI (Games tab only,
// exe/Launcher). (default "."). Uses a second ImGui build in namespace ImGuiStandalone (via compile define
// ImGui=ImGuiStandalone) and ImDrawList=ImDrawListStandalone to avoid symbol clash with ReShade's ImGui/ImDrawList used
// in-game.

#include "config/display_commander_config.hpp"
#include "display/display_cache.hpp"
#include "standalone_ui_settings_bridge.hpp"
#include "ui/cli_detect_exe.hpp"
#include "ui/imgui_wrapper_standalone.hpp"
#include "ui/new_ui/advanced_tab.hpp"
#include "ui/new_ui/experimental_tab.hpp"
#include "ui/new_ui/games_tab.hpp"
#include "ui/new_ui/hotkeys_tab.hpp"
#include "ui/new_ui/main_new_tab_standalone.hpp"
#include "ui/new_ui/performance_tab.hpp"
#include "ui/new_ui/vulkan_tab.hpp"
#include "ui/nvidia_profile_tab_shared.hpp"
#include "utils/file_sha256.hpp"
#include "utils/game_launcher_registry.hpp"
#include "utils/general_utils.hpp"
#include "utils/reshade_load_path.hpp"
#include "utils/reshade_sha256_database.hpp"
#include "utils/steam_library.hpp"
#include "utils/version_check.hpp"
#include "version.hpp"
#include "widgets/remapping_widget/remapping_widget.hpp"
#include "widgets/xinput_widget/xinput_widget.hpp"

#include <windows.h>

#include <GL/gl.h>
#include <shellapi.h>
#include <tlhelp32.h>

// reshade
#include "backends/imgui_impl_opengl3.h"
#include "backends/imgui_impl_win32.h"
#include "imgui.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <cwctype>
#include <filesystem>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#ifndef IMGUI_IMPL_WIN32_DISABLE_GAMEPAD
#define IMGUI_IMPL_WIN32_DISABLE_GAMEPAD
#endif

// opengl32.lib not linked: gl/wgl symbols from opengl32_proxy (forwards to system opengl32)

static HDC g_hDC = nullptr;
static HGLRC g_hGLRC = nullptr;
static UINT g_ResizeWidth = 0, g_ResizeHeight = 0;

static bool CreateContextOpenGL(HWND hWnd);
static void CleanupContextOpenGL(HWND hWnd);
static void SaveLauncherWindowPosition(HWND hwnd);
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
static LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

static const wchar_t* s_reshadeDllNames[] = {L"dxgi.dll",     L"d3d9.dll",      L"d3d11.dll",    L"d3d12.dll",
                                             L"opengl32.dll", L"ReShade64.dll", L"ReShade32.dll"};

// API proxy DLLs: only one of these should exist (game loads one). ReShade64/32 can coexist.
static const wchar_t* s_reshadeApiProxyNames[] = {L"dxgi.dll",     L"d3d9.dll",    L"d3d11.dll", L"d3d12.dll",
                                                  L"opengl32.dll", L"version.dll", L"winmm.dll"};

static bool IsReshadeApiProxyDll(const std::wstring& name) {
    for (const wchar_t* p : s_reshadeApiProxyNames) {
        if (_wcsicmp(name.c_str(), p) == 0) return true;
    }
    return false;
}

static void CollectReShadeDllsInDir(const std::wstring& dir, std::vector<std::wstring>& outPresent) {
    outPresent.clear();
    for (const wchar_t* name : s_reshadeDllNames) {
        std::wstring path = dir + L"\\" + name;
        DWORD att = GetFileAttributesW(path.c_str());
        if (att != INVALID_FILE_ATTRIBUTES && !(att & FILE_ATTRIBUTE_DIRECTORY)) outPresent.push_back(name);
    }
}

// Collect Display Commander addon files (*display_commander*.addon64 / *.addon32) in dir.
static void CollectDisplayCommanderAddonsInDir(const std::wstring& dir, std::vector<std::wstring>& outPresent) {
    outPresent.clear();
    if (dir.empty()) return;
    std::wstring prefix = dir;
    if (prefix.back() != L'\\') prefix += L'\\';
    auto addMatching = [&outPresent, &prefix](const wchar_t* pattern) {
        WIN32_FIND_DATAW fd = {};
        std::wstring searchPath = prefix + pattern;
        HANDLE h = FindFirstFileW(searchPath.c_str(), &fd);
        if (h == INVALID_HANDLE_VALUE) return;
        do {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            std::wstring name = fd.cFileName;
            for (auto& c : name) c = (wchar_t)towlower((wint_t)c);
            if (name.find(L"display_commander") != std::wstring::npos) outPresent.push_back(fd.cFileName);
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    };
    addMatching(L"*.addon64");
    addMatching(L"*.addon32");
}

// Delete from local folder ReShade and DC files for the wrong architecture (e.g. if exe is 64-bit, remove
// ReShade32.dll and *.addon32). Call when we have exe detection so only matching-arch files remain.
static void RemoveWrongArchitectureFiles(const std::wstring& dir, bool exeIs64bit) {
    if (dir.empty()) return;
    std::wstring prefix = dir;
    if (prefix.back() != L'\\') prefix += L'\\';
    auto delIfPresent = [&prefix](const wchar_t* name) {
        std::wstring path = prefix + name;
        if (GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES) DeleteFileW(path.c_str());
    };
    if (exeIs64bit) {
        delIfPresent(L"ReShade32.dll");
        WIN32_FIND_DATAW fd = {};
        HANDLE h = FindFirstFileW((prefix + L"*.addon32").c_str(), &fd);
        if (h != INVALID_HANDLE_VALUE) {
            do {
                if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
                std::wstring lower = fd.cFileName;
                for (auto& c : lower) c = (wchar_t)towlower((wint_t)c);
                if (lower.find(L"display_commander") != std::wstring::npos) delIfPresent(fd.cFileName);
            } while (FindNextFileW(h, &fd));
            FindClose(h);
        }
    } else {
        delIfPresent(L"ReShade64.dll");
        WIN32_FIND_DATAW fd = {};
        HANDLE h = FindFirstFileW((prefix + L"*.addon64").c_str(), &fd);
        if (h != INVALID_HANDLE_VALUE) {
            do {
                if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
                std::wstring lower = fd.cFileName;
                for (auto& c : lower) c = (wchar_t)towlower((wint_t)c);
                if (lower.find(L"display_commander") != std::wstring::npos) delIfPresent(fd.cFileName);
            } while (FindNextFileW(h, &fd));
            FindClose(h);
        }
    }
}

// If multiple API proxy DLLs exist, remove extras so only one remains. Keeps the one matching preferredApi (e.g. "dxgi"
// -> dxgi.dll). preferredApi is from ReShadeDllFromDetect; if empty or "unknown", keeps the first found.
static void RemoveExtraReshadeApiProxyDlls(const std::wstring& dir, const char* preferredApi,
                                           std::vector<std::wstring>& inOutPresent) {
    std::wstring preferredName;
    if (preferredApi && preferredApi[0] && strcmp(preferredApi, "unknown") != 0
        && strcmp(preferredApi, "vulkan") != 0) {
        preferredName.reserve(32);
        for (const char* p = preferredApi; *p; ++p) preferredName += (wchar_t)(unsigned char)*p;
        preferredName += L".dll";
    }
    std::vector<std::wstring> apiProxiesPresent;
    for (const auto& n : inOutPresent) {
        if (IsReshadeApiProxyDll(n)) apiProxiesPresent.push_back(n);
    }
    if (apiProxiesPresent.size() <= 1u) return;
    std::wstring toKeep = preferredName.empty() ? apiProxiesPresent[0] : preferredName;
    bool keepFound = false;
    for (const auto& n : apiProxiesPresent) {
        if (_wcsicmp(n.c_str(), toKeep.c_str()) == 0) {
            keepFound = true;
            break;
        }
    }
    if (!keepFound) toKeep = apiProxiesPresent[0];
    for (const auto& n : apiProxiesPresent) {
        if (_wcsicmp(n.c_str(), toKeep.c_str()) == 0) continue;
        std::wstring path = dir + L"\\" + n;
        if (DeleteFileW(path.c_str())) {
            inOutPresent.erase(std::remove(inOutPresent.begin(), inOutPresent.end(), n), inOutPresent.end());
        }
    }
}

// Check if a PE file (DLL/addon) exports the given symbol (e.g. "StartAndInject"). Reads file, no load.
static bool DllHasExport(const std::wstring& filePath, const char* exportName) {
    HANDLE h = CreateFileW(filePath.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER sizeLi = {};
    if (!GetFileSizeEx(h, &sizeLi) || sizeLi.QuadPart <= 0 || sizeLi.QuadPart > 64 * 1024 * 1024) {
        CloseHandle(h);
        return false;
    }
    size_t fileSize = (size_t)sizeLi.QuadPart;
    std::vector<char> buf(fileSize);
    DWORD read = 0;
    if (!ReadFile(h, buf.data(), (DWORD)fileSize, &read, nullptr) || read != fileSize) {
        CloseHandle(h);
        return false;
    }
    CloseHandle(h);
    const char* bytes = buf.data();
    if (fileSize < 64) return false;
    int e_lfanew = *reinterpret_cast<const int*>(bytes + 0x3C);
    if (e_lfanew <= 0 || e_lfanew + 6 > (int)fileSize) return false;
    if (memcmp(bytes + e_lfanew, "PE\0\0", 4) != 0) return false;
    const size_t coff = (size_t)(e_lfanew + 4);
    if (coff + 20 > fileSize) return false;
    uint16_t sizeOpt = *reinterpret_cast<const uint16_t*>(bytes + coff + 16);
    size_t optHeader = coff + 20;
    if (optHeader + sizeOpt > fileSize) return false;
    uint16_t magic = *reinterpret_cast<const uint16_t*>(bytes + optHeader);
    size_t ddOffset = (magic == 0x20b) ? 112u : 96u;
    if (optHeader + ddOffset + 8 > fileSize) return false;
    uint32_t exportRva = *reinterpret_cast<const uint32_t*>(bytes + optHeader + ddOffset);
    if (exportRva == 0) return false;
    uint16_t numSections = *reinterpret_cast<const uint16_t*>(bytes + coff + 2);
    size_t sectionHeader = optHeader + sizeOpt;
    const size_t sectionSize = 40;
    auto rvaToFileOffset = [&](uint32_t rva) -> size_t {
        for (uint16_t i = 0; i < numSections; ++i) {
            size_t sec = sectionHeader + i * sectionSize;
            if (sec + sectionSize > fileSize) return (size_t)-1;
            uint32_t va = *reinterpret_cast<const uint32_t*>(bytes + sec + 12);
            uint32_t rawSize = *reinterpret_cast<const uint32_t*>(bytes + sec + 16);
            uint32_t rawPtr = *reinterpret_cast<const uint32_t*>(bytes + sec + 20);
            if (rva >= va && rva < va + rawSize) return rawPtr + (rva - va);
        }
        return (size_t)-1;
    };
    size_t exportFileOff = rvaToFileOffset(exportRva);
    if (exportFileOff == (size_t)-1 || exportFileOff + 40 > fileSize) return false;
    uint32_t numNames = *reinterpret_cast<const uint32_t*>(bytes + exportFileOff + 0x18);
    uint32_t addrNamesRva = *reinterpret_cast<const uint32_t*>(bytes + exportFileOff + 0x20);
    if (numNames == 0 || addrNamesRva == 0) return false;
    size_t namesArrayOff = rvaToFileOffset(addrNamesRva);
    if (namesArrayOff == (size_t)-1) return false;
    size_t exportNameLen = strlen(exportName);
    for (uint32_t j = 0; j < numNames; ++j) {
        size_t nameRvaOff = namesArrayOff + j * 4;
        if (nameRvaOff + 4 > fileSize) break;
        uint32_t nameRva = *reinterpret_cast<const uint32_t*>(bytes + nameRvaOff);
        size_t nameOff = rvaToFileOffset(nameRva);
        if (nameOff == (size_t)-1) continue;
        if (nameOff + exportNameLen + 1 > fileSize) continue;
        if (memcmp(bytes + nameOff, exportName, exportNameLen + 1) == 0) return true;
    }
    return false;
}

// Central Reshade folder: %LOCALAPPDATA%\Programs\Display_Commander\Reshade
static std::wstring GetCentralReshadeDir() {
    wchar_t buf[MAX_PATH];
    DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", buf, (DWORD)std::size(buf));
    if (n == 0 || n >= (DWORD)std::size(buf)) return {};
    std::wstring path = buf;
    if (!path.empty() && path.back() != L'\\') path += L'\\';
    path += L"Programs\\Display_Commander\\Reshade";
    return path;
}

// Get file version string (e.g. "1.2.3.4") or empty if not present / no version.
// Prefer 4-part version from VS_FIXEDFILEINFO so build number is always shown when present.
static std::string GetFileVersionStringUtf8(const std::wstring& filePath) {
    DWORD verHandle = 0;
    DWORD size = GetFileVersionInfoSizeW(filePath.c_str(), &verHandle);
    if (size == 0) return {};
    std::vector<char> buf(size);
    if (!GetFileVersionInfoW(filePath.c_str(), 0, size, buf.data())) return {};
    void* pFixed = nullptr;
    UINT fixedLen = 0;
    if (VerQueryValueW(buf.data(), L"\\", &pFixed, &fixedLen) && pFixed && fixedLen >= sizeof(VS_FIXEDFILEINFO)) {
        const VS_FIXEDFILEINFO* pInfo = static_cast<const VS_FIXEDFILEINFO*>(pFixed);
        if (pInfo->dwSignature == 0xFEEF04BD) {
            unsigned major = HIWORD(pInfo->dwFileVersionMS);
            unsigned minor = LOWORD(pInfo->dwFileVersionMS);
            unsigned patch = HIWORD(pInfo->dwFileVersionLS);
            unsigned build = LOWORD(pInfo->dwFileVersionLS);
            char out[32];
            snprintf(out, sizeof(out), "%u.%u.%u.%u", major, minor, patch, build);
            return std::string(out);
        }
    }
    struct LANGANDCODEPAGE {
        WORD wLanguage;
        WORD wCodePage;
    };
    LANGANDCODEPAGE* pTrans = nullptr;
    UINT transLen = 0;
    if (!VerQueryValueW(buf.data(), L"\\VarFileInfo\\Translation", (void**)&pTrans, &transLen) || !pTrans
        || transLen < sizeof(LANGANDCODEPAGE))
        return {};
    wchar_t subBlock[64];
    swprintf_s(subBlock, L"\\StringFileInfo\\%04x%04x\\FileVersion", pTrans[0].wLanguage, pTrans[0].wCodePage);
    void* pBlock = nullptr;
    UINT len = 0;
    if (!VerQueryValueW(buf.data(), subBlock, &pBlock, &len) || !pBlock || len == 0) return {};
    const wchar_t* ver = static_cast<const wchar_t*>(pBlock);
    int need = WideCharToMultiByte(CP_UTF8, 0, ver, (int)(len / sizeof(wchar_t)), nullptr, 0, nullptr, nullptr);
    if (need <= 0) return {};
    std::string out(static_cast<size_t>(need), 0);
    WideCharToMultiByte(CP_UTF8, 0, ver, (int)(len / sizeof(wchar_t)), &out[0], need, nullptr, nullptr);
    return out;
}

// Get product name or file description from exe version resource for display as game title. Returns empty if absent.
static std::string GetExeProductNameUtf8(const std::wstring& filePath) {
    DWORD verHandle = 0;
    DWORD size = GetFileVersionInfoSizeW(filePath.c_str(), &verHandle);
    if (size == 0) return {};
    std::vector<char> buf(size);
    if (!GetFileVersionInfoW(filePath.c_str(), 0, size, buf.data())) return {};
    struct LANGANDCODEPAGE {
        WORD wLanguage;
        WORD wCodePage;
    };
    LANGANDCODEPAGE* pTrans = nullptr;
    UINT transLen = 0;
    if (!VerQueryValueW(buf.data(), L"\\VarFileInfo\\Translation", (void**)&pTrans, &transLen) || !pTrans
        || transLen < sizeof(LANGANDCODEPAGE))
        return {};
    auto queryString = [&buf, &pTrans](const wchar_t* name) -> std::string {
        wchar_t subBlock[64];
        swprintf_s(subBlock, L"\\StringFileInfo\\%04x%04x\\%s", pTrans[0].wLanguage, pTrans[0].wCodePage, name);
        void* pBlock = nullptr;
        UINT len = 0;
        if (!VerQueryValueW(buf.data(), subBlock, &pBlock, &len) || !pBlock || len == 0) return {};
        const wchar_t* wstr = static_cast<const wchar_t*>(pBlock);
        size_t maxChars = len / sizeof(wchar_t);
        size_t strLen = 0;
        while (strLen < maxChars && wstr[strLen] != L'\0') ++strLen;
        if (strLen == 0) return {};
        int need = WideCharToMultiByte(CP_UTF8, 0, wstr, (int)strLen, nullptr, 0, nullptr, nullptr);
        if (need <= 0) return {};
        std::string out(static_cast<size_t>(need), 0);
        WideCharToMultiByte(CP_UTF8, 0, wstr, (int)strLen, &out[0], need, nullptr, nullptr);
        return out;
    };
    std::string product = queryString(L"ProductName");
    if (!product.empty()) return product;
    return queryString(L"FileDescription");
}

// Find largest .exe in directory (by file size). Skips common helper/crash exes. Returns full path (wide) or empty.
static std::wstring FindLargestExeInDir(const std::wstring& dir) {
    if (dir.empty()) return {};
    std::wstring pattern = dir;
    if (pattern.back() != L'\\') pattern += L'\\';
    pattern += L"*.exe";
    WIN32_FIND_DATAW fd = {};
    HANDLE h = FindFirstFileW(pattern.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return {};
    std::wstring best_name;
    ULONGLONG best_size = 0;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        const wchar_t* n = fd.cFileName;
        if (_wcsicmp(n, L"unrealcefsubprocess.exe") == 0 || _wcsicmp(n, L"crashreportclient.exe") == 0
            || _wcsicmp(n, L"unitycrashhandler64.exe") == 0 || _wcsicmp(n, L"unitycrashhandler32.exe") == 0)
            continue;
        ULONGLONG size = (ULONGLONG)fd.nFileSizeHigh << 32 | fd.nFileSizeLow;
        if (size > best_size) {
            best_size = size;
            best_name = n;
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    if (best_name.empty()) return {};
    std::wstring full = dir;
    if (full.back() != L'\\') full += L'\\';
    full += best_name;
    return full;
}

static const wchar_t* s_reshadeCoreNames[] = {L"ReShade64.dll", L"ReShade32.dll"};

static std::string WstringToUtf8(const std::wstring& ws) {
    if (ws.empty()) return {};
    int need = WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), (int)ws.size(), nullptr, 0, nullptr, nullptr);
    if (need <= 0) return {};
    std::string out(static_cast<size_t>(need), 0);
    WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), (int)ws.size(), &out[0], need, nullptr, nullptr);
    return out;
}

// Find PID of a running process whose exe path matches (case-insensitive). Returns 0 if not found.
static DWORD GetPidByExePath(const std::wstring& exePath) {
    if (exePath.empty()) return 0;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;
    PROCESSENTRY32W pe = {};
    pe.dwSize = sizeof(pe);
    DWORD found = 0;
    if (Process32FirstW(snap, &pe)) {
        do {
            HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pe.th32ProcessID);
            if (!h) continue;
            wchar_t pathBuf[32768];
            DWORD pathSize = (DWORD)std::size(pathBuf);
            if (QueryFullProcessImageNameW(h, 0, pathBuf, &pathSize)) {
                std::wstring procPath(pathBuf);
                if (_wcsicmp(procPath.c_str(), exePath.c_str()) == 0) {
                    found = pe.th32ProcessID;
                    CloseHandle(h);
                    break;
                }
            }
            CloseHandle(h);
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return found;
}

// Replace known user profile prefixes with placeholders so we don't show the username.
static std::string RedactPathForDisplay(const std::string& pathUtf8) {
    if (pathUtf8.empty()) return pathUtf8;
    wchar_t buf[MAX_PATH];
    struct EnvReplace {
        const wchar_t* name;
        const char* placeholder;
    };
    static const EnvReplace kEnv[] = {
        {L"LOCALAPPDATA", "%LOCALAPPDATA%"},
        {L"APPDATA", "%APPDATA%"},
        {L"USERPROFILE", "%USERPROFILE%"},
    };
    std::string result = pathUtf8;
    for (const auto& e : kEnv) {
        DWORD n = GetEnvironmentVariableW(e.name, buf, (DWORD)std::size(buf));
        if (n == 0 || n >= (DWORD)std::size(buf)) continue;
        std::wstring valW(buf);
        if (!valW.empty() && valW.back() == L'\\') valW.pop_back();
        std::string valUtf8 = WstringToUtf8(valW);
        if (valUtf8.empty()) continue;
        // Case-insensitive prefix match
        if (result.size() >= valUtf8.size()) {
            bool match = true;
            for (size_t i = 0; i < valUtf8.size(); ++i) {
                char a = result[i];
                char b = valUtf8[i];
                if (a >= 'A' && a <= 'Z') a += 32;
                if (b >= 'A' && b <= 'Z') b += 32;
                if (a != b) {
                    match = false;
                    break;
                }
            }
            if (match
                && (result.size() == valUtf8.size() || result[valUtf8.size()] == '\\'
                    || result[valUtf8.size()] == '/')) {
                result = std::string(e.placeholder) + result.substr(valUtf8.size());
                break;
            }
        }
    }
    return result;
}

// ReShade update: download and override DLLs in local + central. Version from GitHub tags or dropdown.
static const char* const kReshadeUpdateUrlLatest = "https://reshade.me/downloads/ReShade_Setup_Addon.exe";

static std::atomic<bool> s_reshadeUpdateInProgress{false};

// Newest available version (ReShade from reshade.me, DC from GitHub releases/latest). Fetched once in background.
static std::atomic<bool> s_latestVersionsFetchStarted{false};
static std::atomic<bool> s_latestVersionsFetchDone{false};
static std::atomic<std::string*> s_latestReShadeVersion{nullptr};
static std::atomic<std::string*> s_latestDcVersion{nullptr};
static std::string s_reshadeUpdateResult;
static std::string s_gameDetailsReshadeResult;  // result of install action from Game Details popup (DC as proxy)

// ReShade versions: hardcoded list (latest, 6.7.2 default, 6.7.1, 6.6.2).
static std::vector<std::string> s_reshadeVersions;
static int s_reshadeVersionIndex = 1;  // default "6.7.2"
static bool s_reshadeVersionsInitialized = false;

struct ReshadeUpdateParams {
    std::wstring addonDir;
    std::wstring centralDir;
    std::string selectedVersion;  // e.g. "6.7.1"
    bool forGameDetails = false;  // if true, result goes to s_gameDetailsReshadeResult
    // For proxy-only install (game folder): install as dxgi.dll / d3d11.dll / etc., never ReShade64/32 by name.
    std::string proxyName;    // e.g. "dxgi" (empty = legacy: copy both cores to addonDir)
    std::wstring gameDir;     // target game folder when proxyName non-empty
    bool gameIs64bit = true;  // which core to copy as proxy (ReShade64 vs ReShade32)
};

static DWORD WINAPI ReshadeUpdateWorker(LPVOID param) {
    std::unique_ptr<ReshadeUpdateParams> params(static_cast<ReshadeUpdateParams*>(param));
    const std::wstring& addonDir = params->addonDir;
    const std::wstring& centralDir = params->centralDir;

    auto setResult = [&params](const std::string& text) {
        if (params->forGameDetails)
            s_gameDetailsReshadeResult = text;
        else
            s_reshadeUpdateResult = text;
    };
    std::filesystem::path tempDir;
    {
        wchar_t tempPath[MAX_PATH];
        if (GetTempPathW((DWORD)std::size(tempPath), tempPath) == 0) {
            setResult("Install failed: could not get temp path.");
            s_reshadeUpdateInProgress = false;
            return 1;
        }
        tempDir = std::filesystem::path(tempPath) / L"dc_reshade_update";
        std::error_code ec;
        std::filesystem::create_directories(tempDir, ec);
        if (ec) {
            setResult("Install failed: could not create temp dir.");
            s_reshadeUpdateInProgress = false;
            return 1;
        }
    }

    std::filesystem::path archivePath = tempDir / "ReShade_Setup_Addon.exe";
    bool downloaded = false;
    if (!params->selectedVersion.empty() && params->selectedVersion != "latest") {
        std::string versionedUrl =
            "https://reshade.me/downloads/ReShade_Setup_" + params->selectedVersion + "_Addon.exe";
        downloaded = display_commander::utils::version_check::DownloadBinaryFromUrl(versionedUrl, archivePath);
    }
    if (!downloaded) {
        downloaded =
            display_commander::utils::version_check::DownloadBinaryFromUrl(kReshadeUpdateUrlLatest, archivePath);
    }
    if (!downloaded) {
        setResult("Install failed: download failed (check URL or network).");
        s_reshadeUpdateInProgress = false;
        return 1;
    }

    std::wstring archiveW = archivePath.wstring();
    std::wstring cmdLine;
    cmdLine.reserve(archiveW.size() + 64);
    cmdLine = L"tar.exe -xf \"";
    cmdLine += archiveW;
    cmdLine += L"\" ReShade64.dll ReShade32.dll";

    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi = {};
    if (!CreateProcessW(nullptr, &cmdLine[0], nullptr, nullptr, FALSE, 0, nullptr, tempDir.c_str(), &si, &pi)) {
        setResult("Install failed: tar extract failed (need Windows 10+ tar).");
        s_reshadeUpdateInProgress = false;
        return 1;
    }
    CloseHandle(pi.hThread);
    WaitForSingleObject(pi.hProcess, 60000);
    CloseHandle(pi.hProcess);

    std::filesystem::path extracted64 = tempDir / "ReShade64.dll";
    std::filesystem::path extracted32 = tempDir / "ReShade32.dll";
    if (!std::filesystem::exists(extracted64) || !std::filesystem::exists(extracted32)) {
        setResult("Install failed: extraction did not produce DLLs.");
        s_reshadeUpdateInProgress = false;
        return 1;
    }

    auto copyTo = [](const std::filesystem::path& src64, const std::filesystem::path& src32,
                     const std::wstring& destDir) -> bool {
        if (destDir.empty()) return true;
        std::filesystem::path dest(destDir);
        std::error_code ec;
        std::filesystem::copy_file(src64, dest / "ReShade64.dll", std::filesystem::copy_options::overwrite_existing,
                                   ec);
        if (ec) return false;
        std::filesystem::copy_file(src32, dest / "ReShade32.dll", std::filesystem::copy_options::overwrite_existing,
                                   ec);
        return !ec;
    };

    bool proxyOnlyInstall = params->forGameDetails && !params->proxyName.empty() && !params->gameDir.empty();

    if (proxyOnlyInstall) {
        // Install ReShade only as proxy (dxgi.dll / d3d11.dll / etc.) in game folder; cores go to central only.
        if (!centralDir.empty()) {
            if (!copyTo(extracted64, extracted32, centralDir)) {
                setResult("Install failed: could not write to central ReShade folder.");
                s_reshadeUpdateInProgress = false;
                return 1;
            }
        }
        std::wstring proxyDll;
        for (char c : params->proxyName) proxyDll += (wchar_t)(unsigned char)c;
        proxyDll += L".dll";
        const std::filesystem::path& coreSrc = params->gameIs64bit ? extracted64 : extracted32;
        std::filesystem::path gameDest(params->gameDir);
        gameDest /= proxyDll;
        std::error_code ec;
        std::filesystem::copy_file(coreSrc, gameDest, std::filesystem::copy_options::overwrite_existing, ec);
        if (ec) {
            setResult("Install failed: could not write proxy to game folder.");
            s_reshadeUpdateInProgress = false;
            return 1;
        }
    } else {
        // Copy ReShade cores to central only; do not copy to game directory.
        if (centralDir.empty()) {
            setResult("Install failed: central ReShade folder not set (LOCALAPPDATA).");
            s_reshadeUpdateInProgress = false;
            return 1;
        }
        if (!copyTo(extracted64, extracted32, centralDir)) {
            setResult("Install failed: could not write to central ReShade folder.");
            s_reshadeUpdateInProgress = false;
            return 1;
        }
    }

    std::error_code ec;
    std::filesystem::remove_all(tempDir, ec);
    std::string msg;
    if (params->forGameDetails) {
        if (proxyOnlyInstall)
            msg = "ReShade installed as " + params->proxyName + ".dll (" + (params->gameIs64bit ? "64-bit" : "32-bit")
                  + ").";
        else
            msg = (params->selectedVersion.empty() || params->selectedVersion == "latest")
                      ? "ReShade installed (latest)."
                      : "ReShade installed: " + params->selectedVersion + ".";
        s_gameDetailsReshadeResult = msg;
    } else {
        s_reshadeUpdateResult = (params->selectedVersion.empty() || params->selectedVersion == "latest")
                                    ? "ReShade updated (latest)."
                                    : "ReShade updated to " + params->selectedVersion + ".";
    }
    s_reshadeUpdateInProgress = false;
    return 0;
}

// Cache for SHA256 of ReShade DLLs (avoid recomputing every frame). UI thread only.
struct ReshadeSha256CacheEntry {
    std::wstring path;
    std::filesystem::file_time_type mtime{};
    std::string hash;
};
static ReshadeSha256CacheEntry s_sha256Cache64;
static ReshadeSha256CacheEntry s_sha256Cache32;

static std::string GetCachedFileSha256(const std::wstring& pathW, ReshadeSha256CacheEntry& cache) {
    std::filesystem::path p(pathW);
    if (!std::filesystem::exists(p)) return {};
    std::error_code ec;
    auto mtime = std::filesystem::last_write_time(p, ec);
    if (ec) return {};
    if (cache.path == pathW && cache.mtime == mtime && !cache.hash.empty()) return cache.hash;
    std::string hash = display_commander::utils::ComputeFileSha256(p);
    cache.path = pathW;
    cache.mtime = mtime;
    cache.hash = hash;
    return hash;
}

// Print ReShade64.dll / ReShade32.dll presence, version, and signature status for the given directory.
// If onlyCurrentArch is true, only the core for is64bit is shown (ReShade64.dll or ReShade32.dll).
static void ShowReshadeCoreVersionsForDir(const std::wstring& dir, bool onlyCurrentArch = false, bool is64bit = true) {
    if (dir.empty()) return;
    const wchar_t* coreToShow[2] = {is64bit ? L"ReShade64.dll" : L"ReShade32.dll", nullptr};
    const wchar_t* const* names = onlyCurrentArch ? coreToShow : s_reshadeCoreNames;
    const size_t n = onlyCurrentArch ? 1u : (sizeof(s_reshadeCoreNames) / sizeof(s_reshadeCoreNames[0]));
    for (size_t i = 0; i < n; ++i) {
        const wchar_t* name = names[i];
        std::wstring path = dir + L"\\" + name;
        DWORD att = GetFileAttributesW(path.c_str());
        if (att == INVALID_FILE_ATTRIBUTES || (att & FILE_ATTRIBUTE_DIRECTORY)) {
            ImGui::BulletText("%S: (not present)", name);
            continue;
        }
        std::string ver = GetFileVersionStringUtf8(path);
        if (ver.empty()) ver = "(no version info)";
        ImGui::BulletText("%S: %s", name, ver.c_str());

        bool is64 = (std::wcscmp(name, L"ReShade64.dll") == 0);
        ReshadeSha256CacheEntry& cache = is64 ? s_sha256Cache64 : s_sha256Cache32;
        std::string fileHash = GetCachedFileSha256(path, cache);
        if (!fileHash.empty()) {
            std::string versionKey = display_commander::utils::NormalizeReShadeVersionForLookup(ver);
            const char* expected = display_commander::utils::GetReShadeExpectedSha256(versionKey, is64);
            if (expected == nullptr) {
                ImGui::SameLine();
                ImGui::TextDisabled("  (signature: not in database)");
            } else if (fileHash == expected) {
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.4f, 1.0f), "  (signature: OK)");
            } else {
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(0.95f, 0.4f, 0.4f, 1.0f), "  (signature: MISMATCH)");
            }
        }
    }
}

void RunStandaloneSettingsUI(HINSTANCE hInst) {
    std::this_thread::sleep_for(std::chrono::seconds(1));

    ImGui_ImplWin32_EnableDpiAwareness();

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_CLASSDC;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = (HINSTANCE)hInst;
    wc.lpszClassName = L"DisplayCommanderSettingsUI";
    if (!RegisterClassExW(&wc)) return;

    std::string titleUtf8 = "Display Commander - Settings (No ReShade) v";
    titleUtf8 += DISPLAY_COMMANDER_VERSION_STRING;
    int titleLen = MultiByteToWideChar(CP_UTF8, 0, titleUtf8.c_str(), (int)titleUtf8.size() + 1, nullptr, 0);
    std::wstring titleW(titleLen > 0 ? (size_t)titleLen : 0, 0);
    if (titleLen > 0)
        MultiByteToWideChar(CP_UTF8, 0, titleUtf8.c_str(), (int)titleUtf8.size() + 1, &titleW[0], titleLen);

    HWND hwnd = standalone_ui_settings::CreateWindowW_Direct(
        wc.lpszClassName, titleW.empty() ? L"Display Commander - Settings" : titleW.c_str(), WS_OVERLAPPEDWINDOW, 100,
        100, 480, 420, nullptr, nullptr, (HINSTANCE)hInst, nullptr);
    if (!hwnd) {
        UnregisterClassW(wc.lpszClassName, wc.hInstance);
        return;
    }
    standalone_ui_settings::SetStandaloneUiHwnd(reinterpret_cast<uintptr_t>(hwnd));

    if (!CreateContextOpenGL(hwnd)) {
        standalone_ui_settings::SetStandaloneUiHwnd(0);
        CleanupContextOpenGL(hwnd);
        DestroyWindow(hwnd);
        UnregisterClassW(wc.lpszClassName, wc.hInstance);
        return;
    }

    ShowWindow(hwnd, SW_SHOWDEFAULT);
    UpdateWindow(hwnd);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::StyleColorsDark();
    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplOpenGL3_Init();

    static const char* fps_limiter_items[] = {"Default", "NVIDIA Reflex (low latency)", "Disabled",
                                              "Sync to Display Refresh Rate (fraction of monitor refresh rate)"};
    static const int fps_limiter_num = 4;

    bool done = false;
    while (!done) {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0U, 0U, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
            if (msg.message == WM_QUIT) done = true;
        }
        if (done) break;

        if (g_ResizeWidth != 0 && g_ResizeHeight != 0) {
            glViewport(0, 0, (GLsizei)g_ResizeWidth, (GLsizei)g_ResizeHeight);
            g_ResizeWidth = g_ResizeHeight = 0;
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(440, 0), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Display Commander - Settings (No ReShade)", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            if (ImGui::BeginTabBar("NoReshadeSettingsTabs")) {
                if (ImGui::BeginTabItem("Main")) {
                    ui::new_ui::InitMainNewTab();
                    display_commander::ui::ImGuiWrapperStandalone wrapper;
                    ui::new_ui::DrawMainNewTab(ui::new_ui::GetGraphicsApiFromLastDeviceApi(), wrapper);
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("NVIDIA Profile")) {
                    static bool s_noreshadeShowAdvancedProfile = false;
                    display_commander::ui::ImGuiWrapperStandalone wrapper;
                    display_commander::ui::DrawNvidiaProfileTab(ui::new_ui::GetGraphicsApiFromLastDeviceApi(), wrapper,
                                                                &s_noreshadeShowAdvancedProfile);
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("Advanced")) {
                    display_commander::ui::ImGuiWrapperStandalone wrapper;
                    ui::new_ui::DrawAdvancedTab(ui::new_ui::GetGraphicsApiFromLastDeviceApi(), wrapper);
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("Hotkeys")) {
                    display_commander::ui::ImGuiWrapperStandalone wrapper;
                    ui::new_ui::DrawHotkeysTab(wrapper);
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("Controller")) {
                    display_commander::widgets::xinput_widget::InitializeXInputWidget();
                    display_commander::widgets::remapping_widget::InitializeRemappingWidget();
                    display_commander::ui::ImGuiWrapperStandalone wrapper;
                    display_commander::widgets::xinput_widget::DrawXInputWidget(wrapper);
                    ImGui::Spacing();
                    display_commander::widgets::remapping_widget::DrawRemappingWidget(wrapper);
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("Performance")) {
                    display_commander::ui::ImGuiWrapperStandalone wrapper;
                    ui::new_ui::DrawPerformanceTab(wrapper);
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("Performance Overlay")) {
                    display_commander::ui::ImGuiWrapperStandalone wrapper;
                    ui::new_ui::DrawPerformanceOverlayContent(wrapper, ui::new_ui::GetGraphicsApiFromLastDeviceApi(),
                                                              true);
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("Vulkan (Experimental)")) {
                    display_commander::ui::ImGuiWrapperStandalone wrapper;
                    ui::new_ui::DrawVulkanTab(wrapper);
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("Debug")) {
                    display_commander::ui::ImGuiWrapperStandalone wrapper;
                    ui::new_ui::DrawExperimentalTab(wrapper, nullptr);
                    ImGui::EndTabItem();
                }
                ImGui::EndTabBar();
            }
        }
        ImGui::End();

        ImGui::Render();
        glClearColor(0.f, 0.f, 0.f, 1.f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        SwapBuffers(g_hDC);
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    CleanupContextOpenGL(hwnd);
    standalone_ui_settings::SetStandaloneUiHwnd(0);
    SaveLauncherWindowPosition(hwnd);
    DestroyWindow(hwnd);
    UnregisterClassW(wc.lpszClassName, wc.hInstance);
}

// Background worker: fetch newest ReShade (reshade.me) and Display Commander (GitHub latest) versions once.
static void LatestVersionsFetchWorker() {
    std::string reshade_ver;
    std::string reshade_err;
    if (display_commander::utils::version_check::FetchReShadeLatestFromReshadeMe(&reshade_ver, &reshade_err)) {
        s_latestReShadeVersion.store(new std::string(reshade_ver), std::memory_order_release);
    } else {
        s_latestReShadeVersion.store(new std::string(), std::memory_order_release);
    }
    std::string dc_ver;
    std::string dc_err;
    if (display_commander::utils::version_check::FetchLatestStableReleaseVersion(&dc_ver, &dc_err)) {
        s_latestDcVersion.store(new std::string(dc_ver), std::memory_order_release);
    } else {
        s_latestDcVersion.store(new std::string(), std::memory_order_release);
    }
    s_latestVersionsFetchDone.store(true, std::memory_order_release);
}

// Launcher Settings tab: font scale, ReShade global status, Display Commander global version.
static void DrawLauncherSettingsTab() {
    // Start one-time background fetch of newest available versions (ReShade, DC).
    bool expected = false;
    if (s_latestVersionsFetchStarted.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
        std::thread t(LatestVersionsFetchWorker);
        t.detach();
    }

    float font_scale = 1.0f;
    display_commander::config::DisplayCommanderConfigManager::GetInstance().GetConfigValue("Launcher", "FontScale",
                                                                                           font_scale);
    if (font_scale <= 0.0f || font_scale > 3.0f) font_scale = 1.0f;

    if (ImGui::SliderFloat("Font size", &font_scale, 0.5f, 2.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp)) {
        display_commander::config::DisplayCommanderConfigManager::GetInstance().SetConfigValue("Launcher", "FontScale",
                                                                                               font_scale);
        display_commander::config::DisplayCommanderConfigManager::GetInstance().SaveConfig("Launcher font scale");
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Scale the Launcher window text. Takes effect immediately.");
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    ImGui::TextUnformatted("ReShade (global install)");
    std::filesystem::path reshade_global = display_commander::utils::GetGlobalReshadeDirectory();
    std::string reshade_ver = display_commander::utils::GetGlobalReshadeVersion();
    if (reshade_global.empty()) {
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "Not installed");
    } else {
        if (reshade_ver.empty()) {
            ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.5f, 1.0f), "Version: (unknown)");
        } else {
            ImGui::Text("Version: %s", reshade_ver.c_str());
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", reshade_global.string().c_str());
        }
    }
    if (s_latestVersionsFetchDone.load(std::memory_order_acquire)) {
        std::string* latest = s_latestReShadeVersion.load(std::memory_order_acquire);
        if (latest && !latest->empty()) {
            ImGui::Text("Newest available: %s", latest->c_str());
        } else {
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "Newest available: (unavailable)");
        }
    } else {
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "Newest available: (checking...)");
    }
    bool reshade_updating = s_reshadeUpdateInProgress.load(std::memory_order_acquire);
    if (reshade_updating) {
        ImGui::BeginDisabled();
    }
    if (ImGui::Button("Update##reshade_global")) {
        std::wstring central = GetCentralReshadeDir();
        if (!central.empty()) {
            auto* params = new ReshadeUpdateParams;
            params->centralDir = central;
            params->selectedVersion = "latest";
            params->forGameDetails = false;
            s_reshadeUpdateResult.clear();
            s_reshadeUpdateInProgress.store(true, std::memory_order_release);
            HANDLE h = CreateThread(nullptr, 0, ReshadeUpdateWorker, params, 0, nullptr);
            if (h != nullptr) {
                CloseHandle(h);
            } else {
                s_reshadeUpdateInProgress.store(false, std::memory_order_release);
                delete params;
                s_reshadeUpdateResult = "Failed to start update thread.";
            }
        }
    }
    if (reshade_updating) {
        ImGui::EndDisabled();
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Download latest ReShade Addon and install to global folder.");
    }
    if (!s_reshadeUpdateResult.empty()) {
        ImGui::TextColored(ImVec4(0.7f, 0.85f, 0.7f, 1.0f), "%s", s_reshadeUpdateResult.c_str());
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    ImGui::TextUnformatted("Display Commander (global install)");
    std::filesystem::path dc_appdata = GetDisplayCommanderAppDataFolder();
    if (dc_appdata.empty()) {
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "Folder not available");
    } else {
#ifdef _WIN64
        std::filesystem::path addon_path = dc_appdata / L"zzz_display_commander.addon64";
#else
        std::filesystem::path addon_path = dc_appdata / L"zzz_display_commander.addon32";
#endif
        std::string dc_ver = GetFileVersionStringUtf8(addon_path.native());
        if (dc_ver.empty()) {
            ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.5f, 1.0f), "Version: not installed or not found");
        } else {
            ImGui::Text("Version: %s", dc_ver.c_str());
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", dc_appdata.string().c_str());
        }
        ImGui::Text("This instance: %s", DISPLAY_COMMANDER_VERSION_STRING);
    }
    if (s_latestVersionsFetchDone.load(std::memory_order_acquire)) {
        std::string* latest = s_latestDcVersion.load(std::memory_order_acquire);
        if (latest && !latest->empty()) {
            ImGui::Text("Newest available: %s", latest->c_str());
        } else {
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "Newest available: (unavailable)");
        }
    } else {
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "Newest available: (checking...)");
    }
    if (ImGui::Button("Update##dc_global")) {
        display_commander::utils::version_check::CheckForUpdates();
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Check for Display Commander updates (GitHub). Result in main app or next launch.");
    }
}

// Save Launcher (Games) window position/size to config. No-op if hwnd is not the Launcher window class.
static void SaveLauncherWindowPosition(HWND hwnd) {
    wchar_t buf[64] = {};
    if (GetClassNameW(hwnd, buf, (int)std::size(buf)) == 0) return;
    if (wcscmp(buf, L"DisplayCommanderGamesUI") != 0) return;
    RECT r = {};
    if (!GetWindowRect(hwnd, &r)) return;
    int x = r.left;
    int y = r.top;
    int w = r.right - r.left;
    int h = r.bottom - r.top;
    auto& cfg = display_commander::config::DisplayCommanderConfigManager::GetInstance();
    cfg.SetConfigValue("Launcher", "WindowX", x);
    cfg.SetConfigValue("Launcher", "WindowY", y);
    cfg.SetConfigValue("Launcher", "WindowWidth", w);
    cfg.SetConfigValue("Launcher", "WindowHeight", h);
    cfg.SaveConfig("Launcher window position");
}

void RunStandaloneGamesOnlyUI(HINSTANCE hInst) {
    std::this_thread::sleep_for(std::chrono::seconds(1));

    ImGui_ImplWin32_EnableDpiAwareness();

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_CLASSDC;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = (HINSTANCE)hInst;
    wc.lpszClassName = L"DisplayCommanderGamesUI";
    // Use embedded app icon (resource ID 1) for window and taskbar (exe build only; DLL has no icon resource)
#ifdef DISPLAY_COMMANDER_BUILD_EXE
    wc.hIcon = LoadIcon((HINSTANCE)hInst, MAKEINTRESOURCE(1));
    wc.hIconSm = LoadIcon((HINSTANCE)hInst, MAKEINTRESOURCE(1));
#endif
    if (!RegisterClassExW(&wc)) return;

    std::string titleUtf8 = "Display Commander - Games v";
    titleUtf8 += DISPLAY_COMMANDER_VERSION_STRING;
    int titleLen = MultiByteToWideChar(CP_UTF8, 0, titleUtf8.c_str(), (int)titleUtf8.size() + 1, nullptr, 0);
    std::wstring titleW(titleLen > 0 ? (size_t)titleLen : 0, 0);
    if (titleLen > 0)
        MultiByteToWideChar(CP_UTF8, 0, titleUtf8.c_str(), (int)titleUtf8.size() + 1, &titleW[0], titleLen);

    // Restore Win32 window position/size from config (saved on previous close).
    int launcher_x = 100;
    int launcher_y = 100;
    int launcher_w = 600;
    int launcher_h = 800;
    auto& launcher_cfg = display_commander::config::DisplayCommanderConfigManager::GetInstance();
    const bool has_x = launcher_cfg.GetConfigValue("Launcher", "WindowX", launcher_x);
    const bool has_y = launcher_cfg.GetConfigValue("Launcher", "WindowY", launcher_y);
    const bool has_w = launcher_cfg.GetConfigValue("Launcher", "WindowWidth", launcher_w);
    const bool has_h = launcher_cfg.GetConfigValue("Launcher", "WindowHeight", launcher_h);
    if (!(has_x && has_y && has_w && has_h)) {
        launcher_x = 100;
        launcher_y = 100;
        launcher_w = 600;
        launcher_h = 800;
    }
    if (launcher_w < 400) launcher_w = 400;
    if (launcher_h < 300) launcher_h = 300;
    if (launcher_w > 4096) launcher_w = 4096;
    if (launcher_h > 4096) launcher_h = 4096;

    HWND hwnd = standalone_ui_settings::CreateWindowW_Direct(
        wc.lpszClassName, titleW.empty() ? L"Display Commander - Games" : titleW.c_str(), WS_OVERLAPPEDWINDOW,
        launcher_x, launcher_y, launcher_w, launcher_h, nullptr, nullptr, (HINSTANCE)hInst, nullptr);
    if (!hwnd) {
        UnregisterClassW(wc.lpszClassName, wc.hInstance);
        return;
    }
    standalone_ui_settings::SetStandaloneUiHwnd(reinterpret_cast<uintptr_t>(hwnd));

    if (!CreateContextOpenGL(hwnd)) {
        standalone_ui_settings::SetStandaloneUiHwnd(0);
        CleanupContextOpenGL(hwnd);
        DestroyWindow(hwnd);
        UnregisterClassW(wc.lpszClassName, wc.hInstance);
        return;
    }

    ShowWindow(hwnd, SW_SHOWDEFAULT);
    UpdateWindow(hwnd);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::StyleColorsDark();
    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplOpenGL3_Init();

    bool done = false;
    while (!done) {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0U, 0U, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
            if (msg.message == WM_QUIT) done = true;
        }
        if (done) break;

        if (g_ResizeWidth != 0 && g_ResizeHeight != 0) {
            glViewport(0, 0, (GLsizei)g_ResizeWidth, (GLsizei)g_ResizeHeight);
            ImGui::GetIO().DisplaySize = ImVec2((float)g_ResizeWidth, (float)g_ResizeHeight);
            g_ResizeWidth = g_ResizeHeight = 0;
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        // Use current client size every frame so Games window grows and shrinks with the outer window
        RECT rc = {};
        if (GetClientRect(hwnd, &rc)) {
            ImGui::GetIO().DisplaySize = ImVec2((float)(rc.right - rc.left), (float)(rc.bottom - rc.top));
        }

        float launcher_font_scale = 1.0f;
        display_commander::config::DisplayCommanderConfigManager::GetInstance().GetConfigValue("Launcher", "FontScale",
                                                                                               launcher_font_scale);
        if (launcher_font_scale <= 0.0f || launcher_font_scale > 3.0f) launcher_font_scale = 1.0f;
        ImGui::GetIO().FontGlobalScale = launcher_font_scale;

        ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize, ImGuiCond_Always);
        if (ImGui::Begin("Games", nullptr,
                         ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse
                             | ImGuiWindowFlags_NoTitleBar)) {
            if (ImGui::BeginTabBar("##LauncherTabs", ImGuiTabBarFlags_None)) {
                if (ImGui::BeginTabItem("Games")) {
                    display_commander::ui::ImGuiWrapperStandalone wrapper;
                    ui::new_ui::DrawGamesTab(wrapper);
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("Settings")) {
                    DrawLauncherSettingsTab();
                    ImGui::EndTabItem();
                }
                ImGui::EndTabBar();
            }
        }
        ImGui::End();

        ImGui::Render();
        glClearColor(0.f, 0.f, 0.f, 1.f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        SwapBuffers(g_hDC);
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    CleanupContextOpenGL(hwnd);
    standalone_ui_settings::SetStandaloneUiHwnd(0);
    SaveLauncherWindowPosition(hwnd);
    DestroyWindow(hwnd);
    UnregisterClassW(wc.lpszClassName, wc.hInstance);
}

// WGL 3.0 context for ImGui OpenGL3 backend (Win32-only, no DX9)
static bool CreateContextOpenGL(HWND hWnd) {
    g_hDC = GetDC(hWnd);
    if (!g_hDC) return false;

    PIXELFORMATDESCRIPTOR pfd = {};
    pfd.nSize = sizeof(pfd);
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 32;
    pfd.cDepthBits = 0;
    pfd.cStencilBits = 0;
    pfd.iLayerType = PFD_MAIN_PLANE;

    int pixelFormat = ChoosePixelFormat(g_hDC, &pfd);
    if (pixelFormat == 0) {
        ReleaseDC(hWnd, g_hDC);
        g_hDC = nullptr;
        return false;
    }
    if (!SetPixelFormat(g_hDC, pixelFormat, &pfd)) {
        ReleaseDC(hWnd, g_hDC);
        g_hDC = nullptr;
        return false;
    }

    HGLRC tempCtx = wglCreateContext(g_hDC);
    if (!tempCtx) {
        ReleaseDC(hWnd, g_hDC);
        g_hDC = nullptr;
        return false;
    }
    if (!wglMakeCurrent(g_hDC, tempCtx)) {
        wglDeleteContext(tempCtx);
        ReleaseDC(hWnd, g_hDC);
        g_hDC = nullptr;
        return false;
    }

    typedef HGLRC(WINAPI * PFN_wglCreateContextAttribsARB)(HDC, HGLRC, const int*);
    PFN_wglCreateContextAttribsARB wglCreateContextAttribsARB =
        (PFN_wglCreateContextAttribsARB)wglGetProcAddress("wglCreateContextAttribsARB");

    if (wglCreateContextAttribsARB) {
        int attribs[] = {0x2091, 3,       // WGL_CONTEXT_MAJOR_VERSION_ARB = 3
                         0x2092, 0,       // WGL_CONTEXT_MINOR_VERSION_ARB = 0
                         0x2094, 0x0002,  // WGL_CONTEXT_PROFILE_MASK_ARB = WGL_CONTEXT_COMPATIBILITY_PROFILE_BIT_ARB
                         0};
        g_hGLRC = wglCreateContextAttribsARB(g_hDC, nullptr, attribs);
    }
    wglMakeCurrent(nullptr, nullptr);
    wglDeleteContext(tempCtx);

    if (!g_hGLRC && wglCreateContextAttribsARB) {
        ReleaseDC(hWnd, g_hDC);
        g_hDC = nullptr;
        return false;
    }
    if (!g_hGLRC) {
        g_hGLRC = wglCreateContext(g_hDC);
    }
    if (!g_hGLRC) {
        ReleaseDC(hWnd, g_hDC);
        g_hDC = nullptr;
        return false;
    }
    if (!wglMakeCurrent(g_hDC, g_hGLRC)) {
        wglDeleteContext(g_hGLRC);
        g_hGLRC = nullptr;
        ReleaseDC(hWnd, g_hDC);
        g_hDC = nullptr;
        return false;
    }
    return true;
}

static void CleanupContextOpenGL(HWND hWnd) {
    if (g_hGLRC && g_hDC) {
        wglMakeCurrent(nullptr, nullptr);
        wglDeleteContext(g_hGLRC);
        g_hGLRC = nullptr;
    }
    if (g_hDC && hWnd) {
        ReleaseDC(hWnd, g_hDC);
        g_hDC = nullptr;
    }
}

static LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam)) return 1;
    switch (msg) {
        case WM_SIZE:
            if (wParam != SIZE_MINIMIZED) {
                g_ResizeWidth = (UINT)LOWORD(lParam);
                g_ResizeHeight = (UINT)HIWORD(lParam);
            }
            return 0;
        case WM_SYSCOMMAND:
            if ((wParam & 0xfff0) == SC_KEYMENU) return 0;
            break;
        case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}
