#include "platform_api_detector.hpp"
#include "../hooks/loadlibrary_hooks.hpp"

#include <windows.h>
#include <algorithm>
#include <filesystem>
#include <set>
#include <vector>

namespace display_commander::utils {

PlatformAPI DetectPlatformAPIFromDLLName(const std::wstring& dll_name) {
    if (dll_name.empty()) {
        return PlatformAPI::None;
    }

    // Convert to lowercase for case-insensitive comparison
    std::wstring lower_name = dll_name;
    std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(), ::towlower);

    // Steam API DLLs
    if (lower_name == L"steam_api.dll" || lower_name == L"steam_api64.dll") {
        return PlatformAPI::Steam;
    }

    // Epic Games Store (EOS SDK)
    if (lower_name == L"eossdk-win64-shipping.dll" || lower_name == L"eossdk-win32-shipping.dll"
        || lower_name == L"eossdk-win64.dll" || lower_name == L"eossdk-win32.dll") {
        return PlatformAPI::Epic;
    }

    // GOG Galaxy
    if (lower_name == L"galaxy.dll" || lower_name == L"galaxy64.dll") {
        return PlatformAPI::GOG;
    }

    /* // Commented out due to false positives
    // Xbox (Windows.Gaming.Input, Xbox Live, etc.)
      if (lower_name.find(L"xbox") != std::wstring::npos ||
          lower_name.find(L"xbl") != std::wstring::npos ||
          lower_name == L"xgameplatform.dll" ||
          lower_name == L"xboxgipsynthetic.dll") {
          return PlatformAPI::Xbox;
      }*/

    // Origin (EA)
    if (lower_name.find(L"origin") != std::wstring::npos || lower_name == L"eacore.dll"
        || lower_name == L"eagameplatform.dll") {
        return PlatformAPI::Origin;
    }

    // Uplay (Ubisoft)
    if (lower_name.find(L"uplay") != std::wstring::npos || lower_name == L"upc.dll" || lower_name == L"upcr1.dll") {
        return PlatformAPI::Uplay;
    }

    // Battle.net (Blizzard)
    if (lower_name.find(L"battlenet") != std::wstring::npos || lower_name == L"bna.dll" || lower_name == L"bna64.dll") {
        return PlatformAPI::BattleNet;
    }

    // Bethesda.net
    if (lower_name.find(L"bethesda") != std::wstring::npos || lower_name == L"bethnet.dll"
        || lower_name == L"bethnet64.dll") {
        return PlatformAPI::Bethesda;
    }

    // Rockstar Games Launcher
    if (lower_name.find(L"rockstar") != std::wstring::npos || lower_name == L"rsg.dll" || lower_name == L"rsg64.dll") {
        return PlatformAPI::Rockstar;
    }

    return PlatformAPI::None;
}

const char* GetPlatformAPIName(PlatformAPI api) {
    switch (api) {
        case PlatformAPI::Steam:     return "Steam";
        case PlatformAPI::Epic:      return "Epic Games Store";
        case PlatformAPI::GOG:       return "GOG Galaxy";
        case PlatformAPI::Xbox:      return "Xbox";
        case PlatformAPI::Origin:    return "Origin";
        case PlatformAPI::Uplay:     return "Uplay";
        case PlatformAPI::BattleNet: return "Battle.net";
        case PlatformAPI::Bethesda:  return "Bethesda.net";
        case PlatformAPI::Rockstar:  return "Rockstar Games";
        case PlatformAPI::None:      return "None";
        case PlatformAPI::Unknown:   return "Unknown";
        default:                     return "Unknown";
    }
}

