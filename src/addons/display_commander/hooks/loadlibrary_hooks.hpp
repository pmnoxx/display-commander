#pragma once

#include <windows.h>

#include <psapi.h>
#include <string>
#include <vector>


namespace display_commanderhooks {

// Function pointer types for LoadLibrary functions
using LoadLibraryA_pfn = HMODULE(WINAPI *)(LPCSTR);
using LoadLibraryW_pfn = HMODULE(WINAPI *)(LPCWSTR);
using LoadLibraryExA_pfn = HMODULE(WINAPI *)(LPCSTR, HANDLE, DWORD);
using LoadLibraryExW_pfn = HMODULE(WINAPI *)(LPCWSTR, HANDLE, DWORD);
using LoadPackagedLibrary_pfn = HMODULE(WINAPI *)(LPCWSTR, DWORD);
using FreeLibrary_pfn = BOOL(WINAPI *)(HMODULE);
using FreeLibraryAndExitThread_pfn = VOID(WINAPI *)(HMODULE, DWORD);

// Module information structure
struct ModuleInfo {
    HMODULE hModule;
    std::wstring moduleName;
    std::wstring fullPath;
    LPVOID baseAddress;
    DWORD sizeOfImage;
    LPVOID entryPoint;
    FILETIME loadTime;
    bool loadedBeforeHooks;  // True if module was loaded before Display Commander hooks were installed

    ModuleInfo() : hModule(nullptr), baseAddress(nullptr), sizeOfImage(0), entryPoint(nullptr), loadedBeforeHooks(false) {
        loadTime.dwHighDateTime = 0;
        loadTime.dwLowDateTime = 0;
    }
};

// LdrLoadDll (ntdll) - optional hook for loads that bypass kernel32. DllName is UNICODE_STRING (opaque here).
using LdrLoadDll_pfn = LONG(NTAPI*)(PWSTR DllPath, PULONG DllCharacteristics, const void* DllName, PVOID* DllHandle);
extern LdrLoadDll_pfn LdrLoadDll_Original;

// Original function pointers
extern LoadLibraryA_pfn LoadLibraryA_Original;
extern LoadLibraryW_pfn LoadLibraryW_Original;
extern LoadLibraryExA_pfn LoadLibraryExA_Original;
extern LoadLibraryExW_pfn LoadLibraryExW_Original;
extern LoadPackagedLibrary_pfn LoadPackagedLibrary_Original;
extern FreeLibrary_pfn FreeLibrary_Original;
extern FreeLibraryAndExitThread_pfn FreeLibraryAndExitThread_Original;

// Hooked LoadLibrary functions
HMODULE WINAPI LoadLibraryA_Detour(LPCSTR lpLibFileName);
HMODULE WINAPI LoadLibraryW_Detour(LPCWSTR lpLibFileName);
HMODULE WINAPI LoadLibraryExA_Detour(LPCSTR lpLibFileName, HANDLE hFile, DWORD dwFlags);
HMODULE WINAPI LoadLibraryExW_Detour(LPCWSTR lpLibFileName, HANDLE hFile, DWORD dwFlags);
HMODULE WINAPI LoadPackagedLibrary_Detour(LPCWSTR lpwszPackageFullName, DWORD Reserved);
LONG NTAPI LdrLoadDll_Detour(PWSTR DllPath, PULONG DllCharacteristics, const void* DllName, PVOID* DllHandle);
BOOL WINAPI FreeLibrary_Detour(HMODULE hLibModule);
VOID WINAPI FreeLibraryAndExitThread_Detour(HMODULE hLibModule, DWORD dwExitCode);

// Hook management
bool InstallLoadLibraryHooks();
void UninstallLoadLibraryHooks();

// Module enumeration and tracking
// modules_loaded_late_without_noticing: when true (e.g. from continuous monitoring), merge into existing list
// without clearing; newly discovered modules are marked loadedBeforeHooks=false and OnModuleLoaded is called.
bool EnumerateLoadedModules(bool modules_loaded_late_without_noticing = false);
std::vector<ModuleInfo> GetLoadedModules();
bool IsModuleLoaded(const std::wstring &moduleName);

// Call from exit handler: enumerate loaded modules and return hookable module names we never saw
std::vector<std::string> ReportMissedModulesOnExit();

// Module loading callback
void OnModuleLoaded(const std::wstring &moduleName, HMODULE hModule);

// DLL blocking functions
bool ShouldBlockDLL(const std::wstring& dll_path);
void LoadBlockedDLLsFromSettings(const std::string& blocked_dlls_str);
std::string SaveBlockedDLLsToSettings();
bool IsDLLBlocked(const std::wstring& module_name);
void SetDLLBlocked(const std::wstring& module_name, bool blocked);
bool CanBlockDLL(const ModuleInfo& module_info);
std::vector<std::wstring> GetBlockedDLLs();

// SRWLOCK diagnostics for stuck-detection (returns true if lock is currently held)
bool IsModuleSrwlockHeld();
bool IsBlockedDllsSrwlockHeld();

}  // namespace display_commanderhooks
