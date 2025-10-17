/*
 * Enhanced ReShade Injector
 * Based on the original ReShade injector by Patrick Mours
 * Enhanced to support multiple target executables and continuous monitoring
 *
 * Copyright (C) 2014 Patrick Mours
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Enhanced version adds:
 * - Configuration file support for multiple target executables
 * - Continuous monitoring without termination
 * - Process tracking to avoid duplicate injections
 * - Enhanced logging and error handling
 */

#include <stdio.h>
#include <Windows.h>
#include <sddl.h>
#include <AclAPI.h>
#include <TlHelp32.h>
#include <vector>
#include <string>
#include <unordered_set>
#include <fstream>
#include <chrono>
#include <array>

#define RESHADE_LOADING_THREAD_FUNC 1

struct loading_data
{
    WCHAR load_path[MAX_PATH] = L"";
    decltype(&GetLastError) GetLastError = nullptr;
    decltype(&LoadLibraryW) LoadLibraryW = nullptr;
    const WCHAR env_var_name[30] = L"RESHADE_DISABLE_LOADING_CHECK";
    const WCHAR env_var_value[2] = L"1";
    decltype(&SetEnvironmentVariableW) SetEnvironmentVariableW = nullptr;
};

struct scoped_handle
{
    HANDLE handle;

    scoped_handle() :
        handle(INVALID_HANDLE_VALUE) {}
    scoped_handle(HANDLE handle) :
        handle(handle) {}
    scoped_handle(scoped_handle &&other) :
        handle(other.handle) {
        other.handle = NULL;
    }
    ~scoped_handle() { if (handle != NULL && handle != INVALID_HANDLE_VALUE) CloseHandle(handle); }

    operator HANDLE() const { return handle; }

    HANDLE *operator&() { return &handle; }
    const HANDLE *operator&() const { return &handle; }
};

struct TargetProcess
{
    std::wstring exe_name;
    std::wstring display_name;
    bool enabled;
    std::unordered_set<DWORD> injected_pids; // Track injected PIDs to avoid duplicates
};

// Forward declarations
static void update_acl_for_uwp(LPWSTR path);
#if RESHADE_LOADING_THREAD_FUNC
static DWORD WINAPI loading_thread_func(loading_data *arg);
#endif

class EnhancedInjector
{
private:
    std::vector<TargetProcess> targets;
    std::wstring config_file;
    std::wstring reshade_dll_path;
    bool running;
    bool verbose_logging;

    void LogMessage(const std::wstring& message)
    {
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);
        struct tm tm;
        localtime_s(&tm, &time_t);

