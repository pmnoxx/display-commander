// Source Code <Display Commander>
//
// Group 1 — Source Code (Display Commander)
// (no includes)
//
// Group 2 — ReShade / ImGui
// (no includes)
//
// Group 3 — Standard C++
#include <cstdint>
//
// Group 4 — Windows.h
#include <Windows.h>
//
// Group 5 — Other Windows SDK
// Libraries <Windows>
#include <dbghelp.h>

namespace dbghelp_loader {

// ABI surface (stable for other modules):
// - Wrapper functions below (SymGetOptions, SymInitialize, StackWalk64, etc.) — use these in
//   callers; they perform nullptr checks internally and return a safe value (FALSE/0/nullptr) when
//   DbgHelp is not available. No need to check *_Original != nullptr at call sites.
// - *_Original pointers are for the hook layer (trampolines); other code should use the wrappers.
// - Loader helpers: LoadDbgHelp / UnloadDbgHelp / IsDbgHelpAvailable
// - Logging helpers: SetSuppressStackWalkLogging / GetSuppressStackWalkLogging
// - Symbol init helper: EnsureSymbolsInitialized
// This header will not change signatures or remove existing exports without a version bump.

// Function pointer types for dbghelp functions
using SymGetOptions_pfn = DWORD(WINAPI*)();
using SymSetOptions_pfn = DWORD(WINAPI*)(DWORD);
using SymInitialize_pfn = BOOL(WINAPI*)(HANDLE, PCSTR, BOOL);
using SymCleanup_pfn = BOOL(WINAPI*)(HANDLE);
using StackWalk64_pfn = BOOL(WINAPI*)(DWORD, HANDLE, HANDLE, LPSTACKFRAME64, PVOID, PREAD_PROCESS_MEMORY_ROUTINE64,
                                      PFUNCTION_TABLE_ACCESS_ROUTINE64, PGET_MODULE_BASE_ROUTINE64,
                                      PTRANSLATE_ADDRESS_ROUTINE64);
using SymFunctionTableAccess64_pfn = PVOID(WINAPI*)(HANDLE, DWORD64);
using SymGetModuleBase64_pfn = DWORD64(WINAPI*)(HANDLE, DWORD64);
using SymFromAddr_pfn = BOOL(WINAPI*)(HANDLE, DWORD64, PDWORD64, PSYMBOL_INFO);
using SymGetLineFromAddr64_pfn = BOOL(WINAPI*)(HANDLE, DWORD64, PDWORD, PIMAGEHLP_LINE64);
using SymGetModuleInfo64_pfn = BOOL(WINAPI*)(HANDLE, DWORD64, PIMAGEHLP_MODULE64);
using SymSetSearchPathW_pfn = BOOL(WINAPI*)(HANDLE, PCWSTR);
using SymGetSearchPathW_pfn = BOOL(WINAPI*)(HANDLE, PWSTR, DWORD);

// Logical function index; keep in sync with table in cpp
enum class DbgHelpFunction : std::uint8_t {
    SymGetOptions = 0,
    SymSetOptions,
    SymInitialize,
    SymCleanup,
    StackWalk64,
    SymFunctionTableAccess64,
    SymGetModuleBase64,
    SymFromAddr,
    SymGetLineFromAddr64,
    SymGetModuleInfo64,
    SymSetSearchPathW,
    SymGetSearchPathW,
    Count
};

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
extern SymSetSearchPathW_pfn SymSetSearchPathW_Original;
extern SymGetSearchPathW_pfn SymGetSearchPathW_Original;

// Wrapper ABI: same signatures as DbgHelp APIs; return FALSE/0/nullptr when not available.
DWORD SymGetOptions();
DWORD SymSetOptions(DWORD sym_options);
BOOL SymInitialize(HANDLE process, PCSTR user_search_path, BOOL invade_process);
BOOL SymCleanup(HANDLE process);
BOOL StackWalk64(DWORD machine_type, HANDLE process, HANDLE thread, LPSTACKFRAME64 stack_frame,
                 PVOID context_record, PREAD_PROCESS_MEMORY_ROUTINE64 read_memory_routine,
                 PFUNCTION_TABLE_ACCESS_ROUTINE64 function_table_access_routine,
                 PGET_MODULE_BASE_ROUTINE64 get_module_base_routine,
                 PTRANSLATE_ADDRESS_ROUTINE64 translate_address_routine);
PVOID SymFunctionTableAccess64(HANDLE process, DWORD64 addr_base);
DWORD64 SymGetModuleBase64(HANDLE process, DWORD64 address);
BOOL SymFromAddr(HANDLE process, DWORD64 address, PDWORD64 displacement, PSYMBOL_INFO symbol_info);
BOOL SymGetLineFromAddr64(HANDLE process, DWORD64 address, PDWORD displacement, PIMAGEHLP_LINE64 line);
BOOL SymGetModuleInfo64(HANDLE process, DWORD64 address, PIMAGEHLP_MODULE64 module_info);
BOOL SymSetSearchPathW(HANDLE process, PCWSTR search_path);
BOOL SymGetSearchPathW(HANDLE process, PWSTR search_path, DWORD search_path_len);

// Dynamic loading functions
bool LoadDbgHelp();
void UnloadDbgHelp();
bool IsDbgHelpAvailable();

// Best-effort preloading of symbols for all modules in the given process.
// Intended for crash/stack-trace paths: iterates loaded modules and calls
// SymLoadModule64 for each one using the private dbghelp instance so that
// subsequent SymFromAddr/SymGetLineFromAddr64 calls have symbols ready.
// Safe to call multiple times; later calls are no-ops after first success.
void PreloadSymbolsForAllModules(HANDLE process);

// When true, StackWalk64 hook will not log (used by our own stack trace generation)
void SetSuppressStackWalkLogging(bool suppress);
bool GetSuppressStackWalkLogging();

// Ensures SymInitialize has been called for the given process so that symbol resolution
// (SymGetModuleInfo64, SymFromAddr) works when logging stack frames. Idempotent.
void EnsureSymbolsInitialized(HANDLE process);

}  // namespace dbghelp_loader
