#include "loadlibrary_hooks.hpp"
#include "hook_suppression_manager.hpp"
#include "api_hooks.hpp"
#include "xinput_hooks.hpp"
#include "windows_gaming_input_hooks.hpp"
#include "nvapi_hooks.hpp"
#include "ngx_hooks.hpp"
#include "streamline_hooks.hpp"
#include "d3d11/d3d11_hooks.hpp"
#include "../utils.hpp"
#include "../utils/general_utils.hpp"
#include "../utils/logging.hpp"
#include "../settings/streamline_tab_settings.hpp"
#include "../settings/developer_tab_settings.hpp"
#include "../settings/experimental_tab_settings.hpp"
#include "../settings/main_tab_settings.hpp"
#include "../globals.hpp"
#include "utils/srwlock_wrapper.hpp"
#include <MinHook.h>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <filesystem>
#include <unordered_set>
#include <set>
#include <algorithm>

namespace display_commanderhooks {

// Helper function to check if a DLL should be blocked (Ansel libraries)
bool ShouldBlockAnselDLL(const std::wstring& dll_path) {
    // Check if Ansel skip is enabled
    if (!settings::g_mainTabSettings.skip_ansel_loading.GetValue()) {
        return false;
    }

    // Extract filename from full path
    std::filesystem::path path(dll_path);
    std::wstring filename = path.filename().wstring();

    // Convert to lowercase for case-insensitive comparison
    std::transform(filename.begin(), filename.end(), filename.begin(), ::towlower);

    // List of Ansel-related DLLs to block
    static const std::vector<std::wstring> ansel_dlls = {
        L"nvanselsdk.dll",
        L"anselsdk64.dll",
        L"nvcamerasdk64.dll",
        L"nvcameraapi64.dll",
        L"gfexperiencecore.dll",
        L"nvcamera64.dll",
        L"nvcamera32.dll"
    };

    // Check if the DLL is in the Ansel list
    for (const auto& ansel_dll : ansel_dlls) {
        if (filename == ansel_dll) {
            return true;
        }
    }

    return false;
}

// Helper function to check if a DLL should be overridden and get the override path
std::wstring GetDLSSOverridePath(const std::wstring& dll_path) {
    // Check if DLSS override is enabled
    if (!settings::g_streamlineTabSettings.dlss_override_enabled.GetValue()) {
        return L"";
    }

    std::string override_folder = settings::g_streamlineTabSettings.dlss_override_folder.GetValue();
    if (override_folder.empty()) {
        return L"";
    }

    // Extract filename from full path
    std::filesystem::path path(dll_path);
    std::wstring filename = path.filename().wstring();

    // Convert to lowercase for case-insensitive comparison
    std::transform(filename.begin(), filename.end(), filename.begin(), ::towlower);

    // Convert folder path to wide string
    std::wstring w_override_folder(override_folder.begin(), override_folder.end());

    // Check which DLL is being loaded and if override is enabled
    if (filename == L"nvngx_dlss.dll" && settings::g_streamlineTabSettings.dlss_override_dlss.GetValue()) {
        return w_override_folder + L"\\nvngx_dlss.dll";
    }
    else if (filename == L"nvngx_dlssd.dll" && settings::g_streamlineTabSettings.dlss_override_dlss_fg.GetValue()) {
        return w_override_folder + L"\\nvngx_dlssd.dll";
    }
    else if (filename == L"nvngx_dlssg.dll" && settings::g_streamlineTabSettings.dlss_override_dlss_rr.GetValue()) {
        return w_override_folder + L"\\nvngx_dlssg.dll";
    }

    return L"";
}

// Original function pointers
LoadLibraryA_pfn LoadLibraryA_Original = nullptr;
LoadLibraryW_pfn LoadLibraryW_Original = nullptr;
LoadLibraryExA_pfn LoadLibraryExA_Original = nullptr;
LoadLibraryExW_pfn LoadLibraryExW_Original = nullptr;

// Hook state
static std::atomic<bool> g_loadlibrary_hooks_installed{false};

// Module tracking
static std::vector<ModuleInfo> g_loaded_modules;
static std::unordered_set<HMODULE> g_module_handles;
static SRWLOCK g_module_srwlock = SRWLOCK_INIT;

// Display Commander module handle (for determining which modules can be blocked)
static HMODULE g_display_commander_module = nullptr;

// Blocked DLL tracking
static std::set<std::wstring> g_blocked_dlls;
static SRWLOCK g_blocked_dlls_srwlock = SRWLOCK_INIT;

// Helper function to get current timestamp as string
std::string GetCurrentTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;

    std::stringstream ss;
    struct tm timeinfo;
    if (localtime_s(&timeinfo, &time_t) == 0) {
        ss << std::put_time(&timeinfo, "%Y-%m-%d %H:%M:%S");
    } else {
        ss << "0000-00-00 00:00:00";
    }
    ss << '.' << std::setfill('0') << std::setw(3) << ms.count();
    return ss.str();
}