        wprintf(L"[%02d:%02d:%02d] %s\n",
            tm.tm_hour, tm.tm_min, tm.tm_sec, message.c_str());
    }

    void LogError(const std::wstring& message, DWORD error_code = 0)
    {
        if (error_code != 0)
        {
            LogMessage(L"ERROR: " + message + L" (Error: " + std::to_wstring(error_code) + L")");
        }
        else
        {
            LogMessage(L"ERROR: " + message);
        }
    }

    bool LoadConfiguration()
    {
        // Default config file in the same directory as the injector
        wchar_t module_path[MAX_PATH];
        GetModuleFileNameW(nullptr, module_path, MAX_PATH);
        std::wstring module_dir = module_path;
        size_t last_slash = module_dir.find_last_of(L"\\/");
        if (last_slash != std::wstring::npos)
        {
            module_dir = module_dir.substr(0, last_slash);
        }
        config_file = module_dir + L"\\injector_config.ini";

        std::wifstream file(config_file);
        if (!file.is_open())
        {
            // Create default configuration file
            CreateDefaultConfig();
            return LoadConfiguration();
        }

        targets.clear();
        std::wstring line;
        int line_number = 0;

        while (std::getline(file, line))
        {
            line_number++;

            // Skip empty lines and comments
            if (line.empty() || line[0] == L'#' || line[0] == L';')
                continue;

            // Remove whitespace
            line.erase(0, line.find_first_not_of(L" \t"));
            line.erase(line.find_last_not_of(L" \t") + 1);

            // Parse [Targets] section
            if (line == L"[Targets]")
            {
                while (std::getline(file, line) && !line.empty() && line[0] != L'[')
                {
                    line_number++;

                    // Skip comments
                    if (line[0] == L'#' || line[0] == L';')
                        continue;

                    // Check for Games=[...] format
                    if (line.find(L"Games=[") != std::wstring::npos)
                    {
                        std::wstring gamesList;
                        size_t start = line.find(L'[');

                        if (start != std::wstring::npos)
                        {
                            // Check if it's single line format: Games=[game1, game2]
                            size_t end = line.find(L']', start);
                            if (end != std::wstring::npos)
                            {
                                gamesList = line.substr(start + 1, end - start - 1);
                            }
                            else
                            {
                                // Multiline format: Games=[\n  game1,\n  game2\n]
                                gamesList = line.substr(start + 1);

                                // Continue reading lines until we find the closing ]
                                while (std::getline(file, line) && line.find(L']') == std::wstring::npos)
                                {
                                    line_number++;
                                    gamesList += L" " + line;
                                }

                                // Remove the closing ] from the last line
                                if (line.find(L']') != std::wstring::npos)
                                {
                                    size_t endPos = line.find(L']');
                                    gamesList += L" " + line.substr(0, endPos);
                                }
                            }

                            // Split by comma and process each game
                            size_t pos = 0;
                            while (pos < gamesList.length())
                            {
                                size_t comma = gamesList.find(L',', pos);
                                std::wstring game = gamesList.substr(pos, comma - pos);

                                // Remove whitespace
                                game.erase(0, game.find_first_not_of(L" \t"));
                                game.erase(game.find_last_not_of(L" \t") + 1);

                                if (!game.empty())
                                {
                                    TargetProcess target;
                                    target.exe_name = game;
                                    target.display_name = game.substr(0, game.find_last_of(L'.'));
                                    target.enabled = true;
                                    targets.push_back(target);
                                }

                                if (comma == std::wstring::npos) break;
                                pos = comma + 1;
                            }
                        }
                    }
                    else
                    {
                        // Handle traditional key=value format
                        size_t equals_pos = line.find(L'=');
                        if (equals_pos != std::wstring::npos)
                        {
                            std::wstring key = line.substr(0, equals_pos);
                            std::wstring value = line.substr(equals_pos + 1);

                            // Remove whitespace
                            key.erase(0, key.find_first_not_of(L" \t"));
                            key.erase(key.find_last_not_of(L" \t") + 1);
                            value.erase(0, value.find_first_not_of(L" \t"));
                            value.erase(value.find_last_not_of(L" \t") + 1);

                            TargetProcess target;
                            target.exe_name = value;
                            target.display_name = key;
                            target.enabled = true;
                            targets.push_back(target);
                        }
                    }
                }
            }
            // Parse [Settings] section
            else if (line == L"[Settings]")
            {
                while (std::getline(file, line) && !line.empty() && line[0] != L'[')
                {
                    line_number++;

                    if (line[0] == L'#' || line[0] == L';')
                        continue;

                    size_t equals_pos = line.find(L'=');
                    if (equals_pos != std::wstring::npos)
                    {
                        std::wstring key = line.substr(0, equals_pos);
                        std::wstring value = line.substr(equals_pos + 1);

                        key.erase(0, key.find_first_not_of(L" \t"));
                        key.erase(key.find_last_not_of(L" \t") + 1);
                        value.erase(0, value.find_first_not_of(L" \t"));
                        value.erase(value.find_last_not_of(L" \t") + 1);

                        if (key == L"verbose_logging")
                        {
                            verbose_logging = (value == L"true" || value == L"1");
                        }
                        else if (key == L"reshade_dll_path")
                        {
                            reshade_dll_path = value;
                        }
                    }
                }
            }
        }

        file.close();

        if (targets.empty())
        {
            LogError(L"No target executables configured. Please check " + config_file);
            return false;
        }

        LogMessage(L"Loaded " + std::to_wstring(targets.size()) + L" target executables from configuration");
        return true;
    }

    void CreateDefaultConfig()
    {
        std::wofstream file(config_file);
        if (file.is_open())
        {
            file << L"# Enhanced ReShade Injector Configuration\n";
            file << L"# Add your target executables in the [Targets] section\n";
            file << L"# Format: display_name=executable_name.exe\n\n";

            file << L"[Settings]\n";
            file << L"verbose_logging=true\n";
            file << L"reshade_dll_path=\n\n";

            file << L"[Targets]\n";
            file << L"# Example targets (uncomment and modify as needed):\n";
            file << L"# Game1=game1.exe\n";
            file << L"# Game2=game2.exe\n";
            file << L"# Game3=game3.exe\n";

            file.close();
            LogMessage(L"Created default configuration file: " + config_file);
        }
        else
        {
            LogError(L"Failed to create default configuration file: " + config_file);
        }
    }

    void SetupReShadePath()
    {
        if (!reshade_dll_path.empty())
        {
            // Use custom path from config
            return;
        }

        // Use module directory, same as original injector
        wchar_t module_path[MAX_PATH];
        GetModuleFileNameW(nullptr, module_path, MAX_PATH);
        std::wstring module_dir = module_path;
        size_t last_slash = module_dir.find_last_of(L"\\/");
        if (last_slash != std::wstring::npos)
        {
            module_dir = module_dir.substr(0, last_slash);
        }

        reshade_dll_path = module_dir + L"\\ReShade64.dll"; // Default to 64-bit
    }

    bool IsProcessRunning(DWORD pid)
    {
        const scoped_handle snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snapshot == INVALID_HANDLE_VALUE)
            return false;

        PROCESSENTRY32W process = { sizeof(process) };
        for (BOOL next = Process32FirstW(snapshot, &process); next; next = Process32NextW(snapshot, &process))
        {
            if (process.th32ProcessID == pid)
                return true;
        }
        return false;
    }

    void CleanupInjectedPids()
    {
        for (auto& target : targets)
        {
            auto it = target.injected_pids.begin();
            while (it != target.injected_pids.end())
            {
                if (!IsProcessRunning(*it))
                {
                    if (verbose_logging)
                    {
                        LogMessage(L"Process " + target.display_name + L" (PID " +
                                 std::to_wstring(*it) + L") has terminated, removing from tracking");
                    }
                    it = target.injected_pids.erase(it);
                }
                else
                {
                    ++it;
                }
            }
        }
    }

    bool InjectIntoProcess(DWORD pid, const TargetProcess& target)
    {
        if (verbose_logging)
        {
            LogMessage(L"Attempting to inject into " + target.display_name + L" (PID " + std::to_wstring(pid) + L")");
        }

        // Wait a bit for the application to initialize
        Sleep(50);

        // Open target application process
        const scoped_handle remote_process = OpenProcess(
            PROCESS_CREATE_THREAD | PROCESS_VM_OPERATION | PROCESS_VM_READ |
            PROCESS_VM_WRITE | PROCESS_QUERY_INFORMATION, FALSE, pid);

        if (remote_process == nullptr)
        {
            LogError(L"Failed to open target application process (PID " + std::to_wstring(pid) + L")", GetLastError());
            return false;
        }

        // Check process architecture
        BOOL remote_is_wow64 = FALSE;
        IsWow64Process(remote_process, &remote_is_wow64);
#ifndef _WIN64
        if (remote_is_wow64 == FALSE)
#else
        if (remote_is_wow64 != FALSE)
#endif
        {
            LogError(L"Process architecture does not match injection tool (PID " + std::to_wstring(pid) + L")");
            return false;
        }

        // Setup loading data
        loading_data arg;
        wcscpy_s(arg.load_path, reshade_dll_path.c_str());

        // Adjust DLL name based on architecture
        std::wstring dll_name = remote_is_wow64 ? L"ReShade32.dll" : L"ReShade64.dll";
        size_t last_slash = reshade_dll_path.find_last_of(L"\\/");
        if (last_slash != std::wstring::npos)
        {
            arg.load_path[last_slash + 1] = L'\0';
        }
        wcscat_s(arg.load_path, dll_name.c_str());

        if (GetFileAttributesW(arg.load_path) == INVALID_FILE_ATTRIBUTES)
        {
            LogError(L"Failed to find ReShade DLL at \"" + std::wstring(arg.load_path) + L"\"");
            return false;
        }

        // Make sure the DLL has permissions set up for 'ALL_APPLICATION_PACKAGES'
        update_acl_for_uwp(arg.load_path);

        // Setup function pointers
        arg.GetLastError = GetLastError;
        arg.LoadLibraryW = LoadLibraryW;
        arg.SetEnvironmentVariableW = SetEnvironmentVariableW;

        // Allocate memory in target process
#if RESHADE_LOADING_THREAD_FUNC
        const auto loading_thread_func_size = 256;
#else
        const auto loading_thread_func_size = 0;
#endif
        const auto load_param = VirtualAllocEx(remote_process, nullptr,
            loading_thread_func_size + sizeof(arg), MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);

#if RESHADE_LOADING_THREAD_FUNC
        const auto loading_thread_func_address = static_cast<LPBYTE>(load_param) + sizeof(arg);
#else
        const auto loading_thread_func_address = arg.LoadLibraryW;
#endif

        // Write thread entry point function and 'LoadLibrary' call argument to target application
        if (load_param == nullptr
            || !WriteProcessMemory(remote_process, load_param, &arg, sizeof(arg), nullptr)
#if RESHADE_LOADING_THREAD_FUNC
            || !WriteProcessMemory(remote_process, loading_thread_func_address, loading_thread_func, loading_thread_func_size, nullptr)
#endif
            )
        {
            LogError(L"Failed to allocate and write 'LoadLibrary' argument in target application (PID " + std::to_wstring(pid) + L")", GetLastError());
            return false;
        }

        // Execute 'LoadLibrary' in target application
        const scoped_handle load_thread = CreateRemoteThread(remote_process, nullptr, 0,
            reinterpret_cast<LPTHREAD_START_ROUTINE>(loading_thread_func_address), load_param, 0, nullptr);

        if (load_thread == nullptr)
        {
            LogError(L"Failed to execute 'LoadLibrary' in target application (PID " + std::to_wstring(pid) + L")", GetLastError());
            return false;
        }

        // Wait for loading to finish and clean up parameter memory afterwards
        WaitForSingleObject(load_thread, INFINITE);
        VirtualFreeEx(remote_process, load_param, 0, MEM_RELEASE);

        // Check if injection was successful
        DWORD exit_code;
        if (GetExitCodeThread(load_thread, &exit_code) &&
#if RESHADE_LOADING_THREAD_FUNC
            exit_code == ERROR_SUCCESS)
#else
            exit_code != NULL)
