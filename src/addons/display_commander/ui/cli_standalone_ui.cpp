// UITest: run the installer UI inside the addon DLL (no separate .exe).
// Uses a second ImGui build in namespace ImGuiStandalone (via compile define ImGui=ImGuiStandalone)
// to avoid symbol clash with ReShade's ImGui used in-game.

#define ImGui ImGuiStandalone
#include <d3d11.h>
#include <dxgi.h>
#include <shellapi.h>
#include <windows.h>
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cwctype>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "backends/imgui_impl_dx11.h"
#include "backends/imgui_impl_win32.h"
#include "imgui.h"

#include "ui/cli_detect_exe.hpp"
#include "utils/version_check.hpp"
#include "version.hpp"

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
static void CollectDisplayCommanderAddonsInDir(const std::wstring& dir,
                                               std::vector<std::wstring>& outPresent) {
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
            if (name.find(L"display_commander") != std::wstring::npos)
                outPresent.push_back(fd.cFileName);
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    };
    addMatching(L"*.addon64");
    addMatching(L"*.addon32");
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
            if (rva >= va && rva < va + rawSize)
                return rawPtr + (rva - va);
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

// Central Reshade folder: %LOCALAPPDATA%\Programs\Display Commander\Reshade
static std::wstring GetCentralReshadeDir() {
    wchar_t buf[MAX_PATH];
    DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", buf, (DWORD)std::size(buf));
    if (n == 0 || n >= (DWORD)std::size(buf)) return {};
    std::wstring path = buf;
    if (!path.empty() && path.back() != L'\\') path += L'\\';
    path += L"Programs\\Display Commander\\Reshade";
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

// ReShade versions: hardcoded list (latest, 6.7.2 default, 6.7.1, 6.6.2).
static std::vector<std::string> s_reshadeVersions;
static int s_reshadeVersionIndex = 1;  // default "6.7.2"
static bool s_reshadeVersionsInitialized = false;

struct ReshadeUpdateParams {
    std::wstring addonDir;
    std::wstring centralDir;
    std::string selectedVersion;  // e.g. "6.7.1"
};

static DWORD WINAPI ReshadeUpdateWorker(LPVOID param) {
    std::unique_ptr<ReshadeUpdateParams> params(static_cast<ReshadeUpdateParams*>(param));
    const std::wstring& addonDir = params->addonDir;
    const std::wstring& centralDir = params->centralDir;

    std::filesystem::path tempDir;
    {
        wchar_t tempPath[MAX_PATH];
        if (GetTempPathW((DWORD)std::size(tempPath), tempPath) == 0) {
            s_reshadeUpdateResult = "Update failed: could not get temp path.";
            s_reshadeUpdateInProgress = false;
            return 1;
        }
        tempDir = std::filesystem::path(tempPath) / L"dc_reshade_update";
        std::error_code ec;
        std::filesystem::create_directories(tempDir, ec);
        if (ec) {
            s_reshadeUpdateResult = "Update failed: could not create temp dir.";
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
        s_reshadeUpdateResult = "Update failed: download failed (check URL or network).";
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
        s_reshadeUpdateResult = "Update failed: tar extract failed (need Windows 10+ tar).";
        s_reshadeUpdateInProgress = false;
        return 1;
    }
    CloseHandle(pi.hThread);
    WaitForSingleObject(pi.hProcess, 60000);
    CloseHandle(pi.hProcess);

    std::filesystem::path extracted64 = tempDir / "ReShade64.dll";
    std::filesystem::path extracted32 = tempDir / "ReShade32.dll";
    if (!std::filesystem::exists(extracted64) || !std::filesystem::exists(extracted32)) {
        s_reshadeUpdateResult = "Update failed: extraction did not produce DLLs.";
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

    if (!copyTo(extracted64, extracted32, addonDir)) {
        s_reshadeUpdateResult = "Update failed: could not write to local folder.";
        s_reshadeUpdateInProgress = false;
        return 1;
    }
    if (!centralDir.empty()) {
        copyTo(extracted64, extracted32, centralDir);
    }

    std::error_code ec;
    std::filesystem::remove_all(tempDir, ec);
    s_reshadeUpdateResult = (params->selectedVersion.empty() || params->selectedVersion == "latest")
                                ? "ReShade updated (latest)."
                                : "ReShade updated to " + params->selectedVersion + ".";
    s_reshadeUpdateInProgress = false;
    return 0;
}

// Print ReShade64.dll / ReShade32.dll presence and version for the given directory.
static void ShowReshadeCoreVersionsForDir(const std::wstring& dir) {
    if (dir.empty()) return;
    for (const wchar_t* name : s_reshadeCoreNames) {
        std::wstring path = dir + L"\\" + name;
        DWORD att = GetFileAttributesW(path.c_str());
        if (att == INVALID_FILE_ATTRIBUTES || (att & FILE_ATTRIBUTE_DIRECTORY)) {
            ImGui::BulletText("%S: (not present)", name);
            continue;
        }
        std::string ver = GetFileVersionStringUtf8(path);
        if (ver.empty()) ver = "(no version info)";
        ImGui::BulletText("%S: %s", name, ver.c_str());
    }
}

void RunStandaloneUI(HINSTANCE hInst) {
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
    int titleLen = MultiByteToWideChar(CP_UTF8, 0, installerTitleUtf8.c_str(), (int)installerTitleUtf8.size() + 1,
                                       nullptr, 0);
    std::wstring installerTitleW(titleLen > 0 ? (size_t)titleLen : 0, 0);
    if (titleLen > 0)
        MultiByteToWideChar(CP_UTF8, 0, installerTitleUtf8.c_str(), (int)installerTitleUtf8.size() + 1,
                            &installerTitleW[0], titleLen);

    HWND hwnd = CreateWindowW(wc.lpszClassName, installerTitleW.empty() ? L"Display Commander - Installer"
                                                                        : installerTitleW.c_str(),
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

    // Use addon DLL's path (not rundll32's). When invoked via rundll32, hInst may be the addon
    // module, but GetModuleHandleEx(FROM_ADDRESS) guarantees we get this DLL's directory.
    HMODULE addonModule = nullptr;
    (void)GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT | GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                             reinterpret_cast<LPCWSTR>(&RunStandaloneUI), &addonModule);
    wchar_t modulePath[2048];
    DWORD modLen = 0;
    if (addonModule) modLen = GetModuleFileNameW(addonModule, modulePath, (DWORD)std::size(modulePath));
    std::wstring addonDir;
    if (modLen > 0 && modLen < (DWORD)std::size(modulePath)) {
        addonDir.assign(modulePath);
        size_t last = addonDir.find_last_of(L"\\/");
        if (last != std::wstring::npos) addonDir.resize(last);
    }
    std::vector<std::wstring> reshadeDllsPresent;
    static std::string s_setupReshadeResult;
    static DWORD s_startedGamePid = 0;
    static ULONGLONG s_startedGameTick = 0;  // when we started (GetTickCount64), for runtime tooltip
    static std::string s_preferredSetupApi;  // user's "Setup as X" choice; cleanup keeps this, removes others

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
        ImGui::SetNextWindowSize(ImVec2(400, 0), ImGuiCond_FirstUseEver);
        static char s_installerWindowTitle[128];
        snprintf(s_installerWindowTitle, sizeof(s_installerWindowTitle), "Display Commander - Installer (v%s)",
                 DISPLAY_COMMANDER_VERSION_STRING);
        if (ImGui::Begin(s_installerWindowTitle, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            if (!addonDir.empty()) {
                CollectReShadeDllsInDir(addonDir, reshadeDllsPresent);
                const char* detectedApi = exeDetectOk ? cli_detect_exe::ReShadeDllFromDetect(exeDetect) : "";
                const char* preferredApi =
                    s_preferredSetupApi.empty() ? detectedApi : s_preferredSetupApi.c_str();
                RemoveExtraReshadeApiProxyDlls(addonDir, preferredApi, reshadeDllsPresent);
            }

            if (!s_reshadeVersionsInitialized) {
                display_commander::utils::version_check::FetchReShadeVersionsFromGitHub(s_reshadeVersions, nullptr);
                if (s_reshadeVersionIndex >= (int)s_reshadeVersions.size()) s_reshadeVersionIndex = 1;
                s_reshadeVersionsInitialized = true;
            }

            ImGui::Text("Display Commander Installer UI");
            ImGui::Separator();
            ImGui::Text("Run via: rundll32.exe zzz_DisplayCommander.addon64,CommandLine UITest");
            ImGui::Spacing();

            // ---- Local (this folder) ----
            ImGui::Text("Local (this folder)");
            if (addonDir.empty()) {
                ImGui::TextUnformatted("(unknown path)");
            } else {
                ImGui::TextWrapped("%s", RedactPathForDisplay(addonDirUtf8).c_str());
                if (exeFoundUtf8.empty()) {
                    ImGui::Text("Exe found: (none)");
                } else {
                    ImGui::Text("Exe found: %s", RedactPathForDisplay(exeFoundUtf8).c_str());
                    if (exeDetectOk) {
                        const char* bitness = exeDetect.is_64bit ? "64-bit" : "32-bit";
                        const char* api = cli_detect_exe::ReShadeDllFromDetect(exeDetect);
                        ImGui::SameLine();
                        ImGui::TextDisabled("  %s  %s", bitness, api);
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
                    ImGui::SameLine();
                    if (s_startedGamePid != 0) {
                        ImGui::TextDisabled("Running");
                        if (ImGui::IsItemHovered()) {
                            ULONGLONG elapsedMs = GetTickCount64() - s_startedGameTick;
                            unsigned long elapsedSec = (unsigned long)(elapsedMs / 1000);
                            unsigned long h = elapsedSec / 3600;
                            unsigned long m = (elapsedSec % 3600) / 60;
                            unsigned long s = elapsedSec % 60;
                            char runtimeBuf[32];
                            if (h > 0)
                                snprintf(runtimeBuf, sizeof(runtimeBuf), "%luh %lum %lus", h, m, s);
                            else if (m > 0)
                                snprintf(runtimeBuf, sizeof(runtimeBuf), "%lum %lus", m, s);
                            else
                                snprintf(runtimeBuf, sizeof(runtimeBuf), "%lus", s);
                            ImGui::SetTooltip("PID %lu, runtime %s", (unsigned long)s_startedGamePid, runtimeBuf);
                        }
                        ImGui::SameLine();
                        if (ImGui::Button("Stop game")) {
                            HANDLE h = OpenProcess(PROCESS_TERMINATE, FALSE, s_startedGamePid);
                            if (h != nullptr) {
                                TerminateProcess(h, 0);
                                CloseHandle(h);
                            }
                            s_startedGamePid = 0;
                            s_startedGameTick = 0;
                        }
                        if (ImGui::IsItemHovered()) {
                            ImGui::SetTooltip("Terminate the game process (PID %lu).", (unsigned long)s_startedGamePid);
                        }
                    } else {
                        if (ImGui::Button("Start game")) {
                            std::wstring workDir = std::filesystem::path(exeFoundLocal).parent_path().wstring();
                            const wchar_t* workDirPtr =
                                workDir.empty() ? addonDir.c_str() : workDir.c_str();
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
                        if (ImGui::IsItemHovered()) {
                            ImGui::SetTooltip("Launch the detected game executable.");
                        }
                    }
                }
                ShowReshadeCoreVersionsForDir(addonDir);
            }
            ImGui::Spacing();

            // ---- Central (Display Commander\Reshade) ----
            ImGui::Text("Central (Display Commander\\Reshade)");
            if (centralDirUtf8.empty()) {
                ImGui::TextDisabled("(LOCALAPPDATA not set)");
            } else {
                ImGui::TextWrapped("%s", "%%LOCALAPPDATA%%\\Programs\\Display Commander\\Reshade");
                ShowReshadeCoreVersionsForDir(centralReshadeDir);
            }
            ImGui::Spacing();

            ImGui::Separator();
            ImGui::Text("Known DLLs in this folder:");
            if (addonDir.empty()) {
                ImGui::TextUnformatted("(unknown path)");
            } else {
                std::vector<std::wstring> displayCommanderPresent;
                CollectDisplayCommanderAddonsInDir(addonDir, displayCommanderPresent);
                bool anyKnown = !reshadeDllsPresent.empty() || !displayCommanderPresent.empty();
                if (!anyKnown) {
                    ImGui::TextUnformatted("(none of the known ReShade or Display Commander DLLs)");
                } else {
                    // Classify by exports: ReShade only if ReShadeRegisterAddon; Display Commander if StartAndInject
                    auto showKnownDll = [&addonDir](const std::wstring& n) {
                        std::wstring dllPath = addonDir + L"\\" + n;
                        std::string ver = GetFileVersionStringUtf8(dllPath);
                        if (ver.empty()) ver = "(no version info)";
                        bool hasReShadeRegisterAddon = DllHasExport(dllPath, "ReShadeRegisterAddon");
                        bool hasStartAndInject = DllHasExport(dllPath, "StartAndInject");
                        const char* kind = hasReShadeRegisterAddon ? "ReShade"
                                             : hasStartAndInject ? "Display Commander"
                                             : "Other";
                        int need = WideCharToMultiByte(CP_UTF8, 0, n.c_str(), (int)n.size(), nullptr, 0, nullptr, nullptr);
                        if (need > 0) {
                            std::string nameUtf8(static_cast<size_t>(need), 0);
                            WideCharToMultiByte(CP_UTF8, 0, n.c_str(), (int)n.size(), &nameUtf8[0], need, nullptr,
                                                nullptr);
                            if (strcmp(kind, "Display Commander") == 0) {
                                ImGui::BulletText("Display Commander - %s: %s  %s", nameUtf8.c_str(), ver.c_str(),
                                                 hasStartAndInject ? "(StartAndInject)" : "(no StartAndInject)");
                            } else if (strcmp(kind, "ReShade") == 0) {
                                ImGui::BulletText("ReShade - %s: %s", nameUtf8.c_str(), ver.c_str());
                            } else {
                                ImGui::BulletText("Other - %s: %s (no ReShadeRegisterAddon, no StartAndInject)",
                                                 nameUtf8.c_str(), ver.c_str());
                            }
                        }
                    };
                    for (const auto& n : reshadeDllsPresent) showKnownDll(n);
                    for (const auto& n : displayCommanderPresent) showKnownDll(n);
                }
            }
            ImGui::Spacing();

            // Setup ReShade as appropriate DLL (dxgi.dll, d3d9.dll, opengl32.dll, etc.) from detected exe
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
            const char* detectedApi = exeDetectOk ? cli_detect_exe::ReShadeDllFromDetect(exeDetect) : "";
            bool apiSupported =
                (detectedApi && strcmp(detectedApi, "vulkan") != 0 && strcmp(detectedApi, "unknown") != 0);
            bool isDxgi = (detectedApi && strcmp(detectedApi, "dxgi") == 0);
            bool canSetup = !addonDir.empty() && exeDetectOk && apiSupported;

            auto doSetupAs = [&](const char* targetApi) {
                s_setupReshadeResult.clear();
                const wchar_t* sourceDll = exeDetect.is_64bit ? L"ReShade64.dll" : L"ReShade32.dll";
                std::wstring targetDll;
                for (const char* p = targetApi; p && *p; ++p) targetDll += (wchar_t)(unsigned char)*p;
                targetDll += L".dll";
                std::wstring sourcePath = addonDir + L"\\" + sourceDll;
                std::wstring targetPath = addonDir + L"\\" + targetDll;
                if (GetFileAttributesW(sourcePath.c_str()) == INVALID_FILE_ATTRIBUTES) {
                    int need = WideCharToMultiByte(CP_UTF8, 0, sourceDll, -1, nullptr, 0, nullptr, nullptr);
                    std::string srcUtf8(need > 0 ? (size_t)need : 0, 0);
                    if (need > 0) WideCharToMultiByte(CP_UTF8, 0, sourceDll, -1, &srcUtf8[0], need, nullptr, nullptr);
                    s_setupReshadeResult =
                        "Not installed correctly: " + srcUtf8 + " not found. Use the Update ReShade button first.";
                } else if (CopyFileW(sourcePath.c_str(), targetPath.c_str(), FALSE)) {
                    s_preferredSetupApi = targetApi;
                    int need = WideCharToMultiByte(CP_UTF8, 0, targetDll.c_str(), (int)targetDll.size(), nullptr, 0,
                                                   nullptr, nullptr);
                    std::string tgtUtf8(need > 0 ? (size_t)need : 0, 0);
                    if (need > 0)
                        WideCharToMultiByte(CP_UTF8, 0, targetDll.c_str(), (int)targetDll.size(), &tgtUtf8[0], need,
                                            nullptr, nullptr);
                    s_setupReshadeResult = "ReShade installed correctly as " + tgtUtf8 + " ("
                                           + (exeDetect.is_64bit ? "64-bit" : "32-bit") + ").";
                } else {
                    s_setupReshadeResult = "Not installed correctly: could not copy (access denied or disk error).";
                }
            };

            if (!canSetup) ImGui::BeginDisabled();
            if (isDxgi) {
                ImGui::Text("Setup ReShade as:");
                const char* dxgiOptions[] = {"dxgi.dll", "d3d11.dll", "d3d12.dll"};
                const char* dxgiApis[] = {"dxgi", "d3d11", "d3d12"};
                for (int i = 0; i < 3; ++i) {
                    if (i > 0) ImGui::SameLine();
                    if (ImGui::Button(dxgiOptions[i])) {
                        doSetupAs(dxgiApis[i]);
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("Copy ReShade to %s so the game loads ReShade.", dxgiOptions[i]);
                    }
                }
            } else {
                std::string setupLabel = "Setup ReShade as ";
                if (apiSupported)
                    setupLabel += std::string(detectedApi) + ".dll";
                else
                    setupLabel += "...";
                if (ImGui::Button(setupLabel.c_str())) {
                    doSetupAs(detectedApi);
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Copy ReShade to %s so the game loads ReShade (based on detected exe).",
                                     detectedApi);
                }
            }
            if (!canSetup) ImGui::EndDisabled();

            ImGui::Spacing();
            // ReShade version: hardcoded list (latest, 6.7.2 default, 6.7.1, 6.6.2)
            ImGui::Text("ReShade version");
            if (!s_reshadeVersions.empty()) {
                ImGui::SameLine();
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
                ImGui::SameLine();
                ImGui::TextDisabled("(default: 6.7.2)");
            }
            ImGui::Spacing();
            // Update ReShade (overrides local + central) - only when a version is selected from the list
            if (s_reshadeUpdateInProgress) {
                ImGui::TextDisabled("Updating ReShade...");
            } else if (!s_reshadeUpdateResult.empty()) {
                ImGui::TextWrapped("%s", s_reshadeUpdateResult.c_str());
            }
            std::string selectedVer =
                s_reshadeVersions.empty()
                    ? ""
                    : (s_reshadeVersionIndex < (int)s_reshadeVersions.size() ? s_reshadeVersions[(size_t)s_reshadeVersionIndex]
                                                                              : s_reshadeVersions[0]);
            bool canUpdate = !addonDir.empty() && !s_reshadeUpdateInProgress && !s_reshadeVersions.empty();
            if (!canUpdate) ImGui::BeginDisabled();
            std::string updateLabel = selectedVer.empty() ? "Update ReShade (select version from list above)"
                                                          : ("Update ReShade to " + selectedVer);
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
            if (canUpdate && ImGui::IsItemHovered()) {
                ImGui::SetTooltip(
                    "Download ReShade %s and overwrite ReShade64.dll / ReShade32.dll in this folder and in "
                    "%%LOCALAPPDATA%%.",
                    selectedVer.c_str());
            }

            ImGui::Spacing();
            if (ImGui::Button("Close")) done = true;
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
