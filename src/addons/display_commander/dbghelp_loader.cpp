#include "dbghelp_loader.hpp"
#include "utils/logging.hpp"
#include <atomic>

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

// State tracking
static std::atomic<bool> g_dbghelp_loaded{false};
static std::atomic<bool> g_dbghelp_available{false};
static HMODULE g_dbghelp_module = nullptr;

bool LoadDbgHelp() {
    if (g_dbghelp_loaded.load()) {
        return g_dbghelp_available.load();
    }

    // Load dbghelp.dll dynamically
    g_dbghelp_module = LoadLibraryA("dbghelp.dll");
    if (!g_dbghelp_module) {
        LogInfo("DbgHelp not available - dbghelp.dll not found (this is normal on some systems)");
        g_dbghelp_loaded.store(true);
        g_dbghelp_available.store(false);
        return false;
    }

    // Get function addresses
    SymGetOptions_Original = (SymGetOptions_pfn)GetProcAddress(g_dbghelp_module, "SymGetOptions");
    SymSetOptions_Original = (SymSetOptions_pfn)GetProcAddress(g_dbghelp_module, "SymSetOptions");
    SymInitialize_Original = (SymInitialize_pfn)GetProcAddress(g_dbghelp_module, "SymInitialize");
    SymCleanup_Original = (SymCleanup_pfn)GetProcAddress(g_dbghelp_module, "SymCleanup");
    StackWalk64_Original = (StackWalk64_pfn)GetProcAddress(g_dbghelp_module, "StackWalk64");
    SymFunctionTableAccess64_Original = (SymFunctionTableAccess64_pfn)GetProcAddress(g_dbghelp_module, "SymFunctionTableAccess64");
    SymGetModuleBase64_Original = (SymGetModuleBase64_pfn)GetProcAddress(g_dbghelp_module, "SymGetModuleBase64");
    SymFromAddr_Original = (SymFromAddr_pfn)GetProcAddress(g_dbghelp_module, "SymFromAddr");
    SymGetLineFromAddr64_Original = (SymGetLineFromAddr64_pfn)GetProcAddress(g_dbghelp_module, "SymGetLineFromAddr64");
    SymGetModuleInfo64_Original = (SymGetModuleInfo64_pfn)GetProcAddress(g_dbghelp_module, "SymGetModuleInfo64");

    // Check if all required functions are available
    bool all_functions_available =
        SymGetOptions_Original &&
        SymSetOptions_Original &&
        SymInitialize_Original &&
        SymCleanup_Original &&
        StackWalk64_Original &&
        SymFunctionTableAccess64_Original &&
        SymGetModuleBase64_Original &&
        SymFromAddr_Original &&
        SymGetLineFromAddr64_Original &&
        SymGetModuleInfo64_Original;

    if (all_functions_available) {
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

    g_dbghelp_loaded.store(false);
    g_dbghelp_available.store(false);
}

bool IsDbgHelpAvailable() {
    return g_dbghelp_available.load();
}

static thread_local bool g_suppress_stack_walk_logging = false;

void SetSuppressStackWalkLogging(bool suppress) {
    g_suppress_stack_walk_logging = suppress;
}

bool GetSuppressStackWalkLogging() {
    return g_suppress_stack_walk_logging;
}

} // namespace dbghelp_loader
