#include "process_exit_hooks.hpp"
#include <psapi.h>
#include <windows.h>
#include <atomic>
#include <cstdlib>
#include <ctime>
#include <sstream>
#include <string>
#include <unordered_set>
#include <vector>
#include "dbghelp_loader.hpp"
#include "exit_handler.hpp"
#include "globals.hpp"
#include "utils/detour_call_tracker.hpp"
#include "utils/srwlock_wrapper.hpp"
#include "utils/stack_trace.hpp"
#include "utils/timing.hpp"
#include "version.hpp"

namespace {

// Track seen exception addresses to avoid duplicate logging
SRWLOCK g_seen_exception_addresses_lock = SRWLOCK_INIT;
std::unordered_set<uintptr_t> g_seen_exception_addresses;

// Check if exception address was seen before, and record it if not
// Returns true if address was already seen (should skip detailed logging)
bool CheckAndRecordExceptionAddress(uintptr_t address) {
    utils::SRWLockExclusive lock(g_seen_exception_addresses_lock);
    auto result = g_seen_exception_addresses.insert(address);
    return !result.second;  // true if already existed (insert failed)
}

// Helper function to print process information
void PrintProcessInfo() {
    try {
        exit_handler::WriteToDebugLog("=== PROCESS INFORMATION ===");

        // Process ID
        DWORD process_id = GetCurrentProcessId();
        std::ostringstream pid_msg;
        pid_msg << "Process ID: " << process_id;
        exit_handler::WriteToDebugLog(pid_msg.str());

        // Thread ID
        DWORD thread_id = GetCurrentThreadId();
        std::ostringstream tid_msg;
        tid_msg << "Thread ID: " << thread_id;
        exit_handler::WriteToDebugLog(tid_msg.str());

        // Process executable path
        wchar_t process_path[MAX_PATH];
        if (GetModuleFileNameW(nullptr, process_path, MAX_PATH) != 0) {
            int size_needed = WideCharToMultiByte(CP_UTF8, 0, process_path, -1, nullptr, 0, nullptr, nullptr);
            if (size_needed > 0) {
                std::vector<char> buffer(size_needed);
                WideCharToMultiByte(CP_UTF8, 0, process_path, -1, buffer.data(), size_needed, nullptr, nullptr);
                std::ostringstream path_msg;
                path_msg << "Process Path: " << buffer.data();
                exit_handler::WriteToDebugLog(path_msg.str());
            }
        }

        // Command line
        LPSTR command_line = GetCommandLineA();
        if (command_line != nullptr) {
            std::ostringstream cmd_msg;
            cmd_msg << "Command Line: " << command_line;
            exit_handler::WriteToDebugLog(cmd_msg.str());
        }

        // Process memory information
        HANDLE process_handle = GetCurrentProcess();
        PROCESS_MEMORY_COUNTERS_EX mem_counters = {};
        if (GetProcessMemoryInfo(process_handle, reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&mem_counters),
                                 sizeof(mem_counters))
            != 0) {
            std::ostringstream mem_msg;
            mem_msg << "Process Memory - Working Set: " << (mem_counters.WorkingSetSize / 1024 / 1024) << " MB, "
                    << "Peak Working Set: " << (mem_counters.PeakWorkingSetSize / 1024 / 1024) << " MB, "
                    << "Page Faults: " << mem_counters.PageFaultCount;
            exit_handler::WriteToDebugLog(mem_msg.str());
        }

        exit_handler::WriteToDebugLog("=== END PROCESS INFORMATION ===");
    } catch (...) {
        exit_handler::WriteToDebugLog("=== PROCESS INFORMATION ERROR ===");
        exit_handler::WriteToDebugLog("Exception occurred while gathering process information");
        exit_handler::WriteToDebugLog("=== END PROCESS INFORMATION ===");
    }
}

