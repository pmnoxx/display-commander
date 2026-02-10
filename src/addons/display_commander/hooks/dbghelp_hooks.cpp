#include "dbghelp_hooks.hpp"
#include "../dbghelp_loader.hpp"
#include "../utils/logging.hpp"
#include "hook_suppression_manager.hpp"
#include <MinHook.h>
#include <dbghelp.h>
#include <iomanip>
#include <sstream>
#include <vector>

namespace {

using StackWalk64_pfn = BOOL(WINAPI*)(DWORD, HANDLE, HANDLE, LPSTACKFRAME64, PVOID,
                                      PREAD_PROCESS_MEMORY_ROUTINE64,
                                      PFUNCTION_TABLE_ACCESS_ROUTINE64,
                                      PGET_MODULE_BASE_ROUTINE64,
                                      PTRANSLATE_ADDRESS_ROUTINE64);

// StackWalkEx (Windows 8+): same as StackWalk64 but LPSTACKFRAME_EX and extra DWORD Flags
using StackWalkEx_pfn = BOOL(WINAPI*)(DWORD, HANDLE, HANDLE, LPSTACKFRAME_EX, PVOID,
                                     PREAD_PROCESS_MEMORY_ROUTINE64,
                                     PFUNCTION_TABLE_ACCESS_ROUTINE64,
                                     PGET_MODULE_BASE_ROUTINE64,
                                     PTRANSLATE_ADDRESS_ROUTINE64,
                                     DWORD);

StackWalk64_pfn StackWalk64_Trampoline = nullptr;
StackWalkEx_pfn StackWalkEx_Trampoline = nullptr;

thread_local std::vector<DWORD64> s_collected_pcs;

// Format a single PC using DbgHelp symbol APIs (same style as stack_trace)
static std::string FormatPc(HANDLE process, DWORD64 address, int frame_index) {
    std::ostringstream oss;
    oss << "[" << std::setfill('0') << std::setw(2) << frame_index << "] ";

    std::string module_name = "Unknown";
    if (dbghelp_loader::SymGetModuleInfo64_Original) {
        IMAGEHLP_MODULE64 module_info = {};
        module_info.SizeOfStruct = sizeof(IMAGEHLP_MODULE64);
        if (dbghelp_loader::SymGetModuleInfo64_Original(process, address, &module_info) != FALSE) {
            module_name = module_info.ModuleName;
        }
    }
    oss << module_name << "!";

    std::string symbol_name = "Unknown";
    if (dbghelp_loader::SymFromAddr_Original) {
        constexpr size_t SYMBOL_BUFFER_SIZE = 1024;
        char symbol_buffer[sizeof(SYMBOL_INFO) + SYMBOL_BUFFER_SIZE] = {};
        PSYMBOL_INFO symbol_info = reinterpret_cast<PSYMBOL_INFO>(symbol_buffer);
        symbol_info->SizeOfStruct = sizeof(SYMBOL_INFO);
        symbol_info->MaxNameLen = SYMBOL_BUFFER_SIZE;
        DWORD64 displacement = 0;
        if (dbghelp_loader::SymFromAddr_Original(process, address, &displacement, symbol_info) != FALSE) {
            symbol_name = symbol_info->Name;
        }
    }
    oss << symbol_name;

    if (dbghelp_loader::SymGetLineFromAddr64_Original) {
        IMAGEHLP_LINE64 line_info = {};
        line_info.SizeOfStruct = sizeof(IMAGEHLP_LINE64);
        DWORD displacement = 0;
        if (dbghelp_loader::SymGetLineFromAddr64_Original(process, address, &displacement, &line_info) != FALSE) {
            oss << " (" << line_info.FileName << ":" << line_info.LineNumber << ")";
        }
    }

    oss << " [0x" << std::hex << std::uppercase << address << "]";
    return oss.str();
}

static void LogCollectedStackWalk(HANDLE process) {
    if (s_collected_pcs.empty()) {
        return;
    }
    DWORD thread_id = GetCurrentThreadId();
    LogInfo("[DbgHelp stack query] TID %lu, %zu frames:", static_cast<unsigned long>(thread_id), s_collected_pcs.size());
    for (size_t i = 0; i < s_collected_pcs.size(); ++i) {
        std::string line = FormatPc(process, s_collected_pcs[i], static_cast<int>(i));
        LogInfo("  %s", line.c_str());
    }
    s_collected_pcs.clear();
}

BOOL WINAPI StackWalk64_Detour(DWORD machine_type,
                               HANDLE h_process,
                               HANDLE h_thread,
                               LPSTACKFRAME64 stack_frame,
                               PVOID context,
                               PREAD_PROCESS_MEMORY_ROUTINE64 read_memory_routine,
                               PFUNCTION_TABLE_ACCESS_ROUTINE64 function_table_access_routine,
                               PGET_MODULE_BASE_ROUTINE64 get_module_base_routine,
                               PTRANSLATE_ADDRESS_ROUTINE64 translate_address_routine) {
    if (!StackWalk64_Trampoline) {
        return FALSE;
    }

    if (dbghelp_loader::GetSuppressStackWalkLogging()) {
        return StackWalk64_Trampoline(machine_type, h_process, h_thread, stack_frame, context,
                                      read_memory_routine, function_table_access_routine,
                                      get_module_base_routine, translate_address_routine);
    }

    // Record current frame PC before calling original (original updates frame to next)
    if (stack_frame && stack_frame->AddrPC.Offset != 0) {
        s_collected_pcs.push_back(stack_frame->AddrPC.Offset);
    }

    BOOL result = StackWalk64_Trampoline(machine_type, h_process, h_thread, stack_frame, context,
                                         read_memory_routine, function_table_access_routine,
                                         get_module_base_routine, translate_address_routine);

    if (result == FALSE) {
        // End of walk: log the full trace and clear
        LogCollectedStackWalk(h_process);
    }

    return result;
}

BOOL WINAPI StackWalkEx_Detour(DWORD machine_type,
                              HANDLE h_process,
                              HANDLE h_thread,
                              LPSTACKFRAME_EX stack_frame,
                              PVOID context,
                              PREAD_PROCESS_MEMORY_ROUTINE64 read_memory_routine,
                              PFUNCTION_TABLE_ACCESS_ROUTINE64 function_table_access_routine,
                              PGET_MODULE_BASE_ROUTINE64 get_module_base_routine,
                              PTRANSLATE_ADDRESS_ROUTINE64 translate_address_routine,
                              DWORD flags) {
    if (!StackWalkEx_Trampoline) {
        return FALSE;
    }

    if (dbghelp_loader::GetSuppressStackWalkLogging()) {
        return StackWalkEx_Trampoline(machine_type, h_process, h_thread, stack_frame, context,
                                      read_memory_routine, function_table_access_routine,
                                      get_module_base_routine, translate_address_routine, flags);
    }

    if (stack_frame && stack_frame->AddrPC.Offset != 0) {
        s_collected_pcs.push_back(stack_frame->AddrPC.Offset);
    }

    BOOL result = StackWalkEx_Trampoline(machine_type, h_process, h_thread, stack_frame, context,
                                         read_memory_routine, function_table_access_routine,
                                         get_module_base_routine, translate_address_routine, flags);

    if (result == FALSE) {
        LogCollectedStackWalk(h_process);
    }

    return result;
}

} // namespace

