// SetupDC: run the installer UI inside the addon DLL (no separate .exe). Takes optional script directory as argument
// (default "."). Uses a second ImGui build in namespace ImGuiStandalone (via compile define ImGui=ImGuiStandalone) and
// ImDrawList=ImDrawListStandalone to avoid symbol clash with ReShade's ImGui/ImDrawList used in-game.

#define ImGui      ImGuiStandalone
#define ImDrawList ImDrawListStandalone
#include <d3d11.h>
#include <dxgi.h>
#include <shellapi.h>
#include <windows.h>
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <cwctype>
#include <filesystem>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "backends/imgui_impl_dx11.h"
#include "backends/imgui_impl_win32.h"
#include "imgui.h"

#include "ui/cli_detect_exe.hpp"
#include "utils/file_sha256.hpp"
#include "utils/game_launcher_registry.hpp"
#include "utils/reshade_sha256_database.hpp"
#include "utils/steam_library.hpp"
#include "utils/version_check.hpp"
#include "version.hpp"

#include <tlhelp32.h>

#ifndef IMGUI_IMPL_WIN32_DISABLE_GAMEPAD
#define IMGUI_IMPL_WIN32_DISABLE_GAMEPAD
#endif

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

static ID3D11Device* g_pd3dDevice = nullptr;
static ID3D11DeviceContext* g_pd3dDeviceContext = nullptr;
static IDXGISwapChain* g_pSwapChain = nullptr;
static UINT g_ResizeWidth = 0, g_ResizeHeight = 0;
static ID3D11RenderTargetView* g_mainRenderTargetView = nullptr;

static bool CreateDeviceD3D(HWND hWnd);
static void CleanupDeviceD3D();
static void CreateRenderTarget();
static void CleanupRenderTarget();
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
        // Legacy (SetupDC): copy ReShade cores to central only; do not copy to game directory.
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