// Helper function to print system information
void PrintSystemInfo() {
    try {
        exit_handler::WriteToDebugLog("=== SYSTEM INFORMATION ===");

        // OS Version (using RtlGetVersion which is safer than GetVersionEx)
        typedef NTSTATUS(WINAPI * RtlGetVersionFunc)(OSVERSIONINFOEXW*);
        HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
        if (ntdll != nullptr) {
            RtlGetVersionFunc rtl_get_version =
                reinterpret_cast<RtlGetVersionFunc>(GetProcAddress(ntdll, "RtlGetVersion"));
            if (rtl_get_version != nullptr) {
                OSVERSIONINFOEXW os_info = {};
                os_info.dwOSVersionInfoSize = sizeof(OSVERSIONINFOEXW);
                if (rtl_get_version(&os_info) == 0) {
                    std::ostringstream os_msg;
                    os_msg << "OS Version: Windows " << os_info.dwMajorVersion << "." << os_info.dwMinorVersion
                           << " Build " << os_info.dwBuildNumber;
                    if (os_info.wServicePackMajor > 0) {
                        os_msg << " SP" << os_info.wServicePackMajor;
                    }
                    exit_handler::WriteToDebugLog(os_msg.str());
                }
            }
        }

        // CPU Information
        SYSTEM_INFO sys_info = {};
        GetSystemInfo(&sys_info);
        std::ostringstream cpu_msg;
        cpu_msg << "CPU - Processors: " << sys_info.dwNumberOfProcessors << ", Architecture: ";
        switch (sys_info.wProcessorArchitecture) {
            case PROCESSOR_ARCHITECTURE_AMD64: cpu_msg << "x64"; break;
            case PROCESSOR_ARCHITECTURE_INTEL: cpu_msg << "x86"; break;
            case PROCESSOR_ARCHITECTURE_ARM:   cpu_msg << "ARM"; break;
            case PROCESSOR_ARCHITECTURE_ARM64: cpu_msg << "ARM64"; break;
            default:                           cpu_msg << "Unknown (0x" << std::hex << sys_info.wProcessorArchitecture << ")"; break;
        }
        exit_handler::WriteToDebugLog(cpu_msg.str());

        // System memory (more detailed)
        MEMORYSTATUSEX mem_status = {};
        mem_status.dwLength = sizeof(mem_status);
        if (GlobalMemoryStatusEx(&mem_status) != 0) {
            std::ostringstream mem_msg;
            mem_msg << "System Memory - Total: " << (mem_status.ullTotalPhys / 1024 / 1024 / 1024) << " GB, "
                    << "Available: " << (mem_status.ullAvailPhys / 1024 / 1024 / 1024) << " GB, "
                    << "Load: " << mem_status.dwMemoryLoad << "%";
            exit_handler::WriteToDebugLog(mem_msg.str());
        }

        // Current time
        time_t raw_time;
        time(&raw_time);
        struct tm time_info;
        if (localtime_s(&time_info, &raw_time) == 0) {
            char time_str[64];
            strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", &time_info);
            std::ostringstream time_msg;
            time_msg << "Crash Time: " << time_str;
            exit_handler::WriteToDebugLog(time_msg.str());
        }

        exit_handler::WriteToDebugLog("=== END SYSTEM INFORMATION ===");
    } catch (...) {
        exit_handler::WriteToDebugLog("=== SYSTEM INFORMATION ERROR ===");
        exit_handler::WriteToDebugLog("Exception occurred while gathering system information");
        exit_handler::WriteToDebugLog("=== END SYSTEM INFORMATION ===");
    }
}

// Helper function to print version information
void PrintVersionInfo() {
    try {
        exit_handler::WriteToDebugLog("=== VERSION INFORMATION ===");

        // Display Commander version
        exit_handler::WriteToDebugLog(DISPLAY_COMMANDER_FULL_VERSION);

        exit_handler::WriteToDebugLog("=== END VERSION INFORMATION ===");
    } catch (...) {
        exit_handler::WriteToDebugLog("=== VERSION INFORMATION ERROR ===");
        exit_handler::WriteToDebugLog("Exception occurred while gathering version information");
        exit_handler::WriteToDebugLog("=== END VERSION INFORMATION ===");
    }
}

