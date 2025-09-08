#pragma once

#include <windows.h>

namespace renodx::hooks {

// Function pointer types for LoadLibrary functions
using LoadLibraryA_pfn = HMODULE(WINAPI*)(LPCSTR);
using LoadLibraryW_pfn = HMODULE(WINAPI*)(LPCWSTR);
using LoadLibraryExA_pfn = HMODULE(WINAPI*)(LPCSTR, HANDLE, DWORD);
using LoadLibraryExW_pfn = HMODULE(WINAPI*)(LPCWSTR, HANDLE, DWORD);

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

} // namespace renodx::hooks
