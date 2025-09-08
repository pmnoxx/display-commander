#include "loadlibrary_hooks.hpp"
#include "../utils.hpp"
#include <MinHook.h>
#include <chrono>
#include <iomanip>
#include <sstream>

namespace renodx::hooks {

// Original function pointers
LoadLibraryA_pfn LoadLibraryA_Original = nullptr;
LoadLibraryW_pfn LoadLibraryW_Original = nullptr;
LoadLibraryExA_pfn LoadLibraryExA_Original = nullptr;
LoadLibraryExW_pfn LoadLibraryExW_Original = nullptr;

// Hook state
static std::atomic<bool> g_loadlibrary_hooks_installed{false};

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

// Hooked LoadLibraryA function
HMODULE WINAPI LoadLibraryA_Detour(LPCSTR lpLibFileName) {
    std::string timestamp = GetCurrentTimestamp();
    std::string dll_name = lpLibFileName ? lpLibFileName : "NULL";

    LogInfo("[%s] LoadLibraryA called: %s", timestamp.c_str(), dll_name.c_str());

    // Call original function
    HMODULE result = LoadLibraryA_Original ? LoadLibraryA_Original(lpLibFileName) : LoadLibraryA(lpLibFileName);

    if (result) {
        LogInfo("[%s] LoadLibraryA success: %s -> HMODULE: 0x%p", timestamp.c_str(), dll_name.c_str(), result);
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

    // Hook LoadLibraryW
    if (MH_CreateHook(LoadLibraryW, LoadLibraryW_Detour, (LPVOID*)&LoadLibraryW_Original) != MH_OK) {
        LogError("Failed to create LoadLibraryW hook");
        return false;
    }

    // Hook LoadLibraryExA
    if (MH_CreateHook(LoadLibraryExA, LoadLibraryExA_Detour, (LPVOID*)&LoadLibraryExA_Original) != MH_OK) {
        LogError("Failed to create LoadLibraryExA hook");
        return false;
    }

    // Hook LoadLibraryExW
    if (MH_CreateHook(LoadLibraryExW, LoadLibraryExW_Detour, (LPVOID*)&LoadLibraryExW_Original) != MH_OK) {
        LogError("Failed to create LoadLibraryExW hook");
        return false;
    }

    // Enable all hooks
    if (MH_EnableHook(MH_ALL_HOOKS) != MH_OK) {
        LogError("Failed to enable LoadLibrary hooks");
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

} // namespace renodx::hooks
