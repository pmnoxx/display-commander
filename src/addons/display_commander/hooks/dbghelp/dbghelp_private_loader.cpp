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
#include <atomic>
#include <array>
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
SymSetSearchPathW_pfn SymSetSearchPathW_Original = nullptr;
SymGetSearchPathW_pfn SymGetSearchPathW_Original = nullptr;

namespace {

struct DbgHelpFunctionEntry {
    const char* name;
    void**      target;
    bool        required;
};

std::atomic<bool> g_dbghelp_loaded{false};
std::atomic<bool> g_dbghelp_available{false};
HMODULE g_dbghelp_module = nullptr;

// clang-format off
const std::array<DbgHelpFunctionEntry, static_cast<size_t>(DbgHelpFunction::Count)> g_function_entries = {{
    { "SymGetOptions",             reinterpret_cast<void**>(&SymGetOptions_Original),             true  },
    { "SymSetOptions",             reinterpret_cast<void**>(&SymSetOptions_Original),             true  },
    { "SymInitialize",             reinterpret_cast<void**>(&SymInitialize_Original),             true  },
    { "SymCleanup",                reinterpret_cast<void**>(&SymCleanup_Original),                true  },
    { "StackWalk64",               reinterpret_cast<void**>(&StackWalk64_Original),               true  },
    { "SymFunctionTableAccess64",  reinterpret_cast<void**>(&SymFunctionTableAccess64_Original),  true  },
    { "SymGetModuleBase64",        reinterpret_cast<void**>(&SymGetModuleBase64_Original),        true  },
    { "SymFromAddr",               reinterpret_cast<void**>(&SymFromAddr_Original),               true  },
    { "SymGetLineFromAddr64",      reinterpret_cast<void**>(&SymGetLineFromAddr64_Original),      true  },
    { "SymGetModuleInfo64",        reinterpret_cast<void**>(&SymGetModuleInfo64_Original),        true  },
    { "SymSetSearchPathW",         reinterpret_cast<void**>(&SymSetSearchPathW_Original),         false },
    { "SymGetSearchPathW",         reinterpret_cast<void**>(&SymGetSearchPathW_Original),         false },
}};
// clang-format on

} // namespace

bool LoadDbgHelp() {
    if (g_dbghelp_loaded.load()) {
        return g_dbghelp_available.load();
    }

    // Prefer a private copy of dbghelp.dll under
    // %LocalAppData%\Programs\Display_Commander\dbghelp\dbghelp_dc.dll, copied once
    // from the system directory if not already present.
    std::filesystem::path private_dbghelp_path;
    {
        std::filesystem::path dc_base = GetDisplayCommanderAppDataFolder();
        if (!dc_base.empty()) {
            std::filesystem::path dbghelp_dir = dc_base / L"dbghelp";
            std::error_code ec;
            if (!std::filesystem::exists(dbghelp_dir, ec)) {
                if (!std::filesystem::create_directories(dbghelp_dir, ec)) {
                    LogWarn("DbgHelp: failed to create private folder '%s': %s",
                            dbghelp_dir.string().c_str(), ec.message().c_str());
                }
            }
            if (std::filesystem::exists(dbghelp_dir, ec) && std::filesystem::is_directory(dbghelp_dir, ec)) {
                const std::filesystem::path dest = dbghelp_dir / L"dbghelp_dc.dll";

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
        LogWarn("DbgHelp: no private dbghelp_dc.dll found/configured; DbgHelp will be unavailable");
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
    SymSetSearchPathW_Original = nullptr;
    SymGetSearchPathW_Original = nullptr;

    g_dbghelp_loaded.store(false);
    g_dbghelp_available.store(false);
}

bool IsDbgHelpAvailable() { return g_dbghelp_available.load(); }

static thread_local bool g_suppress_stack_walk_logging = false;

void SetSuppressStackWalkLogging(bool suppress) { g_suppress_stack_walk_logging = suppress; }

bool GetSuppressStackWalkLogging() { return g_suppress_stack_walk_logging; }

void EnsureSymbolsInitialized(HANDLE process) {
    if (!g_dbghelp_available.load() || process == nullptr) {
        return;
    }
    if (SymSetOptions_Original) {
        const DWORD opts = SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_INCLUDE_32BIT_MODULES | SYMOPT_LOAD_LINES;
        SymSetOptions_Original(opts);
    }
    if (SymInitialize_Original) {
        // Idempotent: returns TRUE and does nothing if already initialized for this process
        SymInitialize_Original(process, nullptr, TRUE);
    }
}

}  // namespace dbghelp_loader

