// Source Code <Display Commander> // follow this order for includes in all files + add this comment at the top
#include "reshade_module_detection.hpp"

#include "utils/logging.hpp"

// Libraries <standard C++>
#include <algorithm>
#include <cstdio>
#include <cstdint>
#include <string>
#include <vector>

// Libraries <Windows.h>
#include <Windows.h>

// Libraries <Windows>
#include <psapi.h>
#include <winver.h>

namespace {
bool IsVersion662OrAbove(const std::string& version_str) {
    if (version_str.empty()) {
        return false;
    }

    int major = 0;
    int minor = 0;
    int build = 0;
    int revision = 0;

    if (sscanf_s(version_str.c_str(), "%d.%d.%d.%d", &major, &minor, &build, &revision) >= 2) {
        if (major > 6) {
            return true;
        }
        if (major == 6) {
            if (minor > 6) {
                return true;
            }
            if (minor == 6) {
                return build >= 2;
            }
        }
    }

    return false;
}

bool DetectMultipleReShadeVersions_EnumerateModules() {
    HMODULE modules[1024];
    DWORD num_modules = 0;
    if (K32EnumProcessModules(GetCurrentProcess(), modules, sizeof(modules), &num_modules) == 0) {
        DWORD error = GetLastError();
        LogWarn("Failed to enumerate process modules: %lu", error);
        g_reshade_debug_info.error_message = "Failed to enumerate process modules: " + std::to_string(error);
        return false;
    }
    if (num_modules > sizeof(modules)) {
        num_modules = static_cast<DWORD>(sizeof(modules));
    }
    LogInfo("Scanning %lu modules for ReShade...", num_modules / sizeof(HMODULE));
    int reshade_module_count = 0;
    for (DWORD i = 0; i < num_modules / sizeof(HMODULE); ++i) {
        HMODULE module = modules[i];
        if (module == nullptr) continue;
        FARPROC register_func = GetProcAddress(module, "ReShadeRegisterAddon");
        FARPROC unregister_func = GetProcAddress(module, "ReShadeUnregisterAddon");
        if (register_func == nullptr || unregister_func == nullptr) continue;

        reshade_module_count++;
        ReShadeModuleInfo module_info;
        module_info.handle = module;
        wchar_t module_path[MAX_PATH];
        DWORD path_length = GetModuleFileNameW(module, module_path, MAX_PATH);

        if (path_length > 0) {
            char narrow_path[MAX_PATH];
            WideCharToMultiByte(CP_UTF8, 0, module_path, -1, narrow_path, MAX_PATH, nullptr, nullptr);
            module_info.path = narrow_path;
            LogInfo("Found ReShade module #%d: 0x%p - %s", reshade_module_count, module, narrow_path);

            DWORD version_dummy = 0;
            DWORD version_size = GetFileVersionInfoSizeW(module_path, &version_dummy);
            if (version_size > 0) {
                std::vector<uint8_t> version_data(version_size);
                if (GetFileVersionInfoW(module_path, version_dummy, version_size, version_data.data()) != 0) {
                    VS_FIXEDFILEINFO* version_info = nullptr;
                    UINT version_info_size = 0;
                    if (VerQueryValueW(version_data.data(), L"\\", reinterpret_cast<LPVOID*>(&version_info),
                                       &version_info_size)
                            != 0
                        && version_info != nullptr) {
                        char version_str[64];
                        snprintf(version_str, sizeof(version_str), "%hu.%hu.%hu.%hu",
                                 HIWORD(version_info->dwFileVersionMS), LOWORD(version_info->dwFileVersionMS),
                                 HIWORD(version_info->dwFileVersionLS), LOWORD(version_info->dwFileVersionLS));
                        module_info.version = version_str;
                        module_info.is_version_662_or_above = IsVersion662OrAbove(version_str);
                        LogInfo("  Version: %s", version_str);
                        LogInfo("  Version 6.6.2+: %s", module_info.is_version_662_or_above ? "Yes" : "No");
                    }
                }
            }
            FARPROC imgui_func = GetProcAddress(module, "ReShadeGetImGuiFunctionTable");
            module_info.has_imgui_support = (imgui_func != nullptr);
            LogInfo("  ImGui Support: %s", imgui_func != nullptr ? "Yes" : "No");
            if (module_info.version.empty()) {
                module_info.is_version_662_or_above = false;
                LogInfo("  Version 6.6.2+: No (version unknown)");
            }
        } else {
            module_info.path = "(path unavailable)";
            LogInfo("Found ReShade module #%d: 0x%p - (path unavailable)", reshade_module_count, module);
        }
        g_reshade_debug_info.modules.push_back(module_info);
    }
    return true;
}
}  // namespace

ReShadeDetectionDebugInfo g_reshade_debug_info;