// Helper function to convert wide string to narrow string
std::string WideToNarrow(const std::wstring& wstr) {
    if (wstr.empty()) return std::string();

    int size_needed = WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), NULL, 0, NULL, NULL);
    std::string strTo(size_needed, 0);
    WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), &strTo[0], size_needed, NULL, NULL);
    return strTo;
}

// Helper function to get file time from module
FILETIME GetModuleFileTime(HMODULE hModule) {
    FILETIME ft = {0};
    wchar_t modulePath[MAX_PATH];
    if (GetModuleFileNameW(hModule, modulePath, MAX_PATH)) {
        HANDLE hFile = CreateFileW(modulePath, GENERIC_READ, FILE_SHARE_READ,
                                nullptr, OPEN_EXISTING, 0, nullptr);
        if (hFile != INVALID_HANDLE_VALUE) {
            GetFileTime(hFile, nullptr, nullptr, &ft);
            CloseHandle(hFile);
        }
    }
    return ft;
}

// Helper function to extract module name from full path
std::wstring ExtractModuleName(const std::wstring& fullPath) {
    std::filesystem::path path(fullPath);
    return path.filename().wstring();
}

// Hooked LoadLibraryA function
HMODULE WINAPI LoadLibraryA_Detour(LPCSTR lpLibFileName) {
    std::string timestamp = GetCurrentTimestamp();
    std::string dll_name = lpLibFileName ? lpLibFileName : "NULL";

    LogInfo("[%s] LoadLibraryA called: %s", timestamp.c_str(), dll_name.c_str());

    // Check for Ansel blocking
    if (lpLibFileName) {
        std::wstring w_dll_name = std::wstring(dll_name.begin(), dll_name.end());
        if (ShouldBlockAnselDLL(w_dll_name)) {
            LogInfo("[%s] Ansel Block: Blocking %s from loading", timestamp.c_str(), dll_name.c_str());
            return nullptr; // Return nullptr to indicate failure to load
        }
    }

    // Check for user-defined DLL blocking
    if (lpLibFileName) {
        std::wstring w_dll_name = std::wstring(dll_name.begin(), dll_name.end());
        if (ShouldBlockDLL(w_dll_name)) {
            LogInfo("[%s] DLL Block: Blocking %s from loading", timestamp.c_str(), dll_name.c_str());
            return nullptr; // Return nullptr to indicate failure to load
        }
    }

    // Check for DLSS override
    LPCSTR actual_lib_file_name = lpLibFileName;

    if (lpLibFileName) {
        std::wstring w_dll_name = std::wstring(dll_name.begin(), dll_name.end());
        std::wstring override_path = GetDLSSOverridePath(w_dll_name);

        if (!override_path.empty()) {
            // Check if override file exists
            if (std::filesystem::exists(override_path)) {
                std::string narrow_override_path = WideToNarrow(override_path);
                actual_lib_file_name = narrow_override_path.c_str();
                LogInfo("[%s] DLSS Override: Redirecting %s to %s", timestamp.c_str(), dll_name.c_str(), narrow_override_path.c_str());
            } else {
                LogInfo("[%s] DLSS Override: Override file not found: %s", timestamp.c_str(), WideToNarrow(override_path).c_str());
            }
        }
    }

    // Call original function with potentially overridden path
    HMODULE result = LoadLibraryA_Original ? LoadLibraryA_Original(actual_lib_file_name) : LoadLibraryA(actual_lib_file_name);

    if (result) {
        LogInfo("[%s] LoadLibraryA success: %s -> HMODULE: 0x%p", timestamp.c_str(), dll_name.c_str(), result);

        // Track the newly loaded module
        {
            utils::SRWLockExclusive lock(g_module_srwlock);
            if (g_module_handles.find(result) == g_module_handles.end()) {
                ModuleInfo moduleInfo;
                moduleInfo.hModule = result;
                moduleInfo.moduleName = std::wstring(dll_name.begin(), dll_name.end());

                wchar_t modulePath[MAX_PATH];
                if (GetModuleFileNameW(result, modulePath, MAX_PATH)) {
                    moduleInfo.fullPath = modulePath;
                }

                MODULEINFO modInfo;
                if (GetModuleInformation(GetCurrentProcess(), result, &modInfo, sizeof(modInfo))) {
                    moduleInfo.baseAddress = modInfo.lpBaseOfDll;
                    moduleInfo.sizeOfImage = modInfo.SizeOfImage;
                    moduleInfo.entryPoint = modInfo.EntryPoint;
                }

                moduleInfo.loadTime = GetModuleFileTime(result);

                // Mark as loaded after hooks (added through hook detour)
                moduleInfo.loadedBeforeHooks = false;

                g_loaded_modules.push_back(moduleInfo);
                g_module_handles.insert(result);

                LogInfo("Added new module to tracking: %s (0x%p, %u bytes)",
                        dll_name.c_str(), moduleInfo.baseAddress, moduleInfo.sizeOfImage);

                // Call the module loaded callback
                OnModuleLoaded(moduleInfo.moduleName, result);
            }
        }
    } else {
        DWORD error = GetLastError();
        LogInfo("[%s] LoadLibraryA failed: %s -> Error: %lu", timestamp.c_str(), dll_name.c_str(), error);
    }

    return result;
}

