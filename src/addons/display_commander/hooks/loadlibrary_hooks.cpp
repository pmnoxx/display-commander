#include "loadlibrary_hooks.hpp"
#include <MinHook.h>
#include <tlhelp32.h>
#include <algorithm>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <iomanip>
#include <set>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include "../globals.hpp"
#include "../settings/advanced_tab_settings.hpp"
#include "../settings/experimental_tab_settings.hpp"
#include "../settings/main_tab_settings.hpp"
#include "../settings/streamline_tab_settings.hpp"
#include "../utils/detour_call_tracker.hpp"
#include "../utils/general_utils.hpp"  // GetDefaultDlssOverrideFolder
#include "../utils/logging.hpp"
#include "../utils/platform_api_detector.hpp"
#include "../utils/timing.hpp"
#include "api_hooks.hpp"
#include "dbghelp_hooks.hpp"
#include "hook_suppression_manager.hpp"
#include "ngx_hooks.hpp"
#include "nvapi_hooks.hpp"
#include "streamline_hooks.hpp"
#include "utils/srwlock_wrapper.hpp"
#include "vulkan/nvlowlatencyvk_hooks.hpp"
#include "vulkan/vulkan_loader_hooks.hpp"
#include "windows_gaming_input_hooks.hpp"
#include "xinput_hooks.hpp"

// Declare K32EnumProcessModules (kernel32 variant, safe from DllMain)
extern "C" BOOL WINAPI K32EnumProcessModules(HANDLE hProcess, HMODULE* lphModule, DWORD cb, LPDWORD lpcbNeeded);

namespace display_commanderhooks {

// Helper function to check if a DLL is SpecialK (always blocked - incompatible with Display Commander)
bool ShouldBlockSpecialKDLL(const std::wstring& dll_path) {
    std::filesystem::path path(dll_path);
    std::wstring filename = path.filename().wstring();
    std::transform(filename.begin(), filename.end(), filename.begin(), ::towlower);
    return (filename == L"specialk32.dll" || filename == L"specialk64.dll");
}

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
        L"nvanselsdk.dll",       L"anselsdk64.dll", L"nvcamerasdk64.dll", L"nvcameraapi64.dll",
        L"gfexperiencecore.dll", L"nvcamera64.dll", L"nvcamera32.dll"};

    // Check if the DLL is in the Ansel list
    for (const auto& ansel_dll : ansel_dlls) {
        if (filename == ansel_dll) {
            return true;
        }
    }

    return false;
}

// Helper function to check if a DLL should be overridden and get the override path (per-DLL checkbox + subfolder)
std::wstring GetDLSSOverridePath(const std::wstring& dll_path) {
    if (!settings::g_streamlineTabSettings.dlss_override_enabled.GetValue()) {
        return L"";
    }

    std::filesystem::path path(dll_path);
    std::wstring filename = path.filename().wstring();
    std::transform(filename.begin(), filename.end(), filename.begin(), ::towlower);

    std::string subfolder;
    bool enabled = false;
    if (filename == L"nvngx_dlss.dll") {
        enabled = settings::g_streamlineTabSettings.dlss_override_dlss.GetValue();
        subfolder = settings::g_streamlineTabSettings.dlss_override_subfolder.GetValue();
    } else if (filename == L"nvngx_dlssd.dll") {
        // D = denoiser (RR)
        enabled = settings::g_streamlineTabSettings.dlss_override_dlss_rr.GetValue();
        subfolder = settings::g_streamlineTabSettings.dlss_override_subfolder_dlssd.GetValue();
    } else if (filename == L"nvngx_dlssg.dll") {
        // G = generation (FG)
        enabled = settings::g_streamlineTabSettings.dlss_override_dlss_fg.GetValue();
        subfolder = settings::g_streamlineTabSettings.dlss_override_subfolder_dlssg.GetValue();
    } else {
        return L"";
    }

    if (!enabled) {
        return L"";
    }
    std::filesystem::path primary_dir = GetEffectiveDefaultDlssOverrideFolder(subfolder);
    if (primary_dir.empty()) {
        return L"";
    }
    std::filesystem::path primary_file = primary_dir / std::filesystem::path(filename);
    if (std::filesystem::exists(primary_file)) {
        return primary_file.wstring();
    }
    return primary_file.wstring();
}

// Original function pointers
LoadLibraryA_pfn LoadLibraryA_Original = nullptr;
LoadLibraryW_pfn LoadLibraryW_Original = nullptr;
LoadLibraryExA_pfn LoadLibraryExA_Original = nullptr;
LoadLibraryExW_pfn LoadLibraryExW_Original = nullptr;
LoadPackagedLibrary_pfn LoadPackagedLibrary_Original = nullptr;
FreeLibrary_pfn FreeLibrary_Original = nullptr;
FreeLibraryAndExitThread_pfn FreeLibraryAndExitThread_Original = nullptr;

// LoadPackagedLibrary is resolved at runtime (Windows 8+); store target for Uninstall.
static LPVOID g_LoadPackagedLibrary_target = nullptr;

// LdrLoadDll (ntdll) - resolved at runtime; store target for Uninstall.
LdrLoadDll_pfn LdrLoadDll_Original = nullptr;
static LPVOID g_LdrLoadDll_target = nullptr;

// UNICODE_STRING layout (ntdll) for parsing DllName in LdrLoadDll_Detour
struct UnicodeStringNtdll {
    USHORT Length;
    USHORT MaximumLength;
    PWSTR Buffer;
};
static constexpr LONG STATUS_ACCESS_DENIED_NT = 0xC0000022L;

// GetModuleHandle / GetModuleHandleEx (for DLSS override: return override module so hooks and version reporting use it)
using GetModuleHandleW_pfn = HMODULE(WINAPI*)(LPCWSTR);
using GetModuleHandleA_pfn = HMODULE(WINAPI*)(LPCSTR);
using GetModuleHandleExW_pfn = BOOL(WINAPI*)(DWORD, LPCWSTR, HMODULE*);
using GetModuleHandleExA_pfn = BOOL(WINAPI*)(DWORD, LPCSTR, HMODULE*);
static GetModuleHandleW_pfn GetModuleHandleW_Original = nullptr;
static GetModuleHandleA_pfn GetModuleHandleA_Original = nullptr;
static GetModuleHandleExW_pfn GetModuleHandleExW_Original = nullptr;
static GetModuleHandleExA_pfn GetModuleHandleExA_Original = nullptr;

// DLSS override: logical module name (lowercase) -> HMODULE we loaded via redirect (so GetModuleHandle returns this)
static std::unordered_map<std::wstring, HMODULE> g_dlss_override_handles;
static SRWLOCK g_dlss_override_handles_srwlock = SRWLOCK_INIT;

static const wchar_t* k_dlss_dll_names[] = {L"nvngx_dlss.dll", L"nvngx_dlssd.dll", L"nvngx_dlssg.dll"};