#endif
        {
            LogMessage(L"Successfully injected ReShade into " + target.display_name + L" (PID " + std::to_wstring(pid) + L")");
            return true;
        }
        else
        {
#if RESHADE_LOADING_THREAD_FUNC
            LogError(L"Failed to load ReShade in target application (PID " + std::to_wstring(pid) + L"). Error code: 0x" +
                    std::to_wstring(exit_code), exit_code);
#else
            LogError(L"Failed to load ReShade in target application (PID " + std::to_wstring(pid) + L")");
#endif
            return false;
        }
    }

public:
    EnhancedInjector() : running(false), verbose_logging(true) {}

    bool Initialize()
    {
        LogMessage(L"Enhanced ReShade Injector starting...");

        if (!LoadConfiguration())
        {
            return false;
        }

        SetupReShadePath();

        LogMessage(L"Monitoring " + std::to_wstring(targets.size()) + L" target executables");
        for (auto& target : targets)
        {
            LogMessage(L"Target: " + target.display_name + L" (PID " + std::to_wstring(target.injected_pids.size()) + L")");
        }
        LogMessage(L"Press Ctrl+C to stop monitoring");

        return true;
    }

    void Run()
    {
        running = true;

        std::array<bool, 65536> process_seen = {};


        while (running)
        {
            // Clean up terminated processes from tracking
            CleanupInjectedPids();

            // Check for new processes
            const scoped_handle snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
            if (snapshot != INVALID_HANDLE_VALUE)
            {
                PROCESSENTRY32W process = { sizeof(process) };
                for (BOOL next = Process32FirstW(snapshot, &process); next; next = Process32NextW(snapshot, &process))
                {
                    auto pid = process.th32ProcessID;
                    if (process_seen[pid]) continue;
                    process_seen[pid] = true;

                //  ..  LogMessage(L"Process: " + std::wstring(process.szExeFile) + L" (PID " + std::to_wstring(process.th32ProcessID) + L")");
                    if (!running) break; // Check for stop signal

                    for (auto& target : targets)
                    {
                        // arguments target.exe_name
                        //LogMessage(L"Arguments: " + target.exe_name + L" " + process.szExeFile);
                       // if (!target.enabled) continue;

                        // Check if this process matches our target
                        if (_wcsicmp(process.szExeFile, target.exe_name.c_str()) == 0)
                        {
                            // Check if we've already injected into this PID
                      //      if (target.injected_pids.find(process.th32ProcessID) == target.injected_pids.end())
                         //   {
                             //   if (verbose_logging)
                              //  {
                                    LogMessage(L"Found new " + target.display_name + L" process (PID " +
                                             std::to_wstring(process.th32ProcessID) + L")");
                             //   }

                                // Attempt injection
                                if (InjectIntoProcess(process.th32ProcessID, target))
                                {
                                    target.injected_pids.insert(process.th32ProcessID);
                                }
                       //     }
                        }
                    }
                }
            }

            // Sleep to avoid overburdening the CPU
            //Sleep(1000); // Check every second
        }
    }

    void Stop()
    {
        running = false;
        LogMessage(L"Stopping Enhanced ReShade Injector...");
    }
};