// Hooked LoadLibraryW function
HMODULE WINAPI LoadLibraryW_Detour(LPCWSTR lpLibFileName) {
    std::string timestamp = GetCurrentTimestamp();
    std::string dll_name = lpLibFileName ? WideToNarrow(lpLibFileName) : "NULL";

    LogInfo("[%s] LoadLibraryW called: %s", timestamp.c_str(), dll_name.c_str());

    // Check for Ansel blocking
    if (lpLibFileName) {
        std::wstring w_dll_name = lpLibFileName;
        if (ShouldBlockAnselDLL(w_dll_name)) {
            LogInfo("[%s] Ansel Block: Blocking %s from loading", timestamp.c_str(), dll_name.c_str());
            return nullptr; // Return nullptr to indicate failure to load
        }
    }

    // Check for user-defined DLL blocking
    if (lpLibFileName) {
        std::wstring w_dll_name = lpLibFileName;
        if (ShouldBlockDLL(w_dll_name)) {
            LogInfo("[%s] DLL Block: Blocking %s from loading", timestamp.c_str(), dll_name.c_str());
            return nullptr; // Return nullptr to indicate failure to load
        }
    }

    // Check for DLSS override
    LPCWSTR actual_lib_file_name = lpLibFileName;
    std::wstring override_path;

    if (lpLibFileName) {
        std::wstring w_dll_name = lpLibFileName;
        override_path = GetDLSSOverridePath(w_dll_name);

        if (!override_path.empty()) {
            // Check if override file exists
            if (std::filesystem::exists(override_path)) {
                actual_lib_file_name = override_path.c_str();
                LogInfo("[%s] DLSS Override: Redirecting %s to %s", timestamp.c_str(), dll_name.c_str(), WideToNarrow(override_path).c_str());
            } else {
                LogInfo("[%s] DLSS Override: Override file not found: %s", timestamp.c_str(), WideToNarrow(override_path).c_str());
            }
        }
    }

    // Call original function with potentially overridden path
    HMODULE result = LoadLibraryW_Original ? LoadLibraryW_Original(actual_lib_file_name) : LoadLibraryW(actual_lib_file_name);

    if (result) {
        LogInfo("[%s] LoadLibraryW success: %s -> HMODULE: 0x%p", timestamp.c_str(), dll_name.c_str(), result);

        // Track the newly loaded module
        {
            utils::SRWLockExclusive lock(g_module_srwlock);
            if (g_module_handles.find(result) == g_module_handles.end()) {
                ModuleInfo moduleInfo;
                moduleInfo.hModule = result;
                moduleInfo.moduleName = std::wstring(dll_name.begin(), dll_name.end());

                wchar_t modulePath[MAX_PATH];
                if (GetModuleFileNameW(result, modulePath, MAX_PATH)) {
                    moduleInfo.fullPath = modulePath;
                }

                MODULEINFO modInfo;
                if (GetModuleInformation(GetCurrentProcess(), result, &modInfo, sizeof(modInfo))) {
                    moduleInfo.baseAddress = modInfo.lpBaseOfDll;
                    moduleInfo.sizeOfImage = modInfo.SizeOfImage;
                    moduleInfo.entryPoint = modInfo.EntryPoint;
                }

                moduleInfo.loadTime = GetModuleFileTime(result);

                // Mark as loaded after hooks (added through hook detour)
                moduleInfo.loadedBeforeHooks = false;

                g_loaded_modules.push_back(moduleInfo);
                g_module_handles.insert(result);

                LogInfo("Added new module to tracking: %s (0x%p, %u bytes)",
                        dll_name.c_str(), moduleInfo.baseAddress, moduleInfo.sizeOfImage);

                // Call the module loaded callback
                OnModuleLoaded(moduleInfo.moduleName, result);
            }
        }
    } else {
        DWORD error = GetLastError();
        LogInfo("[%s] LoadLibraryW failed: %s -> Error: %lu", timestamp.c_str(), dll_name.c_str(), error);
    }

    return result;
}

