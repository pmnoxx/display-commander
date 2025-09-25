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

// Module information structure
struct ModuleInfo {
    HMODULE hModule;
    std::wstring moduleName;
    std::wstring fullPath;
    LPVOID baseAddress;
    DWORD sizeOfImage;
    LPVOID entryPoint;
    FILETIME loadTime;

    ModuleInfo() : hModule(nullptr), baseAddress(nullptr), sizeOfImage(0), entryPoint(nullptr) {
        loadTime.dwHighDateTime = 0;
        loadTime.dwLowDateTime = 0;
    }
};

// Original function pointers
extern LoadLibraryA_pfn LoadLibraryA_Original;
extern LoadLibraryW_pfn LoadLibraryW_Original;
extern LoadLibraryExA_pfn LoadLibraryExA_Original;
extern LoadLibraryExW_pfn LoadLibraryExW_Original;

// Hooked LoadLibrary functions
HMODULE WINAPI LoadLibraryA_Detour(LPCSTR lpLibFileName);
HMODULE WINAPI LoadLibraryW_Detour(LPCWSTR lpLibFileName);
HMODULE WINAPI LoadLibraryExA_Detour(LPCSTR lpLibFileName, HANDLE hFile, DWORD dwFlags);
HMODULE WINAPI LoadLibraryExW_Detour(LPCWSTR lpLibFileName, HANDLE hFile, DWORD dwFlags);

// Hook management
bool InstallLoadLibraryHooks();
void UninstallLoadLibraryHooks();
bool AreLoadLibraryHooksInstalled();

// Module enumeration and tracking
bool EnumerateLoadedModules();
std::vector<ModuleInfo> GetLoadedModules();
bool IsModuleLoaded(const std::wstring &moduleName);
void RefreshModuleList();

// Module loading callback
void OnModuleLoaded(const std::wstring &moduleName, HMODULE hModule);

} // namespace display_commanderhooks