// Helper function to scan local files in game directory for platform DLLs
void ScanLocalFilesForPlatformAPIs(std::set<PlatformAPI>& detected_apis, std::set<std::wstring>& logged_apis,
                                   bool should_log = true) {
    // Get game executable directory
    wchar_t exe_path[MAX_PATH];
    if (GetModuleFileNameW(nullptr, exe_path, MAX_PATH) == 0) {
        return;  // Failed to get executable path
    }

    std::filesystem::path exe_dir = std::filesystem::path(exe_path).parent_path();

    // Check if directory exists and is accessible
    std::error_code ec;
    if (!std::filesystem::exists(exe_dir, ec) || !std::filesystem::is_directory(exe_dir, ec)) {
        return;  // Directory doesn't exist or isn't accessible
    }

    // List of platform DLL names to look for
    std::vector<std::wstring> platform_dll_names = {L"steam_api.dll",
                                                    L"steam_api64.dll",
                                                    L"eossdk-win64-shipping.dll",
                                                    L"eossdk-win32-shipping.dll",
                                                    L"eossdk-win64.dll",
                                                    L"eossdk-win32.dll",
                                                    L"galaxy.dll",
                                                    L"galaxy64.dll",
                                                    L"xgameplatform.dll",
                                                    L"xboxgipsynthetic.dll",
                                                    L"eacore.dll",
                                                    L"eagameplatform.dll",
                                                    L"upc.dll",
                                                    L"upcr1.dll",
                                                    L"bna.dll",
                                                    L"bna64.dll",
                                                    L"bethnet.dll",
                                                    L"bethnet64.dll",
                                                    L"rsg.dll",
                                                    L"rsg64.dll"};

    // Also check for DLLs with "xbox", "xbl", "origin", "uplay", "battlenet", "bethesda", "rockstar" in name
    try {
        for (const auto& entry : std::filesystem::directory_iterator(exe_dir, ec)) {
            if (ec) continue;  // Skip on error

            if (!entry.is_regular_file(ec)) continue;

            const std::filesystem::path& file_path = entry.path();
            std::wstring file_name = file_path.filename().wstring();

            // Convert to lowercase for comparison
            std::wstring lower_name = file_name;
            std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(), ::towlower);

            // Check if it's a DLL
            if (file_path.extension().wstring() != L".dll") continue;

            // Check against known platform DLL names
            bool is_platform_dll = false;
            for (const auto& platform_dll : platform_dll_names) {
                if (lower_name == platform_dll) {
                    is_platform_dll = true;
                    break;
                }
            }

            // Also check for partial matches (xbox, xbl, origin, uplay, battlenet, bethesda, rockstar)
            if (!is_platform_dll) {
                if (lower_name.find(L"xbox") != std::wstring::npos || lower_name.find(L"xbl") != std::wstring::npos
                    || lower_name.find(L"origin") != std::wstring::npos
                    || lower_name.find(L"uplay") != std::wstring::npos
                    || lower_name.find(L"battlenet") != std::wstring::npos
                    || lower_name.find(L"bethesda") != std::wstring::npos
                    || lower_name.find(L"rockstar") != std::wstring::npos) {
                    is_platform_dll = true;
                }
            }

            if (is_platform_dll) {
                PlatformAPI api = DetectPlatformAPIFromDLLName(file_name);
                if (api != PlatformAPI::None) {
                    detected_apis.insert(api);

                    // Log if not already logged and logging is enabled
                    if (should_log && !logged_apis.contains(file_name)) {
                        logged_apis.insert(file_name);
                        const char* api_name = GetPlatformAPIName(api);
                        char file_name_ansi[MAX_PATH];
                        WideCharToMultiByte(CP_ACP, 0, file_name.c_str(), -1, file_name_ansi, MAX_PATH, nullptr,
                                            nullptr);
                        char msg[512];
                        snprintf(msg, sizeof(msg), "[DisplayCommander] Platform API detected (local file): %s (%s)",
                                 api_name, file_name_ansi);
                        OutputDebugStringA(msg);
                    }
                }
            }
        }
    } catch (...) {
        // Silently handle exceptions (directory access errors, etc.)
    }
}