// Hooked LoadLibraryExA function
HMODULE WINAPI LoadLibraryExA_Detour(LPCSTR lpLibFileName, HANDLE hFile, DWORD dwFlags) {
    std::string timestamp = GetCurrentTimestamp();
    std::string dll_name = lpLibFileName ? lpLibFileName : "NULL";

    LogInfo("[%s] LoadLibraryExA called: %s, hFile: 0x%p, dwFlags: 0x%08X",
            timestamp.c_str(), dll_name.c_str(), hFile, dwFlags);

    // Check for Ansel blocking
    if (lpLibFileName) {
        std::wstring w_dll_name = std::wstring(dll_name.begin(), dll_name.end());
        if (ShouldBlockAnselDLL(w_dll_name)) {
            LogInfo("[%s] Ansel Block: Blocking %s from loading", timestamp.c_str(), dll_name.c_str());
            return nullptr; // Return nullptr to indicate failure to load
        }
    }

    // Check for user-defined DLL blocking
    if (lpLibFileName) {
        std::wstring w_dll_name = std::wstring(dll_name.begin(), dll_name.end());
        if (ShouldBlockDLL(w_dll_name)) {
            LogInfo("[%s] DLL Block: Blocking %s from loading", timestamp.c_str(), dll_name.c_str());
            return nullptr; // Return nullptr to indicate failure to load
        }
    }

    // Check for DLSS override
    LPCSTR actual_lib_file_name = lpLibFileName;

    if (lpLibFileName) {
        std::wstring w_dll_name = std::wstring(dll_name.begin(), dll_name.end());
        std::wstring override_path = GetDLSSOverridePath(w_dll_name);

        if (!override_path.empty()) {
            // Check if override file exists
            if (std::filesystem::exists(override_path)) {
                std::string narrow_override_path = WideToNarrow(override_path);
                actual_lib_file_name = narrow_override_path.c_str();
                LogInfo("[%s] DLSS Override: Redirecting %s to %s", timestamp.c_str(), dll_name.c_str(), narrow_override_path.c_str());
            } else {
                LogInfo("[%s] DLSS Override: Override file not found: %s", timestamp.c_str(), WideToNarrow(override_path).c_str());
            }
        }
    }

    // Call original function with potentially overridden path
    HMODULE result = LoadLibraryExA_Original ?
        LoadLibraryExA_Original(actual_lib_file_name, hFile, dwFlags) :
        LoadLibraryExA(actual_lib_file_name, hFile, dwFlags);

    if (result) {
        LogInfo("[%s] LoadLibraryExA success: %s -> HMODULE: 0x%p", timestamp.c_str(), dll_name.c_str(), result);

        // Track the module if it's not already tracked
        {
            utils::SRWLockExclusive lock(g_module_srwlock);
            if (g_module_handles.find(result) == g_module_handles.end()) {
                ModuleInfo moduleInfo;
                moduleInfo.hModule = result;

                // Convert narrow string to wide string for module name
                std::wstring wideModuleName;
                if (lpLibFileName) {
                    int wideLen = MultiByteToWideChar(CP_ACP, 0, lpLibFileName, -1, nullptr, 0);
                    if (wideLen > 0) {
                        wideModuleName.resize(wideLen - 1);
                        MultiByteToWideChar(CP_ACP, 0, lpLibFileName, -1, &wideModuleName[0], wideLen);
                    }
                }
                moduleInfo.moduleName = wideModuleName.empty() ? L"Unknown" : wideModuleName;

                // Get full path
                wchar_t modulePath[MAX_PATH];
                if (GetModuleFileNameW(result, modulePath, MAX_PATH)) {
                    moduleInfo.fullPath = modulePath;
                }

                MODULEINFO modInfo;
                if (GetModuleInformation(GetCurrentProcess(), result, &modInfo, sizeof(modInfo))) {
                    moduleInfo.baseAddress = modInfo.lpBaseOfDll;
                    moduleInfo.sizeOfImage = modInfo.SizeOfImage;
                    moduleInfo.entryPoint = modInfo.EntryPoint;
                }

                moduleInfo.loadTime = GetModuleFileTime(result);

                // Mark as loaded after hooks (added through hook detour)
                moduleInfo.loadedBeforeHooks = false;

                g_loaded_modules.push_back(moduleInfo);
                g_module_handles.insert(result);

                LogInfo("Added new module to tracking: %s (0x%p, %u bytes)",
                        dll_name.c_str(), moduleInfo.baseAddress, moduleInfo.sizeOfImage);

                // Call the module loaded callback
                OnModuleLoaded(moduleInfo.moduleName, result);
            }
        }
    } else {
        DWORD error = GetLastError();
        LogInfo("[%s] LoadLibraryExA failed: %s -> Error: %lu", timestamp.c_str(), dll_name.c_str(), error);
    }

    return result;
}

