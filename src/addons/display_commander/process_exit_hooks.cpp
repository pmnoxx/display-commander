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
#include "utils/dbghelp_loader.hpp"
#include "exit_handler.hpp"
#include "globals.hpp"
#include "utils/logging.hpp"
#include "utils/detour_call_tracker.hpp"
#include "utils/display_commander_logger.hpp"
#include "utils/srwlock_wrapper.hpp"
#include "utils/stack_trace.hpp"
#include "utils/timing.hpp"
#include "version.hpp"

namespace {

// Track seen exception addresses to avoid duplicate logging
std::unordered_set<uintptr_t> g_seen_exception_addresses;

// Check if exception address was seen before, and record it if not
// Returns true if address was already seen (should skip detailed logging)
bool CheckAndRecordExceptionAddress(uintptr_t address) {
    utils::SRWLockExclusive lock(utils::g_seen_exception_addresses_lock);
    auto result = g_seen_exception_addresses.insert(address);
    return !result.second;  // true if already existed (insert failed)
}

// Helper function to print process information
void PrintProcessInfo() {
    try {
        LogInfo("=== PROCESS INFORMATION ===");

        // Process ID
        DWORD process_id = GetCurrentProcessId();
        std::ostringstream pid_msg;
        pid_msg << "Process ID: " << process_id;
        LogInfo("%s", pid_msg.str().c_str());

        // Thread ID
        DWORD thread_id = GetCurrentThreadId();
        std::ostringstream tid_msg;
        tid_msg << "Thread ID: " << thread_id;
        LogInfo("%s", tid_msg.str().c_str());

        // Process executable path
        wchar_t process_path[MAX_PATH];
        if (GetModuleFileNameW(nullptr, process_path, MAX_PATH) != 0) {
            int size_needed = WideCharToMultiByte(CP_UTF8, 0, process_path, -1, nullptr, 0, nullptr, nullptr);
            if (size_needed > 0) {
                std::vector<char> buffer(size_needed);
                WideCharToMultiByte(CP_UTF8, 0, process_path, -1, buffer.data(), size_needed, nullptr, nullptr);
                std::ostringstream path_msg;
                path_msg << "Process Path: " << buffer.data();
                LogInfo("%s", path_msg.str().c_str());
            }
        }

        // Command line
        LPSTR command_line = GetCommandLineA();
        if (command_line != nullptr) {
            std::ostringstream cmd_msg;
            cmd_msg << "Command Line: " << command_line;
            LogInfo("%s", cmd_msg.str().c_str());
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
            LogInfo("%s", mem_msg.str().c_str());
        }

        LogInfo("=== END PROCESS INFORMATION ===");
    } catch (...) {
        LogInfo("=== PROCESS INFORMATION ERROR ===");
        LogInfo("Exception occurred while gathering process information");
        LogInfo("=== END PROCESS INFORMATION ===");
    }
}

// Helper function to print system information
void PrintSystemInfo() {
    try {
        LogInfo("=== SYSTEM INFORMATION ===");

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
                    LogInfo("%s", os_msg.str().c_str());
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
        LogInfo("%s", cpu_msg.str().c_str());

        // System memory (more detailed)
        MEMORYSTATUSEX mem_status = {};
        mem_status.dwLength = sizeof(mem_status);
        if (GlobalMemoryStatusEx(&mem_status) != 0) {
            std::ostringstream mem_msg;
            mem_msg << "System Memory - Total: " << (mem_status.ullTotalPhys / 1024 / 1024 / 1024) << " GB, "
                    << "Available: " << (mem_status.ullAvailPhys / 1024 / 1024 / 1024) << " GB, "
                    << "Load: " << mem_status.dwMemoryLoad << "%";
            LogInfo("%s", mem_msg.str().c_str());
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
            LogInfo("%s", time_msg.str().c_str());
        }

        LogInfo("=== END SYSTEM INFORMATION ===");
    } catch (...) {
        LogInfo("=== SYSTEM INFORMATION ERROR ===");
        LogInfo("Exception occurred while gathering system information");
        LogInfo("=== END SYSTEM INFORMATION ===");
    }
}

// Helper function to print version information
void PrintVersionInfo() {
    try {
        LogInfo("=== VERSION INFORMATION ===");

        // Display Commander version
        LogInfo("%s", DISPLAY_COMMANDER_FULL_VERSION);

        LogInfo("=== END VERSION INFORMATION ===");
    } catch (...) {
        LogInfo("=== VERSION INFORMATION ERROR ===");
        LogInfo("Exception occurred while gathering version information");
        LogInfo("=== END VERSION INFORMATION ===");
    }
}

// Helper function to print list of loaded modules
void PrintLoadedModules() {
    try {
        LogInfo("=== LOADED MODULES ===");

        HANDLE process_handle = GetCurrentProcess();
        HMODULE modules[1024];
        DWORD bytes_needed;

        if (EnumProcessModules(process_handle, modules, sizeof(modules), &bytes_needed) == 0) {
            std::ostringstream error_msg;
            error_msg << "Failed to enumerate process modules - Error: " << GetLastError();
            LogInfo("%s", error_msg.str().c_str());
            LogInfo("=== END LOADED MODULES ===");
            return;
        }

        DWORD module_count = bytes_needed / sizeof(HMODULE);
        std::ostringstream count_msg;
        count_msg << "Total loaded modules: " << module_count;
        LogInfo("%s", count_msg.str().c_str());

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
                    LogInfo("%s", module_msg.str().c_str());
                }
            } else {
                std::ostringstream module_msg;
                module_msg << "  [" << i << "] <Unknown Module> (Handle: 0x" << std::hex << std::uppercase
                           << reinterpret_cast<uintptr_t>(modules[i]) << ")";
                LogInfo("%s", module_msg.str().c_str());
            }
        }

        LogInfo("=== END LOADED MODULES ===");
    } catch (...) {
        LogInfo("=== LOADED MODULES ERROR ===");
        LogInfo("Exception occurred while enumerating loaded modules");
        LogInfo("=== END LOADED MODULES ===");
    }
}