// Helper function to print list of loaded modules
void PrintLoadedModules() {
    try {
        exit_handler::WriteToDebugLog("=== LOADED MODULES ===");

        HANDLE process_handle = GetCurrentProcess();
        HMODULE modules[1024];
        DWORD bytes_needed;

        if (EnumProcessModules(process_handle, modules, sizeof(modules), &bytes_needed) == 0) {
            std::ostringstream error_msg;
            error_msg << "Failed to enumerate process modules - Error: " << GetLastError();
            exit_handler::WriteToDebugLog(error_msg.str());
            exit_handler::WriteToDebugLog("=== END LOADED MODULES ===");
            return;
        }

        DWORD module_count = bytes_needed / sizeof(HMODULE);
        std::ostringstream count_msg;
        count_msg << "Total loaded modules: " << module_count;
        exit_handler::WriteToDebugLog(count_msg.str());

        for (DWORD i = 0; i < module_count; i++) {
            wchar_t module_path[MAX_PATH];
            if (GetModuleFileNameW(modules[i], module_path, MAX_PATH) != 0) {
                // Convert wide string to narrow string for logging
                int size_needed = WideCharToMultiByte(CP_UTF8, 0, module_path, -1, nullptr, 0, nullptr, nullptr);
                if (size_needed > 0) {
                    std::vector<char> buffer(size_needed);
                    WideCharToMultiByte(CP_UTF8, 0, module_path, -1, buffer.data(), size_needed, nullptr, nullptr);

                    MODULEINFO module_info = {};
                    std::ostringstream module_msg;
                    if (GetModuleInformation(process_handle, modules[i], &module_info, sizeof(module_info)) != 0) {
                        module_msg << "  [" << i << "] " << buffer.data() << " (Base: 0x" << std::hex << std::uppercase
                                   << reinterpret_cast<uintptr_t>(module_info.lpBaseOfDll) << ", Size: " << std::dec
                                   << module_info.SizeOfImage << " bytes)";
                    } else {
                        module_msg << "  [" << i << "] " << buffer.data();
                    }
                    exit_handler::WriteToDebugLog(module_msg.str());
                }
            } else {
                std::ostringstream module_msg;
                module_msg << "  [" << i << "] <Unknown Module> (Handle: 0x" << std::hex << std::uppercase
                           << reinterpret_cast<uintptr_t>(modules[i]) << ")";
                exit_handler::WriteToDebugLog(module_msg.str());
            }
        }

        exit_handler::WriteToDebugLog("=== END LOADED MODULES ===");
    } catch (...) {
        exit_handler::WriteToDebugLog("=== LOADED MODULES ERROR ===");
        exit_handler::WriteToDebugLog("Exception occurred while enumerating loaded modules");
        exit_handler::WriteToDebugLog("=== END LOADED MODULES ===");
    }
}

}  // anonymous namespace