// Hooked LoadLibraryExW function
HMODULE WINAPI LoadLibraryExW_Detour(LPCWSTR lpLibFileName, HANDLE hFile, DWORD dwFlags) {
    std::string timestamp = GetCurrentTimestamp();
    std::string dll_name = lpLibFileName ? WideToNarrow(lpLibFileName) : "NULL";

    LogInfo("[%s] LoadLibraryExW called: %s, hFile: 0x%p, dwFlags: 0x%08X",
            timestamp.c_str(), dll_name.c_str(), hFile, dwFlags);

    // Check for Ansel blocking
    if (lpLibFileName) {
        std::wstring w_dll_name = lpLibFileName;
        if (ShouldBlockAnselDLL(w_dll_name)) {
            LogInfo("[%s] Ansel Block: Blocking %s from loading", timestamp.c_str(), dll_name.c_str());
            return nullptr; // Return nullptr to indicate failure to load
        }
    }

    // Check for user-defined DLL blocking
    if (lpLibFileName) {
        std::wstring w_dll_name = lpLibFileName;
        if (ShouldBlockDLL(w_dll_name)) {
            LogInfo("[%s] DLL Block: Blocking %s from loading", timestamp.c_str(), dll_name.c_str());
            return nullptr; // Return nullptr to indicate failure to load
        }
    }

    // Check for DLSS override
    LPCWSTR actual_lib_file_name = lpLibFileName;
    std::wstring override_path;

    if (lpLibFileName) {
        std::wstring w_dll_name = lpLibFileName;
        override_path = GetDLSSOverridePath(w_dll_name);

        if (!override_path.empty()) {
            // Check if override file exists
            if (std::filesystem::exists(override_path)) {
                actual_lib_file_name = override_path.c_str();
                LogInfo("[%s] DLSS Override: Redirecting %s to %s", timestamp.c_str(), dll_name.c_str(), WideToNarrow(override_path).c_str());
            } else {
                LogInfo("[%s] DLSS Override: Override file not found: %s", timestamp.c_str(), WideToNarrow(override_path).c_str());
            }
        }
    }

    // Call original function with potentially overridden path
    HMODULE result = LoadLibraryExW_Original ?
        LoadLibraryExW_Original(actual_lib_file_name, hFile, dwFlags) :
        LoadLibraryExW(actual_lib_file_name, hFile, dwFlags);

    if (result) {
        LogInfo("[%s] LoadLibraryExW success: %s -> HMODULE: 0x%p", timestamp.c_str(), dll_name.c_str(), result);

        // Track the module if it's not already tracked
        {
            utils::SRWLockExclusive lock(g_module_srwlock);
            if (g_module_handles.find(result) == g_module_handles.end()) {
                ModuleInfo moduleInfo;
                moduleInfo.hModule = result;
                moduleInfo.moduleName = lpLibFileName ? lpLibFileName : L"Unknown";

                // Get full path
                wchar_t modulePath[MAX_PATH];
                if (GetModuleFileNameW(result, modulePath, MAX_PATH)) {
                    moduleInfo.fullPath = modulePath;
                }

                MODULEINFO modInfo;
                if (GetModuleInformation(GetCurrentProcess(), result, &modInfo, sizeof(modInfo))) {
                    moduleInfo.baseAddress = modInfo.lpBaseOfDll;
                    moduleInfo.sizeOfImage = modInfo.SizeOfImage;
                    moduleInfo.entryPoint = modInfo.EntryPoint;
                }

                moduleInfo.loadTime = GetModuleFileTime(result);

                // Mark as loaded after hooks (added through hook detour)
                moduleInfo.loadedBeforeHooks = false;

                g_loaded_modules.push_back(moduleInfo);
                g_module_handles.insert(result);

                LogInfo("Added new module to tracking: %s (0x%p, %u bytes)",
                        dll_name.c_str(), moduleInfo.baseAddress, moduleInfo.sizeOfImage);

                // Call the module loaded callback
                OnModuleLoaded(moduleInfo.moduleName, result);
            }
        }
    } else {
        DWORD error = GetLastError();
        LogInfo("[%s] LoadLibraryExW failed: %s -> Error: %lu", timestamp.c_str(), dll_name.c_str(), error);
    }

    return result;
}

