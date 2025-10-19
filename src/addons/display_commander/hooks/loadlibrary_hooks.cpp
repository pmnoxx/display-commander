#include "loadlibrary_hooks.hpp"
#include "api_hooks.hpp"
#include "xinput_hooks.hpp"
#include "windows_gaming_input_hooks.hpp"
#include "nvapi_hooks.hpp"
#include "ngx_hooks.hpp"
#include "streamline_hooks.hpp"
#include "../utils.hpp"
#include "../settings/streamline_tab_settings.hpp"
#include "../globals.hpp"
#include "utils/srwlock_wrapper.hpp"
#include <MinHook.h>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <filesystem>
#include <unordered_set>
#include <algorithm>

namespace display_commanderhooks {

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

    // First, enumerate all currently loaded modules
    LogInfo("Enumerating currently loaded modules...");
    if (!EnumerateLoadedModules()) {
        LogError("Failed to enumerate loaded modules, but continuing with hook installation");
    }

    // Initialize MinHook (only if not already initialized)
    MH_STATUS init_status = MH_Initialize();
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
        if (InstallDxgiHooks()) {
            LogInfo("DXGI hooks installed successfully");
        } else {
            LogError("Failed to install DXGI hooks");
        }
    }
    else if (lowerModuleName.find(L"sl.interposer.dll") != std::wstring::npos) {
        LogInfo("Installing Streamline hooks for module: %ws", moduleName.c_str());
        if (InstallStreamlineHooks()) {
            LogInfo("Streamline hooks installed successfully");
        } else {
            LogError("Failed to install Streamline hooks");
        }
    }

    // XInput hooks
    else if (lowerModuleName.find(L"xinput") != std::wstring::npos) {
        LogInfo("Installing XInput hooks for module: %ws", moduleName.c_str());
        if (InstallXInputHooks()) {
            LogInfo("XInput hooks installed successfully");
        } else {
            LogError("Failed to install XInput hooks");
        }
    }

    // Windows.Gaming.Input hooks
    else if (lowerModuleName.find(L"windows.gaming.input") != std::wstring::npos ||
    lowerModuleName.find(L"gameinput") != std::wstring::npos) {
        LogInfo("Installing Windows.Gaming.Input hooks for module: %ws", moduleName.c_str());
        if (InstallWindowsGamingInputHooks()) {
            LogInfo("Windows.Gaming.Input hooks installed successfully");
        } else {
            LogError("Failed to install Windows.Gaming.Input hooks");
        }
    }

    // NVAPI hooks
    else if (lowerModuleName.find(L"nvapi64.dll") != std::wstring::npos) {
        LogInfo("Installing NVAPI hooks for module: %ws", moduleName.c_str());
        if (InstallNVAPIHooks()) {
            LogInfo("NVAPI hooks installed successfully");
        } else {
            LogError("Failed to install NVAPI hooks");
        }
    }
    // NGX hooks
    else if (lowerModuleName.find(L"_nvngx.dll") != std::wstring::npos) {
        LogInfo("Installing NGX hooks for module: %ws", moduleName.c_str());
        if (InstallNGXHooks()) {
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

} // namespace display_commanderhooks