// Global instance for signal handling
EnhancedInjector* g_injector = nullptr;

BOOL WINAPI ConsoleCtrlHandler(DWORD dwCtrlType)
{
    if (g_injector && (dwCtrlType == CTRL_C_EVENT || dwCtrlType == CTRL_CLOSE_EVENT))
    {
        g_injector->Stop();
        return TRUE;
    }
    return FALSE;
}

static void update_acl_for_uwp(LPWSTR path)
{
    OSVERSIONINFOEX verinfo_windows7 = { sizeof(OSVERSIONINFOEX), 6, 1 };
    const bool is_windows7 = VerifyVersionInfo(&verinfo_windows7, VER_MAJORVERSION | VER_MINORVERSION,
        VerSetConditionMask(VerSetConditionMask(0, VER_MAJORVERSION, VER_EQUAL), VER_MINORVERSION, VER_EQUAL)) != FALSE;
    if (is_windows7)
        return; // There is no UWP on Windows 7, so no need to update DACL

    PACL old_acl = nullptr, new_acl = nullptr;
    PSECURITY_DESCRIPTOR sd = nullptr;
    SECURITY_INFORMATION siInfo = DACL_SECURITY_INFORMATION;

    // Get the existing DACL for the file
    if (GetNamedSecurityInfoW(path, SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, nullptr, nullptr, &old_acl, nullptr, &sd) != ERROR_SUCCESS)
        return;

    // Get the SID for 'ALL_APPLICATION_PACKAGES'
    PSID sid = nullptr;
    if (ConvertStringSidToSidW(L"S-1-15-2-1", &sid))
    {
        EXPLICIT_ACCESS access = {};
        access.grfAccessPermissions = GENERIC_READ | GENERIC_EXECUTE;
        access.grfAccessMode = SET_ACCESS;
        access.grfInheritance = SUB_CONTAINERS_AND_OBJECTS_INHERIT;
        access.Trustee.TrusteeForm = TRUSTEE_IS_SID;
        access.Trustee.TrusteeType = TRUSTEE_IS_WELL_KNOWN_GROUP;
        access.Trustee.ptstrName = reinterpret_cast<LPTCH>(sid);

        // Update the DACL with the new entry
        if (SetEntriesInAcl(1, &access, old_acl, &new_acl) == ERROR_SUCCESS)
        {
            SetNamedSecurityInfoW(path, SE_FILE_OBJECT, siInfo, NULL, NULL, new_acl, NULL);
            LocalFree(new_acl);
        }

        LocalFree(sid);
    }

    LocalFree(sd);
}