bool InstallLoadLibraryHooks() {
    if (g_loadlibrary_hooks_installed.load()) {
        LogInfo("LoadLibrary hooks already installed");
        return true;
    }

    // Check if LoadLibrary hooks should be suppressed
    if (display_commanderhooks::HookSuppressionManager::GetInstance().ShouldSuppressHook(display_commanderhooks::HookType::LOADLIBRARY)) {
        LogInfo("LoadLibrary hooks installation suppressed by user setting");
        return false;
    }

    // Load blocked DLLs list BEFORE installing hooks to ensure blocking works
    // Check if DLL blocking is enabled in experimental settings
    if (settings::g_experimentalTabSettings.dll_blocking_enabled.GetValue()) {
        settings::g_experimentalTabSettings.blocked_dlls.Load();
        if (!settings::g_experimentalTabSettings.blocked_dlls.GetValue().empty()) {
            LoadBlockedDLLsFromSettings(settings::g_experimentalTabSettings.blocked_dlls.GetValue());
            LogInfo("Loaded blocked DLLs list: %s", settings::g_experimentalTabSettings.blocked_dlls.GetValue().c_str());
        } else {
            LogInfo("No blocked DLLs configured");
        }
    } else {
        LogInfo("DLL blocking is disabled in experimental settings");
    }

    // Get Display Commander's module handle for comparison
    // Try to get it from the current module
    HMODULE hMod = nullptr;
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(&InstallLoadLibraryHooks), &hMod)) {
        g_display_commander_module = hMod;
        LogInfo("Display Commander module handle: 0x%p", hMod);
    } else {
        // Fallback: try to find by name
        g_display_commander_module = GetModuleHandleW(L"display_commander.dll");
        if (!g_display_commander_module) {
            g_display_commander_module = GetModuleHandleW(L"display_commander64.dll");
        }
        if (g_display_commander_module) {
            LogInfo("Display Commander module found by name: 0x%p", g_display_commander_module);
        }
    }

    // First, enumerate all currently loaded modules
    LogInfo("Enumerating currently loaded modules...");
    if (!EnumerateLoadedModules()) {
        LogError("Failed to enumerate loaded modules, but continuing with hook installation");
    }

    // Initialize MinHook (only if not already initialized)
    MH_STATUS init_status = SafeInitializeMinHook(display_commanderhooks::HookType::LOADLIBRARY);
    if (init_status != MH_OK && init_status != MH_ERROR_ALREADY_INITIALIZED) {
        LogError("Failed to initialize MinHook for LoadLibrary hooks - Status: %d", init_status);
        return false;
    }

    if (init_status == MH_ERROR_ALREADY_INITIALIZED) {
        LogInfo("MinHook already initialized, proceeding with LoadLibrary hooks");
    } else {
        LogInfo("MinHook initialized successfully for LoadLibrary hooks");
    }

    // Hook LoadLibraryA
    if (!CreateAndEnableHook(LoadLibraryA, LoadLibraryA_Detour, (LPVOID*)&LoadLibraryA_Original, "LoadLibraryA")) {
        LogError("Failed to create and enable LoadLibraryA hook");
        return false;
    }

    // Hook LoadLibraryW
    if (!CreateAndEnableHook(LoadLibraryW, LoadLibraryW_Detour, (LPVOID*)&LoadLibraryW_Original, "LoadLibraryW")) {
        LogError("Failed to create and enable LoadLibraryW hook");
        return false;
    }

    // Hook LoadLibraryExA
    if (!CreateAndEnableHook(LoadLibraryExA, LoadLibraryExA_Detour, (LPVOID*)&LoadLibraryExA_Original, "LoadLibraryExA")) {
        LogError("Failed to create and enable LoadLibraryExA hook");
        return false;
    }

    // Hook LoadLibraryExW
    if (!CreateAndEnableHook(LoadLibraryExW, LoadLibraryExW_Detour, (LPVOID*)&LoadLibraryExW_Original, "LoadLibraryExW")) {
        LogError("Failed to create and enable LoadLibraryExW hook");
        return false;
    }

    g_loadlibrary_hooks_installed.store(true);
    LogInfo("LoadLibrary hooks installed successfully");

    // Mark LoadLibrary hooks as installed
    display_commanderhooks::HookSuppressionManager::GetInstance().MarkHookInstalled(display_commanderhooks::HookType::LOADLIBRARY);

    return true;
}

void UninstallLoadLibraryHooks() {
    if (!g_loadlibrary_hooks_installed.load()) {
        LogInfo("LoadLibrary hooks not installed");
        return;
    }

    // Disable all hooks
    MH_DisableHook(MH_ALL_HOOKS);

    // Remove hooks
    MH_RemoveHook(LoadLibraryA);
    MH_RemoveHook(LoadLibraryW);
    MH_RemoveHook(LoadLibraryExA);
    MH_RemoveHook(LoadLibraryExW);

    // Uninstall library-specific hooks
    UninstallNVAPIHooks();

    // Clean up
    LoadLibraryA_Original = nullptr;
    LoadLibraryW_Original = nullptr;
    LoadLibraryExA_Original = nullptr;
    LoadLibraryExW_Original = nullptr;

    g_loadlibrary_hooks_installed.store(false);
    LogInfo("LoadLibrary hooks uninstalled successfully");
}
bool EnumerateLoadedModules() {
    utils::SRWLockExclusive lock(g_module_srwlock);

    g_loaded_modules.clear();
    g_module_handles.clear();

    HMODULE hModules[1024];
    DWORD cbNeeded;

    HANDLE hProcess = GetCurrentProcess();
    if (!EnumProcessModules(hProcess, hModules, sizeof(hModules), &cbNeeded)) {
        LogError("Failed to enumerate process modules - Error: %lu", GetLastError());
        return false;
    }

    DWORD moduleCount = cbNeeded / sizeof(HMODULE);
    LogInfo("Found %lu loaded modules", moduleCount);

    for (DWORD i = 0; i < moduleCount; i++) {
        ModuleInfo moduleInfo;
        moduleInfo.hModule = hModules[i];

        // Get module file name
        wchar_t modulePath[MAX_PATH];
        if (GetModuleFileNameW(hModules[i], modulePath, MAX_PATH)) {
            moduleInfo.fullPath = modulePath;
            moduleInfo.moduleName = ExtractModuleName(modulePath);
        } else {
            moduleInfo.moduleName = L"Unknown";
            moduleInfo.fullPath = L"Unknown";
        }

        // Get module information
        MODULEINFO modInfo;
        if (GetModuleInformation(hProcess, hModules[i], &modInfo, sizeof(modInfo))) {
            moduleInfo.baseAddress = modInfo.lpBaseOfDll;
            moduleInfo.sizeOfImage = modInfo.SizeOfImage;
            moduleInfo.entryPoint = modInfo.EntryPoint;
        }

        // Get file time
        moduleInfo.loadTime = GetModuleFileTime(hModules[i]);

        // Mark as loaded before hooks (enumerated modules were all loaded before Display Commander)
        moduleInfo.loadedBeforeHooks = true;

        g_loaded_modules.push_back(moduleInfo);
        g_module_handles.insert(hModules[i]);

        LogInfo("Module %lu: %ws (0x%p, %u bytes)",
                i, moduleInfo.moduleName.c_str(),
                moduleInfo.baseAddress, moduleInfo.sizeOfImage);

        // Call the module loaded callback for existing modules
        OnModuleLoaded(moduleInfo.moduleName, hModules[i]);
    }

    return true;
}

