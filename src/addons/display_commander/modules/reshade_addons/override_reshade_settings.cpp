// Source Code <Display Commander> // follow this order for includes in all files + add this comment at the top
#include "../../config/override_reshade_settings.hpp"

// Source Code <Display Commander>
#include "../../config/display_commander_config.hpp"
#include "../../globals.hpp"
#include "../../utils/general_utils.hpp"
#include "../../utils/logging.hpp"

// Libraries <ReShade> / <imgui>
#include <reshade.hpp>

// Libraries <standard C++>
#include <algorithm>
#include <cctype>
#include <exception>
#include <filesystem>
#include <string>
#include <vector>

namespace {
// Helpers for OverrideReShadeSettings - each handles one logical block.
// When runtime is non-null, config is read/written for that runtime's .ini (e.g. ReShade2.ini); otherwise
// global/current.
void OverrideReShadeSettings_WindowConfig(reshade::api::effect_runtime* runtime) {
    std::string window_config;
    size_t value_size = 0;
    if ((g_reshade_module != nullptr)
        && reshade::get_config_value(runtime, "OVERLAY", "Window", nullptr, &value_size)) {
        window_config.resize(value_size);
        if ((g_reshade_module != nullptr)
            && reshade::get_config_value(runtime, "OVERLAY", "Window", window_config.data(), &value_size)) {
            if (!window_config.empty() && window_config.back() == '\0') {
                window_config.pop_back();
            }
        } else {
            window_config.clear();
        }
    }

    bool changed_window_config = false;
    if (window_config.find("[Window][DC]") == std::string::npos) {
        if (!window_config.empty()) {
            window_config.push_back('\0');
        }
        std::string to_add = "[Window][DC],Pos=1017,,20,Size=1344,,1255,Collapsed=0,DockId=0x00000001,,999999,";
        for (size_t i = 0; i < to_add.size(); i++) {
            if (to_add[i] == ',') {
                window_config.push_back('\0');
            } else {
                window_config.push_back(to_add[i]);
            }
        }
        changed_window_config = true;
    }
    if (window_config.find("[Window][RenoDX]") == std::string::npos) {
        if (!window_config.empty()) {
            window_config.push_back('\0');
        }
        std::string to_add = "[Window][RenoDX],Pos=1017,,20,Size=1344,,1255,Collapsed=0,DockId=0x00000001,,9999999,";
        for (size_t i = 0; i < to_add.size(); i++) {
            if (to_add[i] == ',') {
                window_config.push_back('\0');
            } else {
                window_config.push_back(to_add[i]);
            }
        }
        changed_window_config = true;
    }
    if (changed_window_config) {
        reshade::set_config_value(runtime, "OVERLAY", "Window", window_config.c_str(), window_config.size());
        LogInfo("Updated ReShade Window config with Display Commander and RenoDX docking settings");
    }
}

void OverrideReShadeSettings_TutorialAndUpdates(reshade::api::effect_runtime* runtime) {
    reshade::set_config_value(runtime, "OVERLAY", "TutorialProgress", 4);
    reshade::set_config_value(runtime, "GENERAL", "CheckForUpdates", 0);
    reshade::set_config_value(runtime, "OVERLAY", "ShowClock", 0);
}

// ReShade defaults SCREENSHOT SavePath and PostSaveCommandWorkingDirectory to ".\" (game folder).
// Redirect to .\Screenshots so captures and post-save commands do not clutter the exe directory.
void OverrideReShadeSettings_ScreenshotPaths(reshade::api::effect_runtime* runtime) {
    if (g_reshade_module == nullptr) {
        return;
    }
    auto trim = [](std::string& s) {
        while (!s.empty() && static_cast<unsigned char>(s.back()) <= 32) {
            s.pop_back();
        }
        size_t i = 0;
        while (i < s.size() && static_cast<unsigned char>(s[i]) <= 32) {
            ++i;
        }
        if (i > 0) {
            s.erase(0, i);
        }
    };
    auto is_game_root_relative = [&trim](std::string s) -> bool {
        trim(s);
        if (s.empty()) {
            return false;
        }
        if (s == ".") {
            return true;
        }
        if (s.size() == 2 && s[0] == '.' && (s[1] == '\\' || s[1] == '/')) {
            return true;
        }
        return false;
    };
    static constexpr const char kScreenshots[] = ".\\Screenshots";

    char buf[1024];
    size_t sz = sizeof(buf);
    const bool have_save_path =
        reshade::get_config_value(runtime, "SCREENSHOT", "SavePath", buf, &sz);
    std::string save_val = have_save_path ? std::string(buf) : std::string();
    if (!have_save_path || is_game_root_relative(std::move(save_val))) {
        reshade::set_config_value(runtime, "SCREENSHOT", "SavePath", kScreenshots);
        LogInfo("ReShade settings override - SCREENSHOT SavePath -> %s (was game root / default)",
                kScreenshots);
    }

    sz = sizeof(buf);
    const bool have_post_cwd =
        reshade::get_config_value(runtime, "SCREENSHOT", "PostSaveCommandWorkingDirectory", buf, &sz);
    std::string post_val = have_post_cwd ? std::string(buf) : std::string();
    trim(post_val);
    if (have_post_cwd && post_val.empty()) {
        return;  // user cleared working directory; do not override
    }
    if (!have_post_cwd || is_game_root_relative(std::move(post_val))) {
        reshade::set_config_value(runtime, "SCREENSHOT", "PostSaveCommandWorkingDirectory", kScreenshots);
        LogInfo(
            "ReShade settings override - SCREENSHOT PostSaveCommandWorkingDirectory -> %s (was game root / "
            "default)",
            kScreenshots);
    }
}

constexpr const char* kDisplayCommanderSection = "DisplayCommander";
constexpr const char* kConfigKeyGlobalShadersPathsEnabled = "ReShadeGlobalShadersTexturesPathsEnabled";
constexpr const char* kConfigKeyScreenshotPathEnabled = "ReShadeScreenshotPathEnabled";
constexpr wchar_t kGlobalShadersMarkerFileName[] = L".DC_GLOBAL_SHADERS";

bool IsGlobalShadersMarkerEnabled() {
    std::filesystem::path dc_root = GetDisplayCommanderAppDataRootPathNoCreate();
    if (dc_root.empty()) {
        return false;
    }
    std::error_code ec;
    return std::filesystem::is_regular_file(dc_root / kGlobalShadersMarkerFileName, ec) && !ec;
}

bool IsGlobalShadersPathsConfigEnabled() {
    bool enabled = false;
    display_commander::config::get_config_value_ensure_exists(kDisplayCommanderSection, kConfigKeyGlobalShadersPathsEnabled,
                                                               enabled, false);
    if (enabled) {
        return true;
    }
    return IsGlobalShadersMarkerEnabled();
}

bool IsScreenshotPathConfigEnabled() {
    bool enabled = false;
    display_commander::config::get_config_value_ensure_exists(kDisplayCommanderSection, kConfigKeyScreenshotPathEnabled,
                                                               enabled, false);
    return enabled;
}

void OverrideReShadeSettings_LoadFromDllMainOnce(reshade::api::effect_runtime* runtime) {
    bool load_from_dll_main_set_once = false;
    display_commander::config::get_config_value("DisplayCommander", "LoadFromDllMainSetOnce",
                                                load_from_dll_main_set_once);
    if (!load_from_dll_main_set_once) {
        int32_t current_reshade_value = 0;
        reshade::get_config_value(runtime, "ADDON", "LoadFromDllMain", current_reshade_value);
        LogInfo("ReShade settings override - LoadFromDllMain current ReShade value: %d", current_reshade_value);
        LogInfo("ReShade settings override - LoadFromDllMain set to 0 (first time)");
        display_commander::config::set_config_value("DisplayCommander", "LoadFromDllMainSetOnce", true);
        display_commander::config::save_config("LoadFromDllMainSetOnce flag set");
        LogInfo("ReShade settings override - LoadFromDllMainSetOnce flag saved to DisplayCommander config");
    } else {
        LogInfo("ReShade settings override - LoadFromDllMain already set to 0 previously, skipping");
    }
}

void OverrideReShadeSettings_AddDisplayCommanderPaths(reshade::api::effect_runtime* runtime, bool enabled) {

    std::filesystem::path dc_base_dir = GetDisplayCommanderReshadeRootFolder();
    if (dc_base_dir.empty()) {
        LogWarn("Failed to get DC Reshade root path, skipping ReShade path configuration");
        return;
    }
    std::filesystem::path shaders_dir = dc_base_dir / L"Shaders";
    std::filesystem::path textures_dir = dc_base_dir / L"Textures";

    try {
        std::error_code ec;
        std::filesystem::create_directories(shaders_dir, ec);
        if (ec) {
            LogWarn("Failed to create shaders directory: %ls (error: %s)", shaders_dir.c_str(), ec.message().c_str());
        } else {
            LogInfo("Created/verified shaders directory: %ls", shaders_dir.c_str());
        }
        std::filesystem::create_directories(textures_dir, ec);
        if (ec) {
            LogWarn("Failed to create textures directory: %ls (error: %s)", textures_dir.c_str(), ec.message().c_str());
        } else {
            LogInfo("Created/verified textures directory: %ls", textures_dir.c_str());
        }
    } catch (const std::exception& e) {
        LogWarn("Exception while creating directories: %s", e.what());
        return;
    }

    auto syncPathInSearchPaths = [runtime, enabled](const char* section, const char* key,
                                                    const std::filesystem::path& path_to_sync) -> bool {
        char buffer[4096] = {0};
        size_t buffer_size = sizeof(buffer);
        std::vector<std::string> existing_paths;
        if ((g_reshade_module != nullptr) && reshade::get_config_value(runtime, section, key, buffer, &buffer_size)) {
            const char* ptr = buffer;
            while (*ptr != '\0' && ptr < buffer + buffer_size) {
                std::string path(ptr);
                if (!path.empty()) {
                    existing_paths.push_back(path);
                }
                ptr += path.length() + 1;
            }
        }
        std::string path_str = path_to_sync.string();
        path_str += "\\**";
        auto normalizeForComparison = [](const std::string& path) -> std::string {
            std::string normalized = path;
            if (normalized.length() >= 3 && normalized.substr(normalized.length() - 3) == "\\**") {
                normalized = normalized.substr(0, normalized.length() - 3);
            }
            return normalized;
        };
        std::string normalized_path = normalizeForComparison(path_str);
        bool found_existing = false;
        std::vector<std::string> updated_paths;
        updated_paths.reserve(existing_paths.size());
        for (const auto& existing_path : existing_paths) {
            std::string normalized_existing = normalizeForComparison(existing_path);
            if (normalized_path.length() == normalized_existing.length()
                && std::equal(normalized_path.begin(), normalized_path.end(), normalized_existing.begin(),
                              [](char a, char b) { return std::tolower(a) == std::tolower(b); })) {
                found_existing = true;
                if (enabled) {
                    updated_paths.push_back(existing_path);
                }
                continue;
            }
            updated_paths.push_back(existing_path);
        }

        if (enabled && !found_existing) {
            updated_paths.push_back(path_str);
        }

        if ((enabled && found_existing) || (!enabled && !found_existing)) {
            LogInfo("ReShade %s::%s unchanged for path %s (enabled=%d)", section, key, normalized_path.c_str(),
                    enabled ? 1 : 0);
            return false;
        }

        std::string combined;
        for (const auto& path : updated_paths) {
            combined += path;
            combined += '\0';
        }
        reshade::set_config_value(runtime, section, key, combined.c_str(), combined.size());
        LogInfo("%s path in ReShade %s::%s: %s", enabled ? "Added" : "Removed", section, key, path_str.c_str());
        return true;
    };

    syncPathInSearchPaths("GENERAL", "EffectSearchPaths", shaders_dir);
    syncPathInSearchPaths("GENERAL", "TextureSearchPaths", textures_dir);
}
}  // namespace

