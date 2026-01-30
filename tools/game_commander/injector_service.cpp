#include "injector_service.h"
#include "game_list.h"
#include "srwlock_wrapper.h"
#include <fstream>
#include <iostream>
#include <chrono>
#include <filesystem>
#include <iomanip>
#include <array>
#include <algorithm>
#include <sddl.h>
#include <AclAPI.h>

// Forward declarations
static void update_acl_for_uwp(LPWSTR path);

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

    scoped_handle() : handle(INVALID_HANDLE_VALUE) {}
    scoped_handle(HANDLE handle) : handle(handle) {}
    scoped_handle(scoped_handle &&other) : handle(other.handle) {
        other.handle = NULL;
    }
    ~scoped_handle() {
        if (handle != NULL && handle != INVALID_HANDLE_VALUE)
            CloseHandle(handle);
    }

    operator HANDLE() const { return handle; }
    HANDLE *operator&() { return &handle; }
    const HANDLE *operator&() const { return &handle; }
};

InjectorService::InjectorService()
    : running_(false), verbose_logging_(true) {
    // Set default ReShade DLL paths to run_tmp directory
    wchar_t module_path[MAX_PATH];
    GetModuleFileNameW(nullptr, module_path, MAX_PATH);
    std::wstring module_dir = module_path;
    size_t last_slash = module_dir.find_last_of(L"\\/");
    if (last_slash != std::wstring::npos) {
        module_dir = module_dir.substr(0, last_slash);
    }

    // Navigate to run_tmp directory (go up from build/tools/game_commander to run_tmp)
    std::wstring run_tmp_dir = module_dir;
    // Remove "build/tools/game_commander" and add "run_tmp"
    size_t build_pos = run_tmp_dir.find(L"\\build\\");
    if (build_pos != std::wstring::npos) {
        run_tmp_dir = run_tmp_dir.substr(0, build_pos) + L"\\run_tmp";
    } else {
        // Fallback: try to find run_tmp relative to current directory
        run_tmp_dir = module_dir + L"\\..\\..\\..\\run_tmp";
    }

    // Convert wide string to narrow string
    int size_needed = WideCharToMultiByte(CP_UTF8, 0, run_tmp_dir.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string run_tmp_dir_narrow(size_needed, 0);
    WideCharToMultiByte(CP_UTF8, 0, run_tmp_dir.c_str(), -1, &run_tmp_dir_narrow[0], size_needed, nullptr, nullptr);
    run_tmp_dir_narrow.resize(size_needed - 1); // Remove null terminator

    reshade_dll_path_32bit_ = run_tmp_dir_narrow + "\\ReShade32.dll";
    reshade_dll_path_64bit_ = run_tmp_dir_narrow + "\\ReShade64.dll";

    // Debug logging for path resolution
    if (verbose_logging_.load()) {
        logMessage("Default ReShade DLL paths set to: " + reshade_dll_path_32bit_ + " (32-bit), " + reshade_dll_path_64bit_ + " (64-bit)");
    }
}

InjectorService::~InjectorService() {
    stop();
}

bool InjectorService::start() {
    if (running_.load()) {
        return true; // Already running
    }

    if (targets_.empty()) {
        logError("No target games configured");
        return false;
    }

    if (reshade_dll_path_32bit_.empty() && reshade_dll_path_64bit_.empty()) {
        logError("ReShade DLL paths not configured");
        return false;
    }

    // Check if at least one ReShade DLL exists
    bool has_32bit = !reshade_dll_path_32bit_.empty() && std::filesystem::exists(reshade_dll_path_32bit_);
    bool has_64bit = !reshade_dll_path_64bit_.empty() && std::filesystem::exists(reshade_dll_path_64bit_);

    if (!has_32bit && !has_64bit) {
        logError("No ReShade DLL found. 32-bit: " + reshade_dll_path_32bit_ + ", 64-bit: " + reshade_dll_path_64bit_);
        return false;
    }

    running_.store(true);
    monitoring_thread_ = std::make_unique<std::thread>(&InjectorService::monitoringLoop, this);

    logMessage("Injector service started");
    return true;
}

void InjectorService::stop() {
    if (!running_.load()) {
        return;
    }

    running_.store(false);

    if (monitoring_thread_ && monitoring_thread_->joinable()) {
        monitoring_thread_->join();
        monitoring_thread_.reset();
    }

    logMessage("Injector service stopped");
}

void InjectorService::setTargetGames(const std::vector<Game>& games) {
    game_commander::SRWLockExclusive lock(targets_srwlock_);

    targets_.clear();
    for (const auto& game : games) {
        if (game.enable_reshade && !game.executable_path.empty()) {
            TargetProcess target;
            target.exe_name = std::filesystem::path(game.executable_path).filename().string();
            target.display_name = game.name.empty() ? target.exe_name : game.name;
            target.executable_path = game.executable_path;
            target.working_directory = game.working_directory;
            target.enabled = true;
            target.use_local_injection = game.use_local_injection;
            target.proxy_dll_type = static_cast<int>(game.proxy_dll_type);
            targets_.push_back(target);
        }
    }

    logMessage("Updated target games: " + std::to_string(targets_.size()) + " games with ReShade enabled");
}

void InjectorService::setReShadeDllPaths(const std::string& path_32bit, const std::string& path_64bit) {
    reshade_dll_path_32bit_ = path_32bit;
    reshade_dll_path_64bit_ = path_64bit;
}

void InjectorService::setDisplayCommanderPaths(const std::string& path_32bit, const std::string& path_64bit) {
    display_commander_path_32bit_ = path_32bit;
    display_commander_path_64bit_ = path_64bit;

    if (verbose_logging_.load()) {
        logMessage("Display Commander paths set - 32-bit: " + (path_32bit.empty() ? "not configured" : path_32bit) +
                  ", 64-bit: " + (path_64bit.empty() ? "not configured" : path_64bit));
    }
}

void InjectorService::setVerboseLogging(bool enabled) {
    verbose_logging_.store(enabled);
}

size_t InjectorService::getInjectedProcessCount() const {
    game_commander::SRWLockShared lock(targets_srwlock_);

    size_t total = 0;
    for (const auto& target : targets_) {
        total += target.injected_pids.size();
    }
    return total;
}

std::vector<std::string> InjectorService::getInjectedProcesses() const {
    game_commander::SRWLockShared lock(targets_srwlock_);

    std::vector<std::string> processes;
    for (const auto& target : targets_) {
        for (DWORD pid : target.injected_pids) {
            processes.push_back(target.display_name + " (PID: " + std::to_string(pid) + ")");
        }
    }
    return processes;
}

void InjectorService::monitoringLoop() {
    std::array<DWORD, 65536> process_last_seen = {};

    int cnt = 0;
    while (running_.load()) {
        if (cnt % 1000 == 0) {
            logMessage("Monitoring loop iteration: " + std::to_string(cnt));
        }
        cnt++;
        // Clean up terminated processes
        cleanupInjectedPids();

        // Check for new processes
        const scoped_handle snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snapshot != INVALID_HANDLE_VALUE) {
            PROCESSENTRY32W process = { sizeof(process) };
            for (BOOL next = Process32FirstW(snapshot, &process); next; next = Process32NextW(snapshot, &process)) {
                if (!running_.load()) break;

                auto pid = process.th32ProcessID;
                auto hash = std::hash<std::wstring>{}(process.szExeFile);
                if (process_last_seen[pid] == cnt - 1) {
                    process_last_seen[pid] = cnt;
                    continue;
                }
                process_last_seen[pid] = cnt;

                game_commander::SRWLockExclusive lock(targets_srwlock_);
                // TODO(pmnoxx): optimize as set if list is too long
                for (auto& target : targets_) {
                    // doesn't work turned off for now
                    // .. if (!target.enabled) continue;

                    // Convert to wide string for comparison
                    int size_needed = MultiByteToWideChar(CP_UTF8, 0, target.exe_name.c_str(), -1, nullptr, 0);
                    std::wstring exe_name_wide(size_needed, 0);
                    MultiByteToWideChar(CP_UTF8, 0, target.exe_name.c_str(), -1, &exe_name_wide[0], size_needed);
                    exe_name_wide.resize(size_needed - 1); // Remove null terminator

                    if (_wcsicmp(process.szExeFile, exe_name_wide.c_str()) == 0) {
                        // Check if we've already injected into this PID
                        if (target.injected_pids.find(pid) == target.injected_pids.end()) {
                            logMessage("Found new " + target.display_name + " process (PID " + std::to_string(pid) + ")");

                            // Attempt injection only if not using local injection
                            if (!target.use_local_injection) {
                                if (injectIntoProcess(pid, target)) {
                                    target.injected_pids.insert(pid);
                                }
                            } else {
                                // Perform local injection
                                if (performLocalInjection(target)) {
                                    target.injected_pids.insert(pid);
                                    if (verbose_logging_.load()) {
                                        logMessage("Local injection completed for " + target.display_name);
                                    }
                                } else {
                                    if (verbose_logging_.load()) {
                                        logMessage("Local injection failed for " + target.display_name);
                                    }
                                }
                            }
                            // Copy display commander addon if path is configured and not using local injection
                            if (!target.use_local_injection && (!display_commander_path_32bit_.empty() || !display_commander_path_64bit_.empty())) {
                                copyDisplayCommanderToGameFolder(pid);
                            }
                            if (verbose_logging_.load()) {
                                logMessage("Found new " + target.display_name + " process (PID " + std::to_string(pid) + ")");
                            }
                        }
                    }
                }
            }
        }

        // don't sleep to make sure we catch all new processes
        // std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    }
}

bool InjectorService::injectIntoProcess(DWORD pid, const TargetProcess& target) {
    if (verbose_logging_.load()) {
        logMessage("Attempting to inject into " + target.display_name + " (PID " + std::to_string(pid) + ")");
    }

    // Wait a bit for the application to initialize
    Sleep(50);

    // Open target application process
    const scoped_handle remote_process = OpenProcess(
        PROCESS_CREATE_THREAD | PROCESS_VM_OPERATION | PROCESS_VM_READ |
        PROCESS_VM_WRITE | PROCESS_QUERY_INFORMATION, FALSE, pid);

    if (remote_process == nullptr) {
        logError("Failed to open target application process (PID " + std::to_string(pid) + ")", GetLastError());
        return false;
    }

    // Check process architecture
    BOOL remote_is_wow64 = FALSE;
    IsWow64Process(remote_process, &remote_is_wow64);

    // Setup loading data
    loading_data arg;

    // Choose the appropriate DLL path based on architecture
    std::string reshade_path;
    if (remote_is_wow64) {
        // 32-bit process
        reshade_path = reshade_dll_path_32bit_;
    } else {
        // 64-bit process
        reshade_path = reshade_dll_path_64bit_;
    }

    if (reshade_path.empty()) {
        logError("No ReShade DLL configured for " + std::string(remote_is_wow64 ? "32-bit" : "64-bit") + " process (PID " + std::to_string(pid) + ")");
        return false;
    }

    // Convert narrow string to wide string
    int size_needed = MultiByteToWideChar(CP_UTF8, 0, reshade_path.c_str(), -1, nullptr, 0);
    std::wstring reshade_path_wide(size_needed, 0);
    MultiByteToWideChar(CP_UTF8, 0, reshade_path.c_str(), -1, &reshade_path_wide[0], size_needed);
    reshade_path_wide.resize(size_needed - 1); // Remove null terminator
    wcscpy_s(arg.load_path, reshade_path_wide.c_str());

    if (GetFileAttributesW(arg.load_path) == INVALID_FILE_ATTRIBUTES) {
        logError("Failed to find ReShade DLL at \"" + reshade_path + "\"");
        return false;
    }

    // Make sure the DLL has permissions set up for 'ALL_APPLICATION_PACKAGES'
    update_acl_for_uwp(arg.load_path);

    // Setup function pointers
    arg.GetLastError = GetLastError;
    arg.LoadLibraryW = LoadLibraryW;
    arg.SetEnvironmentVariableW = SetEnvironmentVariableW;

    // Allocate memory in target process
    const auto load_param = VirtualAllocEx(remote_process, nullptr,
        sizeof(arg), MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);

    if (load_param == nullptr || !WriteProcessMemory(remote_process, load_param, &arg, sizeof(arg), nullptr)) {
        logError("Failed to allocate and write 'LoadLibrary' argument in target application (PID " + std::to_string(pid) + ")", GetLastError());
        return false;
    }

    // Execute 'LoadLibrary' in target application
    const scoped_handle load_thread = CreateRemoteThread(remote_process, nullptr, 0,
        reinterpret_cast<LPTHREAD_START_ROUTINE>(arg.LoadLibraryW), load_param, 0, nullptr);

    if (load_thread == nullptr) {
        logError("Failed to execute 'LoadLibrary' in target application (PID " + std::to_string(pid) + ")", GetLastError());
        return false;
    }

    // Wait for loading to finish and clean up parameter memory afterwards
    WaitForSingleObject(load_thread, INFINITE);
    VirtualFreeEx(remote_process, load_param, 0, MEM_RELEASE);

    // Check if injection was successful
    DWORD exit_code;
    if (GetExitCodeThread(load_thread, &exit_code) && exit_code != NULL) {
        logMessage("Successfully injected ReShade into " + target.display_name + " (PID " + std::to_string(pid) + ")");
        return true;
    } else {
        logError("Failed to load ReShade in target application (PID " + std::to_string(pid) + ")");
        return false;
    }
}

bool InjectorService::isProcessRunning(DWORD pid) {
    const scoped_handle snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
        return false;

    PROCESSENTRY32W process = { sizeof(process) };
    for (BOOL next = Process32FirstW(snapshot, &process); next; next = Process32NextW(snapshot, &process)) {
        if (process.th32ProcessID == pid)
            return true;
    }
    return false;
}

void InjectorService::cleanupInjectedPids() {
    game_commander::SRWLockExclusive lock(targets_srwlock_);

    for (auto& target : targets_) {
        auto it = target.injected_pids.begin();
        while (it != target.injected_pids.end()) {
            if (!isProcessRunning(*it)) {
                if (verbose_logging_.load()) {
                    logMessage("Process " + target.display_name + " (PID " + std::to_string(*it) + ") has terminated, removing from tracking");
                }
                it = target.injected_pids.erase(it);
            } else {
                ++it;
            }
        }
    }
}

void InjectorService::logMessage(const std::string& message) {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    struct tm tm;
    localtime_s(&tm, &time_t);

    std::cout << "[" << std::setfill('0') << std::setw(2) << tm.tm_hour << ":"
              << std::setfill('0') << std::setw(2) << tm.tm_min << ":"
              << std::setfill('0') << std::setw(2) << tm.tm_sec << "] "
              << "[Injector] " << message << std::endl;
}

void InjectorService::logError(const std::string& message, DWORD error_code) {
    if (error_code != 0) {
        logMessage("ERROR: " + message + " (Error: " + std::to_string(error_code) + ")");
    } else {
        logMessage("ERROR: " + message);
    }
}

bool InjectorService::copyDisplayCommanderToGameFolder(DWORD pid) {
    // Check if at least one display commander path is configured
    if (display_commander_path_32bit_.empty() && display_commander_path_64bit_.empty()) {
        return true; // Nothing to copy
    }

    // Get the process executable path
    const scoped_handle process_handle = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
    if (process_handle == nullptr) {
        logError("Failed to open process for display commander copy (PID " + std::to_string(pid) + ")", GetLastError());
        return false;
    }

    wchar_t process_path[MAX_PATH];
    DWORD path_size = MAX_PATH;
    if (QueryFullProcessImageNameW(process_handle, 0, process_path, &path_size) == 0) {
        logError("Failed to get process path for display commander copy (PID " + std::to_string(pid) + ")", GetLastError());
        return false;
    }

    // Get the directory of the process executable
    std::wstring process_path_str(process_path);
    size_t last_slash = process_path_str.find_last_of(L"\\/");
    if (last_slash == std::wstring::npos) {
        // Convert wide string to narrow string for error message
        int size_needed = WideCharToMultiByte(CP_UTF8, 0, process_path_str.c_str(), -1, nullptr, 0, nullptr, nullptr);
        std::string process_path_narrow(size_needed, 0);
        WideCharToMultiByte(CP_UTF8, 0, process_path_str.c_str(), -1, &process_path_narrow[0], size_needed, nullptr, nullptr);
        process_path_narrow.resize(size_needed - 1); // Remove null terminator
        logError("Invalid process path for display commander copy: " + process_path_narrow);
        return false;
    }

    std::wstring game_dir = process_path_str.substr(0, last_slash);

    // Determine process architecture
    BOOL is_wow64 = FALSE;
    IsWow64Process(process_handle, &is_wow64);
    bool is_32bit = (is_wow64 == TRUE);

    // Choose the appropriate display commander path based on architecture
    std::string display_commander_path = is_32bit ? display_commander_path_32bit_ : display_commander_path_64bit_;
    if (display_commander_path.empty()) {
        logMessage("No display commander addon configured for " + std::string(is_32bit ? "32-bit" : "64-bit") + " process (PID " + std::to_string(pid) + ") - skipping display commander copy");
        return true; // Not an error, just skip copying
    }

    logMessage("Attempting to copy display commander addon: " + display_commander_path);

    // Convert narrow string to wide string
    int size_needed = MultiByteToWideChar(CP_UTF8, 0, display_commander_path.c_str(), -1, nullptr, 0);
    std::wstring display_commander_path_wide(size_needed, 0);
    MultiByteToWideChar(CP_UTF8, 0, display_commander_path.c_str(), -1, &display_commander_path_wide[0], size_needed);
    display_commander_path_wide.resize(size_needed - 1); // Remove null terminator

    // Get the filename from the display commander path
    std::wstring display_commander_filename = display_commander_path_wide;
    size_t last_slash_dc = display_commander_filename.find_last_of(L"\\/");
    if (last_slash_dc != std::wstring::npos) {
        display_commander_filename = display_commander_filename.substr(last_slash_dc + 1);
    }

    std::wstring destination_path = game_dir + L"\\" + display_commander_filename;

    // Check if source file exists
    if (GetFileAttributesW(display_commander_path_wide.c_str()) == INVALID_FILE_ATTRIBUTES) {
        logError("Display commander addon not found: " + display_commander_path);
        return false;
    }

    // Copy the file
    if (CopyFileW(display_commander_path_wide.c_str(), destination_path.c_str(), FALSE) == 0) {
        DWORD error = GetLastError();
        if (error == ERROR_FILE_EXISTS) {
            // File already exists, try to overwrite
            if (CopyFileW(display_commander_path_wide.c_str(), destination_path.c_str(), TRUE) == 0) {
                logError("Failed to overwrite display commander addon (PID " + std::to_string(pid) + ")", GetLastError());
                return false;
            }
        } else {
            logError("Failed to copy display commander addon (PID " + std::to_string(pid) + ")", error);
            return false;
        }
    }

    if (verbose_logging_.load()) {
        // Convert wide string to narrow string for log message
        int size_needed = WideCharToMultiByte(CP_UTF8, 0, destination_path.c_str(), -1, nullptr, 0, nullptr, nullptr);
        std::string destination_path_narrow(size_needed, 0);
        WideCharToMultiByte(CP_UTF8, 0, destination_path.c_str(), -1, &destination_path_narrow[0], size_needed, nullptr, nullptr);
        destination_path_narrow.resize(size_needed - 1); // Remove null terminator
        logMessage("Copied display commander addon to game folder: " + destination_path_narrow);
    }

    return true;
}

// UWP ACL update function (simplified version from enhanced_injector.cpp)
static void update_acl_for_uwp(LPWSTR path) {
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
    if (ConvertStringSidToSidW(L"S-1-15-2-1", &sid)) {
        EXPLICIT_ACCESS access = {};
        access.grfAccessPermissions = GENERIC_READ | GENERIC_EXECUTE;
        access.grfAccessMode = SET_ACCESS;
        access.grfInheritance = SUB_CONTAINERS_AND_OBJECTS_INHERIT;
        access.Trustee.TrusteeForm = TRUSTEE_IS_SID;
        access.Trustee.TrusteeType = TRUSTEE_IS_WELL_KNOWN_GROUP;
        access.Trustee.ptstrName = reinterpret_cast<LPTCH>(sid);

        // Update the DACL with the new entry
        if (SetEntriesInAcl(1, &access, old_acl, &new_acl) == ERROR_SUCCESS) {
            SetNamedSecurityInfoW(path, SE_FILE_OBJECT, siInfo, NULL, NULL, new_acl, NULL);
            LocalFree(new_acl);
        }

        LocalFree(sid);
    }

    LocalFree(sd);
}

bool InjectorService::performLocalInjection(const Game& game) {
    if (!game.use_local_injection || game.proxy_dll_type == ProxyDllType::None) {
        return false;
    }

    // Determine the game directory
    std::string game_dir;
    if (!game.working_directory.empty()) {
        game_dir = game.working_directory;
    } else {
        game_dir = std::filesystem::path(game.executable_path).parent_path().string();
    }

    // Determine which ReShade DLL to use based on architecture
    // For now, we'll try to detect the architecture by checking if the executable is 32-bit or 64-bit
    bool is_32bit = false;
    std::ifstream file(game.executable_path, std::ios::binary);
    if (file.is_open()) {
        char header[2];
        file.read(header, 2);
        if (header[0] == 'M' && header[1] == 'Z') {
            // PE file, check machine type
            file.seekg(60); // Offset to PE header
            uint32_t pe_offset;
            file.read(reinterpret_cast<char*>(&pe_offset), 4);
            file.seekg(pe_offset + 4); // Machine type offset
            uint16_t machine_type;
            file.read(reinterpret_cast<char*>(&machine_type), 2);
            is_32bit = (machine_type == 0x014c); // IMAGE_FILE_MACHINE_I386
        }
        file.close();
    }

    std::string reshade_dll_path = is_32bit ? reshade_dll_path_32bit_ : reshade_dll_path_64bit_;
    if (reshade_dll_path.empty()) {
        logError("No ReShade DLL configured for " + std::string(is_32bit ? "32-bit" : "64-bit") + " architecture");
        return false;
    }

    if (!std::filesystem::exists(reshade_dll_path)) {
        logError("ReShade DLL not found: " + reshade_dll_path);
        return false;
    }

    // Get the proxy DLL filename(s) to copy
    std::vector<std::string> proxy_dll_names = getProxyDllFilenames(game.proxy_dll_type);
    bool all_success = true;
    std::string success_message = "Local injection successful: ";

    // Copy selected proxy DLLs
    for (const auto& proxy_dll_name : proxy_dll_names) {
        std::string proxy_dll_path = game_dir + "\\" + proxy_dll_name;

        try {
            // Copy the ReShade DLL as the proxy DLL
            std::filesystem::copy_file(reshade_dll_path, proxy_dll_path, std::filesystem::copy_options::overwrite_existing);
            success_message += proxy_dll_name + " ";
        } catch (const std::exception& e) {
            logError("Failed to copy ReShade DLL as " + proxy_dll_name + ": " + e.what());
            all_success = false;
        }
    }

    // Clean up unselected proxy DLLs
    std::vector<std::string> all_proxy_dlls = getAllProxyDllFilenames();
    for (const auto& dll_name : all_proxy_dlls) {
        // Skip if this DLL is in our selected list
        if (std::find(proxy_dll_names.begin(), proxy_dll_names.end(), dll_name) != proxy_dll_names.end()) {
            continue;
        }

        std::string dll_path = game_dir + "\\" + dll_name;
        if (std::filesystem::exists(dll_path)) {
            try {
                std::filesystem::remove(dll_path);
                logMessage("Removed unselected proxy DLL: " + dll_name);
            } catch (const std::exception& e) {
                logError("Failed to remove unselected proxy DLL " + dll_name + ": " + e.what());
            }
        }
    }

    if (all_success) {
        success_message += "copied to " + game_dir;
        logMessage(success_message);
        return true;
    } else {
        logError("Some proxy DLLs failed to copy");
        return false;
    }
}

bool InjectorService::performLocalInjection(const TargetProcess& target) {
    if (!target.use_local_injection || target.proxy_dll_type == 0) { // 0 = ProxyDllType::None
        return false;
    }

    // Determine the game directory
    std::string game_dir;
    if (!target.working_directory.empty()) {
        game_dir = target.working_directory;
    } else {
        game_dir = std::filesystem::path(target.executable_path).parent_path().string();
    }

    // Determine which ReShade DLL to use based on architecture
    // For now, we'll try to detect the architecture by checking if the executable is 32-bit or 64-bit
    bool is_32bit = false;
    std::ifstream file(target.executable_path, std::ios::binary);
    if (file.is_open()) {
        char header[2];
        file.read(header, 2);
        if (header[0] == 'M' && header[1] == 'Z') {
            // PE file, check machine type
            file.seekg(60); // Offset to PE header
            uint32_t pe_offset;
            file.read(reinterpret_cast<char*>(&pe_offset), 4);
            file.seekg(pe_offset + 4); // Machine type offset
            uint16_t machine_type;
            file.read(reinterpret_cast<char*>(&machine_type), 2);
            is_32bit = (machine_type == 0x014c); // IMAGE_FILE_MACHINE_I386
        }
        file.close();
    }

    std::string reshade_dll_path = is_32bit ? reshade_dll_path_32bit_ : reshade_dll_path_64bit_;
    if (reshade_dll_path.empty()) {
        logError("No ReShade DLL configured for " + std::string(is_32bit ? "32-bit" : "64-bit") + " architecture");
        return false;
    }

    if (!std::filesystem::exists(reshade_dll_path)) {
        logError("ReShade DLL not found: " + reshade_dll_path);
        return false;
    }

    // Get the proxy DLL filename(s) to copy
    std::vector<std::string> proxy_dll_names = getProxyDllFilenames(static_cast<ProxyDllType>(target.proxy_dll_type));
    bool all_success = true;
    std::string success_message = "Local injection successful: ";

    // Copy selected proxy DLLs
    for (const auto& proxy_dll_name : proxy_dll_names) {
        std::string proxy_dll_path = game_dir + "\\" + proxy_dll_name;

        try {
            // Copy the ReShade DLL as the proxy DLL
            std::filesystem::copy_file(reshade_dll_path, proxy_dll_path, std::filesystem::copy_options::overwrite_existing);
            success_message += proxy_dll_name + " ";
        } catch (const std::exception& e) {
            logError("Failed to copy ReShade DLL as " + proxy_dll_name + ": " + e.what());
            all_success = false;
        }
    }

    // Clean up unselected proxy DLLs
    std::vector<std::string> all_proxy_dlls = getAllProxyDllFilenames();
    for (const auto& dll_name : all_proxy_dlls) {
        // Skip if this DLL is in our selected list
        if (std::find(proxy_dll_names.begin(), proxy_dll_names.end(), dll_name) != proxy_dll_names.end()) {
            continue;
        }

        std::string dll_path = game_dir + "\\" + dll_name;
        if (std::filesystem::exists(dll_path)) {
            try {
                std::filesystem::remove(dll_path);
                logMessage("Removed unselected proxy DLL: " + dll_name);
            } catch (const std::exception& e) {
                logError("Failed to remove unselected proxy DLL " + dll_name + ": " + e.what());
            }
        }
    }

    if (all_success) {
        success_message += "copied to " + game_dir;
        logMessage(success_message);
        return true;
    } else {
        logError("Some proxy DLLs failed to copy");
        return false;
    }
}