std::vector<ModuleInfo> GetLoadedModules() {
    utils::SRWLockShared lock(g_module_srwlock);
    return g_loaded_modules;
}

bool IsModuleLoaded(const std::wstring& moduleName) {
    utils::SRWLockShared lock(g_module_srwlock);

    for (const auto& module : g_loaded_modules) {
        if (_wcsicmp(module.moduleName.c_str(), moduleName.c_str()) == 0) {
            return true;
        }
    }
    return false;
}

void OnModuleLoaded(const std::wstring& moduleName, HMODULE hModule) {
    LogInfo("Module loaded: %ws (0x%p)", moduleName.c_str(), hModule);

    // Convert to lowercase for case-insensitive comparison
    std::wstring lowerModuleName = moduleName;
    std::transform(lowerModuleName.begin(), lowerModuleName.end(), lowerModuleName.begin(), ::towlower);


    // dxgi.dll
    if (lowerModuleName.find(L"dxgi.dll") != std::wstring::npos) {
        LogInfo("Installing DXGI hooks for module: %ws", moduleName.c_str());
        if (InstallDxgiHooks(hModule)) {
            LogInfo("DXGI hooks installed successfully");
        } else {
            LogError("Failed to install DXGI hooks");
        }
    }
    // d3d11.dll - D3D11 hooks will be installed via vtable hooking when device is created
    // This is handled in swapchain initialization (swapchain_events.cpp)
    else if (lowerModuleName.find(L"d3d11.dll") != std::wstring::npos) {
        LogInfo("D3D11 hooks will be installed via vtable when device is created");
    }
    else if (lowerModuleName.find(L"sl.interposer.dll") != std::wstring::npos) {
        // Check if Streamline loading is enabled
        LogInfo("Installing Streamline hooks for module: %ws", moduleName.c_str());
        if (InstallStreamlineHooks(hModule)) {
            LogInfo("Streamline hooks installed successfully");
        } else {
            LogError("Failed to install Streamline hooks");
        }
    }

    // XInput hooks
    else if (lowerModuleName.find(L"xinput") != std::wstring::npos) {
        LogInfo("Installing XInput hooks for module: %ws", moduleName.c_str());
        if (InstallXInputHooks(hModule)) {
            LogInfo("XInput hooks installed successfully");
        } else {
            LogError("Failed to install XInput hooks");
        }
    }

    // Windows.Gaming.Input hooks
    else if (lowerModuleName.find(L"windows.gaming.input") != std::wstring::npos ||
    lowerModuleName.find(L"gameinput") != std::wstring::npos) {
        LogInfo("Installing Windows.Gaming.Input hooks for module: %ws", moduleName.c_str());
        if (InstallWindowsGamingInputHooks(hModule)) {
            LogInfo("Windows.Gaming.Input hooks installed successfully");
        } else {
            LogError("Failed to install Windows.Gaming.Input hooks");
        }
    }

    // NVAPI hooks
    else if (lowerModuleName.find(L"nvapi64.dll") != std::wstring::npos) {
        // Check if nvapi64 loading is enabled
        LogInfo("Installing NVAPI hooks for module: %ws", moduleName.c_str());
        if (InstallNVAPIHooks(hModule)) {
            LogInfo("NVAPI hooks installed successfully");
        } else {
            LogError("Failed to install NVAPI hooks");
        }
    }
    // NGX hooks
    else if (lowerModuleName.find(L"_nvngx.dll") != std::wstring::npos) {
        // Check if _nvngx loading is enabled
            LogInfo("Installing NGX hooks for module: %ws", moduleName.c_str());
            if (InstallNGXHooks(hModule)) {
                LogInfo("NGX hooks installed successfully");
            } else {
                LogError("Failed to install NGX hooks");
            }
    }

    // Generic logging for other modules
    else {
        LogInfo("Other module loaded: %ws (0x%p)", moduleName.c_str(), hModule);
    }
}