static std::wstring ToLowerModuleName(LPCWSTR name) {
    if (!name || !*name) return std::wstring();
    std::wstring s = std::filesystem::path(name).filename().wstring();
    std::transform(s.begin(), s.end(), s.begin(), ::towlower);
    return s;
}

static std::wstring ToLowerModuleName(LPCSTR name) {
    if (!name || !*name) return std::wstring();
    std::string n(name);
    std::wstring w(n.begin(), n.end());
    return ToLowerModuleName(w.c_str());
}

static bool IsDlssOverrideDllName(const std::wstring& lowerName) {
    for (const wchar_t* dll : k_dlss_dll_names) {
        std::wstring lower(dll);
        std::transform(lower.begin(), lower.end(), lower.begin(), ::towlower);
        if (lowerName == lower) return true;
    }
    return false;
}

static void RecordDlssOverrideHandle(const std::wstring& logicalName, HMODULE hMod) {
    std::wstring key = ToLowerModuleName(logicalName.c_str());
    if (!IsDlssOverrideDllName(key)) return;
    utils::SRWLockExclusive lock(g_dlss_override_handles_srwlock);
    g_dlss_override_handles[key] = hMod;
}

static HMODULE GetDlssOverrideHandle(const std::wstring& logicalName) {
    std::wstring key = ToLowerModuleName(logicalName.c_str());
    if (!IsDlssOverrideDllName(key)) return nullptr;
    utils::SRWLockShared lock(g_dlss_override_handles_srwlock);
    auto it = g_dlss_override_handles.find(key);
    return (it != g_dlss_override_handles.end()) ? it->second : nullptr;
}

