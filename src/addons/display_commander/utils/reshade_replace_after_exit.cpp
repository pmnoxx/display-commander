// Source Code <Display Commander> // follow this order for includes in all files + add this comment at the top
#include "reshade_replace_after_exit.hpp"
#include "../globals.hpp"
#include "logging.hpp"
#include "reshade_load_path.hpp"

// Libraries <standard C++>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

// Libraries <Windows.h>
#include <Windows.h>

namespace display_commander::utils {

std::filesystem::path GetReshadeLoadedModulePath() {
    RefreshReShadeModuleIfNeeded();
    HMODULE h = g_reshade_module.load();
    if (h == nullptr) {
        return std::filesystem::path();
    }
    wchar_t buf[MAX_PATH] = {};
    if (GetModuleFileNameW(h, buf, MAX_PATH) == 0) {
        return std::filesystem::path();
    }
    return std::filesystem::path(buf);
}

bool StartReplaceWithGlobalAfterExitScript(std::string* out_error, std::string* out_script_path) {
    LogInfo("[reshade_replace] StartReplaceWithGlobalAfterExitScript: begin");
    std::filesystem::path target = GetReshadeLoadedModulePath();
    if (target.empty()) {
        LogWarn("[reshade_replace] ReShade module path unknown (g_reshade_module null or GetModuleFileName failed)");
        if (out_error) *out_error = "ReShade module path unknown.";
        return false;
    }
    LogInfo("[reshade_replace] target DLL: %s", target.string().c_str());
    std::filesystem::path global_dir = GetGlobalReshadeDirectory();
    if (global_dir.empty()) {
        LogWarn("[reshade_replace] Global ReShade directory not found");
        if (out_error) *out_error = "Global ReShade directory not found.";
        return false;
    }
    int bitness = (sizeof(void*) == 8) ? 64 : 32;
    const wchar_t* dll_name = (bitness == 32) ? L"Reshade32.dll" : L"Reshade64.dll";
    std::filesystem::path source = global_dir / dll_name;
    std::error_code ec;
    if (!std::filesystem::exists(source, ec)) {
        LogWarn("[reshade_replace] Global ReShade DLL not found: %s (error: %s)", source.string().c_str(), ec.message().c_str());
        if (out_error) *out_error = "Global ReShade DLL not found.";
        return false;
    }
    LogInfo("[reshade_replace] source DLL: %s", source.string().c_str());
    wchar_t temp_dir[MAX_PATH] = {};
    if (GetTempPathW(MAX_PATH, temp_dir) == 0) {
        DWORD gle = GetLastError();
        LogWarn("[reshade_replace] GetTempPath failed, gle=%lu", static_cast<unsigned long>(gle));
        if (out_error) *out_error = "GetTempPath failed.";
        return false;
    }
    DWORD pid = GetCurrentProcessId();
    std::filesystem::path cmd_path =
        std::filesystem::path(temp_dir) / ("dc_reshade_replace_" + std::to_string(static_cast<unsigned long>(pid)) + ".cmd");
    LogInfo("[reshade_replace] create script: %s", cmd_path.string().c_str());
    // Script: wait for PID to exit, then copy in a loop until successful. %1=PID %2=source %3=target.
    std::ostringstream script;
    script << "@echo off\r\n"
           << "setlocal\r\n"
           << "set PID=%1\r\n"
           << "set \"SRC=%~2\"\r\n"
           << "set \"DST=%~3\"\r\n"
           << ":wait\r\n"
           << "tasklist /FI \"PID eq %PID%\" 2>nul | find \"%PID%\" >nul\r\n"
           << "if not errorlevel 1 (timeout /t 2 /nobreak >nul & goto wait)\r\n"
           << ":retry\r\n"
           << "copy /Y \"%SRC%\" \"%DST%\"\r\n"
           << "if errorlevel 1 (timeout /t 2 /nobreak >nul & goto retry)\r\n"
           << "endlocal\r\n";
    std::ofstream of(cmd_path);
    if (!of) {
        LogWarn("[reshade_replace] Failed to create script file (ofstream open): %s", cmd_path.string().c_str());
        if (out_error) *out_error = "Failed to create script file.";
        return false;
    }
    of << script.str();
    if (!of) {
        LogWarn("[reshade_replace] Failed to write script file (ofstream write)");
        if (out_error) *out_error = "Failed to write script file.";
        return false;
    }
    of.close();
    std::error_code ec2;
    bool script_exists = std::filesystem::exists(cmd_path, ec2);
    LogInfo("[reshade_replace] script created, exists=%d", script_exists ? 1 : 0);
    std::wstring source_w = source.wstring();
    std::wstring target_w = target.wstring();
    std::wstring cmd_w = cmd_path.wstring();
    // cmd /c "scriptpath" pid "source" "target" & del /q "scriptpath"
    std::wstring cmd_line;
    cmd_line += L"cmd.exe /c \"";
    cmd_line += L"\"" + cmd_w + L"\" ";
    cmd_line += std::to_wstring(static_cast<unsigned long>(pid));
    cmd_line += L" \"" + source_w + L"\" \"" + target_w + L"\"";
    cmd_line += L" & del /q \"" + cmd_w + L"\"\"";
    const size_t kMaxLogCmd = 400;
    std::string cmd_line_narrow(cmd_line.begin(), cmd_line.end());
    if (cmd_line_narrow.size() > kMaxLogCmd) {
        cmd_line_narrow.resize(kMaxLogCmd);
        cmd_line_narrow += "...";
    }
    LogInfo("[reshade_replace] run script: CreateProcess cmd (truncated): %s", cmd_line_narrow.c_str());
    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi = {};
    std::vector<wchar_t> cmd_buf(cmd_line.begin(), cmd_line.end());
    cmd_buf.push_back(L'\0');
    BOOL created = CreateProcessW(nullptr, cmd_buf.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, temp_dir, &si, &pi);
    if (!created) {
        DWORD gle = GetLastError();
        LogWarn("[reshade_replace] CreateProcess failed, gle=%lu", static_cast<unsigned long>(gle));
        if (out_error) {
            *out_error = "Failed to run script (CreateProcess). Error ";
            *out_error += std::to_string(static_cast<unsigned long>(gle));
            *out_error += ". Check Display Commander log for script path.";
        }
        return false;
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    LogInfo("[reshade_replace] script started OK, PID=%lu script_pid=%lu", static_cast<unsigned long>(pid),
            static_cast<unsigned long>(pi.dwProcessId));
    if (out_script_path != nullptr) {
        *out_script_path = cmd_path.string();
    }
    return true;
}

}  // namespace display_commander::utils
