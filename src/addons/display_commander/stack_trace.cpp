#include "stack_trace.hpp"
#include "exit_handler.hpp"
#include "utils.hpp"
#include "dbghelp_loader.hpp"
#include <sstream>
#include <iomanip>
#include <atomic>

namespace stack_trace {

namespace {
    std::atomic<bool> g_initialized{false};
    std::atomic<bool> g_available{false};
    HANDLE g_process_handle = INVALID_HANDLE_VALUE;
    CRITICAL_SECTION g_critical_section;
} // namespace

bool Initialize() {
    bool expected = false;
    if (!g_initialized.compare_exchange_strong(expected, true)) {
        return g_available.load();
    }

    try {
        // Initialize critical section for thread safety
        InitializeCriticalSection(&g_critical_section);

        // Load dbghelp dynamically
        if (!dbghelp_loader::LoadDbgHelp()) {
            LogInfo("[Stack Trace] DbgHelp not available - stack trace functionality disabled");
            g_available.store(false);
            return false;
        }

        // Get current process handle
        g_process_handle = GetCurrentProcess();

        // Initialize symbol handler
        DWORD options = dbghelp_loader::SymGetOptions_Original();
        options |= SYMOPT_UNDNAME;           // Undecorate names
        options |= SYMOPT_DEFERRED_LOADS;    // Defer symbol loading
        options |= SYMOPT_LOAD_LINES;        // Load line information
        options |= SYMOPT_INCLUDE_32BIT_MODULES; // Include 32-bit modules
        options |= SYMOPT_AUTO_PUBLICS;      // Auto-load public symbols
        dbghelp_loader::SymSetOptions_Original(options);

        // Initialize symbol handler for current process
        if (dbghelp_loader::SymInitialize_Original(g_process_handle, nullptr, TRUE) != FALSE) {
            g_available.store(true);
            LogInfo("[Stack Trace] Initialized successfully");
        } else {
            DWORD error = GetLastError();
            LogWarn("[Stack Trace] Failed to initialize: %lu", error);
            g_available.store(false);
        }
    } catch (...) {
        LogError("[Stack Trace] Exception during initialization");
        g_available.store(false);
    }

    return g_available.load();
}

void Shutdown() {
    bool expected = true;
    if (!g_initialized.compare_exchange_strong(expected, false)) {
        return;
    }

    try {
        EnterCriticalSection(&g_critical_section);

        if (g_available.load() && dbghelp_loader::IsDbgHelpAvailable()) {
            dbghelp_loader::SymCleanup_Original(g_process_handle);
            g_available.store(false);
            LogInfo("[Stack Trace] Shutdown completed");
        }

        // Unload dbghelp
        dbghelp_loader::UnloadDbgHelp();

        g_process_handle = INVALID_HANDLE_VALUE;
        LeaveCriticalSection(&g_critical_section);
        DeleteCriticalSection(&g_critical_section);
    } catch (...) {
        LogError("[Stack Trace] Exception during shutdown");
    }
}

std::vector<StackFrame> CaptureStackTrace(DWORD max_frames, CONTEXT* context) {
    std::vector<StackFrame> frames;

    if (!g_available.load()) {
        return frames;
    }

    try {
        EnterCriticalSection(&g_critical_section);

        // Prepare stack frame
        STACKFRAME64 stack_frame = {};
        stack_frame.AddrPC.Mode = AddrModeFlat;
        stack_frame.AddrFrame.Mode = AddrModeFlat;
        stack_frame.AddrStack.Mode = AddrModeFlat;

        // Get current context if not provided
        CONTEXT current_context;
        if (context == nullptr) {
            RtlCaptureContext(&current_context);
            context = &current_context;
        }

        // Set up initial frame addresses based on architecture
#ifdef _WIN64
        stack_frame.AddrPC.Offset = context->Rip;
        stack_frame.AddrFrame.Offset = context->Rbp;
        stack_frame.AddrStack.Offset = context->Rsp;
        DWORD machine_type = IMAGE_FILE_MACHINE_AMD64;
#else
        stack_frame.AddrPC.Offset = context->Eip;
        stack_frame.AddrFrame.Offset = context->Ebp;
        stack_frame.AddrStack.Offset = context->Esp;
        DWORD machine_type = IMAGE_FILE_MACHINE_I386;
#endif

        // Walk the stack
        for (DWORD i = 0; i < max_frames; i++) {
            if (dbghelp_loader::StackWalk64_Original(machine_type, g_process_handle, GetCurrentThread(),
                           &stack_frame, context, nullptr, dbghelp_loader::SymFunctionTableAccess64_Original,
                           dbghelp_loader::SymGetModuleBase64_Original, nullptr) == FALSE) {
                break;
            }

            // Skip invalid addresses
            if (stack_frame.AddrPC.Offset == 0) {
                break;
            }

            // Get symbol information for this frame
            StackFrame frame = GetSymbolInfo(stack_frame.AddrPC.Offset);
            frames.push_back(frame);
        }

        LeaveCriticalSection(&g_critical_section);
    } catch (...) {
        LogError("[Stack Trace] Exception during stack capture");
        LeaveCriticalSection(&g_critical_section);
    }

    return frames;
}

std::vector<StackFrame> CaptureStackTraceFromException(EXCEPTION_POINTERS* exception_info, DWORD max_frames) {
    if (exception_info == nullptr || exception_info->ContextRecord == nullptr) {
        return CaptureStackTrace(max_frames, nullptr);
    }

    return CaptureStackTrace(max_frames, exception_info->ContextRecord);
}

std::string FormatStackTrace(const std::vector<StackFrame>& frames, bool include_addresses) {
    std::ostringstream output;

    if (frames.empty()) {
        output << "  [No stack frames captured]";
        return output.str();
    }

    for (size_t i = 0; i < frames.size(); i++) {
        const StackFrame& frame = frames[i];

        output << "  #" << std::setw(2) << std::setfill('0') << i << " ";

        if (include_addresses) {
            output << "0x" << std::hex << std::setw(16) << std::setfill('0') << frame.address << " ";
        }

        if (!frame.module_name.empty()) {
            output << frame.module_name;
            if (!frame.function_name.empty()) {
                output << "!" << frame.function_name;
            }
        } else if (!frame.function_name.empty()) {
            output << frame.function_name;
        } else {
            output << "<unknown>";
        }

        if (frame.line_number > 0 && !frame.file_name.empty()) {
            output << " (" << frame.file_name << ":" << std::dec << frame.line_number << ")";
        } else if (frame.offset > 0) {
            output << " +0x" << std::hex << frame.offset;
        }

        output << "\n";
    }

    return output.str();
}

StackFrame GetSymbolInfo(DWORD64 address) {
    StackFrame frame = {};
    frame.address = address;

    if (!g_available.load()) {
        return frame;
    }

    try {
        // Allocate symbol info buffer
        const DWORD symbol_size = sizeof(SYMBOL_INFO) + MAX_SYM_NAME * sizeof(TCHAR);
        std::vector<BYTE> symbol_buffer(symbol_size);
        PSYMBOL_INFO symbol_info = reinterpret_cast<PSYMBOL_INFO>(symbol_buffer.data());
        symbol_info->SizeOfStruct = sizeof(SYMBOL_INFO);
        symbol_info->MaxNameLen = MAX_SYM_NAME;

        // Get symbol information
        DWORD64 displacement = 0;
        if (dbghelp_loader::SymFromAddr_Original(g_process_handle, address, &displacement, symbol_info) != FALSE) {
            frame.function_name = symbol_info->Name;
            frame.offset = displacement;
        }

        // Get line information
        IMAGEHLP_LINE64 line_info = {};
        line_info.SizeOfStruct = sizeof(IMAGEHLP_LINE64);
        DWORD line_displacement = 0;
        if (dbghelp_loader::SymGetLineFromAddr64_Original(g_process_handle, address, &line_displacement, &line_info) != FALSE) {
            frame.file_name = line_info.FileName;
            frame.line_number = line_info.LineNumber;
        }

        // Get module information
        IMAGEHLP_MODULE64 module_info = {};
        module_info.SizeOfStruct = sizeof(IMAGEHLP_MODULE64);
        if (dbghelp_loader::SymGetModuleInfo64_Original(g_process_handle, address, &module_info) != FALSE) {
            frame.module_name = module_info.ModuleName;
        }
    } catch (...) {
        LogError("[Stack Trace] Exception getting symbol info for address 0x%llx", address);
    }

    return frame;
}

bool IsAvailable() {
    return g_available.load() && dbghelp_loader::IsDbgHelpAvailable();
}

void TestStackTrace() {
    if (!g_available.load()) {
        LogWarn("[Stack Trace] Test failed - stack trace not available");
        return;
    }

    try {
        LogInfo("[Stack Trace] Testing stack trace capture...");

        // Capture current stack trace
        auto frames = CaptureStackTrace(10, nullptr);
        std::string stack_trace = FormatStackTrace(frames, true);

        LogInfo("[Stack Trace] Test stack trace captured:");
        LogInfo("%s", stack_trace.c_str());

        // Also write to debug.log for verification
        exit_handler::WriteToDebugLog("=== TEST STACK TRACE ===");
        exit_handler::WriteToDebugLog(stack_trace);
        exit_handler::WriteToDebugLog("=== END TEST STACK TRACE ===");

        LogInfo("[Stack Trace] Test completed successfully");
    } catch (...) {
        LogError("[Stack Trace] Test failed with exception");
    }
}

} // namespace stack_trace