static void RemoveDlssOverrideHandle(HMODULE hMod) {
    if (!hMod) return;
    utils::SRWLockExclusive lock(g_dlss_override_handles_srwlock);
    for (auto it = g_dlss_override_handles.begin(); it != g_dlss_override_handles.end();) {
        if (it->second == hMod)
            it = g_dlss_override_handles.erase(it);
        else
            ++it;
    }
}

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
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;

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
        HANDLE hFile = CreateFileW(modulePath, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
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
    RECORD_DETOUR_CALL(utils::get_now_ns());
    std::string timestamp = GetCurrentTimestamp();
    std::string dll_name = lpLibFileName ? lpLibFileName : "NULL";

    LogInfo("[%s] LoadLibraryA called: %s", timestamp.c_str(), dll_name.c_str());

    // Check for SpecialK blocking (incompatible with Display Commander)
    if (lpLibFileName) {
        std::wstring w_dll_name = std::wstring(dll_name.begin(), dll_name.end());
        if (ShouldBlockSpecialKDLL(w_dll_name)) {
            LogInfo("[%s] SpecialK Block: Blocking %s from loading", timestamp.c_str(), dll_name.c_str());
            SetLastError(ERROR_ACCESS_DENIED);
            return nullptr;
        }
    }

    // Check for Ansel blocking
    if (lpLibFileName) {
        std::wstring w_dll_name = std::wstring(dll_name.begin(), dll_name.end());
        if (ShouldBlockAnselDLL(w_dll_name)) {
            LogInfo("[%s] Ansel Block: Blocking %s from loading", timestamp.c_str(), dll_name.c_str());
            return nullptr;  // Return nullptr to indicate failure to load
        }
    }

    // Check for user-defined DLL blocking
    if (lpLibFileName) {
        std::wstring w_dll_name = std::wstring(dll_name.begin(), dll_name.end());
        if (ShouldBlockDLL(w_dll_name)) {
            LogInfo("[%s] DLL Block: Blocking %s from loading", timestamp.c_str(), dll_name.c_str());
            return nullptr;  // Return nullptr to indicate failure to load
        }
    }

    // Check for DLSS override
    LPCSTR actual_lib_file_name = lpLibFileName;
    bool used_dlss_override = false;

    if (lpLibFileName) {
        std::wstring w_dll_name = std::wstring(dll_name.begin(), dll_name.end());
        std::wstring override_path = GetDLSSOverridePath(w_dll_name);

        if (!override_path.empty()) {
            // Check if override file exists
            if (std::filesystem::exists(override_path)) {
                std::string narrow_override_path = WideToNarrow(override_path);
                actual_lib_file_name = narrow_override_path.c_str();
                used_dlss_override = true;
                LogInfo("[%s] DLSS Override: Redirecting %s to %s", timestamp.c_str(), dll_name.c_str(),
                        narrow_override_path.c_str());
            } else {
                LogInfo("[%s] DLSS Override: Override file not found: %s", timestamp.c_str(),
                        WideToNarrow(override_path).c_str());
            }
        }
    }

    // Call original function with potentially overridden path
    HMODULE result =
        LoadLibraryA_Original ? LoadLibraryA_Original(actual_lib_file_name) : LoadLibraryA(actual_lib_file_name);

    if (result && used_dlss_override) {
        RecordDlssOverrideHandle(std::wstring(dll_name.begin(), dll_name.end()), result);
    }

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

                LogInfo("Added new module to tracking: %s (0x%p, %u bytes)", dll_name.c_str(), moduleInfo.baseAddress,
                        moduleInfo.sizeOfImage);

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
    RECORD_DETOUR_CALL(utils::get_now_ns());
    std::string timestamp = GetCurrentTimestamp();
    std::string dll_name = lpLibFileName ? WideToNarrow(lpLibFileName) : "NULL";

    LogInfo("[%s] LoadLibraryW called: %s", timestamp.c_str(), dll_name.c_str());

    // Check for SpecialK blocking (incompatible with Display Commander)
    if (lpLibFileName) {
        std::wstring w_dll_name = lpLibFileName;
        if (ShouldBlockSpecialKDLL(w_dll_name)) {
            LogInfo("[%s] SpecialK Block: Blocking %s from loading", timestamp.c_str(), dll_name.c_str());
            SetLastError(ERROR_ACCESS_DENIED);
            return nullptr;
        }
    }

    // Check for Ansel blocking
    if (lpLibFileName) {
        std::wstring w_dll_name = lpLibFileName;
        if (ShouldBlockAnselDLL(w_dll_name)) {
            LogInfo("[%s] Ansel Block: Blocking %s from loading", timestamp.c_str(), dll_name.c_str());
            return nullptr;  // Return nullptr to indicate failure to load
        }
    }

    // Check for user-defined DLL blocking
    if (lpLibFileName) {
        std::wstring w_dll_name = lpLibFileName;
        if (ShouldBlockDLL(w_dll_name)) {
            LogInfo("[%s] DLL Block: Blocking %s from loading", timestamp.c_str(), dll_name.c_str());
            return nullptr;  // Return nullptr to indicate failure to load
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
                LogInfo("[%s] DLSS Override: Redirecting %s to %s", timestamp.c_str(), dll_name.c_str(),
                        WideToNarrow(override_path).c_str());
            } else {
                LogInfo("[%s] DLSS Override: Override file not found: %s", timestamp.c_str(),
                        WideToNarrow(override_path).c_str());
            }
        }
    }

    // Call original function with potentially overridden path
    HMODULE result =
        LoadLibraryW_Original ? LoadLibraryW_Original(actual_lib_file_name) : LoadLibraryW(actual_lib_file_name);

    if (result && !override_path.empty() && std::filesystem::exists(override_path)) {
        RecordDlssOverrideHandle(lpLibFileName, result);
    }

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

                LogInfo("Added new module to tracking: %s (0x%p, %u bytes)", dll_name.c_str(), moduleInfo.baseAddress,
                        moduleInfo.sizeOfImage);

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
    RECORD_DETOUR_CALL(utils::get_now_ns());
    std::string timestamp = GetCurrentTimestamp();
    std::string dll_name = lpLibFileName ? lpLibFileName : "NULL";

    LogInfo("[%s] LoadLibraryExA called: %s, hFile: 0x%p, dwFlags: 0x%08X", timestamp.c_str(), dll_name.c_str(), hFile,
            dwFlags);

    // Check for SpecialK blocking (incompatible with Display Commander)
    if (lpLibFileName) {
        std::wstring w_dll_name = std::wstring(dll_name.begin(), dll_name.end());
        if (ShouldBlockSpecialKDLL(w_dll_name)) {
            LogInfo("[%s] SpecialK Block: Blocking %s from loading", timestamp.c_str(), dll_name.c_str());
            SetLastError(ERROR_ACCESS_DENIED);
            return nullptr;
        }
    }

    // Check for Ansel blocking
    if (lpLibFileName) {
        std::wstring w_dll_name = std::wstring(dll_name.begin(), dll_name.end());
        if (ShouldBlockAnselDLL(w_dll_name)) {
            LogInfo("[%s] Ansel Block: Blocking %s from loading", timestamp.c_str(), dll_name.c_str());
            return nullptr;  // Return nullptr to indicate failure to load
        }
    }

    // Check for user-defined DLL blocking
    if (lpLibFileName) {
        std::wstring w_dll_name = std::wstring(dll_name.begin(), dll_name.end());
        if (ShouldBlockDLL(w_dll_name)) {
            LogInfo("[%s] DLL Block: Blocking %s from loading", timestamp.c_str(), dll_name.c_str());
            return nullptr;  // Return nullptr to indicate failure to load
        }
    }

    // Check for DLSS override
    LPCSTR actual_lib_file_name = lpLibFileName;
    bool used_dlss_override_exa = false;

    if (lpLibFileName) {
        std::wstring w_dll_name = std::wstring(dll_name.begin(), dll_name.end());
        std::wstring override_path = GetDLSSOverridePath(w_dll_name);

        if (!override_path.empty()) {
            // Check if override file exists
            if (std::filesystem::exists(override_path)) {
                std::string narrow_override_path = WideToNarrow(override_path);
                actual_lib_file_name = narrow_override_path.c_str();
                used_dlss_override_exa = true;
                LogInfo("[%s] DLSS Override: Redirecting %s to %s", timestamp.c_str(), dll_name.c_str(),
                        narrow_override_path.c_str());
            } else {
                LogInfo("[%s] DLSS Override: Override file not found: %s", timestamp.c_str(),
                        WideToNarrow(override_path).c_str());
            }
        }
    }

    // Call original function with potentially overridden path
    HMODULE result = LoadLibraryExA_Original ? LoadLibraryExA_Original(actual_lib_file_name, hFile, dwFlags)
                                             : LoadLibraryExA(actual_lib_file_name, hFile, dwFlags);

    if (result && used_dlss_override_exa) {
        RecordDlssOverrideHandle(std::wstring(dll_name.begin(), dll_name.end()), result);
    }

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

                LogInfo("Added new module to tracking: %s (0x%p, %u bytes)", dll_name.c_str(), moduleInfo.baseAddress,
                        moduleInfo.sizeOfImage);

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
    RECORD_DETOUR_CALL(utils::get_now_ns());
    std::string timestamp = GetCurrentTimestamp();
    std::string dll_name = lpLibFileName ? WideToNarrow(lpLibFileName) : "NULL";

    LogInfo("[%s] LoadLibraryExW called: %s, hFile: 0x%p, dwFlags: 0x%08X", timestamp.c_str(), dll_name.c_str(), hFile,
            dwFlags);

    // Check for SpecialK blocking (incompatible with Display Commander)
    if (lpLibFileName) {
        std::wstring w_dll_name = lpLibFileName;
        if (ShouldBlockSpecialKDLL(w_dll_name)) {
            LogInfo("[%s] SpecialK Block: Blocking %s from loading", timestamp.c_str(), dll_name.c_str());
            SetLastError(ERROR_ACCESS_DENIED);
            return nullptr;
        }
    }

    // Check for Ansel blocking
    if (lpLibFileName) {
        std::wstring w_dll_name = lpLibFileName;
        if (ShouldBlockAnselDLL(w_dll_name)) {
            LogInfo("[%s] Ansel Block: Blocking %s from loading", timestamp.c_str(), dll_name.c_str());
            return nullptr;  // Return nullptr to indicate failure to load
        }
    }

    // Check for user-defined DLL blocking
    if (lpLibFileName) {
        std::wstring w_dll_name = lpLibFileName;
        if (ShouldBlockDLL(w_dll_name)) {
            LogInfo("[%s] DLL Block: Blocking %s from loading", timestamp.c_str(), dll_name.c_str());
            return nullptr;  // Return nullptr to indicate failure to load
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
                LogInfo("[%s] DLSS Override: Redirecting %s to %s", timestamp.c_str(), dll_name.c_str(),
                        WideToNarrow(override_path).c_str());
            } else {
                LogInfo("[%s] DLSS Override: Override file not found: %s", timestamp.c_str(),
                        WideToNarrow(override_path).c_str());
            }
        }
    }

    // Call original function with potentially overridden path
    HMODULE result = LoadLibraryExW_Original ? LoadLibraryExW_Original(actual_lib_file_name, hFile, dwFlags)
                                             : LoadLibraryExW(actual_lib_file_name, hFile, dwFlags);

    if (result && !override_path.empty() && std::filesystem::exists(override_path)) {
        RecordDlssOverrideHandle(lpLibFileName, result);
    }

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

                LogInfo("Added new module to tracking: %s (0x%p, %u bytes)", dll_name.c_str(), moduleInfo.baseAddress,
                        moduleInfo.sizeOfImage);

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