void RunStandaloneUI(HINSTANCE hInst, const char* script_dir_utf8) {
    ImGui_ImplWin32_EnableDpiAwareness();

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_CLASSDC;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = (HINSTANCE)hInst;
    wc.lpszClassName = L"DisplayCommanderUI";
    if (!RegisterClassExW(&wc)) return;

    std::string installerTitleUtf8 = "Display Commander - Installer (v";
    installerTitleUtf8 += DISPLAY_COMMANDER_VERSION_STRING;
    installerTitleUtf8 += ")";
    int titleLen =
        MultiByteToWideChar(CP_UTF8, 0, installerTitleUtf8.c_str(), (int)installerTitleUtf8.size() + 1, nullptr, 0);
    std::wstring installerTitleW(titleLen > 0 ? (size_t)titleLen : 0, 0);
    if (titleLen > 0)
        MultiByteToWideChar(CP_UTF8, 0, installerTitleUtf8.c_str(), (int)installerTitleUtf8.size() + 1,
                            &installerTitleW[0], titleLen);

    HWND hwnd = CreateWindowW(wc.lpszClassName,
                              installerTitleW.empty() ? L"Display Commander - Installer" : installerTitleW.c_str(),
                              WS_OVERLAPPEDWINDOW, 100, 100, 1920, 1080, nullptr, nullptr, (HINSTANCE)hInst, nullptr);
    if (!hwnd) {
        UnregisterClassW(wc.lpszClassName, wc.hInstance);
        return;
    }

    if (!CreateDeviceD3D(hwnd)) {
        CleanupDeviceD3D();
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
    ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);

    std::wstring addonDir;
    if (script_dir_utf8 && script_dir_utf8[0] != '\0') {
        int wlen = MultiByteToWideChar(CP_UTF8, 0, script_dir_utf8, -1, nullptr, 0);
        if (wlen > 0) {
            addonDir.resize(static_cast<size_t>(wlen));
            if (MultiByteToWideChar(CP_UTF8, 0, script_dir_utf8, -1, &addonDir[0], wlen) > 0) {
                if (!addonDir.empty() && addonDir.back() == L'\0') addonDir.pop_back();
            } else {
                addonDir.clear();
            }
        }
    }
    if (addonDir.empty()) {
        // Use addon DLL's path (not rundll32's). When invoked via rundll32, hInst may be the addon
        // module, but GetModuleHandleEx(FROM_ADDRESS) guarantees we get this DLL's directory.
        HMODULE addonModule = nullptr;
        (void)GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT | GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                                 reinterpret_cast<LPCWSTR>(&RunStandaloneUI), &addonModule);
        wchar_t modulePath[2048];
        DWORD modLen = 0;
        if (addonModule) modLen = GetModuleFileNameW(addonModule, modulePath, (DWORD)std::size(modulePath));
        if (modLen > 0 && modLen < (DWORD)std::size(modulePath)) {
            addonDir.assign(modulePath);
            size_t last = addonDir.find_last_of(L"\\/");
            if (last != std::wstring::npos) addonDir.resize(last);
        }
    }
    std::vector<std::wstring> reshadeDllsPresent;
    static std::string s_setupReshadeResult;
    static DWORD s_startedGamePid = 0;
    static ULONGLONG s_startedGameTick = 0;   // when we started (GetTickCount64), for runtime tooltip
    static std::string s_preferredSetupApi;   // user's "Setup as X" choice; cleanup keeps this, removes others
    static bool s_showDebug = false;          // Debug button toggles advanced/debug UI
    static bool s_autoInstallDone = false;    // one-time auto-install ReShade 6.7.2 when core missing
    static bool s_autoInstallDcDone = false;  // one-time auto-install DC as proxy when DC not installed (Setup tab)

    std::wstring centralReshadeDir = GetCentralReshadeDir();
    std::wstring exeFoundLocal = FindLargestExeInDir(addonDir);
    std::string exeFoundUtf8 = WstringToUtf8(exeFoundLocal);
    std::string addonDirUtf8 = WstringToUtf8(addonDir);
    std::string centralDirUtf8 = WstringToUtf8(centralReshadeDir);

    cli_detect_exe::DetectResult exeDetect = {};
    bool exeDetectOk = false;
    if (!exeFoundLocal.empty()) exeDetectOk = cli_detect_exe::DetectExeForPath(exeFoundLocal.c_str(), &exeDetect);

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
            CleanupRenderTarget();
            g_pSwapChain->ResizeBuffers(0, g_ResizeWidth, g_ResizeHeight, DXGI_FORMAT_UNKNOWN, 0);
            g_ResizeWidth = g_ResizeHeight = 0;
            CreateRenderTarget();
        }

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(580, 0), ImGuiCond_FirstUseEver);
        static char s_installerWindowTitle[128];
        snprintf(s_installerWindowTitle, sizeof(s_installerWindowTitle), "Display Commander - Installer (v%s)",
                 DISPLAY_COMMANDER_VERSION_STRING);
        if (ImGui::Begin(s_installerWindowTitle, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            static display_commander::game_launcher_registry::GameEntry s_gameDetailsEntry;
            static bool s_pleaseOpenGameDetails = false;
            static bool s_pleaseOpenSteamSearch = false;
            static std::string s_updateDcResult;
            if (!addonDir.empty()) {
                if (exeDetectOk) RemoveWrongArchitectureFiles(addonDir, exeDetect.is_64bit);
                CollectReShadeDllsInDir(addonDir, reshadeDllsPresent);
                const char* detectedApi = exeDetectOk ? cli_detect_exe::ReShadeDllFromDetect(exeDetect) : "";
                const char* preferredApi = s_preferredSetupApi.empty() ? detectedApi : s_preferredSetupApi.c_str();
                RemoveExtraReshadeApiProxyDlls(addonDir, preferredApi, reshadeDllsPresent);
                // Auto-install ReShade 6.7.2 once when core DLL for current arch is missing
                if (exeDetectOk && !s_autoInstallDone && !s_reshadeUpdateInProgress.load()) {
                    const wchar_t* coreName = exeDetect.is_64bit ? L"ReShade64.dll" : L"ReShade32.dll";
                    std::wstring corePath = addonDir + L"\\" + coreName;
                    if (GetFileAttributesW(corePath.c_str()) == INVALID_FILE_ATTRIBUTES) {
                        s_autoInstallDone = true;
                        auto* params = new ReshadeUpdateParams{addonDir, centralReshadeDir, "6.7.2"};
                        s_reshadeUpdateResult.clear();
                        s_reshadeUpdateInProgress = true;
                        HANDLE h = CreateThread(nullptr, 0, ReshadeUpdateWorker, params, 0, nullptr);
                        if (h)
                            CloseHandle(h);
                        else {
                            delete params;
                            s_reshadeUpdateInProgress = false;
                        }
                    }
                }
            }

            if (!s_reshadeVersionsInitialized) {
                display_commander::utils::version_check::FetchReShadeVersionsFromGitHub(s_reshadeVersions, nullptr);
                if (s_reshadeVersionIndex >= (int)s_reshadeVersions.size()) s_reshadeVersionIndex = 1;
                s_reshadeVersionsInitialized = true;
            }

            ImGui::Text("Display Commander Installer");
            ImGui::Separator();

            if (ImGui::BeginTabBar("InstallerTabs")) {
                if (ImGui::BeginTabItem("Setup")) {
                    bool is64bit = exeDetectOk ? exeDetect.is_64bit : true;
                    const char* detectedApi = exeDetectOk ? cli_detect_exe::ReShadeDllFromDetect(exeDetect) : "";
                    bool apiSupported =
                        (detectedApi && strcmp(detectedApi, "vulkan") != 0 && strcmp(detectedApi, "unknown") != 0);
                    bool canSetup = !addonDir.empty() && exeDetectOk && apiSupported;

                    // Install Display Commander addon from central as API proxy (e.g. dxgi.dll) in game dir.
                    auto doInstallDcAsProxy = [&](const char* targetApi) {
                        s_setupReshadeResult.clear();
                        std::wstring centralAddonDir = display_commander::game_launcher_registry::GetCentralAddonDir();
                        if (centralAddonDir.empty()) {
                            s_setupReshadeResult = "Install failed: central addon folder not set (LOCALAPPDATA).";
                            return;
                        }
                        const wchar_t* addonFileName =
                            exeDetect.is_64bit ? L"zzz_display_commander.addon64" : L"zzz_display_commander.addon32";
                        std::wstring sourcePath = centralAddonDir + L"\\" + addonFileName;
                        std::wstring targetDll;
                        for (const char* p = targetApi; p && *p; ++p) targetDll += (wchar_t)(unsigned char)*p;
                        targetDll += L".dll";
                        std::wstring targetPath = addonDir + L"\\" + targetDll;
                        if (GetFileAttributesW(sourcePath.c_str()) == INVALID_FILE_ATTRIBUTES) {
                            s_setupReshadeResult = "Install failed: " + WstringToUtf8(addonFileName)
                                + " not found in central folder. Copy the addon to %LOCALAPPDATA%\\Programs\\Display_Commander first.";
                        } else if (CopyFileW(sourcePath.c_str(), targetPath.c_str(), FALSE)) {
                            s_preferredSetupApi = targetApi;
                            s_setupReshadeResult = "Display Commander installed as " + WstringToUtf8(targetDll) + " ("
                                                   + (exeDetect.is_64bit ? "64-bit" : "32-bit") + ").";
                        } else {
                            s_setupReshadeResult = "Install failed: could not copy (access denied or disk error).";
                        }
                    };

                    // ---- Exe name and Start / Stop ----
                    if (!exeFoundUtf8.empty()) {
                        std::string exeNameUtf8 =
                            WstringToUtf8(std::filesystem::path(exeFoundLocal).filename().wstring());
                        if (!exeNameUtf8.empty()) {
                            ImGui::Text("Exe: %s", exeNameUtf8.c_str());
                            if (ImGui::IsItemHovered())
                                ImGui::SetTooltip("%s", RedactPathForDisplay(exeFoundUtf8).c_str());
                        }
                        if (s_startedGamePid != 0) {
                            HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, s_startedGamePid);
                            if (h == nullptr) {
                                s_startedGamePid = 0;
                                s_startedGameTick = 0;
                            } else {
                                CloseHandle(h);
                            }
                        }
                        if (s_startedGamePid != 0) {
                            if (ImGui::Button("Stop")) {
                                HANDLE h = OpenProcess(PROCESS_TERMINATE, FALSE, s_startedGamePid);
                                if (h != nullptr) {
                                    TerminateProcess(h, 0);
                                    CloseHandle(h);
                                }
                                s_startedGamePid = 0;
                                s_startedGameTick = 0;
                            }
                            if (ImGui::IsItemHovered()) {
                                ULONGLONG elapsedMs = GetTickCount64() - s_startedGameTick;
                                unsigned long elapsedSec = (unsigned long)(elapsedMs / 1000);
                                ImGui::SetTooltip("Terminate game (PID %lu, %lus)", (unsigned long)s_startedGamePid,
                                                  elapsedSec);
                            }
                            ImGui::SameLine();
                            ImGui::TextDisabled("Running");
                        } else {
                            if (ImGui::Button("Start")) {
                                std::wstring workDir = std::filesystem::path(exeFoundLocal).parent_path().wstring();
                                const wchar_t* workDirPtr = workDir.empty() ? addonDir.c_str() : workDir.c_str();
                                SHELLEXECUTEINFOW sei = {};
                                sei.cbSize = sizeof(sei);
                                sei.fMask = SEE_MASK_NOCLOSEPROCESS;
                                sei.lpVerb = L"open";
                                sei.lpFile = exeFoundLocal.c_str();
                                sei.lpDirectory = workDirPtr;
                                sei.nShow = SW_SHOWNORMAL;
                                if (ShellExecuteExW(&sei) && sei.hProcess != nullptr) {
                                    s_startedGamePid = GetProcessId(sei.hProcess);
                                    s_startedGameTick = GetTickCount64();
                                    CloseHandle(sei.hProcess);
                                }
                            }
                            if (ImGui::IsItemHovered())
                                ImGui::SetTooltip("Launch %s", RedactPathForDisplay(exeFoundUtf8).c_str());
                        }
                    }

                    // ---- Detected: 64-bit/32-bit and graphics API (read-only from exe) ----
                    if (exeDetectOk) {
                        const char* apiDisplay = (detectedApi && detectedApi[0]) ? detectedApi : "(not detected)";
                        ImGui::Text("%s  ·  Graphics: %s", is64bit ? "64-bit" : "32-bit", apiDisplay);
                        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Detected from exe (override in Advanced).");
                    } else if (!exeFoundUtf8.empty()) {
                        ImGui::TextDisabled("(exe not detected: bitness/graphics unknown)");
                    }

                    // ---- ReShade State / DC State ----
                    bool reshadeInstalled = false;
                    bool dcInstalled = false;
                    std::string reshadeStateDisplay = "Not Installed";
                    std::string reshadeStateTooltip = "ReShade core DLL not found. Use Advanced -> Update ReShade.";
                    std::string reshadeGlobalVer = "(not installed)";
                    std::string dcLocalDisplay = "Not installed";
                    if (!addonDir.empty() && exeDetectOk) {
                        const wchar_t* coreName = exeDetect.is_64bit ? L"ReShade64.dll" : L"ReShade32.dll";
                        std::wstring corePath = addonDir + L"\\" + coreName;
                        reshadeInstalled = (GetFileAttributesW(corePath.c_str()) != INVALID_FILE_ATTRIBUTES);
                        if (reshadeInstalled) {
                            std::string ver = GetFileVersionStringUtf8(corePath);
                            if (ver.empty()) ver = "?";
                            const wchar_t* proxyFound = nullptr;
                            for (const auto& n : reshadeDllsPresent) {
                                if (IsReshadeApiProxyDll(n)) {
                                    proxyFound = n.c_str();
                                    break;
                                }
                            }
                            if (proxyFound) {
                                reshadeStateDisplay = WstringToUtf8(proxyFound) + " " + ver;
                                reshadeStateTooltip = "ReShade is installed. Core: ";
                                reshadeStateTooltip += WstringToUtf8(coreName);
                                reshadeStateTooltip += " (";
                                reshadeStateTooltip += ver;
                                reshadeStateTooltip += "). Proxy in use: ";
                                reshadeStateTooltip += WstringToUtf8(proxyFound);
                                reshadeStateTooltip += " (game loads this DLL).";
                            } else {
                                reshadeStateDisplay = "(core only) " + ver;
                                reshadeStateTooltip = "ReShade core ";
                                reshadeStateTooltip += WstringToUtf8(coreName);
                                reshadeStateTooltip += " present (";
                                reshadeStateTooltip += ver;
                                reshadeStateTooltip +=
                                    "). No proxy DLL (dxgi/d3d11/etc.) found; use Advanced -> Install Display "
                                    "Commander as proxy to "
                                    "install.";
                            }
                        }
                        // ReShade version in central (global)
                        std::wstring centralCorePath = centralReshadeDir + L"\\" + coreName;
                        std::string gv = GetFileVersionStringUtf8(centralCorePath);
                        if (!gv.empty()) reshadeGlobalVer = gv;
                        std::vector<std::wstring> dcAddons;
                        CollectDisplayCommanderAddonsInDir(addonDir, dcAddons);
                        const wchar_t* suffix = exeDetect.is_64bit ? L".addon64" : L".addon32";
                        const size_t suffixLen = wcslen(suffix);
                        for (const auto& n : dcAddons) {
                            if (n.size() >= suffixLen && _wcsicmp(n.c_str() + n.size() - suffixLen, suffix) == 0) {
                                dcInstalled = true;
                                std::wstring addonPath = addonDir + L"\\" + n;
                                std::string av = GetFileVersionStringUtf8(addonPath);
                                dcLocalDisplay = WstringToUtf8(n) + (av.empty() ? "" : " " + av);
                                break;
                            }
                        }
                        // DC as proxy (.dll): same check as debug (StartAndInject export)
                        if (dcLocalDisplay == "Not installed") {
                            static const char* const proxyApis[] = {"dxgi", "d3d11", "d3d12", "d3d9", "opengl32"};
                            for (const char* api : proxyApis) {
                                std::wstring path = addonDir + L"\\";
                                for (const char* p = api; *p; ++p) path += (wchar_t)(unsigned char)*p;
                                path += L".dll";
                                if (GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES
                                    && DllHasExport(path, "StartAndInject")) {
                                    dcInstalled = true;
                                    std::string pv = GetFileVersionStringUtf8(path);
                                    dcLocalDisplay = std::string(api) + ".dll" + (pv.empty() ? "" : " " + pv);
                                    break;
                                }
                            }
                        }
                        // Auto-install DC as proxy once when DC not installed and exe detected (API supported)
                        if (!dcInstalled && !s_autoInstallDcDone && detectedApi && detectedApi[0]
                            && strcmp(detectedApi, "vulkan") != 0 && strcmp(detectedApi, "unknown") != 0) {
                            std::wstring centralAddonDir =
                                display_commander::game_launcher_registry::GetCentralAddonDir();
                            if (!centralAddonDir.empty()) {
                                const wchar_t* addonFileName = exeDetect.is_64bit ? L"zzz_display_commander.addon64"
                                                                                  : L"zzz_display_commander.addon32";
                                std::wstring sourcePath = centralAddonDir + L"\\" + addonFileName;
                                std::wstring targetDll;
                                for (const char* p = detectedApi; p && *p; ++p) targetDll += (wchar_t)(unsigned char)*p;
                                targetDll += L".dll";
                                std::wstring targetPath = addonDir + L"\\" + targetDll;
                                if (GetFileAttributesW(sourcePath.c_str()) != INVALID_FILE_ATTRIBUTES
                                    && CopyFileW(sourcePath.c_str(), targetPath.c_str(), FALSE)) {
                                    s_autoInstallDcDone = true;
                                    s_preferredSetupApi = detectedApi;
                                    dcInstalled = true;
                                    std::string pv = GetFileVersionStringUtf8(sourcePath);
                                    dcLocalDisplay = std::string(detectedApi) + ".dll" + (pv.empty() ? "" : " " + pv);
                                }
                            }
                            s_autoInstallDcDone = true;  // don't retry every frame if copy failed
                        }
                    }
                    ImGui::Text("ReShade (local): %s  |  ReShade (global): %s", reshadeStateDisplay.c_str(),
                                reshadeGlobalVer.c_str());
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", reshadeStateTooltip.c_str());
                    ImGui::Text("DC (local): %s", dcLocalDisplay.c_str());

                    ImGui::Spacing();
                    if (ImGui::CollapsingHeader("Advanced", ImGuiTreeNodeFlags_None)) {
                        if (addonDir.empty()) ImGui::BeginDisabled();
                        if (ImGui::Button("Open current folder")) {
                            SHELLEXECUTEINFOW sei = {};
                            sei.cbSize = sizeof(sei);
                            sei.lpVerb = L"open";
                            sei.lpFile = addonDir.c_str();
                            sei.nShow = SW_SHOWNORMAL;
                            ShellExecuteExW(&sei);
                        }
                        if (addonDir.empty()) ImGui::EndDisabled();
                        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Open this game/addon folder in Explorer.");
                        ImGui::Spacing();

                        // Debug (inside Advanced)
                        if (ImGui::Checkbox("Debug", &s_showDebug)) {
                        }
                        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Show paths, DLL list, and setup result.");
                        if (s_showDebug) {
                            ImGui::Indent();
                            ImGui::Text("Local (this folder)");
                            if (addonDir.empty()) {
                                ImGui::TextUnformatted("(unknown path)");
                            } else {
                                ImGui::TextWrapped("%s", RedactPathForDisplay(addonDirUtf8).c_str());
                                if (!exeFoundUtf8.empty()) {
                                    ImGui::Text("Exe: %s", RedactPathForDisplay(exeFoundUtf8).c_str());
                                    if (exeDetectOk) {
                                        ImGui::SameLine();
                                        ImGui::TextDisabled("  %s  %s", is64bit ? "64-bit" : "32-bit", detectedApi);
                                    }
                                }
                                ShowReshadeCoreVersionsForDir(addonDir, exeDetectOk, is64bit);
                            }
                            ImGui::Spacing();
                            ImGui::Text("Central (Display_Commander\\Reshade)");
                            if (centralDirUtf8.empty()) {
                                ImGui::TextDisabled("(LOCALAPPDATA not set)");
                            } else {
                                ImGui::TextWrapped("%s", "%%LOCALAPPDATA%%\\Programs\\Display_Commander\\Reshade");
                                ShowReshadeCoreVersionsForDir(centralReshadeDir, exeDetectOk, is64bit);
                            }
                            ImGui::Spacing();
                            ImGui::Text("Known DLLs in this folder:");
                            if (!addonDir.empty()) {
                                std::vector<std::wstring> displayCommanderPresent;
                                CollectDisplayCommanderAddonsInDir(addonDir, displayCommanderPresent);
                                auto archFilterReshade = [is64bit](const std::wstring& n) -> bool {
                                    if (_wcsicmp(n.c_str(), L"ReShade64.dll") == 0) return is64bit;
                                    if (_wcsicmp(n.c_str(), L"ReShade32.dll") == 0) return !is64bit;
                                    return true;
                                };
                                auto archFilterDc = [is64bit](const std::wstring& n) -> bool {
                                    std::wstring lower = n;
                                    for (auto& c : lower) c = (wchar_t)towlower((wint_t)c);
                                    if (lower.size() >= 8 && lower.compare(lower.size() - 8, 8, L".addon64") == 0)
                                        return is64bit;
                                    if (lower.size() >= 8 && lower.compare(lower.size() - 8, 8, L".addon32") == 0)
                                        return !is64bit;
                                    return false;
                                };
                                auto showKnownDll = [&addonDir](const std::wstring& n) {
                                    std::wstring dllPath = addonDir + L"\\" + n;
                                    std::string ver = GetFileVersionStringUtf8(dllPath);
                                    if (ver.empty()) ver = "(no version info)";
                                    bool hasReShadeRegisterAddon = DllHasExport(dllPath, "ReShadeRegisterAddon");
                                    bool hasStartAndInject = DllHasExport(dllPath, "StartAndInject");
                                    const char* kind = hasReShadeRegisterAddon ? "ReShade"
                                                       : hasStartAndInject     ? "Display Commander"
                                                                               : "Other";
                                    int need = WideCharToMultiByte(CP_UTF8, 0, n.c_str(), (int)n.size(), nullptr, 0,
                                                                   nullptr, nullptr);
                                    if (need > 0) {
                                        std::string nameUtf8(static_cast<size_t>(need), 0);
                                        WideCharToMultiByte(CP_UTF8, 0, n.c_str(), (int)n.size(), &nameUtf8[0], need,
                                                            nullptr, nullptr);
                                        if (strcmp(kind, "Display Commander") == 0) {
                                            ImGui::BulletText("Display Commander - %s: %s  %s", nameUtf8.c_str(),
                                                              ver.c_str(), hasStartAndInject ? "(StartAndInject)" : "");
                                        } else if (strcmp(kind, "ReShade") == 0) {
                                            ImGui::BulletText("ReShade - %s: %s", nameUtf8.c_str(), ver.c_str());
                                        } else {
                                            ImGui::BulletText("Other - %s: %s", nameUtf8.c_str(), ver.c_str());
                                        }
                                    }
                                };
                                bool anyShown = false;
                                for (const auto& n : reshadeDllsPresent) {
                                    if (archFilterReshade(n)) {
                                        showKnownDll(n);
                                        anyShown = true;
                                    }
                                }
                                for (const auto& n : displayCommanderPresent) {
                                    if (archFilterDc(n)) {
                                        showKnownDll(n);
                                        anyShown = true;
                                    }
                                }
                                if (!anyShown) ImGui::TextUnformatted("(none for this architecture)");
                            }
                            if (!s_setupReshadeResult.empty()) {
                                bool isSuccess = (s_setupReshadeResult.find("correctly") != std::string::npos
                                                  || s_setupReshadeResult.find("installed") != std::string::npos);
                                if (isSuccess)
                                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.9f, 0.4f, 1.0f));
                                else
                                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.4f, 0.4f, 1.0f));
                                ImGui::TextWrapped("%s", s_setupReshadeResult.c_str());
                                ImGui::PopStyleColor();
                            }
                            ImGui::Unindent();
                        }
                        ImGui::Spacing();

                        // Install Display Commander as proxy (copy addon from central as dxgi.dll / d3d11.dll / etc.)
                        ImGui::Text("Install Display Commander as proxy:");
                        bool hasAnyProxyInDir = false;
                        {
                            static const char* const proxyApis[] = {"dxgi", "d3d11", "d3d12", "d3d9", "opengl32"};
                            for (const char* api : proxyApis) {
                                std::wstring path = addonDir + L"\\";
                                for (const char* p = api; *p; ++p) path += (wchar_t)(unsigned char)*p;
                                path += L".dll";
                                if (GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES) {
                                    hasAnyProxyInDir = true;
                                    break;
                                }
                            }
                        }
                        if (!hasAnyProxyInDir && detectedApi && detectedApi[0]) {
                            if (strcmp(detectedApi, "vulkan") == 0)
                                ImGui::TextDisabled("Default (from Graphics): vulkan — not supported yet.");
                            else
                                ImGui::Text("Default (from Graphics): %s", detectedApi);
                        }
                        struct ApiChoice {
                            const char* label;
                            const char* api;
                            bool supported;
                        };
                        const ApiChoice apis[] = {
                            {"dxgi.dll", "dxgi", true},         {"d3d9.dll", "d3d9", true},
                            {"d3d11.dll", "d3d11", true},       {"d3d12.dll", "d3d12", true},
                            {"opengl32.dll", "opengl32", true}, {"vulkan", "vulkan", false},
                        };
                        for (const auto& a : apis) {
                            if (a.label != apis[0].label) ImGui::SameLine();
                            if (!a.supported || !canSetup) ImGui::BeginDisabled();
                            if (ImGui::Button((std::string(a.label) + "##override").c_str())) {
                                if (a.supported) doInstallDcAsProxy(a.api);
                            }
                            if (ImGui::IsItemHovered()) {
                                if (a.supported)
                                    ImGui::SetTooltip("Copy Display Commander addon from central as %s.dll", a.api);
                                else
                                    ImGui::SetTooltip("Not supported yet.");
                            }
                            if (!a.supported || !canSetup) ImGui::EndDisabled();
                        }
                        ImGui::Spacing();

                        ImGui::Text("ReShade version (default: 6.7.2)");
                        if (!s_reshadeVersions.empty()) {
                            if (s_reshadeVersionIndex >= (int)s_reshadeVersions.size()) s_reshadeVersionIndex = 1;
                            auto getter = [](void* data, int idx, const char** out) -> bool {
                                const auto* v = static_cast<const std::vector<std::string>*>(data);
                                if (idx < 0 || (size_t)idx >= v->size()) return false;
                                *out = (*v)[(size_t)idx].c_str();
                                return true;
                            };
                            ImGui::SetNextItemWidth(120.f);
                            ImGui::Combo("##reshade_ver", &s_reshadeVersionIndex, getter, &s_reshadeVersions,
                                         (int)s_reshadeVersions.size());
                        }
                        if (s_reshadeUpdateInProgress) {
                            ImGui::TextDisabled("Updating ReShade...");
                        } else if (!s_reshadeUpdateResult.empty()) {
                            ImGui::TextWrapped("%s", s_reshadeUpdateResult.c_str());
                        }
                        std::string selectedVer = s_reshadeVersions.empty()
                                                      ? "6.7.2"
                                                      : (s_reshadeVersionIndex < (int)s_reshadeVersions.size()
                                                             ? s_reshadeVersions[(size_t)s_reshadeVersionIndex]
                                                             : s_reshadeVersions[0]);
                        bool canUpdate = !addonDir.empty() && !s_reshadeUpdateInProgress && !s_reshadeVersions.empty();
                        if (!canUpdate) ImGui::BeginDisabled();
                        std::string updateLabel = "Update ReShade to " + selectedVer;
                        if (ImGui::Button(updateLabel.c_str())) {
                            auto* params = new ReshadeUpdateParams{addonDir, centralReshadeDir, selectedVer};
                            s_reshadeUpdateResult.clear();
                            s_reshadeUpdateInProgress = true;
                            HANDLE h = CreateThread(nullptr, 0, ReshadeUpdateWorker, params, 0, nullptr);
                            if (h) {
                                CloseHandle(h);
                            } else {
                                delete params;
                                s_reshadeUpdateResult = "Update failed: could not start worker.";
                                s_reshadeUpdateInProgress = false;
                            }
                        }
                        if (!canUpdate) ImGui::EndDisabled();
                        if (ImGui::IsItemHovered())
                            ImGui::SetTooltip(
                                "Download and store ReShade in central folder only (not in game directory).");
                    }

                    ImGui::Spacing();
                    if (ImGui::Button("Close##installer")) done = true;
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("Games")) {
                    // ---- Games that ran Display Commander ----
                    ImGui::Text("Games that ran Display Commander");
                    ImGui::SameLine();
                    if (ImGui::Button("Add Steam game")) s_pleaseOpenSteamSearch = true;
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("Search and add an installed Steam game to this list.");
                    ImGui::Spacing();
                    {
                        std::vector<display_commander::game_launcher_registry::GameEntry> games;
                        display_commander::game_launcher_registry::EnumerateGames(games);
                        std::sort(games.begin(), games.end(),
                                  [](const auto& a, const auto& b) { return a.last_run > b.last_run; });
                        std::wstring centralAddonDir = display_commander::game_launcher_registry::GetCentralAddonDir();
                        if (games.empty()) {
                            ImGui::TextDisabled(
                                "(No games recorded yet. Run a game with Display Commander to add it here.)");
                        } else {
                            time_t now = time(nullptr);
                            struct tm nowTm = {};
                            localtime_s(&nowTm, &now);
                            const int currentYear = nowTm.tm_year + 1900;
                            const int currentMonth = nowTm.tm_mon;

                            const char* monthNames[] = {"January",   "February", "March",    "April",
                                                        "May",       "June",     "July",     "August",
                                                        "September", "October",  "November", "December"};

                            int sectionYear = -1;
                            int sectionMonth = -1;
                            for (const auto& entry : games) {
                                time_t lastRun = (time_t)entry.last_run;
                                int y = currentYear, m = 12;
                                if (lastRun > 0) {
                                    struct tm ptm = {};
                                    if (localtime_s(&ptm, &lastRun) == 0) {
                                        y = ptm.tm_year + 1900;
                                        m = ptm.tm_mon;
                                    }
                                }
                                if (y != sectionYear || m != sectionMonth) {
                                    sectionYear = y;
                                    sectionMonth = m;
                                    ImGui::Spacing();
                                    if (y == currentYear && m == currentMonth) {
                                        ImGui::TextDisabled("Recent (this month)");
                                    } else if ((y == currentYear && m == currentMonth - 1)
                                               || (currentMonth == 0 && y == currentYear - 1 && m == 11)) {
                                        ImGui::TextDisabled("%s", monthNames[m]);
                                    } else {
                                        ImGui::TextDisabled("%s %d", monthNames[m], y);
                                    }
                                }
                                std::string titleUtf8 = !entry.window_title.empty() ? WstringToUtf8(entry.window_title)
                                                                                    : GetExeProductNameUtf8(entry.path);
                                if (titleUtf8.empty()) titleUtf8 = WstringToUtf8(entry.name);
                                std::string pathUtf8 = WstringToUtf8(entry.path);
                                std::string gameIdUtf8 = WstringToUtf8(entry.key);
                                ImGui::PushID(gameIdUtf8.c_str());
                                ImGui::TextWrapped("%s", titleUtf8.c_str());
                                if (ImGui::IsItemHovered())
                                    ImGui::SetTooltip("%s", RedactPathForDisplay(pathUtf8).c_str());
                                DWORD pid = GetPidByExePath(entry.path);
                                if (pid != 0) {
                                    ImGui::SameLine();
                                    ImGui::TextDisabled("Running");
                                    ImGui::SameLine();
                                    if (ImGui::Button(("Stop##" + gameIdUtf8).c_str())) {
                                        DWORD pidCopy = pid;
                                        std::thread([pidCopy]() {
                                            HANDLE h = OpenProcess(PROCESS_TERMINATE, FALSE, pidCopy);
                                            if (h != nullptr) {
                                                TerminateProcess(h, 0);
                                                CloseHandle(h);
                                            }
                                        }).detach();
                                    }
                                    if (ImGui::IsItemHovered())
                                        ImGui::SetTooltip("Terminate process (PID %lu).", (unsigned long)pid);
                                } else {
                                    ImGui::SameLine();
                                    if (ImGui::Button(("Start##" + gameIdUtf8).c_str())) {
                                        std::wstring pathCopy = entry.path;
                                        std::wstring argsCopy = entry.arguments;
                                        std::wstring workDir =
                                            std::filesystem::path(entry.path).parent_path().wstring();
                                        if (workDir.empty()) workDir = L".";
                                        std::thread([pathCopy, argsCopy, workDir]() {
                                            SHELLEXECUTEINFOW sei = {};
                                            sei.cbSize = sizeof(sei);
                                            sei.fMask = SEE_MASK_NOCLOSEPROCESS;
                                            sei.lpVerb = L"open";
                                            sei.lpFile = pathCopy.c_str();
                                            sei.lpParameters = argsCopy.empty() ? nullptr : argsCopy.c_str();
                                            sei.lpDirectory = workDir.c_str();
                                            sei.nShow = SW_SHOWNORMAL;
                                            if (ShellExecuteExW(&sei) && sei.hProcess != nullptr)
                                                CloseHandle(sei.hProcess);
                                        }).detach();
                                    }
                                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Launch game.");
                                }
                                ImGui::SameLine();
                                if (ImGui::Button(("Details##" + gameIdUtf8).c_str())) {
                                    s_gameDetailsEntry = entry;
                                    s_pleaseOpenGameDetails = true;
                                }
                                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Show path, arguments, and last run.");
                                ImGui::PopID();
                            }
                        }
                        if (!s_updateDcResult.empty()) {
                            ImGui::Spacing();
                            ImGui::TextWrapped("%s", s_updateDcResult.c_str());
                        }
                    }
                    ImGui::Spacing();

                    ImGui::EndTabItem();
                }
                ImGui::EndTabBar();
            }
            // Open popup from window level so it matches BeginPopupModal's stack (fixes Details not opening)
            if (s_pleaseOpenGameDetails) {
                ImGui::OpenPopup("Game Details");
                s_pleaseOpenGameDetails = false;
            }
            if (s_pleaseOpenSteamSearch) {
                ImGui::OpenPopup("Add Steam game");
                s_pleaseOpenSteamSearch = false;
            }
            if (ImGui::BeginPopupModal("Game Details", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                std::string titleUtf8 = !s_gameDetailsEntry.window_title.empty()
                                            ? WstringToUtf8(s_gameDetailsEntry.window_title)
                                            : GetExeProductNameUtf8(s_gameDetailsEntry.path);
                if (titleUtf8.empty()) titleUtf8 = WstringToUtf8(s_gameDetailsEntry.name);
                ImGui::Text("Title: %s", titleUtf8.c_str());
                ImGui::Text("Path: %s", RedactPathForDisplay(WstringToUtf8(s_gameDetailsEntry.path)).c_str());
                ImGui::Text("Exe: %s", WstringToUtf8(s_gameDetailsEntry.name).c_str());
                ImGui::Text("Arguments: %s", s_gameDetailsEntry.arguments.empty()
                                                 ? "(none)"
                                                 : WstringToUtf8(s_gameDetailsEntry.arguments).c_str());
                if (s_gameDetailsEntry.last_run > 0) {
                    time_t lastRun = (time_t)s_gameDetailsEntry.last_run;
                    struct tm ptm = {};
                    if (localtime_s(&ptm, &lastRun) == 0) {
                        char buf[64];
                        strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", &ptm);
                        ImGui::Text("Last run: %s", buf);
                    }
                } else {
                    ImGui::Text("Last run: (never)");
                }
                ImGui::Spacing();
                ImGui::Separator();
                // ---- Setup-like: detected bitness and graphics API ----
                std::wstring gameDir = std::filesystem::path(s_gameDetailsEntry.path).parent_path().wstring();
                cli_detect_exe::DetectResult gameDetect = {};
                bool gameDetectOk = (GetFileAttributesW(s_gameDetailsEntry.path.c_str()) != INVALID_FILE_ATTRIBUTES)
                                    && cli_detect_exe::DetectExeForPath(s_gameDetailsEntry.path.c_str(), &gameDetect);
                bool gameIs64bit = gameDetectOk ? gameDetect.is_64bit : true;
                const char* detectedApi = gameDetectOk ? cli_detect_exe::ReShadeDllFromDetect(gameDetect) : "";
                if (gameDetectOk) {
                    const char* apiDisplay = (detectedApi && detectedApi[0]) ? detectedApi : "(not detected)";
                    ImGui::Text("%s  ·  Graphics: %s", gameIs64bit ? "64-bit" : "32-bit", apiDisplay);
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Detected from exe.");
                } else {
                    ImGui::TextDisabled("(exe not detected: bitness/graphics unknown)");
                }
                if (!gameDir.empty() && ImGui::Button("Open game folder##game_details")) {
                    SHELLEXECUTEINFOW sei = {};
                    sei.cbSize = sizeof(sei);
                    sei.lpVerb = L"open";
                    sei.lpFile = gameDir.c_str();
                    sei.nShow = SW_SHOWNORMAL;
                    ShellExecuteExW(&sei);
                }
                if (ImGui::IsItemHovered() && !gameDir.empty())
                    ImGui::SetTooltip("Open this game's folder in Explorer.");
                // ---- ReShade State: proxy DLLs that are ReShade (no StartAndInject = not DC) ----
                static const char* const kReshadeProxyApis[] = {"dxgi", "d3d11", "d3d12", "d3d9", "opengl32"};
                std::string reshadeStateDisplay = "Not Installed";
                std::string reshadeStateTooltip =
                    "No ReShade proxy (dxgi/d3d11/etc.) in this game's folder. DC proxy DLLs are excluded.";
                for (const char* api : kReshadeProxyApis) {
                    std::wstring proxyPath = gameDir + L"\\";
                    for (const char* p = api; *p; ++p) proxyPath += (wchar_t)(unsigned char)*p;
                    proxyPath += L".dll";
                    if (GetFileAttributesW(proxyPath.c_str()) != INVALID_FILE_ATTRIBUTES) {
                        if (DllHasExport(proxyPath, "StartAndInject"))
                            continue;  // Display Commander proxy, not ReShade
                        std::string ver = GetFileVersionStringUtf8(proxyPath);
                        if (ver.empty()) ver = "?";
                        reshadeStateDisplay = std::string(api) + ".dll " + ver;
                        reshadeStateTooltip = "ReShade is installed as ";
                        reshadeStateTooltip += api;
                        reshadeStateTooltip += ".dll (game loads this DLL). Version ";
                        reshadeStateTooltip += ver;
                        break;
                    }
                }
                ImGui::Text("ReShade State(local): %s", reshadeStateDisplay.c_str());
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", reshadeStateTooltip.c_str());
                // ---- DC State: all proxy DLLs with StartAndInject + all .addon64/.addon32 with
                // GetDisplayCommanderVersion ----
                struct DcFileEntry {
                    std::string name;  // e.g. "dxgi.dll" or "zzz_display_commander.addon64"
                    std::string version;
                };
                std::vector<DcFileEntry> dcProxyList;
                std::vector<DcFileEntry> dcAddonList;
                for (const char* api : kReshadeProxyApis) {
                    std::wstring proxyPath = gameDir + L"\\";
                    for (const char* p = api; *p; ++p) proxyPath += (wchar_t)(unsigned char)*p;
                    proxyPath += L".dll";
                    if (GetFileAttributesW(proxyPath.c_str()) != INVALID_FILE_ATTRIBUTES
                        && DllHasExport(proxyPath, "StartAndInject")) {
                        std::string ver = GetFileVersionStringUtf8(proxyPath);
                        dcProxyList.push_back({std::string(api) + ".dll", ver.empty() ? "?" : ver});
                    }
                }
                {
                    std::wstring prefix = gameDir;
                    if (!prefix.empty() && prefix.back() != L'\\') prefix += L'\\';
                    for (const wchar_t* ext : {L"*.addon64", L"*.addon32"}) {
                        WIN32_FIND_DATAW fd = {};
                        HANDLE h = FindFirstFileW((prefix + ext).c_str(), &fd);
                        if (h == INVALID_HANDLE_VALUE) continue;
                        do {
                            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
                            std::wstring path = prefix + fd.cFileName;
                            if (DllHasExport(path, "GetDisplayCommanderVersion")) {
                                std::string ver = GetFileVersionStringUtf8(path);
                                dcAddonList.push_back({WstringToUtf8(fd.cFileName), ver.empty() ? "?" : ver});
                            }
                        } while (FindNextFileW(h, &fd));
                        FindClose(h);
                    }
                }
                bool dcInstalledInGame = !dcProxyList.empty() || !dcAddonList.empty();
                // Auto-install DC as proxy once per game when no DC proxy and exe detected (API supported)
                static std::wstring s_autoInstallDcGameDir;
                if (dcProxyList.empty() && gameDetectOk && detectedApi && detectedApi[0]
                    && strcmp(detectedApi, "vulkan") != 0 && strcmp(detectedApi, "unknown") != 0
                    && s_autoInstallDcGameDir != gameDir) {
                    s_autoInstallDcGameDir = gameDir;
                    std::wstring centralAddonDir = display_commander::game_launcher_registry::GetCentralAddonDir();
                    if (!centralAddonDir.empty()) {
                        const wchar_t* addonFileName =
                            gameIs64bit ? L"zzz_display_commander.addon64" : L"zzz_display_commander.addon32";
                        std::wstring sourcePath = centralAddonDir + L"\\" + addonFileName;
                        std::wstring targetDll;
                        for (const char* p = detectedApi; p && *p; ++p) targetDll += (wchar_t)(unsigned char)*p;
                        targetDll += L".dll";
                        std::wstring targetPath = gameDir + L"\\" + targetDll;
                        if (GetFileAttributesW(sourcePath.c_str()) != INVALID_FILE_ATTRIBUTES
                            && CopyFileW(sourcePath.c_str(), targetPath.c_str(), FALSE)) {
                            dcProxyList.push_back({WstringToUtf8(targetDll), GetFileVersionStringUtf8(sourcePath)});
                            if (dcProxyList.back().version.empty()) dcProxyList.back().version = "?";
                        }
                    }
                }
                if (dcInstalledInGame) {
                    std::string dcLine;
                    if (!dcProxyList.empty()) {
                        for (size_t i = 0; i < dcProxyList.size(); ++i) {
                            if (i) dcLine += "; ";
                            dcLine += dcProxyList[i].name + " (" + dcProxyList[i].version + ")";
                        }
                    }
                    if (!dcAddonList.empty()) {
                        if (!dcLine.empty()) dcLine += "  |  ";
                        for (size_t i = 0; i < dcAddonList.size(); ++i) {
                            if (i) dcLine += "; ";
                            dcLine += dcAddonList[i].name + " (" + dcAddonList[i].version + ")";
                        }
                    }
                    ImGui::Text("DC State: %s", dcLine.c_str());
                    std::string dcTooltip = "Display Commander: ";
                    if (!dcProxyList.empty()) {
                        dcTooltip += "proxy " + std::to_string(dcProxyList.size()) + " DLL(s). ";
                        if (!dcAddonList.empty())
                            dcTooltip += "Remove .addon64/.addon32 when using proxy (redundant). ";
                    }
                    if (!dcAddonList.empty()) dcTooltip += std::to_string(dcAddonList.size()) + " addon file(s). ";
                    dcTooltip += "Identified by StartAndInject (proxy) / GetDisplayCommanderVersion (addon).";
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", dcTooltip.c_str());
                } else {
                    ImGui::Text("DC State: Not Installed (install as proxy: dxgi.dll etc.)");
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip(
                            "Display Commander is installed as proxy (e.g. dxgi.dll). We do not install "
                            ".addon64/.addon32 "
                            "in the game folder.");
                }
                ImGui::Spacing();
                // ---- Install Display Commander as proxy (same logic as Setup: central addon -> game as api.dll) ----
                ImGui::Text("Install Display Commander as proxy:");
                bool canInstallDcGame = !gameDir.empty() && gameDetectOk && detectedApi && detectedApi[0]
                                        && strcmp(detectedApi, "vulkan") != 0 && strcmp(detectedApi, "unknown") != 0;
                struct GameDetailsApiChoice {
                    const char* label;
                    const char* api;
                    bool supported;
                };
                const GameDetailsApiChoice gameDetailsApis[] = {
                    {"dxgi.dll", "dxgi", true},   {"d3d9.dll", "d3d9", true},         {"d3d11.dll", "d3d11", true},
                    {"d3d12.dll", "d3d12", true}, {"opengl32.dll", "opengl32", true}, {"vulkan", "vulkan", false},
                };
                if (!canInstallDcGame) ImGui::BeginDisabled();
                for (const auto& a : gameDetailsApis) {
                    if (a.label != gameDetailsApis[0].label) ImGui::SameLine();
                    if (!a.supported) ImGui::BeginDisabled();
                    if (ImGui::Button((std::string(a.label) + "##game_details_install").c_str())) {
                        if (a.supported) {
                            s_gameDetailsReshadeResult.clear();
                            std::wstring centralAddonDir =
                                display_commander::game_launcher_registry::GetCentralAddonDir();
                            if (centralAddonDir.empty()) {
                                s_gameDetailsReshadeResult =
                                    "Install failed: central addon folder not set (LOCALAPPDATA).";
                            } else {
                                const wchar_t* addonFileName =
                                    gameIs64bit ? L"zzz_display_commander.addon64" : L"zzz_display_commander.addon32";
                                std::wstring sourcePath = centralAddonDir + L"\\" + addonFileName;
                                std::wstring targetDll;
                                for (const char* p = a.api; p && *p; ++p) targetDll += (wchar_t)(unsigned char)*p;
                                targetDll += L".dll";
                                std::wstring targetPath = gameDir + L"\\" + targetDll;
                                if (GetFileAttributesW(sourcePath.c_str()) == INVALID_FILE_ATTRIBUTES) {
                                    s_gameDetailsReshadeResult =
                                        "Install failed: " + WstringToUtf8(addonFileName)
                                        + " not found in central folder. Copy the addon to %LOCALAPPDATA%\\Programs\\Display_Commander first.";
                                } else if (CopyFileW(sourcePath.c_str(), targetPath.c_str(), FALSE)) {
                                    s_gameDetailsReshadeResult = "Display Commander installed as "
                                                                 + WstringToUtf8(targetDll) + " ("
                                                                 + (gameIs64bit ? "64-bit" : "32-bit") + ").";
                                } else {
                                    s_gameDetailsReshadeResult =
                                        "Install failed: could not copy (access denied or disk error).";
                                }
                            }
                        }
                    }
                    if (ImGui::IsItemHovered()) {
                        if (a.supported)
                            ImGui::SetTooltip("Copy Display Commander addon from central as %s.dll", a.api);
                        else
                            ImGui::SetTooltip("Not supported yet.");
                    }
                    if (!a.supported) ImGui::EndDisabled();
                }
                if (!canInstallDcGame) ImGui::EndDisabled();
                if (!s_gameDetailsReshadeResult.empty()) ImGui::TextWrapped("%s", s_gameDetailsReshadeResult.c_str());
                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();
                // Update DC proxy: overwrite existing proxy DLL(s) in game from central (no .addon64/.addon32)
                if (ImGui::Button("Update DC proxy##game_details")) {
                    s_updateDcResult.clear();
                    std::wstring centralAddonDir = display_commander::game_launcher_registry::GetCentralAddonDir();
                    if (centralAddonDir.empty()) {
                        s_updateDcResult = "Update failed: LOCALAPPDATA not set.";
                    } else {
                        std::wstring gdir = std::filesystem::path(s_gameDetailsEntry.path).parent_path().wstring();
                        const wchar_t* addonFileName =
                            gameIs64bit ? L"zzz_display_commander.addon64" : L"zzz_display_commander.addon32";
                        std::wstring sourcePath = centralAddonDir + L"\\" + addonFileName;
                        if (GetFileAttributesW(sourcePath.c_str()) == INVALID_FILE_ATTRIBUTES) {
                            s_updateDcResult = "No Display Commander addon in central folder for this bitness.";
                        } else {
                            int updated = 0;
                            for (const char* api : kReshadeProxyApis) {
                                std::wstring proxyPath = gdir + L"\\";
                                for (const char* p = api; *p; ++p) proxyPath += (wchar_t)(unsigned char)*p;
                                proxyPath += L".dll";
                                if (GetFileAttributesW(proxyPath.c_str()) != INVALID_FILE_ATTRIBUTES
                                    && DllHasExport(proxyPath, "StartAndInject")) {
                                    if (CopyFileW(sourcePath.c_str(), proxyPath.c_str(), FALSE)) ++updated;
                                }
                            }
                            s_updateDcResult = (updated > 0) ? "Display Commander proxy updated ("
                                                                   + std::to_string(updated) + " DLL(s))."
                                                             : "No DC proxy DLL found in game folder to update.";
                        }
                    }
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip(
                        "Overwrite existing DC proxy DLL(s) (dxgi/d3d11/etc.) in this game's folder from central. Does "
                        "not copy .addon64/.addon32.");
                if (!s_updateDcResult.empty()) ImGui::TextWrapped("%s", s_updateDcResult.c_str());
                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();
                if (ImGui::Button("Remove from list##game_details")) {
                    ImGui::OpenPopup("Remove game from list?");
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip(
                        "Remove this game from the registry. It will no longer appear in the installer list. You can "
                        "re-add it by running the game with Display Commander.");
                if (ImGui::BeginPopupModal("Remove game from list?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                    ImGui::Text("Remove this game from the list?");
                    ImGui::TextUnformatted(
                        "It will no longer appear in the installer. You can re-add it by running the game with Display "
                        "Commander.");
                    ImGui::Spacing();
                    if (ImGui::Button("Yes, remove##game_details_confirm")) {
                        display_commander::game_launcher_registry::RemoveGame(s_gameDetailsEntry.path.c_str());
                        ImGui::CloseCurrentPopup();
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Cancel##game_details_remove_cancel")) ImGui::CloseCurrentPopup();
                    ImGui::EndPopup();
                }
                ImGui::Spacing();
                if (ImGui::Button("Close##game_details")) ImGui::CloseCurrentPopup();
                ImGui::EndPopup();
            }
            if (ImGui::BeginPopupModal("Add Steam game", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                static std::vector<display_commander::steam_library::SteamGame> s_steamGameList;
                static char s_steamSearchBuf[256] = "";
                if (s_steamGameList.empty()) {
                    display_commander::steam_library::GetInstalledGames(s_steamGameList);
                }
                ImGui::Text("Search installed Steam games (substring match):");
                ImGui::SetNextItemWidth(-1.f);
                ImGui::InputText("##steam_search", s_steamSearchBuf, sizeof(s_steamSearchBuf));
                auto toLowerAscii = [](std::string s) {
                    for (char& c : s)
                        if (c >= 'A' && c <= 'Z') c += 32;
                    return s;
                };
                std::string searchLower = toLowerAscii(s_steamSearchBuf);
                ImGui::BeginChild("##steam_list", ImVec2(400, 220), true);
                int shown = 0;
                for (const auto& game : s_steamGameList) {
                    std::string nameLower = toLowerAscii(game.name);
                    if (!searchLower.empty() && nameLower.find(searchLower) == std::string::npos) continue;
                    ++shown;
                    ImGui::PushID((int)(game.app_id));
                    ImGui::TextWrapped("%s", game.name.c_str());
                    ImGui::SameLine(320.f);
                    if (ImGui::Button("Add")) {
                        std::wstring exePath = display_commander::steam_library::FindMainExeInDir(game.install_dir);
                        if (!exePath.empty()) {
                            display_commander::game_launcher_registry::RecordGameRun(exePath.c_str(), L"", L"");
                        }
                    }
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("Add this game to the list (uses first .exe in install folder).");
                    ImGui::PopID();
                }
                if (shown == 0)
                    ImGui::TextDisabled(s_steamGameList.empty() ? "No Steam library found." : "No games match search.");
                ImGui::EndChild();
                ImGui::Spacing();
                if (ImGui::Button("Close##steam_search")) {
                    s_steamGameList.clear();
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }
            ImGui::End();
        }

        ImGui::Render();
        const float clear[4] = {0.15f, 0.15f, 0.18f, 1.0f};
        g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, nullptr);
        g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, clear);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

        g_pSwapChain->Present(1, 0);
    }

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    CleanupDeviceD3D();
    DestroyWindow(hwnd);
    UnregisterClassW(wc.lpszClassName, wc.hInstance);
}

static bool CreateDeviceD3D(HWND hWnd) {
    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferCount = 2;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hWnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    D3D_FEATURE_LEVEL featureLevel;
    const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0};
    HRESULT hr =
        D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, levels, 2, D3D11_SDK_VERSION, &sd,
                                      &g_pSwapChain, &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext);
    if (hr == DXGI_ERROR_UNSUPPORTED)
        hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, levels, 2, D3D11_SDK_VERSION, &sd,
                                           &g_pSwapChain, &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext);
    if (hr != S_OK) return false;
    CreateRenderTarget();
    return true;
}

static void CleanupDeviceD3D() {
    CleanupRenderTarget();
    if (g_pSwapChain) {
        g_pSwapChain->Release();
        g_pSwapChain = nullptr;
    }
    if (g_pd3dDeviceContext) {
        g_pd3dDeviceContext->Release();
        g_pd3dDeviceContext = nullptr;
    }
    if (g_pd3dDevice) {
        g_pd3dDevice->Release();
        g_pd3dDevice = nullptr;
    }
}

static void CreateRenderTarget() {
    ID3D11Texture2D* pBackBuffer = nullptr;
    g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
    if (pBackBuffer) {
        g_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_mainRenderTargetView);
        pBackBuffer->Release();
    }
}

static void CleanupRenderTarget() {
    if (g_mainRenderTargetView) {
        g_mainRenderTargetView->Release();
        g_mainRenderTargetView = nullptr;
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
