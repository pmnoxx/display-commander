#include "stack_trace.hpp"
#include <dbghelp.h>
#include <windows.h>
#include <iomanip>
#include <sstream>
#include "utils/dbghelp_loader.hpp"


namespace stack_trace {

namespace {
// Helper function to get module name from address
std::string GetModuleName(HANDLE process, DWORD64 address) {
    IMAGEHLP_MODULE64 module_info = {};
    module_info.SizeOfStruct = sizeof(IMAGEHLP_MODULE64);

    if (dbghelp_loader::SymGetModuleInfo64_Original
        && dbghelp_loader::SymGetModuleInfo64_Original(process, address, &module_info) != FALSE) {
        return module_info.ModuleName;
    }
    return "Unknown";
}

// Helper function to get symbol name from address
std::string GetSymbolName(HANDLE process, DWORD64 address) {
    constexpr size_t SYMBOL_BUFFER_SIZE = 1024;
    char symbol_buffer[sizeof(SYMBOL_INFO) + SYMBOL_BUFFER_SIZE] = {};
    PSYMBOL_INFO symbol_info = reinterpret_cast<PSYMBOL_INFO>(symbol_buffer);
    symbol_info->SizeOfStruct = sizeof(SYMBOL_INFO);
    symbol_info->MaxNameLen = SYMBOL_BUFFER_SIZE;

    DWORD64 displacement = 0;
    if (dbghelp_loader::SymFromAddr_Original
        && dbghelp_loader::SymFromAddr_Original(process, address, &displacement, symbol_info) != FALSE) {
        return symbol_info->Name;
    }
    return "Unknown";
}

// Helper function to get source file and line number
std::string GetSourceInfo(HANDLE process, DWORD64 address) {
    IMAGEHLP_LINE64 line_info = {};
    line_info.SizeOfStruct = sizeof(IMAGEHLP_LINE64);

    DWORD displacement = 0;
    if (dbghelp_loader::SymGetLineFromAddr64_Original
        && dbghelp_loader::SymGetLineFromAddr64_Original(process, address, &displacement, &line_info) != FALSE) {
        std::ostringstream oss;
        oss << line_info.FileName << ":" << line_info.LineNumber;
        return oss.str();
    }
    return "Unknown";
}

// Memory read routine for StackWalk64
BOOL CALLBACK ReadProcessMemoryRoutine64(HANDLE h_process, DWORD64 lp_base_address, PVOID lp_buffer, DWORD n_size,
                                         LPDWORD lp_number_of_bytes_read) {
    SIZE_T bytes_read = 0;
    if (ReadProcessMemory(h_process, reinterpret_cast<LPCVOID>(lp_base_address), lp_buffer, n_size, &bytes_read)) {
        if (lp_number_of_bytes_read) {
            *lp_number_of_bytes_read = static_cast<DWORD>(bytes_read);
        }
        return TRUE;
    }
    return FALSE;
}
}  // namespace

namespace {
// Internal helper function that does the actual stack trace generation
std::vector<std::string> GenerateStackTraceInternal(CONTEXT* context_ptr) {
    std::vector<std::string> stack_trace;

    // Try to load DbgHelp on demand if not yet available (e.g. game never loaded dbghelp.dll)
    if (!dbghelp_loader::IsDbgHelpAvailable()) {
        dbghelp_loader::LoadDbgHelp();
    }
    if (!dbghelp_loader::IsDbgHelpAvailable()) {
        stack_trace.push_back("DbgHelp not available - cannot generate stack trace");
        return stack_trace;
    }

    HANDLE process = GetCurrentProcess();
    HANDLE thread = GetCurrentThread();

    // Initialize symbol handler if not already done
    static bool symbols_initialized = false;
    if (!symbols_initialized) {
        if (dbghelp_loader::SymInitialize_Original && dbghelp_loader::SymSetOptions_Original) {
            // Set symbol options for better symbol resolution
            // SYMOPT_UNDNAME: Undecorate C++ names
            // SYMOPT_DEFERRED_LOADS: Load symbols on demand
            // SYMOPT_INCLUDE_32BIT_MODULES: Include 32-bit modules in 64-bit process
            // SYMOPT_LOAD_LINES: Load line number information
            DWORD sym_options =
                SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_INCLUDE_32BIT_MODULES | SYMOPT_LOAD_LINES;
            dbghelp_loader::SymSetOptions_Original(sym_options);

            // Get the directory where the DLL is located to help DbgHelp find the PDB
            char module_path[MAX_PATH] = {};
            HMODULE current_module = nullptr;
            if (GetModuleHandleExA(
                    GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                    reinterpret_cast<LPCSTR>(&GenerateStackTraceInternal), &current_module)) {
                if (GetModuleFileNameA(current_module, module_path, MAX_PATH)) {
                    // Extract directory path
                    char* last_slash = strrchr(module_path, '\\');
                    if (last_slash) {
                        *last_slash = '\0';
                    }
                }
            }

            // Initialize with symbol path including the DLL directory
            // nullptr as second parameter uses default symbol path + current directory
            // TRUE means we want to load symbols for all modules
            if (dbghelp_loader::SymInitialize_Original(process, nullptr, TRUE) != FALSE) {
                symbols_initialized = true;
            }
        }
    }

    // Use provided context or capture current context
    CONTEXT context = {};
    if (context_ptr != nullptr) {
        // Copy the provided context
        context = *context_ptr;
    } else {
        // Get current context
        context.ContextFlags = CONTEXT_FULL;
        RtlCaptureContext(&context);
    }

    // Initialize stack frame
    STACKFRAME64 stack_frame = {};

#ifdef _WIN64
    stack_frame.AddrPC.Offset = context.Rip;
    stack_frame.AddrPC.Mode = AddrModeFlat;
    stack_frame.AddrFrame.Offset = context.Rbp;
    stack_frame.AddrFrame.Mode = AddrModeFlat;
    stack_frame.AddrStack.Offset = context.Rsp;
    stack_frame.AddrStack.Mode = AddrModeFlat;
#else
    stack_frame.AddrPC.Offset = context.Eip;
    stack_frame.AddrPC.Mode = AddrModeFlat;
    stack_frame.AddrFrame.Offset = context.Ebp;
    stack_frame.AddrFrame.Mode = AddrModeFlat;
    stack_frame.AddrStack.Offset = context.Esp;
    stack_frame.AddrStack.Mode = AddrModeFlat;
#endif

    // Walk the stack
    int frame_count = 0;
    constexpr int MAX_FRAMES = 50;

    struct SuppressGuard {
        ~SuppressGuard() { dbghelp_loader::SetSuppressStackWalkLogging(false); }
    };
    dbghelp_loader::SetSuppressStackWalkLogging(true);
    SuppressGuard suppress_guard;
    while (frame_count < MAX_FRAMES) {
        if (!dbghelp_loader::StackWalk64_Original) {
            break;
        }

        BOOL result = dbghelp_loader::StackWalk64_Original(
#ifdef _WIN64
            IMAGE_FILE_MACHINE_AMD64,
#else
            IMAGE_FILE_MACHINE_I386,
#endif
            process, thread, &stack_frame, &context, ReadProcessMemoryRoutine64,
            dbghelp_loader::SymFunctionTableAccess64_Original, dbghelp_loader::SymGetModuleBase64_Original, nullptr);

        if (result == FALSE) {
            break;
        }

        std::ostringstream frame_info;
        frame_info << "[" << std::setfill('0') << std::setw(2) << frame_count << "] ";
        bool has_pdb = false;
        if (stack_frame.AddrPC.Offset != 0) {
            // Format the stack frame

            // Get module name
            std::string module_name = GetModuleName(process, stack_frame.AddrPC.Offset);
            frame_info << module_name << "!";

            // Get symbol name
            std::string symbol_name = GetSymbolName(process, stack_frame.AddrPC.Offset);
            frame_info << symbol_name;

            // Get source info (line info requires PDB for the module)
            std::string source_info = GetSourceInfo(process, stack_frame.AddrPC.Offset);
            if (source_info != "Unknown") {
                frame_info << " (" << source_info << ")";
                has_pdb = true;
            }
        }

        // Add address
        frame_info << " [0x" << std::hex << std::uppercase << stack_frame.AddrPC.Offset << "]";
        frame_info << (has_pdb ? " [pdb]" : " [no pdb]");

        stack_trace.push_back(frame_info.str());
        frame_count++;
    }

    return stack_trace;
}
}  // anonymous namespace

std::vector<std::string> GenerateStackTrace() { return GenerateStackTraceInternal(nullptr); }

std::vector<std::string> GenerateStackTrace(CONTEXT* context) { return GenerateStackTraceInternal(context); }

}  // namespace stack_trace
