// Source Code <Display Commander> // follow this order for includes in all files + add this comment at the top
#include "dll_boot_logging.hpp"

#include "globals.hpp"
#include "utils/general_utils.hpp"
#include "utils/string_utils.hpp"

// Libraries <standard C++>
#include <cstdio>
#include <filesystem>
#include <string>
#include <string_view>

// Libraries <Windows.h>
#include <Windows.h>

namespace {
bool IsLoaderModule(const wchar_t* path) {
    if (!path || !path[0]) return false;
    const wchar_t* name = path;
    const wchar_t* last = wcsrchr(path, L'\\');
    if (last) name = last + 1;
    const wchar_t* last_slash = wcsrchr(path, L'/');
    if (last_slash && last_slash > name) name = last_slash + 1;
    return _wcsicmp(name, L"ntdll.dll") == 0 || _wcsicmp(name, L"kernel32.dll") == 0
           || _wcsicmp(name, L"kernelbase.dll") == 0 || _wcsicmp(name, L"wow64.dll") == 0
           || _wcsicmp(name, L"wow64win.dll") == 0 || _wcsicmp(name, L"wow64cpu.dll") == 0;
}

void LogBoot(const std::string& text) { AppendDisplayCommanderBootLog(text); }

}  // namespace

void ChooseAndSetDcConfigPath(HMODULE h_module) {
    std::wstring config_path_w;
    WCHAR module_path[MAX_PATH] = {};
    if (GetModuleFileNameW(h_module, module_path, MAX_PATH) > 0) {
        std::filesystem::path dll_dir = std::filesystem::path(module_path).parent_path();
        std::error_code ec;

        bool use_global_config = false;
        if (std::filesystem::is_regular_file(dll_dir / L".DC_CONFIG_GLOBAL", ec) && !ec) {
            use_global_config = true;
        } else {
            ec.clear();
            std::filesystem::path dc_root = GetDisplayCommanderAppDataRootPathNoCreate();
            if (!dc_root.empty() && std::filesystem::is_regular_file(dc_root / L".DC_CONFIG_GLOBAL", ec) && !ec) {
                use_global_config = true;
            }
        }
        if (use_global_config) {
            std::filesystem::path base = GetDisplayCommanderAppDataFolder();
            if (!base.empty()) {
                std::wstring game_name = GetGameInstallFolderNameWideFromProcess();
                if (game_name.empty()) {
                    game_name = L"Game";
                }
                config_path_w = (base / L"Games" / game_name).wstring();
            }
        }
        if (config_path_w.empty() &&
            std::filesystem::is_regular_file(dll_dir / L".DC_CONFIG_IN_DLL", ec) && !ec) {
            config_path_w = dll_dir.wstring();
        }
    }
    if (config_path_w.empty()) {
        WCHAR exe_path[MAX_PATH] = {};
        if (GetModuleFileNameW(nullptr, exe_path, MAX_PATH) == 0) return;
        WCHAR* last_slash = wcsrchr(exe_path, L'\\');
        if (last_slash == nullptr || last_slash <= exe_path) return;
        *last_slash = L'\0';
        config_path_w = exe_path;
    }
    if (config_path_w.empty()) return;
    SetEnvironmentVariableW(L"RESHADE_BASE_PATH_OVERRIDE", config_path_w.c_str());
    g_dc_config_directory.store(std::make_shared<std::wstring>(config_path_w));
}

void CaptureDllLoadCallerPath(HMODULE h_our_module) {
    try {
        void* backtrace[256] = {};
        const USHORT n =
            CaptureStackBackTrace(0, static_cast<ULONG>(sizeof(backtrace) / sizeof(backtrace[0])), backtrace, nullptr);
        wchar_t path_buf[MAX_PATH] = {};
        std::string fallback;
        bool fallback_is_loader = false;
        std::string list_buf;
        std::string last_path;
        for (USHORT i = 0; i < n; ++i) {
            HMODULE hmod = nullptr;
            if (!GetModuleHandleExW(
                    GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                    static_cast<LPCWSTR>(backtrace[i]), &hmod)
                || hmod == nullptr) {
                continue;
            }
            if (hmod == h_our_module) continue;
            if (GetModuleFileNameW(hmod, path_buf, MAX_PATH) == 0) continue;
            const std::string path_str = display_commander::utils::WideToUtf8(path_buf);
            if (path_str != last_path) {
                last_path = path_str;
                if (!list_buf.empty()) list_buf += '\n';
                list_buf += path_str;
            }
            if (fallback.empty()) {
                fallback = path_str;
                fallback_is_loader = IsLoaderModule(path_buf);
            }
            if (IsLoaderModule(path_buf)) continue;
            g_dll_load_caller_path = path_str;
            g_dll_load_call_stack_list = std::move(list_buf);
            return;
        }
        g_dll_load_call_stack_list = std::move(list_buf);
        if (!fallback_is_loader && !fallback.empty()) g_dll_load_caller_path = std::move(fallback);
    } catch (...) {
        // avoid crashing DllMain
    }
}

void LogBootDllMainStage(const char* stage_message) { LogBoot(std::string("[DllMain] ") + stage_message); }

void LogBootRegisterAndPostInitStage(const char* stage_message) {
    LogBoot(std::string("[RegisterAndPostInit] ") + stage_message);
}

void LogBootInitWithoutHwndStage(const char* stage_message) {
    LogBoot(std::string("[InitWithoutHwnd] ") + stage_message);
}

void LogBootDcConfigPath() {
    const auto dc_dir = g_dc_config_directory.load(std::memory_order_acquire);
    if (!dc_dir || dc_dir->empty()) {
        LogBoot("[DC] config path: (not set)");
        return;
    }
    LogBoot("[DC] config path: " + display_commander::utils::WideToUtf8(*dc_dir));
}

void EnsureDisplayCommanderLogWithModulePath(HMODULE h_module) {
    wchar_t module_path_buf[MAX_PATH] = {};
    if (GetModuleFileNameW(h_module, module_path_buf, MAX_PATH) == 0) return;

    const std::filesystem::path log_path =
        std::filesystem::path(module_path_buf).parent_path() / L"DisplayCommander.log";
    g_dll_main_log_path = log_path;

    const std::string module_path_utf8 = display_commander::utils::WideToUtf8(module_path_buf);
    char dbg_buf[MAX_PATH + 128];
    int dbg_len = snprintf(dbg_buf, sizeof(dbg_buf), "[DisplayCommander] [Boot] module path: %s",
                           module_path_utf8.c_str());
    if (!g_dll_load_caller_path.empty() && dbg_len >= 0 && static_cast<size_t>(dbg_len) < sizeof(dbg_buf) - 32) {
        dbg_len += snprintf(dbg_buf + dbg_len, sizeof(dbg_buf) - static_cast<size_t>(dbg_len), " [Caller] %s",
                            g_dll_load_caller_path.c_str());
    }
    if (dbg_len >= 0 && static_cast<size_t>(dbg_len) < sizeof(dbg_buf)) {
        snprintf(dbg_buf + dbg_len, sizeof(dbg_buf) - static_cast<size_t>(dbg_len), "\n");
        OutputDebugStringA(dbg_buf);
    }

    std::string module_path_line = std::string("[Boot] module path: ") + module_path_utf8;
    if (!g_dll_load_caller_path.empty()) {
        module_path_line += " [Caller] " + g_dll_load_caller_path;
    }

    LogBoot(module_path_line);
}
