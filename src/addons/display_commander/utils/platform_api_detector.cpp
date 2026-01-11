#include "platform_api_detector.hpp"
#include "../hooks/loadlibrary_hooks.hpp"

#include <algorithm>

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
    if (lower_name == L"eossdk-win64-shipping.dll" ||
        lower_name == L"eossdk-win32-shipping.dll" ||
        lower_name == L"eossdk-win64.dll" ||
        lower_name == L"eossdk-win32.dll") {
        return PlatformAPI::Epic;
    }

    // GOG Galaxy
    if (lower_name == L"galaxy.dll" || lower_name == L"galaxy64.dll") {
        return PlatformAPI::GOG;
    }

    // Xbox (Windows.Gaming.Input, Xbox Live, etc.)
    if (lower_name.find(L"xbox") != std::wstring::npos ||
        lower_name.find(L"xbl") != std::wstring::npos ||
        lower_name == L"xgameplatform.dll" ||
        lower_name == L"xboxgipsynthetic.dll") {
        return PlatformAPI::Xbox;
    }

    // Origin (EA)
    if (lower_name.find(L"origin") != std::wstring::npos ||
        lower_name == L"eacore.dll" ||
        lower_name == L"eagameplatform.dll") {
        return PlatformAPI::Origin;
    }

    // Uplay (Ubisoft)
    if (lower_name.find(L"uplay") != std::wstring::npos ||
        lower_name == L"upc.dll" ||
        lower_name == L"upcr1.dll") {
        return PlatformAPI::Uplay;
    }

    // Battle.net (Blizzard)
    if (lower_name.find(L"battlenet") != std::wstring::npos ||
        lower_name == L"bna.dll" ||
        lower_name == L"bna64.dll") {
        return PlatformAPI::BattleNet;
    }

    // Bethesda.net
    if (lower_name.find(L"bethesda") != std::wstring::npos ||
        lower_name == L"bethnet.dll" ||
        lower_name == L"bethnet64.dll") {
        return PlatformAPI::Bethesda;
    }

    // Rockstar Games Launcher
    if (lower_name.find(L"rockstar") != std::wstring::npos ||
        lower_name == L"rsg.dll" ||
        lower_name == L"rsg64.dll") {
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
        default:                      return "Unknown";
    }
}

void DetectAndLogPlatformAPIs() {
    // Get all loaded modules
    std::vector<display_commanderhooks::ModuleInfo> modules = display_commanderhooks::GetLoadedModules();

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

    // Check each module
    for (const auto& module : modules) {
        PlatformAPI api = DetectPlatformAPIFromDLLName(module.moduleName);

        if (api != PlatformAPI::None) {
            const char* api_name = GetPlatformAPIName(api);

            // Convert wide string to narrow for OutputDebugStringA
            char dll_name_ansi[MAX_PATH];
            WideCharToMultiByte(CP_ACP, 0, module.moduleName.c_str(), -1,
                               dll_name_ansi, MAX_PATH, nullptr, nullptr);

            // Output debug message only once per platform
            switch (api) {
                case PlatformAPI::Steam:
                    if (!found_steam) {
                        char msg[512];
                        snprintf(msg, sizeof(msg), "[DisplayCommander] Platform API detected: %s (%s)",
                                api_name, dll_name_ansi);
                        OutputDebugStringA(msg);
                        found_steam = true;
                    }
                    break;
                case PlatformAPI::Epic:
                    if (!found_epic) {
                        char msg[512];
                        snprintf(msg, sizeof(msg), "[DisplayCommander] Platform API detected: %s (%s)",
                                api_name, dll_name_ansi);
                        OutputDebugStringA(msg);
                        found_epic = true;
                    }
                    break;
                case PlatformAPI::GOG:
                    if (!found_gog) {
                        char msg[512];
                        snprintf(msg, sizeof(msg), "[DisplayCommander] Platform API detected: %s (%s)",
                                api_name, dll_name_ansi);
                        OutputDebugStringA(msg);
                        found_gog = true;
                    }
                    break;
                case PlatformAPI::Xbox:
                    if (!found_xbox) {
                        char msg[512];
                        snprintf(msg, sizeof(msg), "[DisplayCommander] Platform API detected: %s (%s)",
                                api_name, dll_name_ansi);
                        OutputDebugStringA(msg);
                        found_xbox = true;
                    }
                    break;
                case PlatformAPI::Origin:
                    if (!found_origin) {
                        char msg[512];
                        snprintf(msg, sizeof(msg), "[DisplayCommander] Platform API detected: %s (%s)",
                                api_name, dll_name_ansi);
                        OutputDebugStringA(msg);
                        found_origin = true;
                    }
                    break;
                case PlatformAPI::Uplay:
                    if (!found_uplay) {
                        char msg[512];
                        snprintf(msg, sizeof(msg), "[DisplayCommander] Platform API detected: %s (%s)",
                                api_name, dll_name_ansi);
                        OutputDebugStringA(msg);
                        found_uplay = true;
                    }
                    break;
                case PlatformAPI::BattleNet:
                    if (!found_battlenet) {
                        char msg[512];
                        snprintf(msg, sizeof(msg), "[DisplayCommander] Platform API detected: %s (%s)",
                                api_name, dll_name_ansi);
                        OutputDebugStringA(msg);
                        found_battlenet = true;
                    }
                    break;
                case PlatformAPI::Bethesda:
                    if (!found_bethesda) {
                        char msg[512];
                        snprintf(msg, sizeof(msg), "[DisplayCommander] Platform API detected: %s (%s)",
                                api_name, dll_name_ansi);
                        OutputDebugStringA(msg);
                        found_bethesda = true;
                    }
                    break;
                case PlatformAPI::Rockstar:
                    if (!found_rockstar) {
                        char msg[512];
                        snprintf(msg, sizeof(msg), "[DisplayCommander] Platform API detected: %s (%s)",
                                api_name, dll_name_ansi);
                        OutputDebugStringA(msg);
                        found_rockstar = true;
                    }
                    break;
                default:
                    break;
            }
        }
    }
}

} // namespace display_commander::utils