// Hooked LoadPackagedLibrary (Windows 8+; UWP packaged apps). Same blocking/tracking as LoadLibraryW; no DLSS path
// override (package full name is not a file path).
HMODULE WINAPI LoadPackagedLibrary_Detour(LPCWSTR lpwszPackageFullName, DWORD Reserved) {
    RECORD_DETOUR_CALL(utils::get_now_ns());
    std::string timestamp = GetCurrentTimestamp();
    std::string name_str = lpwszPackageFullName ? WideToNarrow(lpwszPackageFullName) : "NULL";

    LogInfo("[%s] LoadPackagedLibrary called: %s, Reserved: 0x%08X", timestamp.c_str(), name_str.c_str(), Reserved);

    if (lpwszPackageFullName) {
        std::wstring w_name = lpwszPackageFullName;
        if (ShouldBlockSpecialKDLL(w_name)) {
            LogInfo("[%s] SpecialK Block: Blocking packaged lib %s from loading", timestamp.c_str(), name_str.c_str());
            SetLastError(ERROR_ACCESS_DENIED);
            return nullptr;
        }
        if (ShouldBlockAnselDLL(w_name)) {
            LogInfo("[%s] Ansel Block: Blocking packaged lib %s from loading", timestamp.c_str(), name_str.c_str());
            return nullptr;
        }
        if (ShouldBlockDLL(w_name)) {
            LogInfo("[%s] DLL Block: Blocking packaged lib %s from loading", timestamp.c_str(), name_str.c_str());
            return nullptr;
        }
    }

    // Original is always set when this hook is installed; no fallback (API resolved at install time).
    HMODULE result =
        LoadPackagedLibrary_Original ? LoadPackagedLibrary_Original(lpwszPackageFullName, Reserved) : nullptr;

    if (result) {
        LogInfo("[%s] LoadPackagedLibrary success: %s -> HMODULE: 0x%p", timestamp.c_str(), name_str.c_str(), result);
        {
            utils::SRWLockExclusive lock(g_module_srwlock);
            if (g_module_handles.find(result) == g_module_handles.end()) {
                ModuleInfo moduleInfo;
                moduleInfo.hModule = result;
                moduleInfo.moduleName = std::wstring(name_str.begin(), name_str.end());
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
                moduleInfo.loadedBeforeHooks = false;
                g_loaded_modules.push_back(moduleInfo);
                g_module_handles.insert(result);
                LogInfo("Added new module to tracking (LoadPackagedLibrary): %s (0x%p)", name_str.c_str(),
                        moduleInfo.baseAddress);
                OnModuleLoaded(moduleInfo.moduleName, result);
            }
        }
    } else {
        DWORD error = GetLastError();
        LogInfo("[%s] LoadPackagedLibrary failed: %s -> Error: %lu", timestamp.c_str(), name_str.c_str(), error);
    }

    return result;
}

// Hooked LdrLoadDll (ntdll). Catches loads that bypass kernel32 (e.g. direct ntdll calls). Blocking + tracking only
// (no DLSS path override from this path).
LONG NTAPI LdrLoadDll_Detour(PWSTR DllPath, PULONG DllCharacteristics, const void* DllName, PVOID* DllHandle) {
    RECORD_DETOUR_CALL(utils::get_now_ns());
    const auto* name = static_cast<const UnicodeStringNtdll*>(DllName);
    std::wstring dll_name_wide;
    if (name != nullptr && name->Buffer != nullptr && name->Length > 0) {
        size_t char_count = name->Length / sizeof(wchar_t);
        dll_name_wide.assign(name->Buffer, char_count);
    }
    std::string dll_name = WideToNarrow(dll_name_wide);
    std::string timestamp = GetCurrentTimestamp();
    // Only log "called" when the library is not already in our tracked set (avoids spam for e.g. gdi32.dll)
    const bool already_loaded = !dll_name_wide.empty() && IsModuleLoaded(dll_name_wide);
    if (!already_loaded) {
        LogInfo("[%s] LdrLoadDll called: %s", timestamp.c_str(), dll_name.empty() ? "(no name)" : dll_name.c_str());
    }

    if (!dll_name_wide.empty()) {
        if (ShouldBlockSpecialKDLL(dll_name_wide)) {
            LogInfo("[%s] SpecialK Block (LdrLoadDll): Blocking %s", timestamp.c_str(), dll_name.c_str());
            if (DllHandle != nullptr) {
                *DllHandle = nullptr;
            }
            return STATUS_ACCESS_DENIED_NT;
        }
        if (ShouldBlockAnselDLL(dll_name_wide)) {
            LogInfo("[%s] Ansel Block (LdrLoadDll): Blocking %s", timestamp.c_str(), dll_name.c_str());
            if (DllHandle != nullptr) {
                *DllHandle = nullptr;
            }
            return STATUS_ACCESS_DENIED_NT;
        }
        if (ShouldBlockDLL(dll_name_wide)) {
            LogInfo("[%s] DLL Block (LdrLoadDll): Blocking %s", timestamp.c_str(), dll_name.c_str());
            if (DllHandle != nullptr) {
                *DllHandle = nullptr;
            }
            return STATUS_ACCESS_DENIED_NT;
        }
    }

    if (!LdrLoadDll_Original || DllHandle == nullptr) {
        return STATUS_ACCESS_DENIED_NT;
    }
    LONG status = LdrLoadDll_Original(DllPath, DllCharacteristics, DllName, DllHandle);
    PVOID base = *DllHandle;

    if (status == 0 && base != nullptr) {
        HMODULE hMod = static_cast<HMODULE>(base);
        bool added = false;
        {
            utils::SRWLockExclusive lock(g_module_srwlock);
            if (g_module_handles.find(hMod) == g_module_handles.end()) {
                added = true;
                ModuleInfo moduleInfo;
                moduleInfo.hModule = hMod;
                moduleInfo.moduleName = dll_name_wide.empty() ? L"Unknown" : dll_name_wide;
                wchar_t modulePath[MAX_PATH];
                if (GetModuleFileNameW(hMod, modulePath, MAX_PATH)) {
                    moduleInfo.fullPath = modulePath;
                }
                MODULEINFO modInfo;
                if (GetModuleInformation(GetCurrentProcess(), hMod, &modInfo, sizeof(modInfo))) {
                    moduleInfo.baseAddress = modInfo.lpBaseOfDll;
                    moduleInfo.sizeOfImage = modInfo.SizeOfImage;
                    moduleInfo.entryPoint = modInfo.EntryPoint;
                }
                moduleInfo.loadTime = GetModuleFileTime(hMod);
                moduleInfo.loadedBeforeHooks = false;
                g_loaded_modules.push_back(moduleInfo);
                g_module_handles.insert(hMod);
                LogInfo("Added new module to tracking (LdrLoadDll): %s (0x%p)",
                        dll_name.empty() ? "Unknown" : dll_name.c_str(), moduleInfo.baseAddress);
                OnModuleLoaded(moduleInfo.moduleName, hMod);
            }
        }
        // Only log success when we actually added (avoids spam when library was already loaded)
        if (added) {
            LogInfo("[%s] LdrLoadDll success: %s -> 0x%p", timestamp.c_str(),
                    dll_name.empty() ? "(no name)" : dll_name.c_str(), base);
        }
    } else if (status != 0) {
        LogInfo("[%s] LdrLoadDll failed: %s -> NTSTATUS 0x%08lX", timestamp.c_str(),
                dll_name.empty() ? "(no name)" : dll_name.c_str(), static_cast<unsigned long>(status));
    }

    return status;
}

