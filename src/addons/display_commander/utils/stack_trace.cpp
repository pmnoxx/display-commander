// Source Code <Display Commander>

// Group 1 — Source Code (Display Commander)
#include "stack_trace.hpp"
#include "hooks/dbghelp/dbghelp_loader.hpp"
#include "utils/logging.hpp"

// Group 2 — ReShade / ImGui
// (no includes)

// Group 3 — Standard C++
#include <iomanip>
#include <sstream>
#include <string>

// Group 4 — Windows.h
#include <Windows.h>

// Group 5 — Other Windows SDK
// Libraries <Windows>
#include <dbghelp.h>

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

void InitializeSymbolsOnce(HANDLE process) {
    static bool symbols_initialized = false;
    if (symbols_initialized) {
        return;
    }

    if (dbghelp_loader::SymInitialize_Original && dbghelp_loader::SymSetOptions_Original) {
        // Set symbol options for better symbol resolution
        // SYMOPT_UNDNAME: Undecorate C++ names
        // SYMOPT_DEFERRED_LOADS: Load symbols on demand
        // SYMOPT_INCLUDE_32BIT_MODULES: Include 32-bit modules in 64-bit process
        // SYMOPT_LOAD_LINES: Load line number information
        DWORD sym_options = SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_INCLUDE_32BIT_MODULES | SYMOPT_LOAD_LINES;
        dbghelp_loader::SymSetOptions_Original(sym_options);

        // Initialize with default symbol path; TRUE loads symbols for all modules.
        if (dbghelp_loader::SymInitialize_Original(process, nullptr, TRUE) != FALSE) {
            symbols_initialized = true;
        }
    }
}

void ConfigureSymbolSearchPathAndWarnIfPdbMissing(HANDLE process) {
    if (!dbghelp_loader::SymSetSearchPathW_Original) {
        return;
    }

    HMODULE our_module = nullptr;
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(&ConfigureSymbolSearchPathAndWarnIfPdbMissing), &our_module)
        && our_module != nullptr) {
        wchar_t module_path[MAX_PATH] = {};
        if (GetModuleFileNameW(our_module, module_path, MAX_PATH) != 0) {
            std::wstring path(module_path);
            const size_t last_slash = path.find_last_of(L"\\/");
            std::wstring dir = (last_slash != std::wstring::npos) ? path.substr(0, last_slash + 1) : L".\\";
            std::wstring pdb_path = path;
            const size_t dot = pdb_path.rfind(L'.');
            if (dot != std::wstring::npos) {
                pdb_path.replace(dot, std::wstring::npos, L".pdb");
            } else {
                pdb_path += L".pdb";
            }

            std::wstring search_path;
            if (dbghelp_loader::SymGetSearchPathW_Original) {
                wchar_t current_path[MAX_PATH * 3] = {};
                if (dbghelp_loader::SymGetSearchPathW_Original(process, current_path, MAX_PATH * 3) != FALSE
                    && current_path[0] != L'\0') {
                    search_path = current_path;
                    search_path += L';';
                }
            }
            search_path += dir;
            if (!search_path.empty()) {
                // Log the effective search path for debugging (convert from UTF-16 to UTF-8).
                const int path_utf8_len =
                    WideCharToMultiByte(CP_UTF8, 0, search_path.c_str(), static_cast<int>(search_path.size() + 1),
                                        nullptr, 0, nullptr, nullptr);
                if (path_utf8_len > 0) {
                    std::string search_path_utf8(static_cast<size_t>(path_utf8_len), '\0');
                    WideCharToMultiByte(CP_UTF8, 0, search_path.c_str(), static_cast<int>(search_path.size() + 1),
                                        search_path_utf8.data(), path_utf8_len, nullptr, nullptr);
                    LogInfo("DbgHelp symbol search path: '%s'", search_path_utf8.c_str());
                }

                dbghelp_loader::SymSetSearchPathW_Original(process, search_path.c_str());
            }

            if (GetFileAttributesW(pdb_path.c_str()) == INVALID_FILE_ATTRIBUTES) {
                const int utf8_len = WideCharToMultiByte(
                    CP_UTF8, 0, pdb_path.c_str(), static_cast<int>(pdb_path.size() + 1), nullptr, 0, nullptr, nullptr);
                if (utf8_len > 0) {
                    std::string pdb_utf8(static_cast<size_t>(utf8_len), '\0');
                    WideCharToMultiByte(CP_UTF8, 0, pdb_path.c_str(), static_cast<int>(pdb_path.size() + 1),
                                        pdb_utf8.data(), utf8_len, nullptr, nullptr);
                    LogWarn("Display Commander debug symbols ('%s') not found, stack trace may be inaccurate.",
                            pdb_utf8.c_str());
                } else {
                    LogWarn("Display Commander debug symbols not found, stack trace may be inaccurate.");
                }
            }
        }
    }
}

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

    InitializeSymbolsOnce(process);
    ConfigureSymbolSearchPathAndWarnIfPdbMissing(process);

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