void DetectMultipleReShadeVersions() {
    LogInfo("=== ReShade Module Detection ===");
    g_reshade_debug_info = ReShadeDetectionDebugInfo();

    if (!DetectMultipleReShadeVersions_EnumerateModules()) {
        g_reshade_debug_info.detection_completed = true;
        return;
    }

    const int reshade_module_count = static_cast<int>(g_reshade_debug_info.modules.size());
    LogInfo("=== ReShade Detection Complete ===");
    LogInfo("Total ReShade modules found: %d", reshade_module_count);

    bool has_compatible_version = false;
    for (const auto& m : g_reshade_debug_info.modules) {
        if (m.is_version_662_or_above) {
            has_compatible_version = true;
            LogInfo("Found compatible ReShade version: %s", m.version.c_str());
            break;
        }
    }
    if (!has_compatible_version && !g_reshade_debug_info.modules.empty()) {
        LogWarn("No ReShade modules found with version 6.6.2 or above");
    }

    g_reshade_debug_info.total_modules_found = reshade_module_count;
    g_reshade_debug_info.detection_completed = true;

    if (reshade_module_count > 1) {
        LogWarn("WARNING: Multiple ReShade versions detected! This may cause conflicts.");
        LogWarn("Found %d ReShade modules - only the first one will be used for registration.", reshade_module_count);
        for (size_t i = 0; i < g_reshade_debug_info.modules.size(); ++i) {
            LogWarn("  ReShade module %zu: 0x%p", i + 1, g_reshade_debug_info.modules[i].handle);
        }
    } else if (reshade_module_count == 1) {
        LogInfo("Single ReShade module detected - proceeding with registration.");
    } else {
        LogError("No ReShade modules found! Registration will likely fail.");
        g_reshade_debug_info.error_message = "No ReShade modules found! Registration will likely fail.";
    }
}

bool CheckReShadeVersionCompatibility() {
    static bool first_time = true;
    if (!first_time) {
        return false;
    }
    first_time = false;
    LogError("ReShade addon registration failed - API version not supported");

    std::string debug_info = "ERROR DETAILS:\n";
    debug_info += "• Required API Version: 17 (ReShade 6.6.2+)\n";

    bool has_version_info = false;
    bool has_compatible_version = false;
    std::string detected_versions;

    if (g_reshade_debug_info.detection_completed && !g_reshade_debug_info.modules.empty()) {
        for (const auto& module : g_reshade_debug_info.modules) {
            if (!module.version.empty()) {
                has_version_info = true;
                if (!detected_versions.empty()) {
                    detected_versions += ", ";
                }
                detected_versions += module.version;

                if (module.is_version_662_or_above) {
                    has_compatible_version = true;
                }
            }
        }
    }

    if (has_version_info) {
        debug_info += "• Detected ReShade Versions: " + detected_versions + "\n";
        debug_info += "• Version 6.6.2+ Compatible: " + std::string(has_compatible_version ? "Yes" : "No") + "\n";
    } else {
        debug_info += "• Your ReShade Version: Unknown (version detection failed)\n";
    }
    debug_info += "• Status: Incompatible\n\n";

    if (g_reshade_debug_info.detection_completed) {
        debug_info += "MODULE DETECTION RESULTS:\n";
        debug_info +=
            "• Total ReShade modules found: " + std::to_string(g_reshade_debug_info.total_modules_found) + "\n";

        if (!g_reshade_debug_info.error_message.empty()) {
            debug_info += "• Error: " + g_reshade_debug_info.error_message + "\n";
        }

        if (!g_reshade_debug_info.modules.empty()) {
            debug_info += "• Detected modules:\n";
            for (size_t i = 0; i < g_reshade_debug_info.modules.size(); ++i) {
                const auto& module = g_reshade_debug_info.modules[i];
                debug_info += "  " + std::to_string(i + 1) + ". " + module.path + "\n";
                if (!module.version.empty()) {
                    debug_info += "     Version: " + module.version + "\n";
                    debug_info +=
                        "     Version 6.6.2+: " + std::string(module.is_version_662_or_above ? "Yes" : "No") + "\n";
                } else {
                    debug_info += "     Version: Unknown\n";
                    debug_info += "     Version 6.6.2+: No (version unknown)\n";
                }
                debug_info += "     ImGui Support: " + std::string(module.has_imgui_support ? "Yes" : "No") + "\n";
                debug_info += "     Handle: 0x" + std::to_string(reinterpret_cast<uintptr_t>(module.handle)) + "\n";
            }
        } else {
            debug_info += "• No ReShade modules detected\n";
        }
        debug_info += "\n";
    } else {
        debug_info += "MODULE DETECTION:\n";
        debug_info += "• Detection not completed or failed\n\n";
    }

    debug_info += "SOLUTION:\n";
    debug_info += "1. Download the latest ReShade from: https://reshade.me/\n";
    debug_info += "2. Install ReShade 6.6.2 or newer\n";
    debug_info += "3. Restart your game to load the updated ReShade\n\n";
    debug_info += "This addon uses advanced features that require the newer ReShade API.";

    MessageBoxA(nullptr, debug_info.c_str(), "ReShade Version Incompatible - Update Required",
                MB_OK | MB_ICONERROR | MB_TOPMOST);

    return false;
}