// Hooked GetModuleHandleW: return DLSS override module when we loaded it via redirect (so hooks and version use it)
HMODULE WINAPI GetModuleHandleW_Detour(LPCWSTR lpModuleName) {
    HMODULE override_handle = GetDlssOverrideHandle(lpModuleName ? lpModuleName : L"");
    if (override_handle != nullptr) {
        return override_handle;
    }
    return GetModuleHandleW_Original ? GetModuleHandleW_Original(lpModuleName) : GetModuleHandleW(lpModuleName);
}

// Hooked GetModuleHandleA: same for ANSI
HMODULE WINAPI GetModuleHandleA_Detour(LPCSTR lpModuleName) {
    if (lpModuleName && *lpModuleName) {
        std::wstring wkey = ToLowerModuleName(lpModuleName);
        HMODULE override_handle = GetDlssOverrideHandle(wkey);
        if (override_handle != nullptr) {
            return override_handle;
        }
    }
    return GetModuleHandleA_Original ? GetModuleHandleA_Original(lpModuleName) : GetModuleHandleA(lpModuleName);
}

// Hooked GetModuleHandleExW: return override when querying by name (not by FROM_ADDRESS)
BOOL WINAPI GetModuleHandleExW_Detour(DWORD dwFlags, LPCWSTR lpModuleName, HMODULE* phModule) {
    constexpr DWORD k_from_address = 0x00000004;  // GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
    if (phModule == nullptr) {
        return GetModuleHandleExW_Original ? GetModuleHandleExW_Original(dwFlags, lpModuleName, phModule)
                                           : GetModuleHandleExW(dwFlags, lpModuleName, phModule);
    }
    if ((dwFlags & k_from_address) == 0 && lpModuleName && *lpModuleName) {
        HMODULE override_handle = GetDlssOverrideHandle(lpModuleName);
        if (override_handle != nullptr) {
            *phModule = override_handle;
            return TRUE;
        }
    }
    return GetModuleHandleExW_Original ? GetModuleHandleExW_Original(dwFlags, lpModuleName, phModule)
                                       : GetModuleHandleExW(dwFlags, lpModuleName, phModule);
}

// Hooked GetModuleHandleExA: same for ANSI
BOOL WINAPI GetModuleHandleExA_Detour(DWORD dwFlags, LPCSTR lpModuleName, HMODULE* phModule) {
    constexpr DWORD k_from_address = 0x00000004;  // GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
    if (phModule == nullptr) {
        return GetModuleHandleExA_Original ? GetModuleHandleExA_Original(dwFlags, lpModuleName, phModule)
                                           : GetModuleHandleExA(dwFlags, lpModuleName, phModule);
    }
    if ((dwFlags & k_from_address) == 0 && lpModuleName && *lpModuleName) {
        std::wstring wkey = ToLowerModuleName(lpModuleName);
        HMODULE override_handle = GetDlssOverrideHandle(wkey);
        if (override_handle != nullptr) {
            *phModule = override_handle;
            return TRUE;
        }
    }
    return GetModuleHandleExA_Original ? GetModuleHandleExA_Original(dwFlags, lpModuleName, phModule)
                                       : GetModuleHandleExA(dwFlags, lpModuleName, phModule);
}

// Hooked FreeLibrary function
BOOL WINAPI FreeLibrary_Detour(HMODULE hLibModule) {
    RECORD_DETOUR_CALL(utils::get_now_ns());

    // Check if this is the ReShade module being unloaded
    bool is_reshade_module = (hLibModule != nullptr && hLibModule == g_reshade_module);

    // Call original function first to get the result
    BOOL result = FreeLibrary_Original ? FreeLibrary_Original(hLibModule) : FreeLibrary(hLibModule);

    // When refcount reaches 0 (result is FALSE), stop returning this handle from GetModuleHandle
    if (result == FALSE && hLibModule != nullptr) {
        RemoveDlssOverrideHandle(hLibModule);
    }

    // Only clear if refcount reached 0 (result is FALSE) and it's the ReShade module
    if (is_reshade_module && result == FALSE) {
        LogInfo("FreeLibrary: Detected ReShade module unload (refcount reached 0) (0x%p)", hLibModule);
        OnReshadeUnload();
        g_reshade_module = nullptr;
    }

    return result;
}

// Hooked FreeLibraryAndExitThread function
VOID WINAPI FreeLibraryAndExitThread_Detour(HMODULE hLibModule, DWORD dwExitCode) {
    // Record detour call but don't create guard (this function never returns, so guard would be incorrectly flagged as
    // crash)
    static const uint32_t s_fle_detour_idx = detour_call_tracker::AllocateEntryIndex(DETOUR_CALL_SITE_KEY);
    detour_call_tracker::RecordCallNoGuard(s_fle_detour_idx, utils::get_now_ns());

    // Check if this is the ReShade module being unloaded
    // FreeLibraryAndExitThread always unloads the module (doesn't check refcount)
    if (hLibModule != nullptr && hLibModule == g_reshade_module) {
        LogInfo("FreeLibraryAndExitThread: Detected ReShade module unload (0x%p)", hLibModule);
        OnReshadeUnload();
        g_reshade_module = nullptr;
    }

    // Call original function (this function never returns)
    if (FreeLibraryAndExitThread_Original) {
        FreeLibraryAndExitThread_Original(hLibModule, dwExitCode);
    } else {
        FreeLibraryAndExitThread(hLibModule, dwExitCode);
    }
}

