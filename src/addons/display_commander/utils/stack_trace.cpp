#include "stack_trace.hpp"
#include "../dbghelp_loader.hpp"
#include <windows.h>
#include <dbghelp.h>
#include <sstream>
#include <iomanip>
#include <tlhelp32.h>

namespace stack_trace {

namespace {
// Helper function to get module name from address
std::string GetModuleName(HANDLE process, DWORD64 address) {
    IMAGEHLP_MODULE64 module_info = {};
    module_info.SizeOfStruct = sizeof(IMAGEHLP_MODULE64);

    if (dbghelp_loader::SymGetModuleInfo64_Original &&
        dbghelp_loader::SymGetModuleInfo64_Original(process, address, &module_info) != FALSE) {
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
    if (dbghelp_loader::SymFromAddr_Original &&
        dbghelp_loader::SymFromAddr_Original(process, address, &displacement, symbol_info) != FALSE) {
        return symbol_info->Name;
    }
    return "Unknown";
}

// Helper function to get source file and line number
std::string GetSourceInfo(HANDLE process, DWORD64 address) {
    IMAGEHLP_LINE64 line_info = {};
    line_info.SizeOfStruct = sizeof(IMAGEHLP_LINE64);

    DWORD displacement = 0;
    if (dbghelp_loader::SymGetLineFromAddr64_Original &&
        dbghelp_loader::SymGetLineFromAddr64_Original(process, address, &displacement, &line_info) != FALSE) {
        std::ostringstream oss;
        oss << line_info.FileName << ":" << line_info.LineNumber;
        return oss.str();
    }
    return "Unknown";
}

// Memory read routine for StackWalk64
BOOL CALLBACK ReadProcessMemoryRoutine64(
    HANDLE h_process,
    DWORD64 lp_base_address,
    PVOID lp_buffer,
    DWORD n_size,
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
} // namespace

bool IsNvngxUpdateRunning() {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        return false;
    }

    PROCESSENTRY32W process_entry = {};
    process_entry.dwSize = sizeof(PROCESSENTRY32W);

    bool found = false;
    if (Process32FirstW(snapshot, &process_entry)) {
        do {
            // Convert wide string to narrow string for comparison
            std::wstring process_name(process_entry.szExeFile);
            std::string process_name_narrow(process_name.begin(), process_name.end());

            if (_stricmp(process_name_narrow.c_str(), "nvngx_update.exe") == 0) {
                found = true;
                break;
            }
        } while (Process32NextW(snapshot, &process_entry));
    }

    CloseHandle(snapshot);
    return found;
}

namespace {
// Internal helper function that does the actual stack trace generation
std::vector<std::string> GenerateStackTraceInternal(CONTEXT* context_ptr) {
    std::vector<std::string> stack_trace;

    // Check if DbgHelp is available
    if (!dbghelp_loader::IsDbgHelpAvailable()) {
        stack_trace.push_back("DbgHelp not available - cannot generate stack trace");
        return stack_trace;
    }

    HANDLE process = GetCurrentProcess();
    HANDLE thread = GetCurrentThread();

    // Initialize symbol handler if not already done
    static bool symbols_initialized = false;
    if (!symbols_initialized) {
        if (dbghelp_loader::SymInitialize_Original) {
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
            process,
            thread,
            &stack_frame,
            &context,
            ReadProcessMemoryRoutine64,
            dbghelp_loader::SymFunctionTableAccess64_Original,
            dbghelp_loader::SymGetModuleBase64_Original,
            nullptr
        );

        if (result == FALSE) {
            break;
        }

        std::ostringstream frame_info;
        frame_info << "[" << std::setfill('0') << std::setw(2) << frame_count << "] ";
        if (stack_frame.AddrPC.Offset != 0) {
            // Format the stack frame

            // Get module name
            std::string module_name = GetModuleName(process, stack_frame.AddrPC.Offset);
            frame_info << module_name << "!";

            // Get symbol name
            std::string symbol_name = GetSymbolName(process, stack_frame.AddrPC.Offset);
            frame_info << symbol_name;

            // Get source info
            std::string source_info = GetSourceInfo(process, stack_frame.AddrPC.Offset);
            if (source_info != "Unknown") {
                frame_info << " (" << source_info << ")";
            }
        }

        // Add address
        frame_info << " [0x" << std::hex << std::uppercase << stack_frame.AddrPC.Offset << "]";

        stack_trace.push_back(frame_info.str());
        frame_count++;
    }

    return stack_trace;
}
} // anonymous namespace

std::vector<std::string> GenerateStackTrace() {
    return GenerateStackTraceInternal(nullptr);
}

std::vector<std::string> GenerateStackTrace(CONTEXT* context) {
    return GenerateStackTraceInternal(context);
}

void PrintStackTraceToDbgView() {
    PrintStackTraceToDbgView(nullptr);
}

void PrintStackTraceToDbgView(CONTEXT* context) {
    try {
        auto stack_trace = GenerateStackTraceInternal(context);

        // Print header
        OutputDebugStringA("=== STACK TRACE ===\n");

        // Print each frame
        for (const auto& frame : stack_trace) {
            OutputDebugStringA((frame + "\n").c_str());
        }

        // Print footer
        OutputDebugStringA("=== END STACK TRACE ===\n");

        // Check and print nvngx_update.exe status
        if (IsNvngxUpdateRunning()) {
            OutputDebugStringA("=== NVNGX UPDATE STATUS ===\n");
            OutputDebugStringA("nvngx_update.exe is currently running\n");
            OutputDebugStringA("=== END NVNGX UPDATE STATUS ===\n");
        }

    } catch (...) {
        OutputDebugStringA("=== STACK TRACE ERROR ===\n");
        OutputDebugStringA("Exception occurred while generating stack trace\n");
        OutputDebugStringA("=== END STACK TRACE ===\n");
    }
}

std::string GetStackTraceString() {
    return GetStackTraceString(nullptr);
}

std::string GetStackTraceString(CONTEXT* context) {
    try {
        auto stack_trace = GenerateStackTraceInternal(context);

        std::ostringstream result;
        result << "=== STACK TRACE ===\n";

        for (const auto& frame : stack_trace) {
            result << frame << "\n";
        }

        result << "=== END STACK TRACE ===\n";

        // Check and add nvngx_update.exe status
        if (IsNvngxUpdateRunning()) {
            result << "=== NVNGX UPDATE STATUS ===\n";
            result << "nvngx_update.exe is currently running\n";
            result << "TODO: add logic to stop nvngx_update.exe if it is running\n";
            result << "TODO: add display popup to inform user about the issue\n";
            result << "=== END NVNGX UPDATE STATUS ===\n";
        }

        return result.str();

    } catch (...) {
        return "=== STACK TRACE ERROR ===\nException occurred while generating stack trace\n=== END STACK TRACE ===\n";
    }
}

} // namespace stack_trace