bool InstallDbgHelpHooks(HMODULE dbghelp_module) {
    if (!dbghelp_module) {
        return false;
    }

    if (display_commanderhooks::HookSuppressionManager::GetInstance().ShouldSuppressHook(
            display_commanderhooks::HookType::DBGHELP)) {
        LogInfo("DbgHelp hooks installation suppressed by user setting");
        return false;
    }

    void* stack_walk64_target = reinterpret_cast<void*>(GetProcAddress(dbghelp_module, "StackWalk64"));
    if (!stack_walk64_target) {
        LogInfo("DbgHelp hooks: StackWalk64 not found in dbghelp.dll");
        return false;
    }

    if (MH_CreateHook(stack_walk64_target, reinterpret_cast<LPVOID>(&StackWalk64_Detour),
                      reinterpret_cast<LPVOID*>(&StackWalk64_Trampoline)) != MH_OK) {
        LogInfo("DbgHelp hooks: MH_CreateHook(StackWalk64) failed");
        return false;
    }

    if (MH_EnableHook(stack_walk64_target) != MH_OK) {
        LogInfo("DbgHelp hooks: MH_EnableHook(StackWalk64) failed");
        return false;
    }

    LogInfo("DbgHelp hooks: StackWalk64 hook installed - stack trace queries will be logged");
    display_commanderhooks::HookSuppressionManager::GetInstance().MarkHookInstalled(
        display_commanderhooks::HookType::DBGHELP);

    // StackWalkEx (Windows 8+): extended stack walk API used by some runtimes
    void* stack_walk_ex_target = reinterpret_cast<void*>(GetProcAddress(dbghelp_module, "StackWalkEx"));
    if (stack_walk_ex_target) {
        if (MH_CreateHook(stack_walk_ex_target, reinterpret_cast<LPVOID>(&StackWalkEx_Detour),
                          reinterpret_cast<LPVOID*>(&StackWalkEx_Trampoline)) == MH_OK &&
            MH_EnableHook(stack_walk_ex_target) == MH_OK) {
            LogInfo("DbgHelp hooks: StackWalkEx hook installed");
        }
    }

    return true;
}