// Override ReShade settings to set tutorial as viewed and disable auto updates.
// When runtime is non-null (e.g. from OnInitEffectRuntime), config is applied to that runtime's .ini; otherwise
// global/current.
void OverrideReShadeSettings(reshade::api::effect_runtime* runtime) {
    if (g_reshade_module == nullptr) {
        return;  // No-ReShade mode or ReShade not loaded; skip ReShade config override
    }
    LogInfo("Overriding ReShade settings - Setting tutorial as viewed and disabling auto updates");

    OverrideReShadeSettings_WindowConfig(runtime);
    OverrideReShadeSettings_TutorialAndUpdates(runtime);
    if (IsScreenshotPathConfigEnabled()) {
        OverrideReShadeSettings_ScreenshotPaths(runtime);
    } else {
        // Restore defaults when this local toggle is disabled and the config still points to .\Screenshots.
        static constexpr const char kScreenshots[] = ".\\Screenshots";
        static constexpr const char kGameRoot[] = ".\\";
        char buf[1024];
        size_t sz = sizeof(buf);
        if (reshade::get_config_value(runtime, "SCREENSHOT", "SavePath", buf, &sz) && std::string(buf) == kScreenshots) {
            reshade::set_config_value(runtime, "SCREENSHOT", "SavePath", kGameRoot);
        }
        sz = sizeof(buf);
        if (reshade::get_config_value(runtime, "SCREENSHOT", "PostSaveCommandWorkingDirectory", buf, &sz)
            && std::string(buf) == kScreenshots) {
            reshade::set_config_value(runtime, "SCREENSHOT", "PostSaveCommandWorkingDirectory", kGameRoot);
        }
    }
    OverrideReShadeSettings_LoadFromDllMainOnce(runtime);
    OverrideReShadeSettings_AddDisplayCommanderPaths(runtime, IsGlobalShadersPathsConfigEnabled());

    LogInfo("ReShade settings override completed successfully");
}