#if RESHADE_LOADING_THREAD_FUNC
static DWORD WINAPI loading_thread_func(loading_data *arg)
{
    arg->SetEnvironmentVariableW(arg->env_var_name, arg->env_var_value);
    if (arg->LoadLibraryW(arg->load_path) == NULL)
        return arg->GetLastError();
    return ERROR_SUCCESS;
}
#endif

int wmain(int argc, wchar_t *argv[])
{
    // Check for command line arguments
    if (argc > 1)
    {
        if (wcscmp(argv[1], L"--help") == 0 || wcscmp(argv[1], L"-h") == 0)
        {
            wprintf(L"Enhanced ReShade Injector\n");
            wprintf(L"Usage: %s [options]\n", argv[0]);
            wprintf(L"\nOptions:\n");
            wprintf(L"  --help, -h     Show this help message\n");
            wprintf(L"  --config FILE  Use custom configuration file\n");
            wprintf(L"\nConfiguration:\n");
            wprintf(L"  The injector will look for 'injector_config.ini' in the same directory\n");
            wprintf(L"  as the executable. If not found, it will create a default configuration.\n");
            wprintf(L"\nExample configuration:\n");
            wprintf(L"  [Settings]\n");
            wprintf(L"  verbose_logging=true\n");
            wprintf(L"  reshade_dll_path=C:\\Path\\To\\ReShade64.dll\n");
            wprintf(L"\n  [Targets]\n");
            wprintf(L"  Game1=game1.exe\n");
            wprintf(L"  Game2=game2.exe\n");
            return 0;
        }
    }

    // Set up console control handler for graceful shutdown
    SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE);

    // Create and initialize injector
    EnhancedInjector injector;
    g_injector = &injector;

    if (!injector.Initialize())
    {
        wprintf(L"Failed to initialize Enhanced ReShade Injector\n");
        return 1;
    }

    // Start monitoring
    injector.Run();

    return 0;
}