// Helper function to check if a DLL should be blocked (user-defined blocking)
bool ShouldBlockDLL(const std::wstring& dll_path) {
    // Extract filename from full path
    std::filesystem::path path(dll_path);
    std::wstring filename = path.filename().wstring();

    // Convert to lowercase for case-insensitive comparison
    std::transform(filename.begin(), filename.end(), filename.begin(), ::towlower);

    utils::SRWLockShared lock(g_blocked_dlls_srwlock);
    bool is_blocked = g_blocked_dlls.find(filename) != g_blocked_dlls.end();

    if (is_blocked) {
        std::string narrow_filename(filename.begin(), filename.end());
        std::string narrow_path(dll_path.begin(), dll_path.end());
        LogInfo("ShouldBlockDLL: Found '%s' (from '%s') in blocked list", narrow_filename.c_str(), narrow_path.c_str());
    }

    return is_blocked;
}

bool IsDLLBlocked(const std::wstring& module_name) {
    std::wstring lower_name = module_name;
    std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(), ::towlower);

    utils::SRWLockShared lock(g_blocked_dlls_srwlock);
    return g_blocked_dlls.find(lower_name) != g_blocked_dlls.end();
}

void SetDLLBlocked(const std::wstring& module_name, bool blocked) {
    std::wstring lower_name = module_name;
    std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(), ::towlower);

    utils::SRWLockExclusive lock(g_blocked_dlls_srwlock);
    if (blocked) {
        g_blocked_dlls.insert(lower_name);
    } else {
        g_blocked_dlls.erase(lower_name);
    }
}

void LoadBlockedDLLsFromSettings(const std::string& blocked_dlls_str) {
    if (blocked_dlls_str.empty()) {
        return;
    }

    utils::SRWLockExclusive lock(g_blocked_dlls_srwlock);
    g_blocked_dlls.clear();

    // Parse comma-separated DLL names
    std::stringstream ss(blocked_dlls_str);
    std::string dll_name;

    while (std::getline(ss, dll_name, ',')) {
        // Trim whitespace
        dll_name.erase(0, dll_name.find_first_not_of(" \t"));
        dll_name.erase(dll_name.find_last_not_of(" \t") + 1);
        if (!dll_name.empty()) {
            // Convert to wide string
            std::wstring wdll_name(dll_name.begin(), dll_name.end());

            // Extract filename from path (in case full path is stored)
            std::filesystem::path path(wdll_name);
            std::wstring filename = path.filename().wstring();

            // Convert to lowercase for case-insensitive comparison
            std::transform(filename.begin(), filename.end(), filename.begin(), ::towlower);

            // Store just the filename (matching ShouldBlockDLL behavior)
            g_blocked_dlls.insert(filename);

            // Log for debugging
            std::string narrow_filename(filename.begin(), filename.end());
            if (filename != wdll_name) {
                std::string narrow_original(wdll_name.begin(), wdll_name.end());
                LogInfo("Blocked DLL: Extracted filename '%s' from path '%s'", narrow_filename.c_str(), narrow_original.c_str());
            } else {
                LogInfo("Blocked DLL: '%s'", narrow_filename.c_str());
            }
        }
    }
}

std::string SaveBlockedDLLsToSettings() {
    utils::SRWLockShared lock(g_blocked_dlls_srwlock);

    std::string result;
    for (auto it = g_blocked_dlls.begin(); it != g_blocked_dlls.end(); ++it) {
        if (!result.empty()) {
            result += ",";
        }
        // Convert wide string to narrow string
        std::string narrow_name(it->begin(), it->end());
        result += narrow_name;
    }

    return result;
}

std::vector<std::wstring> GetBlockedDLLs() {
    utils::SRWLockShared lock(g_blocked_dlls_srwlock);

    std::vector<std::wstring> result;
    result.reserve(g_blocked_dlls.size());
    for (const auto& dll_name : g_blocked_dlls) {
        result.push_back(dll_name);
    }

    return result;
}

bool CanBlockDLL(const ModuleInfo& module_info) {
    // Modules loaded before hooks were installed cannot be blocked
    // (they were already loaded when Display Commander started)
    if (module_info.loadedBeforeHooks) {
        return false;
    }

    // Also check if the module name suggests it's Display Commander itself
    std::wstring lower_name = module_info.moduleName;
    std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(), ::towlower);
    if (lower_name.find(L"display_commander") != std::wstring::npos) {
        return false; // Can't block Display Commander itself
    }

    // Modules loaded after hooks were installed can be blocked
    return true;
}

} // namespace display_commanderhooks