// Shared crash report: header, optional section context, version/system/process info,
// exception details, memory load, recent detour calls, undestroyed guards, stack trace, loaded modules.
void LogCrashReport(PEXCEPTION_POINTERS exception_info, const char* header_line, bool log_section_context) {
    LogInfo("%s", header_line);

    if (log_section_context) {
        const char* monitoring_section = g_continuous_monitoring_section.load(std::memory_order_acquire);
        const char* rendering_section = g_rendering_ui_section.load(std::memory_order_acquire);
        std::ostringstream section_msg;
        section_msg << "g_continuous_monitoring_section: "
                    << (monitoring_section != nullptr ? monitoring_section : "(null)");
        LogInfo("%s", section_msg.str().c_str());
        section_msg.str("");
        section_msg << "g_rendering_ui_section: " << (rendering_section != nullptr ? rendering_section : "(null)");
        LogInfo("%s", section_msg.str().c_str());
    }

    PrintVersionInfo();
    PrintSystemInfo();
    PrintProcessInfo();

    if (exception_info && exception_info->ExceptionRecord) {
        const EXCEPTION_RECORD* rec = exception_info->ExceptionRecord;
        std::ostringstream oss;
        oss << "Exception Code: 0x" << std::hex << std::uppercase << rec->ExceptionCode;
        LogInfo("%s", oss.str().c_str());
        oss.str("");
        oss << "Exception Flags: 0x" << std::hex << std::uppercase << rec->ExceptionFlags;
        LogInfo("%s", oss.str().c_str());
        oss.str("");
        oss << "Exception Address: 0x" << std::hex << std::uppercase
            << reinterpret_cast<uintptr_t>(rec->ExceptionAddress);
        LogInfo("%s", oss.str().c_str());
    }

    MEMORYSTATUSEX mem_status = {};
    mem_status.dwLength = sizeof(mem_status);
    if (GlobalMemoryStatusEx(&mem_status)) {
        std::ostringstream mem_details;
        mem_details << "System Memory Load: " << mem_status.dwMemoryLoad << "%";
        LogInfo("%s", mem_details.str().c_str());
    }

    uint64_t crash_timestamp_ns = utils::get_real_time_ns();
    std::string recent_detour_info = detour_call_tracker::FormatRecentDetourCalls(crash_timestamp_ns, 256);
    LogInfo("=== RECENT DETOUR CALLS ===");
    exit_handler::WriteMultiLineToDebugLog(recent_detour_info, "Recent Detour Calls: <none recorded>");
    LogInfo("=== END RECENT DETOUR CALLS ===");

    std::string undestroyed_guards_info = detour_call_tracker::FormatUndestroyedGuards(crash_timestamp_ns);
    LogInfo("=== UNDESTROYED DETOUR GUARDS (CRASH DETECTION) ===");
    exit_handler::WriteMultiLineToDebugLog(undestroyed_guards_info, "Undestroyed Detour Guards: 0");
    LogInfo("=== END UNDESTROYED DETOUR GUARDS ===");

    LogInfo("=== GENERATING STACK TRACE ===");
    CONTEXT* exception_context =
        (exception_info && exception_info->ContextRecord) ? exception_info->ContextRecord : nullptr;
    auto stack_trace = stack_trace::GenerateStackTrace(exception_context);
    LogInfo("=== STACK TRACE ===");
    for (const auto& frame : stack_trace) {
        LogInfo("%s", frame.c_str());
    }
    LogInfo("=== END STACK TRACE ===");

    PrintLoadedModules();

    display_commander::logger::FlushLogs();
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
            // LogInfo("%s", skip_msg.str().c_str());

            // Do not chain to ReShade (or other) crash handler
            return EXCEPTION_EXECUTE_HANDLER;
        }
    }

    dbghelp_loader::LoadDbgHelp();

    LogCrashReport(exception_info, "=== CRASH DETECTED - DETAILED CRASH REPORT ===", false);

    return EXCEPTION_EXECUTE_HANDLER;
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
            LogInfo("%s", skip_msg.str().c_str());
            return EXCEPTION_CONTINUE_SEARCH;
        }
    }

    dbghelp_loader::LoadDbgHelp();

    LogCrashReport(ex, "=== VECTORED EXCEPTION HANDLER - CRASH DETECTED ===", true);

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
    LogInfo("Installing SEH unhandled exception filter");
    g_last_detour_handler = ::SetUnhandledExceptionFilter(&UnhandledExceptionHandler);

    // Install vectored exception handler to catch exceptions early (before SetUnhandledExceptionFilter)
    // This allows us to print stack traces for all crashes, similar to ReShade's approach
    // First parameter (1) means this handler is called first (before other handlers)
    LogInfo("Installing vectored exception handler");
    g_vectored_exception_handler_handle = ::AddVectoredExceptionHandler(1, &VectoredExceptionHandler);
    if (g_vectored_exception_handler_handle == nullptr) {
        LogInfo("Failed to install vectored exception handler");
    } else {
        LogInfo("Vectored exception handler installed successfully");
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
        LogInfo("Vectored exception handler removed");
    }

    // Clear seen exception addresses
    {
        utils::SRWLockExclusive lock(utils::g_seen_exception_addresses_lock);
        g_seen_exception_addresses.clear();
    }
}

}  // namespace process_exit_hooks
