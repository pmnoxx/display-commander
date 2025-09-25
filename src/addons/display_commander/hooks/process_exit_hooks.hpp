#pragma once

#include <windows.h>

namespace renodx::hooks {

// Function pointer types for process exit functions
using ExitProcess_pfn = void(WINAPI *)(UINT uExitCode);
using TerminateProcess_pfn = BOOL(WINAPI *)(HANDLE hProcess, UINT uExitCode);

// Process exit hook function pointers
extern ExitProcess_pfn ExitProcess_Original;
extern TerminateProcess_pfn TerminateProcess_Original;

// Hooked process exit functions
void WINAPI ExitProcess_Detour(UINT uExitCode);
BOOL WINAPI TerminateProcess_Detour(HANDLE hProcess, UINT uExitCode);

// Hook management
bool InstallProcessExitHooks();
void UninstallProcessExitHooks();
bool AreProcessExitHooksInstalled();

} // namespace renodx::hooks
