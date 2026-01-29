#include "dpi_management.hpp"
#include "../utils/logging.hpp"

#include <shlwapi.h>
#include <winreg.h>
#include <string>

#pragma comment(lib, "shlwapi.lib")

// Windows SDK DPI awareness types
#ifndef PROCESS_DPI_AWARENESS
typedef enum PROCESS_DPI_AWARENESS {
    PROCESS_DPI_UNAWARE = 0,
    PROCESS_SYSTEM_DPI_AWARE = 1,
    PROCESS_PER_MONITOR_DPI_AWARE = 2
} PROCESS_DPI_AWARENESS;
#endif

namespace display_commander::display::dpi {

// Function pointer types for DPI awareness APIs
using SetProcessDpiAwareness_pfn = HRESULT(WINAPI*)(PROCESS_DPI_AWARENESS);
using SetThreadDpiAwarenessContext_pfn = DPI_AWARENESS_CONTEXT(WINAPI*)(DPI_AWARENESS_CONTEXT);

// Check if Windows 10 or greater
static bool IsWindows10OrGreater() {
    OSVERSIONINFOEXW osvi = {};
    osvi.dwOSVersionInfoSize = sizeof(osvi);
    DWORDLONG const dwlConditionMask =
        VerSetConditionMask(VerSetConditionMask(VerSetConditionMask(0, VER_MAJORVERSION, VER_GREATER_EQUAL),
                                                VER_MINORVERSION, VER_GREATER_EQUAL),
                            VER_SERVICEPACKMAJOR, VER_GREATER_EQUAL);

    osvi.dwMajorVersion = 10;
    osvi.dwMinorVersion = 0;
    osvi.wServicePackMajor = 0;

    return VerifyVersionInfoW(&osvi, VER_MAJORVERSION | VER_MINORVERSION | VER_SERVICEPACKMAJOR, dwlConditionMask)
           != FALSE;
}

// Check if Windows 8.1 or greater
static bool IsWindows8Point1OrGreater() {
    OSVERSIONINFOEXW osvi = {};
    osvi.dwOSVersionInfoSize = sizeof(osvi);
    DWORDLONG const dwlConditionMask =
        VerSetConditionMask(VerSetConditionMask(VerSetConditionMask(0, VER_MAJORVERSION, VER_GREATER_EQUAL),
                                                VER_MINORVERSION, VER_GREATER_EQUAL),
                            VER_SERVICEPACKMAJOR, VER_GREATER_EQUAL);

    osvi.dwMajorVersion = 6;
    osvi.dwMinorVersion = 3;
    osvi.wServicePackMajor = 0;

    return VerifyVersionInfoW(&osvi, VER_MAJORVERSION | VER_MINORVERSION | VER_SERVICEPACKMAJOR, dwlConditionMask)
           != FALSE;
}

// Set thread DPI awareness context (Windows 10+)
static void SetThreadDpiAwarenessContextLocal(DPI_AWARENESS_CONTEXT dpi_ctx) {
    if (!IsWindows10OrGreater()) {
        return;
    }

    static HMODULE user32_dll = GetModuleHandleW(L"user32.dll");
    if (user32_dll == nullptr) {
        return;
    }

    static auto SetThreadDpiAwarenessContextFn =
        reinterpret_cast<SetThreadDpiAwarenessContext_pfn>(GetProcAddress(user32_dll, "SetThreadDpiAwarenessContext"));

    if (SetThreadDpiAwarenessContextFn != nullptr) {
        SetThreadDpiAwarenessContextFn(dpi_ctx);
    }
}

bool IsDPIAwarenessUsingAppCompat() {
    bool bDPIAppCompat = false;

    DWORD dwProcessSize = MAX_PATH;
    wchar_t wszProcessName[MAX_PATH + 2] = {};

    HANDLE hProc = GetCurrentProcess();

    QueryFullProcessImageNameW(hProc, 0, wszProcessName, &dwProcessSize);

    const wchar_t* wszKey = LR"(Software\Microsoft\Windows NT\CurrentVersion\AppCompatFlags\Layers)";
    DWORD dwDisposition = 0x00;
    HKEY hKey = nullptr;

    const LSTATUS status =
        RegCreateKeyExW(HKEY_CURRENT_USER, wszKey, 0, nullptr, 0x0, KEY_READ, nullptr, &hKey, &dwDisposition);

    if (status == ERROR_SUCCESS && hKey != nullptr) {
        wchar_t wszKeyVal[2048] = {};
        DWORD len = sizeof(wszKeyVal) / 2;

        RegGetValueW(hKey, nullptr, wszProcessName, RRF_RT_REG_SZ, nullptr, wszKeyVal, &len);

        bDPIAppCompat = (StrStrIW(wszKeyVal, L"HIGHDPIAWARE") != nullptr);

        RegCloseKey(hKey);
    }

    return bDPIAppCompat;
}

void ForceDPIAwarenessUsingAppCompat(bool set) {
    DWORD dwProcessSize = MAX_PATH;
    wchar_t wszProcessName[MAX_PATH + 2] = {};

    HANDLE hProc = GetCurrentProcess();

    QueryFullProcessImageNameW(hProc, 0, wszProcessName, &dwProcessSize);

    const wchar_t* wszKey = LR"(Software\Microsoft\Windows NT\CurrentVersion\AppCompatFlags\Layers)";
    DWORD dwDisposition = 0x00;
    HKEY hKey = nullptr;

    const LSTATUS status = RegCreateKeyExW(HKEY_CURRENT_USER, wszKey, 0, nullptr, 0x0, KEY_READ | KEY_WRITE, nullptr,
                                           &hKey, &dwDisposition);

    if (status == ERROR_SUCCESS && hKey != nullptr) {
        wchar_t wszOrigKeyVal[2048] = {};
        DWORD len = sizeof(wszOrigKeyVal) / 2;

        RegGetValueW(hKey, nullptr, wszProcessName, RRF_RT_REG_SZ, nullptr, wszOrigKeyVal, &len);

        wchar_t* pwszHIGHDPIAWARE = StrStrIW(wszOrigKeyVal, L"HIGHDPIAWARE");
        wchar_t* pwszNextToken = pwszHIGHDPIAWARE ? (pwszHIGHDPIAWARE + 13) : nullptr;

        if ((!set) && pwszHIGHDPIAWARE != nullptr) {
            *pwszHIGHDPIAWARE = L'\0';
            if (pwszNextToken && *pwszNextToken == L' ') {
                pwszNextToken++;
            }

            std::wstring combined;
            if (wszOrigKeyVal[0] != L'\0') {
                combined = wszOrigKeyVal;
            }
            if (pwszNextToken && *pwszNextToken != L'\0') {
                if (!combined.empty()) {
                    combined += L" ";
                }
                combined += pwszNextToken;
            }

            wcsncpy_s(wszOrigKeyVal, len, combined.c_str(), _TRUNCATE);

            StrTrimW(wszOrigKeyVal, L" ");

            if (wszOrigKeyVal[0] != L'\0') {
                RegSetValueExW(hKey, wszProcessName, 0, REG_SZ, reinterpret_cast<BYTE*>(wszOrigKeyVal),
                               static_cast<DWORD>((wcslen(wszOrigKeyVal) + 1) * sizeof(wchar_t)));
            } else {
                RegDeleteValueW(hKey, wszProcessName);
                RegCloseKey(hKey);
                return;
            }
        } else if (set && pwszHIGHDPIAWARE == nullptr) {
            StrCatW(wszOrigKeyVal, L" HIGHDPIAWARE");
            StrTrimW(wszOrigKeyVal, L" ");

            RegSetValueExW(hKey, wszProcessName, 0, REG_SZ, reinterpret_cast<BYTE*>(wszOrigKeyVal),
                           static_cast<DWORD>((wcslen(wszOrigKeyVal) + 1) * sizeof(wchar_t)));
        }

        RegFlushKey(hKey);
        RegCloseKey(hKey);
    }
}

void SetMonitorDPIAwareness(bool bOnlyIfWin10) {
    if (IsWindows10OrGreater()) {
        SetThreadDpiAwarenessContextLocal(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        return;
    }

    if (bOnlyIfWin10) {
        return;
    }

    if (IsWindows8Point1OrGreater()) {
        HMODULE shcore_dll = LoadLibraryW(L"shcore.dll");
        if (shcore_dll != nullptr) {
            auto SetProcessDpiAwarenessFn =
                reinterpret_cast<SetProcessDpiAwareness_pfn>(GetProcAddress(shcore_dll, "SetProcessDpiAwareness"));

            if (SetProcessDpiAwarenessFn != nullptr) {
                SetProcessDpiAwarenessFn(PROCESS_PER_MONITOR_DPI_AWARE);
                FreeLibrary(shcore_dll);
                return;
            }
            FreeLibrary(shcore_dll);
        }
    }

    SetProcessDPIAware();
}

void DisableDPIScaling() {
    if (IsProcessDPIAware()) {
        // Already DPI-aware, nothing to do
        return;
    }

    bool bWasAppCompatAware = IsDPIAwarenessUsingAppCompat();

    // Persistently disable DPI scaling problems so that initialization order doesn't matter
    ForceDPIAwarenessUsingAppCompat(true);
    SetMonitorDPIAwareness(false);

    if ((!bWasAppCompatAware) && IsDPIAwarenessUsingAppCompat()) {
        LogInfo("DPI awareness set via AppCompat. A game restart may be required for full effect.");
    }

    if (IsWindows10OrGreater()) {
        SetThreadDpiAwarenessContextLocal(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    }

    if (IsWindows8Point1OrGreater()) {
        HMODULE shcore_dll = LoadLibraryW(L"shcore.dll");
        if (shcore_dll != nullptr) {
            auto SetProcessDpiAwarenessFn =
                reinterpret_cast<SetProcessDpiAwareness_pfn>(GetProcAddress(shcore_dll, "SetProcessDpiAwareness"));

            if (SetProcessDpiAwarenessFn != nullptr) {
                SetProcessDpiAwarenessFn(PROCESS_PER_MONITOR_DPI_AWARE);
                FreeLibrary(shcore_dll);
                return;
            }
            FreeLibrary(shcore_dll);
        }
    }

    SetProcessDPIAware();
}

}  // namespace display_commander::display::dpi
