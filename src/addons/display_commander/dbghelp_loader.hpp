#pragma once

#include <windows.h>
#include <dbghelp.h>

namespace dbghelp_loader {

// Function pointer types for dbghelp functions
using SymGetOptions_pfn = DWORD(WINAPI *)();
using SymSetOptions_pfn = DWORD(WINAPI *)(DWORD);
using SymInitialize_pfn = BOOL(WINAPI *)(HANDLE, PCSTR, BOOL);
using SymCleanup_pfn = BOOL(WINAPI *)(HANDLE);
using StackWalk64_pfn = BOOL(WINAPI *)(DWORD, HANDLE, HANDLE, LPSTACKFRAME64, PVOID, PREAD_PROCESS_MEMORY_ROUTINE64, PFUNCTION_TABLE_ACCESS_ROUTINE64, PGET_MODULE_BASE_ROUTINE64, PTRANSLATE_ADDRESS_ROUTINE64);
using SymFunctionTableAccess64_pfn = PVOID(WINAPI *)(HANDLE, DWORD64);
using SymGetModuleBase64_pfn = DWORD64(WINAPI *)(HANDLE, DWORD64);
using SymFromAddr_pfn = BOOL(WINAPI *)(HANDLE, DWORD64, PDWORD64, PSYMBOL_INFO);
using SymGetLineFromAddr64_pfn = BOOL(WINAPI *)(HANDLE, DWORD64, PDWORD, PIMAGEHLP_LINE64);
using SymGetModuleInfo64_pfn = BOOL(WINAPI *)(HANDLE, DWORD64, PIMAGEHLP_MODULE64);

// Function pointers
extern SymGetOptions_pfn SymGetOptions_Original;
extern SymSetOptions_pfn SymSetOptions_Original;
extern SymInitialize_pfn SymInitialize_Original;
extern SymCleanup_pfn SymCleanup_Original;
extern StackWalk64_pfn StackWalk64_Original;
extern SymFunctionTableAccess64_pfn SymFunctionTableAccess64_Original;
extern SymGetModuleBase64_pfn SymGetModuleBase64_Original;
extern SymFromAddr_pfn SymFromAddr_Original;
extern SymGetLineFromAddr64_pfn SymGetLineFromAddr64_Original;
extern SymGetModuleInfo64_pfn SymGetModuleInfo64_Original;

// Dynamic loading functions
bool LoadDbgHelp();
void UnloadDbgHelp();
bool IsDbgHelpAvailable();

// When true, StackWalk64 hook will not log (used by our own stack trace generation)
void SetSuppressStackWalkLogging(bool suppress);
bool GetSuppressStackWalkLogging();

// Ensures SymInitialize has been called for the given process so that symbol resolution
// (SymGetModuleInfo64, SymFromAddr) works when logging stack frames. Idempotent.
void EnsureSymbolsInitialized(HANDLE process);

} // namespace dbghelp_loader