namespace process_exit_hooks {

std::atomic<bool> g_installed{false};
std::atomic<LPTOP_LEVEL_EXCEPTION_FILTER> g_last_detour_handler{nullptr};
PVOID g_vectored_exception_handler_handle = nullptr;

void AtExitHandler() {
    // Log exit detection
    exit_handler::OnHandleExit(exit_handler::ExitSource::ATEXIT, "Normal process exit via atexit");
}

LONG WINAPI UnhandledExceptionHandler(EXCEPTION_POINTERS* exception_info) {
    // Check if shutdown is in progress to avoid crashes during DLL unload
    if (g_shutdown.load()) {
        // During shutdown, just return without doing anything to avoid crashes
        return EXCEPTION_EXECUTE_HANDLER;
    }

    // Check if we've seen this exception address before
    uintptr_t exception_address = 0;
    if (exception_info && exception_info->ExceptionRecord) {
        exception_address = reinterpret_cast<uintptr_t>(exception_info->ExceptionRecord->ExceptionAddress);
        if (CheckAndRecordExceptionAddress(exception_address)) {
            // Address was already seen, skip detailed logging
            // std::ostringstream skip_msg;
            // skip_msg << "Exception at address 0x" << std::hex << std::uppercase << exception_address
            //          << " already logged, skipping duplicate report";
            // exit_handler::WriteToDebugLog(skip_msg.str());

            // Still chain to last handler
            LPTOP_LEVEL_EXCEPTION_FILTER last_detour_handler = g_last_detour_handler.load();
            if (last_detour_handler != nullptr) {
                return last_detour_handler(exception_info);
            }
            return EXCEPTION_EXECUTE_HANDLER;
        }
    }

    // Ensure DbgHelp is loaded before attempting stack trace
    dbghelp_loader::LoadDbgHelp();

    // Log detailed crash information similar to Special-K's approach
    exit_handler::WriteToDebugLog("=== CRASH DETECTED - DETAILED CRASH REPORT ===");

    // Print version information first
    PrintVersionInfo();

    // Print system information
    PrintSystemInfo();

    // Print process information
    PrintProcessInfo();

    // Log exception information
    if (exception_info && exception_info->ExceptionRecord) {
        std::ostringstream exception_details;
        exception_details << "Exception Code: 0x" << std::hex << std::uppercase
                          << exception_info->ExceptionRecord->ExceptionCode;
        exit_handler::WriteToDebugLog(exception_details.str());

        // Log exception flags
        std::ostringstream flags_details;
        flags_details << "Exception Flags: 0x" << std::hex << std::uppercase
                      << exception_info->ExceptionRecord->ExceptionFlags;
        exit_handler::WriteToDebugLog(flags_details.str());

        // Log exception address
        std::ostringstream address_details;
        address_details << "Exception Address: 0x" << std::hex << std::uppercase
                        << reinterpret_cast<uintptr_t>(exception_info->ExceptionRecord->ExceptionAddress);
        exit_handler::WriteToDebugLog(address_details.str());
    }

    // Log system information
    MEMORYSTATUSEX mem_status = {};
    mem_status.dwLength = sizeof(mem_status);
    if (GlobalMemoryStatusEx(&mem_status)) {
        std::ostringstream mem_details;
        mem_details << "System Memory Load: " << mem_status.dwMemoryLoad << "%";
        exit_handler::WriteToDebugLog(mem_details.str());
    }

    // Print recent detour calls information
    uint64_t crash_timestamp_ns = utils::get_real_time_ns();  // Use real time to avoid spoofed timers
    std::string recent_detour_info = detour_call_tracker::FormatRecentDetourCalls(crash_timestamp_ns, 256);
    exit_handler::WriteToDebugLog("=== RECENT DETOUR CALLS ===");

    // Split multi-line string and write each line separately
    if (!recent_detour_info.empty()) {
        std::istringstream iss(recent_detour_info);
        std::string line;
        while (std::getline(iss, line)) {
            // Remove any trailing carriage return
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            if (!line.empty()) {
                exit_handler::WriteToDebugLog(line);
            }
        }
    } else {
        exit_handler::WriteToDebugLog("Recent Detour Calls: <none recorded>");
    }

    exit_handler::WriteToDebugLog("=== END RECENT DETOUR CALLS ===");

    // Print stack trace to DbgView using exception context
    exit_handler::WriteToDebugLog("=== GENERATING STACK TRACE ===");
    CONTEXT* exception_context = nullptr;
    if (exception_info && exception_info->ContextRecord) {
        exception_context = exception_info->ContextRecord;
    }

    // Generate stack trace using exception context
    auto stack_trace = stack_trace::GenerateStackTrace(exception_context);

    // Also log stack trace to file frame by frame to avoid truncation
    exit_handler::WriteToDebugLog("=== STACK TRACE ===");
    for (const auto& frame : stack_trace) {
        exit_handler::WriteToDebugLog(frame);
    }
    exit_handler::WriteToDebugLog("=== END STACK TRACE ===");

    // Print list of loaded modules
    PrintLoadedModules();

    // Log exit detection
    // exit_handler::OnHandleExit(exit_handler::ExitSource::UNHANDLED_EXCEPTION, "Unhandled exception detected");

    // Chain to last handler set via SetUnhandledExceptionFilter_Detour if any
    LPTOP_LEVEL_EXCEPTION_FILTER last_detour_handler = g_last_detour_handler.load();
    if (last_detour_handler != nullptr) {
        return last_detour_handler(exception_info);
    }

    return EXCEPTION_EXECUTE_HANDLER;
    // assert(IsDebuggerPresent());
    //  return EXCEPTION_CONTINUE_EXECUTION;
}

// Vectored exception handler - catches exceptions early and prints stack traces
// Similar to ReShade's implementation but prints stack traces instead of minidumps
LONG WINAPI VectoredExceptionHandler(PEXCEPTION_POINTERS ex) {
    // Check if shutdown is in progress to avoid crashes during DLL unload
    if (g_shutdown.load()) {
        return EXCEPTION_CONTINUE_SEARCH;
    }

    // Ignore debugging and some common language exceptions (same filter as ReShade)
    const DWORD code = ex->ExceptionRecord->ExceptionCode;
    if (code == CONTROL_C_EXIT || code == 0x406D1388 /* SetThreadName */ || code == DBG_PRINTEXCEPTION_C
        || code == DBG_PRINTEXCEPTION_WIDE_C || code == STATUS_BREAKPOINT
        || code == 0xE0434352 /* CLR exception */ || code == 0xE06D7363 /* Visual C++ exception */
        || ((code ^ 0xE24C4A00) <= 0xFF) /* LuaJIT exception */) {
        return EXCEPTION_CONTINUE_SEARCH;
    }

    // Check if we've seen this exception address before
    uintptr_t exception_address = 0;
    if (ex && ex->ExceptionRecord) {
        exception_address = reinterpret_cast<uintptr_t>(ex->ExceptionRecord->ExceptionAddress);
        if (CheckAndRecordExceptionAddress(exception_address)) {
            // Address was already seen, skip detailed logging
            std::ostringstream skip_msg;
            skip_msg << "Vectored exception at address 0x" << std::hex << std::uppercase << exception_address
                     << " already logged, skipping duplicate report";
            exit_handler::WriteToDebugLog(skip_msg.str());
            return EXCEPTION_CONTINUE_SEARCH;
        }
    }

    // Ensure DbgHelp is loaded before attempting stack trace
    dbghelp_loader::LoadDbgHelp();

    // Log detailed crash information
    exit_handler::WriteToDebugLog("=== VECTORED EXCEPTION HANDLER - CRASH DETECTED ===");

    // Print version information first
    PrintVersionInfo();

    // Print system information
    PrintSystemInfo();

    // Print process information
    PrintProcessInfo();

    // Log exception information
    if (ex && ex->ExceptionRecord) {
        std::ostringstream exception_details;
        exception_details << "Exception Code: 0x" << std::hex << std::uppercase << ex->ExceptionRecord->ExceptionCode;
        exit_handler::WriteToDebugLog(exception_details.str());

        // Log exception flags
        std::ostringstream flags_details;
        flags_details << "Exception Flags: 0x" << std::hex << std::uppercase << ex->ExceptionRecord->ExceptionFlags;
        exit_handler::WriteToDebugLog(flags_details.str());

        // Log exception address
        std::ostringstream address_details;
        address_details << "Exception Address: 0x" << std::hex << std::uppercase
                        << reinterpret_cast<uintptr_t>(ex->ExceptionRecord->ExceptionAddress);
        exit_handler::WriteToDebugLog(address_details.str());
    }

    // Print recent detour calls information
    uint64_t crash_timestamp_ns = utils::get_real_time_ns();  // Use real time to avoid spoofed timers
    std::string recent_detour_info = detour_call_tracker::FormatRecentDetourCalls(crash_timestamp_ns, 256);
    exit_handler::WriteToDebugLog("=== RECENT DETOUR CALLS ===");

    // Split multi-line string and write each line separately
    if (!recent_detour_info.empty()) {
        std::istringstream iss(recent_detour_info);
        std::string line;
        while (std::getline(iss, line)) {
            // Remove any trailing carriage return
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            if (!line.empty()) {
                exit_handler::WriteToDebugLog(line);
            }
        }
    } else {
        exit_handler::WriteToDebugLog("Recent Detour Calls: <none recorded>");
    }

    exit_handler::WriteToDebugLog("=== END RECENT DETOUR CALLS ===");

    // Print stack trace using exception context
    exit_handler::WriteToDebugLog("=== GENERATING STACK TRACE ===");
    CONTEXT* exception_context = nullptr;
    if (ex && ex->ContextRecord) {
        exception_context = ex->ContextRecord;
    }

    // Generate stack trace using exception context
    auto stack_trace = stack_trace::GenerateStackTrace(exception_context);

    // Log stack trace to file frame by frame to avoid truncation
    exit_handler::WriteToDebugLog("=== STACK TRACE ===");
    for (const auto& frame : stack_trace) {
        exit_handler::WriteToDebugLog(frame);
    }
    exit_handler::WriteToDebugLog("=== END STACK TRACE ===");

    // Print list of loaded modules
    PrintLoadedModules();

    // Continue searching for other handlers (like SetUnhandledExceptionFilter)
    return EXCEPTION_CONTINUE_SEARCH;
}

void Initialize() {
    bool expected = false;
    if (!g_installed.compare_exchange_strong(expected, true)) {
        return;
    }

    // atexit for graceful exits
    std::atexit(&AtExitHandler);

    // SEH unhandled exception filter for most crash scenarios
    exit_handler::WriteToDebugLog("Installing SEH unhandled exception filter");
    g_last_detour_handler = ::SetUnhandledExceptionFilter(&UnhandledExceptionHandler);

    // Install vectored exception handler to catch exceptions early (before SetUnhandledExceptionFilter)
    // This allows us to print stack traces for all crashes, similar to ReShade's approach
    // First parameter (1) means this handler is called first (before other handlers)
    exit_handler::WriteToDebugLog("Installing vectored exception handler");
    g_vectored_exception_handler_handle = ::AddVectoredExceptionHandler(1, &VectoredExceptionHandler);
    if (g_vectored_exception_handler_handle == nullptr) {
        exit_handler::WriteToDebugLog("Failed to install vectored exception handler");
    } else {
        exit_handler::WriteToDebugLog("Vectored exception handler installed successfully");
    }
}

void Shutdown() {
    bool expected = true;
    if (!g_installed.compare_exchange_strong(expected, false)) {
        return;
    }

    // Restore previous unhandled exception filter
    ::SetUnhandledExceptionFilter(g_last_detour_handler);

    // Clear last detour handler
    g_last_detour_handler.store(nullptr);

    // Remove vectored exception handler
    if (g_vectored_exception_handler_handle != nullptr) {
        ::RemoveVectoredExceptionHandler(g_vectored_exception_handler_handle);
        g_vectored_exception_handler_handle = nullptr;
        exit_handler::WriteToDebugLog("Vectored exception handler removed");
    }

    // Clear seen exception addresses
    {
        utils::SRWLockExclusive lock(g_seen_exception_addresses_lock);
        g_seen_exception_addresses.clear();
    }
}

}  // namespace process_exit_hooks
