#include "loadlibrary_hooks.hpp"
#include "xinput_hooks.hpp"
#include "windows_gaming_input_hooks.hpp"
#include "nvapi_hooks.hpp"
#include "ngx_hooks.hpp"
#include "streamline_hooks.hpp"
#include "../utils.hpp"
#include "utils/srwlock_wrapper.hpp"
#include <MinHook.h>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <filesystem>
#include <unordered_set>

namespace display_commanderhooks {

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

    // Call original function
    HMODULE result = LoadLibraryA_Original ? LoadLibraryA_Original(lpLibFileName) : LoadLibraryA(lpLibFileName);

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

    // Call original function
    HMODULE result = LoadLibraryW_Original ? LoadLibraryW_Original(lpLibFileName) : LoadLibraryW(lpLibFileName);

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

    // Call original function
    HMODULE result = LoadLibraryExA_Original ?
        LoadLibraryExA_Original(lpLibFileName, hFile, dwFlags) :
        LoadLibraryExA(lpLibFileName, hFile, dwFlags);

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

    // Call original function
    HMODULE result = LoadLibraryExW_Original ?
        LoadLibraryExW_Original(lpLibFileName, hFile, dwFlags) :
        LoadLibraryExW(lpLibFileName, hFile, dwFlags);

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
    if (MH_CreateHook(LoadLibraryA, LoadLibraryA_Detour, (LPVOID*)&LoadLibraryA_Original) != MH_OK) {
        LogError("Failed to create LoadLibraryA hook");
        return false;
    }
    if (MH_EnableHook(LoadLibraryA) != MH_OK) {
        LogError("Failed to enable LoadLibraryA hook");
        return false;
    }

    // Hook LoadLibraryW
    if (MH_CreateHook(LoadLibraryW, LoadLibraryW_Detour, (LPVOID*)&LoadLibraryW_Original) != MH_OK) {
        LogError("Failed to create LoadLibraryW hook");
        return false;
    }
    if (MH_EnableHook(LoadLibraryW) != MH_OK) {
        LogError("Failed to enable LoadLibraryW hook");
        return false;
    }

    // Hook LoadLibraryExA
    if (MH_CreateHook(LoadLibraryExA, LoadLibraryExA_Detour, (LPVOID*)&LoadLibraryExA_Original) != MH_OK) {
        LogError("Failed to create LoadLibraryExA hook");
        return false;
    }
    if (MH_EnableHook(LoadLibraryExA) != MH_OK) {
        LogError("Failed to enable LoadLibraryExA hook");
        return false;
    }

    // Hook LoadLibraryExW
    if (MH_CreateHook(LoadLibraryExW, LoadLibraryExW_Detour, (LPVOID*)&LoadLibraryExW_Original) != MH_OK) {
        LogError("Failed to create LoadLibraryExW hook");
        return false;
    }
    if (MH_EnableHook(LoadLibraryExW) != MH_OK) {
        LogError("Failed to enable LoadLibraryExW hook");
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

bool AreLoadLibraryHooksInstalled() {
    return g_loadlibrary_hooks_installed.load();
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


    if (lowerModuleName.find(L"sl.interposer.dll") != std::wstring::npos) {
        LogInfo("Installing Streamline hooks for module: %ws", moduleName.c_str());
        if (InstallStreamlineHooks()) {
            LogInfo("Streamline hooks installed successfully");
        } else {
            LogError("Failed to install Streamline hooks");
        }
    }

    // XInput hooks
    if (lowerModuleName.find(L"xinput") != std::wstring::npos) {
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


    // Direct3D hooks
    else if (lowerModuleName.find(L"d3d") != std::wstring::npos) {
        LogInfo("Direct3D module detected: %ws", moduleName.c_str());
        // TODO: Add Direct3D hook installation when implemented
    }

    // OpenGL hooks
    else if (lowerModuleName.find(L"opengl") != std::wstring::npos) {
        LogInfo("OpenGL module detected: %ws", moduleName.c_str());
        // TODO: Add OpenGL hook installation when implemented
    }

    // Vulkan hooks
    else if (lowerModuleName.find(L"vulkan") != std::wstring::npos) {
        LogInfo("Vulkan module detected: %ws", moduleName.c_str());
        // TODO: Add Vulkan hook installation when implemented
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

    // Steam hooks
    else if (lowerModuleName.find(L"steam") != std::wstring::npos) {
        LogInfo("Steam module detected: %ws", moduleName.c_str());
        // TODO: Add Steam hook installation when implemented
    }

    // Epic Games hooks
    else if (lowerModuleName.find(L"eos") != std::wstring::npos ||
    lowerModuleName.find(L"epic") != std::wstring::npos) {
        LogInfo("Epic Games module detected: %ws", moduleName.c_str());
        // TODO: Add Epic Games hook installation when implemented
    }

    // NVIDIA hooks
    else if (lowerModuleName.find(L"nv") != std::wstring::npos ||
    lowerModuleName.find(L"nvidia") != std::wstring::npos) {
        LogInfo("NVIDIA module detected: %ws", moduleName.c_str());
        // TODO: Add NVIDIA hook installation when implemented
    }

    // AMD hooks
    else if (lowerModuleName.find(L"amd") != std::wstring::npos ||
    lowerModuleName.find(L"ati") != std::wstring::npos) {
        LogInfo("AMD module detected: %ws", moduleName.c_str());
        // TODO: Add AMD hook installation when implemented
    }

    // Intel hooks
    else if (lowerModuleName.find(L"intel") != std::wstring::npos) {
        LogInfo("Intel module detected: %ws", moduleName.c_str());
        // TODO: Add Intel hook installation when implemented
    }

    // Generic logging for other modules
    else {
        LogInfo("Other module loaded: %ws (0x%p)", moduleName.c_str(), hModule);
    }
}

} // namespace display_commanderhooks
