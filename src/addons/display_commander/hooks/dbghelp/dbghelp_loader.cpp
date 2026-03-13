// Source Code <Display Commander>

// Group 1 — Source Code (Display Commander)
#include "hooks/dbghelp/dbghelp_loader.hpp"
#include "utils/logging.hpp"

// Group 2 — ReShade / ImGui
// (no includes)

// Group 3 — Standard C++
#include <atomic>
#include <cstdint>
#include <array>

// Group 4 — Windows.h
// (no includes)

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

struct DbgHelpFunctionGroup {
    DbgHelpGroup group;
    const DbgHelpFunctionEntry* entries;
    uint32_t                    count;
};

std::atomic<bool> g_dbghelp_loaded{false};
std::atomic<bool> g_dbghelp_available{false};
HMODULE g_dbghelp_module = nullptr;

// clang-format off
const std::array<DbgHelpFunctionEntry, 7> g_core_symbol_entries = {{
    { "SymGetOptions",        reinterpret_cast<void**>(&SymGetOptions_Original),        true  },
    { "SymSetOptions",        reinterpret_cast<void**>(&SymSetOptions_Original),        true  },
    { "SymInitialize",        reinterpret_cast<void**>(&SymInitialize_Original),        true  },
    { "SymCleanup",           reinterpret_cast<void**>(&SymCleanup_Original),           true  },
    { "SymFromAddr",          reinterpret_cast<void**>(&SymFromAddr_Original),          true  },
    { "SymGetLineFromAddr64", reinterpret_cast<void**>(&SymGetLineFromAddr64_Original), true  },
    { "SymGetModuleInfo64",   reinterpret_cast<void**>(&SymGetModuleInfo64_Original),   true  },
}};

const std::array<DbgHelpFunctionEntry, 3> g_stack_walk_entries = {{
    { "StackWalk64",              reinterpret_cast<void**>(&StackWalk64_Original),              true  },
    { "SymFunctionTableAccess64", reinterpret_cast<void**>(&SymFunctionTableAccess64_Original), true  },
    { "SymGetModuleBase64",       reinterpret_cast<void**>(&SymGetModuleBase64_Original),       true  },
}};

const std::array<DbgHelpFunctionEntry, 2> g_search_path_entries = {{
    { "SymSetSearchPathW", reinterpret_cast<void**>(&SymSetSearchPathW_Original), false },
    { "SymGetSearchPathW", reinterpret_cast<void**>(&SymGetSearchPathW_Original), false },
}};

const std::array<DbgHelpFunctionGroup, 3> g_function_groups = {{
    { DbgHelpGroup::CoreSymbols,  g_core_symbol_entries.data(),
      static_cast<uint32_t>(g_core_symbol_entries.size()) },
    { DbgHelpGroup::StackWalking, g_stack_walk_entries.data(),
      static_cast<uint32_t>(g_stack_walk_entries.size()) },
    { DbgHelpGroup::SearchPath,   g_search_path_entries.data(),
      static_cast<uint32_t>(g_search_path_entries.size()) },
}};
// clang-format on

} // namespace

bool LoadDbgHelp() {
    if (g_dbghelp_loaded.load()) {
        return g_dbghelp_available.load();
    }

    // Load dbghelp.dll dynamically
    g_dbghelp_module = LoadLibraryA("dbghelp.dll");
    if (g_dbghelp_module == nullptr) {
        LogInfo("DbgHelp not available - dbghelp.dll not found (this is normal on some systems)");
        g_dbghelp_loaded.store(true);
        g_dbghelp_available.store(false);
        return false;
    }

    // Get function addresses. If DbgHelp hooks were already installed (game loaded dbghelp first),
    // the hook layer set _Original to trampolines; do not overwrite those.
    bool all_required_available = true;
    for (const DbgHelpFunctionGroup& group : g_function_groups) {
        for (uint32_t i = 0; i < group.count; ++i) {
            const DbgHelpFunctionEntry& entry = group.entries[i];
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
    if (!g_dbghelp_available.load() || !process) {
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