void DetectAndLogPlatformAPIs() {
    // Track which APIs we've found (to avoid duplicate messages)
    bool found_steam = false;
    bool found_epic = false;
    bool found_gog = false;
    bool found_xbox = false;
    bool found_origin = false;
    bool found_uplay = false;
    bool found_battlenet = false;
    bool found_bethesda = false;
    bool found_rockstar = false;

    // Track which DLL names we've already logged (to avoid duplicate messages for same DLL)
    std::set<std::wstring> logged_dlls;

    // 1. Check loaded modules
    std::vector<display_commanderhooks::ModuleInfo> modules = display_commanderhooks::GetLoadedModules();
    for (const auto& module : modules) {
        PlatformAPI api = DetectPlatformAPIFromDLLName(module.moduleName);

        if (api != PlatformAPI::None) {
            const char* api_name = GetPlatformAPIName(api);

            // Convert wide string to narrow for OutputDebugStringA
            char dll_name_ansi[MAX_PATH];
            WideCharToMultiByte(CP_ACP, 0, module.moduleName.c_str(), -1, dll_name_ansi, MAX_PATH, nullptr, nullptr);

            // Output debug message only once per platform
            switch (api) {
                case PlatformAPI::Steam:
                    if (!found_steam) {
                        char msg[512];
                        snprintf(msg, sizeof(msg), "[DisplayCommander] Platform API detected: %s (%s)", api_name,
                                 dll_name_ansi);
                        OutputDebugStringA(msg);
                        found_steam = true;
                        logged_dlls.insert(module.moduleName);
                    }
                    break;
                case PlatformAPI::Epic:
                    if (!found_epic) {
                        char msg[512];
                        snprintf(msg, sizeof(msg), "[DisplayCommander] Platform API detected: %s (%s)", api_name,
                                 dll_name_ansi);
                        OutputDebugStringA(msg);
                        found_epic = true;
                        logged_dlls.insert(module.moduleName);
                    }
                    break;
                case PlatformAPI::GOG:
                    if (!found_gog) {
                        char msg[512];
                        snprintf(msg, sizeof(msg), "[DisplayCommander] Platform API detected: %s (%s)", api_name,
                                 dll_name_ansi);
                        OutputDebugStringA(msg);
                        found_gog = true;
                        logged_dlls.insert(module.moduleName);
                    }
                    break;
                case PlatformAPI::Xbox:
                    if (!found_xbox) {
                        char msg[512];
                        snprintf(msg, sizeof(msg), "[DisplayCommander] Platform API detected: %s (%s)", api_name,
                                 dll_name_ansi);
                        OutputDebugStringA(msg);
                        found_xbox = true;
                        logged_dlls.insert(module.moduleName);
                    }
                    break;
                case PlatformAPI::Origin:
                    if (!found_origin) {
                        char msg[512];
                        snprintf(msg, sizeof(msg), "[DisplayCommander] Platform API detected: %s (%s)", api_name,
                                 dll_name_ansi);
                        OutputDebugStringA(msg);
                        found_origin = true;
                        logged_dlls.insert(module.moduleName);
                    }
                    break;
                case PlatformAPI::Uplay:
                    if (!found_uplay) {
                        char msg[512];
                        snprintf(msg, sizeof(msg), "[DisplayCommander] Platform API detected: %s (%s)", api_name,
                                 dll_name_ansi);
                        OutputDebugStringA(msg);
                        found_uplay = true;
                        logged_dlls.insert(module.moduleName);
                    }
                    break;
                case PlatformAPI::BattleNet:
                    if (!found_battlenet) {
                        char msg[512];
                        snprintf(msg, sizeof(msg), "[DisplayCommander] Platform API detected: %s (%s)", api_name,
                                 dll_name_ansi);
                        OutputDebugStringA(msg);
                        found_battlenet = true;
                        logged_dlls.insert(module.moduleName);
                    }
                    break;
                case PlatformAPI::Bethesda:
                    if (!found_bethesda) {
                        char msg[512];
                        snprintf(msg, sizeof(msg), "[DisplayCommander] Platform API detected: %s (%s)", api_name,
                                 dll_name_ansi);
                        OutputDebugStringA(msg);
                        found_bethesda = true;
                        logged_dlls.insert(module.moduleName);
                    }
                    break;
                case PlatformAPI::Rockstar:
                    if (!found_rockstar) {
                        char msg[512];
                        snprintf(msg, sizeof(msg), "[DisplayCommander] Platform API detected: %s (%s)", api_name,
                                 dll_name_ansi);
                        OutputDebugStringA(msg);
                        found_rockstar = true;
                        logged_dlls.insert(module.moduleName);
                    }
                    break;
                default: break;
            }
        }
    }

    // 2. Check local files in game directory
    std::set<PlatformAPI> local_file_apis;  // Track APIs found in local files
    ScanLocalFilesForPlatformAPIs(local_file_apis, logged_dlls, true);
}

std::vector<PlatformAPI> GetDetectedPlatformAPIs() {
    // Use a set to avoid duplicates
    std::set<PlatformAPI> detected_apis;
    std::set<std::wstring>
        logged_dlls;  // Not used for logging in this function, but needed for ScanLocalFilesForPlatformAPIs

    // 1. Check loaded modules
    std::vector<display_commanderhooks::ModuleInfo> modules = display_commanderhooks::GetLoadedModules();
    for (const auto& module : modules) {
        PlatformAPI api = DetectPlatformAPIFromDLLName(module.moduleName);
        if (api != PlatformAPI::None) {
            detected_apis.insert(api);
        }
    }

    // 2. Check local files in game directory (no logging)
    ScanLocalFilesForPlatformAPIs(detected_apis, logged_dlls, false);

    // Convert set to vector
    return std::vector<PlatformAPI>(detected_apis.begin(), detected_apis.end());
}

bool TestWhitelist(const std::wstring& executable_path) {
    if (executable_path.empty()) {
        return false;
    }

    // Convert to lowercase for case-insensitive comparison
    std::wstring lower_path = executable_path;
    std::transform(lower_path.begin(), lower_path.end(), lower_path.begin(), ::towlower);

    // Check for SteamApps (Steam games)
    if (lower_path.find(L"steamapps") != std::wstring::npos) {
        return true;
    }

    // Check for common game store patterns
    if (lower_path.find(L"epic games") != std::wstring::npos || lower_path.find(L"gog games") != std::wstring::npos
        || lower_path.find(L"xbox games") != std::wstring::npos || lower_path.find(L"ubisoft") != std::wstring::npos
        || lower_path.find(L"origin games") != std::wstring::npos) {
        return true;
    }

    // TODO: Add support for reading from whitelist.ini file similar to Special K
    // For now, we use hardcoded patterns

    return false;
}

}  // namespace display_commander::utils
