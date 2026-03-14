// Source Code <Display Commander>
//
// Group 1 — Source Code (Display Commander)
#include "hooks/dbghelp/dbghelp_private_loader.hpp"
#include "utils/general_utils.hpp"
#include "utils/logging.hpp"
//
// Group 2 — ReShade / ImGui
// (no includes)
//
// Group 3 — Standard C++
#include <array>
#include <atomic>
#include <filesystem>

//
// Group 4 — Windows.h
#include <Windows.h>
//
// Group 5 — Other Windows SDK
// Libraries <Windows>
// (no includes)

namespace dbghelp_loader {

// Function pointers
SymGetOptions_pfn SymGetOptions_Original = nullptr;
SymSetOptions_pfn SymSetOptions_Original = nullptr;
SymInitialize_pfn SymInitialize_Original = nullptr;
SymCleanup_pfn SymCleanup_Original = nullptr;
StackWalk64_pfn StackWalk64_Original = nullptr;
SymFunctionTableAccess64_pfn SymFunctionTableAccess64_Original = nullptr;
SymGetModuleBase64_pfn SymGetModuleBase64_Original = nullptr;
SymFromAddr_pfn SymFromAddr_Original = nullptr;
SymGetLineFromAddr64_pfn SymGetLineFromAddr64_Original = nullptr;
SymGetModuleInfo64_pfn SymGetModuleInfo64_Original = nullptr;
SymLoadModule64_pfn SymLoadModule64_Original = nullptr;
SymSetSearchPathW_pfn SymSetSearchPathW_Original = nullptr;
SymGetSearchPathW_pfn SymGetSearchPathW_Original = nullptr;

namespace {

struct DbgHelpFunctionEntry {
    const char* name;
    void** target;
    bool required;
};

std::atomic<bool> g_dbghelp_loaded{false};
std::atomic<bool> g_dbghelp_available{false};
HMODULE g_dbghelp_module = nullptr;

#if defined(_WIN64)
constexpr const wchar_t* kPrivateDbgHelpFilenameW = L"dbghelp_dc64.dll";
constexpr const char*    kPrivateDbgHelpFilename  = "dbghelp_dc64.dll";
#else
constexpr const wchar_t* kPrivateDbgHelpFilenameW = L"dbghelp_dc32.dll";
constexpr const char*    kPrivateDbgHelpFilename  = "dbghelp_dc32.dll";
#endif

// clang-format off
const std::array<DbgHelpFunctionEntry, static_cast<size_t>(DbgHelpFunction::Count)> g_function_entries = {{
    {
        .name     = "SymGetOptions",
        .target   = reinterpret_cast<void**>(&SymGetOptions_Original),
        .required = true,
    },
    {
        .name     = "SymSetOptions",
        .target   = reinterpret_cast<void**>(&SymSetOptions_Original),
        .required = true,
    },
    {
        .name     = "SymInitialize",
        .target   = reinterpret_cast<void**>(&SymInitialize_Original),
        .required = true,
    },
    {
        .name     = "SymCleanup",
        .target   = reinterpret_cast<void**>(&SymCleanup_Original),
        .required = true,
    },
    {
        .name     = "StackWalk64",
        .target   = reinterpret_cast<void**>(&StackWalk64_Original),
        .required = true,
    },
    {
        .name     = "SymFunctionTableAccess64",
        .target   = reinterpret_cast<void**>(&SymFunctionTableAccess64_Original),
        .required = true,
    },
    {
        .name     = "SymGetModuleBase64",
        .target   = reinterpret_cast<void**>(&SymGetModuleBase64_Original),
        .required = true,
    },
    {
        .name     = "SymFromAddr",
        .target   = reinterpret_cast<void**>(&SymFromAddr_Original),
        .required = true,
    },
    {
        .name     = "SymGetLineFromAddr64",
        .target   = reinterpret_cast<void**>(&SymGetLineFromAddr64_Original),
        .required = true,
    },
    {
        .name     = "SymGetModuleInfo64",
        .target   = reinterpret_cast<void**>(&SymGetModuleInfo64_Original),
        .required = true,
    },
    {
        .name     = "SymLoadModule64",
        .target   = reinterpret_cast<void**>(&SymLoadModule64_Original),
        .required = false,
    },
    {
        .name     = "SymSetSearchPathW",
        .target   = reinterpret_cast<void**>(&SymSetSearchPathW_Original),
        .required = false,
    },
    {
        .name     = "SymGetSearchPathW",
        .target   = reinterpret_cast<void**>(&SymGetSearchPathW_Original),
        .required = false,
    },
}};
// clang-format on

}  // namespace

bool LoadDbgHelp() {
    if (g_dbghelp_loaded.load()) {
        return g_dbghelp_available.load();
    }

    // Prefer a private copy of dbghelp.dll under
    // %LocalAppData%\Programs\Display_Commander\dbghelp\dbghelp_dc64.dll (or dbghelp_dc32.dll),
    // copied once from the system directory if not already present.
    std::filesystem::path private_dbghelp_path;
    {
        std::filesystem::path dc_base = GetDisplayCommanderAppDataFolder();
        if (!dc_base.empty()) {
            std::filesystem::path dbghelp_dir = dc_base / L"dbghelp";
            std::error_code ec;
            if (!std::filesystem::exists(dbghelp_dir, ec)) {
                if (!std::filesystem::create_directories(dbghelp_dir, ec)) {
                    LogWarn("DbgHelp: failed to create private folder '%s': %s", dbghelp_dir.string().c_str(),
                            ec.message().c_str());
                }
            }
            if (std::filesystem::exists(dbghelp_dir, ec) && std::filesystem::is_directory(dbghelp_dir, ec)) {
                const std::filesystem::path dest = dbghelp_dir / kPrivateDbgHelpFilenameW;

                if (!std::filesystem::exists(dest, ec)) {
                    // Resolve the system directory for the source dbghelp.dll (bitness-appropriate).
                    wchar_t system_dir[MAX_PATH] = {};
                    UINT len = GetSystemDirectoryW(system_dir, MAX_PATH);
                    if (len != 0 && len < MAX_PATH) {
                        const std::filesystem::path src =
                            std::filesystem::path(system_dir) / std::filesystem::path(L"dbghelp.dll");
                        if (std::filesystem::exists(src, ec) && std::filesystem::is_regular_file(src, ec)) {
                            if (std::filesystem::copy_file(src, dest, std::filesystem::copy_options::none, ec)) {
                                LogInfo("DbgHelp: copied private dbghelp.dll from system32 '%s' to '%s'",
                                        src.string().c_str(), dest.string().c_str());
                            } else {
                                LogWarn("DbgHelp: failed to copy private dbghelp.dll from system32 '%s' to '%s': %s",
                                        src.string().c_str(), dest.string().c_str(), ec.message().c_str());
                            }
                        } else {
                            LogWarn("DbgHelp: system dbghelp.dll not found at '%s'; private copy will not be available",
                                    src.string().c_str());
                        }
                    } else {
                        LogWarn("DbgHelp: GetSystemDirectoryW failed; private dbghelp.dll will not be available");
                    }
                }

                if (std::filesystem::exists(dest, ec) && std::filesystem::is_regular_file(dest, ec)) {
                    private_dbghelp_path = dest;
                }
            }
        }
    }

    if (!private_dbghelp_path.empty()) {
        g_dbghelp_module = LoadLibraryW(private_dbghelp_path.c_str());
        if (g_dbghelp_module != nullptr) {
            LogInfo("DbgHelp loaded from private copy: '%s'", private_dbghelp_path.string().c_str());
        } else {
            LogWarn("DbgHelp: failed to load private copy '%s'; DbgHelp will be unavailable",
                    private_dbghelp_path.string().c_str());
        }
    } else {
        LogWarn("DbgHelp: no private %s found/configured; DbgHelp will be unavailable", kPrivateDbgHelpFilename);
    }
    if (g_dbghelp_module == nullptr) {
        LogInfo("DbgHelp not available - dbghelp.dll not found (this is normal on some systems)");
        g_dbghelp_loaded.store(true);
        g_dbghelp_available.store(false);
        return false;
    }

    // Get function addresses. If DbgHelp hooks were already installed (game loaded dbghelp first),
    // the hook layer set _Original to trampolines; do not overwrite those.
    bool all_required_available = true;
    for (const DbgHelpFunctionEntry& entry : g_function_entries) {
        if (entry.target == nullptr) {
            continue;
        }

        if (*entry.target != nullptr) {
            continue;
        }

        void* address = reinterpret_cast<void*>(GetProcAddress(g_dbghelp_module, entry.name));
        if (address != nullptr) {
            *entry.target = address;
        } else if (entry.required) {
            all_required_available = false;
        }
    }

    if (all_required_available) {
        g_dbghelp_available.store(true);
        LogInfo("DbgHelp loaded successfully - all required functions available");
    } else {
        LogWarn("DbgHelp loaded but some required functions are missing");
        g_dbghelp_available.store(false);
        UnloadDbgHelp();
    }

    g_dbghelp_loaded.store(true);
    return g_dbghelp_available.load();
}

void PreloadSymbolsForAllModules(HANDLE process) {
    if (!g_dbghelp_available.load() || process == nullptr || g_dbghelp_module == nullptr) {
        return;
    }

    // Only do this work once per process; subsequent calls are no-ops.
    static std::atomic<bool> s_preloaded{false};
    bool expected = false;
    if (!s_preloaded.compare_exchange_strong(expected, true)) {
        return;
    }

    using EnumProcessModules_pfn = BOOL(WINAPI*)(HANDLE, HMODULE*, DWORD, LPDWORD);
    HMODULE psapi_module = GetModuleHandleW(L"psapi.dll");
    if (!psapi_module) {
        psapi_module = LoadLibraryW(L"psapi.dll");
    }
    if (!psapi_module) {
        return;
    }

    auto enum_process_modules = reinterpret_cast<EnumProcessModules_pfn>(GetProcAddress(psapi_module, "EnumProcessModules"));
    if (!enum_process_modules) {
        return;
    }

    if (!SymLoadModule64_Original) {
        return;
    }

    HMODULE modules[512] = {};
    DWORD bytes_needed = 0;
    if (!enum_process_modules(process, modules, static_cast<DWORD>(sizeof(modules)), &bytes_needed) || bytes_needed == 0) {
        return;
    }

    const size_t module_count = std::min<size_t>(bytes_needed / sizeof(HMODULE), _countof(modules));
    for (size_t i = 0; i < module_count; ++i) {
        HMODULE mod = modules[i];
        if (!mod) {
            continue;
        }

        char module_path[MAX_PATH] = {};
        if (GetModuleFileNameA(mod, module_path, MAX_PATH) == 0) {
            continue;
        }

        const DWORD64 base = reinterpret_cast<DWORD64>(mod);
        // SizeOfImage is optional for SymLoadModule64; passing 0 lets DbgHelp query it.
        SymLoadModule64(process, nullptr, module_path, nullptr, base, 0);
    }
}

void UnloadDbgHelp() {
    if (g_dbghelp_module) {
        FreeLibrary(g_dbghelp_module);
        g_dbghelp_module = nullptr;
    }

    // Reset function pointers
    SymGetOptions_Original = nullptr;
    SymSetOptions_Original = nullptr;
    SymInitialize_Original = nullptr;
    SymCleanup_Original = nullptr;
    StackWalk64_Original = nullptr;
    SymFunctionTableAccess64_Original = nullptr;
    SymGetModuleBase64_Original = nullptr;
    SymFromAddr_Original = nullptr;
    SymGetLineFromAddr64_Original = nullptr;
    SymGetModuleInfo64_Original = nullptr;
    SymLoadModule64_Original = nullptr;
    SymSetSearchPathW_Original = nullptr;
    SymGetSearchPathW_Original = nullptr;

    g_dbghelp_loaded.store(false);
    g_dbghelp_available.store(false);
}

bool IsDbgHelpAvailable() { return g_dbghelp_available.load(); }

// ABI wrappers: safe to call from other modules; no nullptr checks required at call sites.
DWORD SymGetOptions() {
    return SymGetOptions_Original ? SymGetOptions_Original() : 0;
}
DWORD SymSetOptions(DWORD sym_options) {
    return SymSetOptions_Original ? SymSetOptions_Original(sym_options) : 0;
}
BOOL SymInitialize(HANDLE process, PCSTR user_search_path, BOOL invade_process) {
    return SymInitialize_Original ? SymInitialize_Original(process, user_search_path, invade_process) : FALSE;
}
BOOL SymCleanup(HANDLE process) {
    return SymCleanup_Original ? SymCleanup_Original(process) : FALSE;
}
BOOL StackWalk64(DWORD machine_type, HANDLE process, HANDLE thread, LPSTACKFRAME64 stack_frame,
                 PVOID context_record, PREAD_PROCESS_MEMORY_ROUTINE64 read_memory_routine,
                 PFUNCTION_TABLE_ACCESS_ROUTINE64 function_table_access_routine,
                 PGET_MODULE_BASE_ROUTINE64 get_module_base_routine,
                 PTRANSLATE_ADDRESS_ROUTINE64 translate_address_routine) {
    return StackWalk64_Original
               ? StackWalk64_Original(machine_type, process, thread, stack_frame, context_record,
                                     read_memory_routine, function_table_access_routine, get_module_base_routine,
                                     translate_address_routine)
               : FALSE;
}
PVOID WINAPI SymFunctionTableAccess64(HANDLE process, DWORD64 addr_base) {
    return SymFunctionTableAccess64_Original ? SymFunctionTableAccess64_Original(process, addr_base) : nullptr;
}
DWORD64 WINAPI SymGetModuleBase64(HANDLE process, DWORD64 address) {
    return SymGetModuleBase64_Original ? SymGetModuleBase64_Original(process, address) : 0;
}
BOOL SymFromAddr(HANDLE process, DWORD64 address, PDWORD64 displacement, PSYMBOL_INFO symbol_info) {
    return SymFromAddr_Original ? SymFromAddr_Original(process, address, displacement, symbol_info) : FALSE;
}
BOOL SymGetLineFromAddr64(HANDLE process, DWORD64 address, PDWORD displacement, PIMAGEHLP_LINE64 line) {
    return SymGetLineFromAddr64_Original ? SymGetLineFromAddr64_Original(process, address, displacement, line)
                                         : FALSE;
}
BOOL SymGetModuleInfo64(HANDLE process, DWORD64 address, PIMAGEHLP_MODULE64 module_info) {
    return SymGetModuleInfo64_Original ? SymGetModuleInfo64_Original(process, address, module_info) : FALSE;
}
DWORD64 SymLoadModule64(HANDLE process, HANDLE file, PCSTR image_name, PCSTR module_name, DWORD64 base_of_dll,
                        DWORD dll_size) {
    return SymLoadModule64_Original ? SymLoadModule64_Original(process, file, image_name, module_name, base_of_dll,
                                                               dll_size)
                                   : 0;
}
BOOL SymSetSearchPathW(HANDLE process, PCWSTR search_path) {
    return SymSetSearchPathW_Original ? SymSetSearchPathW_Original(process, search_path) : FALSE;
}
BOOL SymGetSearchPathW(HANDLE process, PWSTR search_path, DWORD search_path_len) {
    return SymGetSearchPathW_Original ? SymGetSearchPathW_Original(process, search_path, search_path_len) : FALSE;
}

static thread_local bool g_suppress_stack_walk_logging = false;

void SetSuppressStackWalkLogging(bool suppress) { g_suppress_stack_walk_logging = suppress; }

bool GetSuppressStackWalkLogging() { return g_suppress_stack_walk_logging; }

void EnsureSymbolsInitialized(HANDLE process) {
    if (!g_dbghelp_available.load() || process == nullptr) {
        return;
    }
    const DWORD opts = SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_INCLUDE_32BIT_MODULES | SYMOPT_LOAD_LINES |
                       SYMOPT_NO_PROMPTS | SYMOPT_OMAP_FIND_NEAREST | SYMOPT_FAVOR_COMPRESSED |
                       SYMOPT_FAIL_CRITICAL_ERRORS | SYMOPT_NO_UNQUALIFIED_LOADS | SYMOPT_LOAD_ANYTHING;
    SymSetOptions(opts);
    // Idempotent: returns TRUE and does nothing if already initialized for this process
    SymInitialize(process, nullptr, TRUE);
}

}  // namespace dbghelp_loader