bool InstallLoadLibraryHooks() {
    // Initialize MinHook before enumerating modules so that OnModuleLoaded -> InstallNGXHooks (etc.)
    // can use CreateAndEnableHook when _nvngx.dll or other modules are already loaded at startup.
    MH_STATUS init_status = SafeInitializeMinHook(display_commanderhooks::HookType::LOADLIBRARY);
    if (init_status != MH_OK && init_status != MH_ERROR_ALREADY_INITIALIZED) {
        LogError("Failed to initialize MinHook for LoadLibrary hooks - Status: %d", init_status);
        if (!EnumerateLoadedModules()) {
            LogError("Failed to enumerate loaded modules, but continuing with hook installation");
        }
        return false;
    }

    if (init_status == MH_ERROR_ALREADY_INITIALIZED) {
        LogInfo("MinHook already initialized, proceeding with LoadLibrary hooks");
    } else {
        LogInfo("MinHook initialized successfully for LoadLibrary hooks");
    }

    if (g_loadlibrary_hooks_installed.load()) {
        if (!EnumerateLoadedModules()) {
            LogError("Failed to enumerate loaded modules, but continuing with hook installation");
        }
        LogInfo("LoadLibrary hooks already installed");
        return true;
    }

    // Check if LoadLibrary hooks should be suppressed
    if (display_commanderhooks::HookSuppressionManager::GetInstance().ShouldSuppressHook(
            display_commanderhooks::HookType::LOADLIBRARY)) {
        if (!EnumerateLoadedModules()) {
            LogError("Failed to enumerate loaded modules, but continuing with hook installation");
        }
        LogInfo("LoadLibrary hooks installation suppressed by user setting");
        return false;
    }
    // Load blocked DLLs list BEFORE installing hooks to ensure blocking works
    if (settings::g_experimentalTabSettings.dll_blocking_enabled.GetValue()) {
        settings::g_experimentalTabSettings.blocked_dlls.Load();
        if (!settings::g_experimentalTabSettings.blocked_dlls.GetValue().empty()) {
            LoadBlockedDLLsFromSettings(settings::g_experimentalTabSettings.blocked_dlls.GetValue());
            LogInfo("Loaded blocked DLLs list: %s",
                    settings::g_experimentalTabSettings.blocked_dlls.GetValue().c_str());
        } else {
            LogInfo("No blocked DLLs configured");
        }
    } else {
        LogInfo("DLL blocking is disabled in experimental settings");
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
    if (!CreateAndEnableHook(LoadLibraryExA, LoadLibraryExA_Detour, (LPVOID*)&LoadLibraryExA_Original,
                             "LoadLibraryExA")) {
        LogError("Failed to create and enable LoadLibraryExA hook");
        return false;
    }

    // Hook LoadLibraryExW
    if (!CreateAndEnableHook(LoadLibraryExW, LoadLibraryExW_Detour, (LPVOID*)&LoadLibraryExW_Original,
                             "LoadLibraryExW")) {
        LogError("Failed to create and enable LoadLibraryExW hook");
        return false;
    }

    // Hook LoadPackagedLibrary (Windows 8+; optional - skip if not available e.g. Windows 7)
    HMODULE hKernel32 = GetModuleHandleW(L"kernel32.dll");
    if (hKernel32) {
        auto pLoadPackagedLibrary =
            reinterpret_cast<LoadPackagedLibrary_pfn>(GetProcAddress(hKernel32, "LoadPackagedLibrary"));
        if (pLoadPackagedLibrary != nullptr) {
            g_LoadPackagedLibrary_target = reinterpret_cast<LPVOID>(pLoadPackagedLibrary);
            if (!CreateAndEnableHook(g_LoadPackagedLibrary_target, LoadPackagedLibrary_Detour,
                                     (LPVOID*)&LoadPackagedLibrary_Original, "LoadPackagedLibrary")) {
                LogError("Failed to create and enable LoadPackagedLibrary hook");
                g_LoadPackagedLibrary_target = nullptr;
            }
        } else {
            LogInfo("LoadPackagedLibrary not available (e.g. Windows 7), skipping hook");
        }
    }

    // Hook LdrLoadDll (ntdll) - catches loads that bypass kernel32
    HMODULE hNtdll = GetModuleHandleW(L"ntdll.dll");
    if (hNtdll != nullptr) {
        auto pLdrLoadDll = reinterpret_cast<LdrLoadDll_pfn>(GetProcAddress(hNtdll, "LdrLoadDll"));
        if (pLdrLoadDll != nullptr) {
            g_LdrLoadDll_target = reinterpret_cast<LPVOID>(pLdrLoadDll);
            if (!CreateAndEnableHook(g_LdrLoadDll_target, LdrLoadDll_Detour,
                                     reinterpret_cast<LPVOID*>(&LdrLoadDll_Original), "LdrLoadDll")) {
                LogError("Failed to create and enable LdrLoadDll hook");
                g_LdrLoadDll_target = nullptr;
            } else {
                LogInfo("LdrLoadDll hook installed");
            }
        } else {
            LogInfo("LdrLoadDll not found in ntdll, skipping hook");
        }
    }

    // Hook GetModuleHandleW / GetModuleHandleA / GetModuleHandleEx (so DLSS override handle is returned for
    // hooks/version)
    if (!CreateAndEnableHook(GetModuleHandleW, GetModuleHandleW_Detour, (LPVOID*)&GetModuleHandleW_Original,
                             "GetModuleHandleW")) {
        LogError("Failed to create and enable GetModuleHandleW hook");
        return false;
    }
    if (!CreateAndEnableHook(GetModuleHandleA, GetModuleHandleA_Detour, (LPVOID*)&GetModuleHandleA_Original,
                             "GetModuleHandleA")) {
        LogError("Failed to create and enable GetModuleHandleA hook");
        return false;
    }
    if (!CreateAndEnableHook(GetModuleHandleExW, GetModuleHandleExW_Detour, (LPVOID*)&GetModuleHandleExW_Original,
                             "GetModuleHandleExW")) {
        LogError("Failed to create and enable GetModuleHandleExW hook");
        return false;
    }
    if (!CreateAndEnableHook(GetModuleHandleExA, GetModuleHandleExA_Detour, (LPVOID*)&GetModuleHandleExA_Original,
                             "GetModuleHandleExA")) {
        LogError("Failed to create and enable GetModuleHandleExA hook");
        return false;
    }

    // Hook FreeLibrary
    if (!CreateAndEnableHook(FreeLibrary, FreeLibrary_Detour, (LPVOID*)&FreeLibrary_Original, "FreeLibrary")) {
        LogError("Failed to create and enable FreeLibrary hook");
        return false;
    }

    // Hook FreeLibraryAndExitThread
    if (!CreateAndEnableHook(FreeLibraryAndExitThread, FreeLibraryAndExitThread_Detour,
                             (LPVOID*)&FreeLibraryAndExitThread_Original, "FreeLibraryAndExitThread")) {
        LogError("Failed to create and enable FreeLibraryAndExitThread hook");
        return false;
    }

    g_loadlibrary_hooks_installed.store(true);
    LogInfo("LoadLibrary hooks installed successfully");

    if (!EnumerateLoadedModules()) {
        LogError("Failed to enumerate loaded modules, but continuing with hook installation");
    }
    // Mark LoadLibrary hooks as installed
    display_commanderhooks::HookSuppressionManager::GetInstance().MarkHookInstalled(
        display_commanderhooks::HookType::LOADLIBRARY);

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
    if (g_LoadPackagedLibrary_target != nullptr) {
        MH_RemoveHook(g_LoadPackagedLibrary_target);
        g_LoadPackagedLibrary_target = nullptr;
    }
    if (g_LdrLoadDll_target != nullptr) {
        MH_RemoveHook(g_LdrLoadDll_target);
        g_LdrLoadDll_target = nullptr;
    }
    MH_RemoveHook(GetModuleHandleW);
    MH_RemoveHook(GetModuleHandleA);
    MH_RemoveHook(GetModuleHandleExW);
    MH_RemoveHook(GetModuleHandleExA);
    MH_RemoveHook(FreeLibrary);
    MH_RemoveHook(FreeLibraryAndExitThread);

    // Uninstall library-specific hooks
    UninstallNVAPIHooks();

    // Clean up
    LoadLibraryA_Original = nullptr;
    LoadLibraryW_Original = nullptr;
    LoadLibraryExA_Original = nullptr;
    LoadLibraryExW_Original = nullptr;
    LoadPackagedLibrary_Original = nullptr;
    LdrLoadDll_Original = nullptr;
    GetModuleHandleW_Original = nullptr;
    GetModuleHandleA_Original = nullptr;
    GetModuleHandleExW_Original = nullptr;
    GetModuleHandleExA_Original = nullptr;
    FreeLibrary_Original = nullptr;
    FreeLibraryAndExitThread_Original = nullptr;
    {
        utils::SRWLockExclusive lock(g_dlss_override_handles_srwlock);
        g_dlss_override_handles.clear();
    }

    g_loadlibrary_hooks_installed.store(false);
    LogInfo("LoadLibrary hooks uninstalled successfully");
}

// modules_loaded_late_without_noticing is needed for Doom Dark Ages
// NvLowLatencyVk.dll - gets loaded by unknown api
// dinput9_1.dll - gets loaded by unknown api
bool EnumerateLoadedModules(bool modules_loaded_late_without_noticing) {
    utils::SRWLockExclusive lock(g_module_srwlock);

    if (!modules_loaded_late_without_noticing) {
        g_loaded_modules.clear();
        g_module_handles.clear();
    }

    constexpr DWORD kMaxModules = 1024;
    HMODULE hModules[kMaxModules];
    DWORD cbNeeded = 0;

    HANDLE hProcess = GetCurrentProcess();
    if (!EnumProcessModules(hProcess, hModules, sizeof(hModules), &cbNeeded)) {
        LogError("Failed to enumerate process modules - Error: %lu", GetLastError());
        return false;
    }

    const DWORD moduleCount = (cbNeeded / sizeof(HMODULE) < kMaxModules) ? (cbNeeded / sizeof(HMODULE)) : kMaxModules;
    if (!modules_loaded_late_without_noticing) {
        LogInfo("Found %lu loaded modules", moduleCount);
    }

    int added = 0;
    for (DWORD i = 0; i < moduleCount; i++) {
        if (g_module_handles.find(hModules[i]) != g_module_handles.end()) {
            continue;
        }

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

        // Late enumeration: we discovered this module now; initial enumeration: loaded before our hooks
        moduleInfo.loadedBeforeHooks = !modules_loaded_late_without_noticing;

        g_loaded_modules.push_back(moduleInfo);
        g_module_handles.insert(hModules[i]);
        added++;

        if (modules_loaded_late_without_noticing) {
            LogInfo("Late enumeration: added %ws (0x%p) - was loaded without us noticing",
                    moduleInfo.moduleName.c_str(), moduleInfo.baseAddress);
        } else {
            LogInfo("Module %lu: %ws (0x%p, %u bytes)", i, moduleInfo.moduleName.c_str(), moduleInfo.baseAddress,
                    moduleInfo.sizeOfImage);
        }

        // Call the module loaded callback (so hooks can be installed for this module)
        OnModuleLoaded(moduleInfo.moduleName, hModules[i]);
    }

    if (modules_loaded_late_without_noticing && added > 0) {
        LogInfo("Late enumeration: %d new module(s) added", added);
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

// Module name substrings we install hooks for (must match OnModuleLoaded logic)
static const std::wstring* GetInterestingModulePatterns(size_t* out_count) {
    static const std::wstring k_patterns[] = {L"dxgi.dll",          L"d3d11.dll",   L"d3d12.dll",
                                              L"sl.interposer.dll", L"xinput",      L"windows.gaming.input",
                                              L"gameinput",         L"nvapi64.dll", L"nvlowlatencyvk.dll",
                                              L"vulkan-1.dll",      L"_nvngx.dll",  L"dbghelp.dll"};
    *out_count = sizeof(k_patterns) / sizeof(k_patterns[0]);
    return k_patterns;
}

static bool IsInterestingModule(const std::wstring& lowerFilename) {
    size_t count = 0;
    const std::wstring* patterns = GetInterestingModulePatterns(&count);
    for (size_t i = 0; i < count; ++i) {
        if (lowerFilename.find(patterns[i]) != std::wstring::npos) {
            return true;
        }
    }
    return false;
}

std::vector<std::string> ReportMissedModulesOnExit() {
    std::vector<std::string> missed;
    std::unordered_set<std::wstring> tracked;
    {
        utils::SRWLockShared lock(g_module_srwlock);
        for (const auto& m : g_loaded_modules) {
            std::wstring path = m.fullPath.empty() ? m.moduleName : m.fullPath;
            std::wstring fn = ExtractModuleName(path);
            std::transform(fn.begin(), fn.end(), fn.begin(), ::towlower);
            tracked.insert(fn);
        }
    }

    // Set of module names we've already added to missed (avoids duplicates when using multiple enumeration methods)
    std::unordered_set<std::wstring> reported;

    constexpr DWORD kMaxModules = 1024;
    HMODULE hModules[kMaxModules];
    DWORD cbNeeded = 0;
    if (EnumProcessModules(GetCurrentProcess(), hModules, sizeof(hModules), &cbNeeded)) {
        const DWORD moduleCount =
            (cbNeeded / sizeof(HMODULE) < kMaxModules) ? (cbNeeded / sizeof(HMODULE)) : kMaxModules;
        for (DWORD i = 0; i < moduleCount; i++) {
            wchar_t modulePath[MAX_PATH];
            if (GetModuleFileNameW(hModules[i], modulePath, MAX_PATH) == 0) {
                continue;
            }
            std::wstring fn = ExtractModuleName(modulePath);
            std::transform(fn.begin(), fn.end(), fn.begin(), ::towlower);
            if (!IsInterestingModule(fn)) {
                continue;
            }
            if (tracked.find(fn) != tracked.end() || reported.find(fn) != reported.end()) {
                continue;
            }
            reported.insert(fn);
            std::string narrow_fn(fn.begin(), fn.end());
            LogError(
                "Missed module on exit: '%s' was loaded in process but we never received OnModuleLoaded for it "
                "(e.g. loaded via LdrLoadDll, static import before hooks, or manual map)",
                narrow_fn.c_str());
            missed.push_back(narrow_fn);
        }
    }

    // Fallback: enumerate via Tool Help API (CreateToolhelp32Snapshot). Some modules (e.g. NvLowLatencyVk.dll)
    // can be missed by EnumProcessModules but appear in the snapshot.
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, GetCurrentProcessId());
    if (hSnap != INVALID_HANDLE_VALUE) {
        MODULEENTRY32W me;
        me.dwSize = sizeof(me);
        if (Module32FirstW(hSnap, &me)) {
            do {
                std::wstring fn = me.szModule;
                std::transform(fn.begin(), fn.end(), fn.begin(), ::towlower);
                if (!IsInterestingModule(fn)) {
                    continue;
                }
                if (tracked.find(fn) != tracked.end() || reported.find(fn) != reported.end()) {
                    continue;
                }
                reported.insert(fn);
                std::string narrow_fn(fn.begin(), fn.end());
                LogError(
                    "Missed module on exit (Toolhelp32): '%s' was loaded in process but we never received "
                    "OnModuleLoaded for it",
                    narrow_fn.c_str());
                missed.push_back(narrow_fn);
            } while (Module32NextW(hSnap, &me));
        }
        CloseHandle(hSnap);
    }

    return missed;
}

// Helper function to check if any loaded module has "reframework\plugins" in its path
bool HasReframeworkPluginModule() {
    HMODULE modules[1024];
    DWORD num_modules = 0;

    // Enumerate all loaded modules
    if (K32EnumProcessModules(GetCurrentProcess(), modules, sizeof(modules), &num_modules) == 0) {
        // If enumeration fails, fall back to checking tracked modules
        utils::SRWLockShared lock(g_module_srwlock);
        for (const auto& module : g_loaded_modules) {
            if (!module.fullPath.empty()) {
                std::wstring lowerPath = module.fullPath;
                std::transform(lowerPath.begin(), lowerPath.end(), lowerPath.begin(), ::towlower);
                if (lowerPath.find(L"reframework\\plugins") != std::wstring::npos) {
                    return true;
                }
            }
        }
        return false;
    }

    if (num_modules > sizeof(modules)) {
        num_modules = static_cast<DWORD>(sizeof(modules));
    }

    // Check each module's path
    for (DWORD i = 0; i < num_modules / sizeof(HMODULE); ++i) {
        if (modules[i] == nullptr) {
            continue;
        }

        wchar_t module_path[MAX_PATH];
        if (GetModuleFileNameW(modules[i], module_path, MAX_PATH) > 0) {
            std::wstring path(module_path);
            std::transform(path.begin(), path.end(), path.begin(), ::towlower);
            if (path.find(L"reframework\\plugins") != std::wstring::npos) {
                return true;
            }
        }
    }

    return false;
}

// Returns true if ReShade was loaded from C:\ProgramData\ReShade\ReShade64.dll or ReShade32.dll
static bool IsReshadeFromProgramData() {
    HMODULE reshade = g_reshade_module;
    if (reshade == nullptr) {
        return false;
    }
    wchar_t module_path[MAX_PATH];
    if (GetModuleFileNameW(reshade, module_path, MAX_PATH) == 0) {
        return false;
    }
    std::wstring path(module_path);
    // Strip long path prefix if present
    if (path.size() >= 4 && path.compare(0, 4, L"\\\\?\\") == 0) {
        path.erase(0, 4);
    }
    std::transform(path.begin(), path.end(), path.begin(), ::towlower);
    static const std::wstring programdata_reshade64(L"c:\\programdata\\reshade\\reshade64.dll");
    static const std::wstring programdata_reshade32(L"c:\\programdata\\reshade\\reshade32.dll");
    return (path == programdata_reshade64 || path == programdata_reshade32);
}

void OnModuleLoaded(const std::wstring& moduleName, HMODULE hModule) {
    RECORD_DETOUR_CALL(utils::get_now_ns());
    LogInfo("Module loaded: %ws (0x%p)", moduleName.c_str(), hModule);

    // Convert to lowercase for case-insensitive comparison
    std::wstring lowerModuleName = moduleName;
    std::transform(lowerModuleName.begin(), lowerModuleName.end(), lowerModuleName.begin(), ::towlower);

    // dxgi.dll
    if (lowerModuleName.find(L"dxgi.dll") != std::wstring::npos) {
        // Check if any module has "reframework\plugins" in its path
        if (HasReframeworkPluginModule()) {
            LogInfo("Skipping DXGI hooks installation - ReFramework plugin detected");
        } else if (IsReshadeFromProgramData()) {
            LogInfo("Skipping DXGI hooks installation - ReShade loaded from ProgramData");
        } else if (GetModuleHandleW(L"vulkan-1.dll") != nullptr) {
            LogInfo("Skipping DXGI hooks installation - vulkan-1.dll loaded");
        } else {
            LogInfo("Installing DXGI hooks for module: %ws", moduleName.c_str());
            if (InstallDxgiFactoryHooks(hModule)) {
                LogInfo("DXGI hooks installed successfully");
            }
        }
    }
    // d3d11.dll
    else if (lowerModuleName.find(L"d3d11.dll") != std::wstring::npos) {
        LogInfo("Installing D3D11 device hooks for module: %ws", moduleName.c_str());
        if (InstallD3D11DeviceHooks(hModule)) {
            LogInfo("D3D11 device hooks installed successfully");
        }
    }
    // d3d12.dll
    else if (lowerModuleName.find(L"d3d12.dll") != std::wstring::npos) {
        LogInfo("Installing D3D12 device hooks for module: %ws", moduleName.c_str());
        if (InstallD3D12DeviceHooks(hModule)) {
            LogInfo("D3D12 device hooks installed successfully");
        }
    } else if (lowerModuleName.find(L"sl.interposer.dll") != std::wstring::npos) {
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
        if ((hModule)) {
            LogInfo("XInput hooks installed successfully");
        } else {
            LogError("Failed to install XInput hooks");
        }
    }

    // Windows.Gaming.Input hooks
    else if (lowerModuleName.find(L"windows.gaming.input") != std::wstring::npos
             || lowerModuleName.find(L"gameinput") != std::wstring::npos) {
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
    // NvLowLatencyVk (Vulkan Reflex) hooks
    else if (lowerModuleName.find(L"nvlowlatencyvk.dll") != std::wstring::npos) {
        LogInfo("Installing nvlowlatencyvk.dll hooks for module: %ws", moduleName.c_str());
        if (InstallNvLowLatencyVkHooks(hModule)) {
            LogInfo("NvLowLatencyVk hooks installed successfully");
        } else {
            LogInfo("NvLowLatencyVk hooks not installed (disabled by setting or already installed)");
        }
    }
    // vulkan-1.dll (Vulkan loader) – VK_NV_low_latency2: hook vkGetDeviceProcAddr to wrap vkSetLatencyMarkerNV
    else if (lowerModuleName.find(L"vulkan-1.dll") != std::wstring::npos) {
        LogInfo("Installing vulkan-1.dll loader hooks for module: %ws", moduleName.c_str());
        if (InstallVulkanLoaderHooks(hModule)) {
            LogInfo("Vulkan loader (VK_NV_low_latency2) hooks installed successfully");
        } else {
            LogInfo("Vulkan loader hooks not installed (disabled by setting or already installed)");
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
    // dbghelp.dll – log stack trace queries from any thread
    else if (lowerModuleName.find(L"dbghelp.dll") != std::wstring::npos) {
        LogInfo("Installing DbgHelp hooks for module: %ws", moduleName.c_str());
        if (InstallDbgHelpHooks(hModule)) {
            LogInfo("DbgHelp hooks installed successfully");
        } else {
            LogInfo("DbgHelp hooks not installed (e.g. already installed or symbol not found)");
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
                LogInfo("Blocked DLL: Extracted filename '%s' from path '%s'", narrow_filename.c_str(),
                        narrow_original.c_str());
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
        return false;  // Can't block Display Commander itself
    }

    // Modules loaded after hooks were installed can be blocked
    return true;
}

bool IsModuleSrwlockHeld() { return utils::TryIsSRWLockHeld(g_module_srwlock); }

bool IsBlockedDllsSrwlockHeld() { return utils::TryIsSRWLockHeld(g_blocked_dlls_srwlock); }

}  // namespace display_commanderhooks
