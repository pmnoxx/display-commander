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
constexpr wchar_t kNoReshadeClockMarkerFileName[] = L".NO_RESHADE_CLOCK";
constexpr const char* kWindowPrefix = "[Window][";
constexpr const char* kDcWindowHeader = "[Window][DC]";
constexpr const char* kRenodxWindowHeader = "[Window][RenoDX]";
constexpr const char* kDefaultDcWindowTemplate =
    "[Window][DC],Pos=1017,,20,Size=1344,,1255,Collapsed=0,DockId=0x00000001,,999999,";
constexpr const char* kDefaultRenodxWindowTemplate =
    "[Window][RenoDX],Pos=1017,,20,Size=1344,,1255,Collapsed=0,DockId=0x00000001,,9999999,";

using WindowEntry = std::vector<std::string>;
using WindowEntries = std::vector<WindowEntry>;

bool IsNoReshadeClockMarkerEnabled() {
    std::filesystem::path dc_root = GetDisplayCommanderAppDataRootPathNoCreate();
    if (dc_root.empty()) {
        return false;
    }
    std::error_code ec;
    return std::filesystem::is_regular_file(dc_root / kNoReshadeClockMarkerFileName, ec) && !ec;
}

bool StartsWithWindowHeader(const std::string& token) {
    return token.rfind(kWindowPrefix, 0) == 0;
}

std::vector<std::string> SplitNullDelimitedTokens(const std::string& value) {
    std::vector<std::string> tokens;
    std::string current;
    current.reserve(value.size());
    for (char ch : value) {
        if (ch == '\0') {
            tokens.push_back(current);
            current.clear();
            continue;
        }
        current.push_back(ch);
    }
    if (!current.empty()) {
        tokens.push_back(current);
    }
    return tokens;
}

WindowEntry ParseCommaTemplate(const std::string& template_value) {
    WindowEntry tokens;
    std::string current;
    current.reserve(template_value.size());
    for (char ch : template_value) {
        if (ch == ',') {
            tokens.push_back(current);
            current.clear();
            continue;
        }
        current.push_back(ch);
    }
    tokens.push_back(current);
    return tokens;
}

WindowEntries ParseWindowEntries(const std::vector<std::string>& tokens) {
    WindowEntries entries;
    WindowEntry current_entry;
    for (const std::string& token : tokens) {
        if (StartsWithWindowHeader(token) && !current_entry.empty()) {
            entries.push_back(current_entry);
            current_entry.clear();
        }
        current_entry.push_back(token);
    }
    if (!current_entry.empty()) {
        entries.push_back(current_entry);
    }
    return entries;
}

int FindWindowEntryIndex(const WindowEntries& entries, const std::string& window_header) {
    for (size_t i = 0; i < entries.size(); ++i) {
        if (!entries[i].empty() && entries[i][0] == window_header) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

bool EntryHasDockId(const WindowEntry& entry) {
    return std::any_of(entry.begin(), entry.end(), [](const std::string& token) {
        return token.rfind("DockId=", 0) == 0;
    });
}

std::string BuildWindowEntryForLog(const WindowEntry& entry) {
    std::string joined;
    for (size_t i = 0; i < entry.size(); ++i) {
        if (i > 0) {
            joined.push_back(',');
        }
        joined += entry[i];
    }
    return joined;
}

std::string SerializeWindowEntries(const WindowEntries& entries) {
    std::string serialized;
    for (const WindowEntry& entry : entries) {
        for (const std::string& token : entry) {
            serialized += token;
            serialized.push_back('\0');
        }
    }
    return serialized;
}

// Helpers for OverrideReShadeSettings - each handles one logical block.
// When runtime is non-null, config is read/written for that runtime's .ini (e.g. ReShade2.ini); otherwise
// global/current.
void OverrideReShadeSettings_WindowConfig(reshade::api::effect_runtime* runtime) {
    LogDebug("[ReShadeOverride] OVERLAY.Window: begin runtime=%p", static_cast<void*>(runtime));

    std::string window_config;
    size_t value_size = 0;
    if ((g_reshade_module != nullptr)
        && reshade::get_config_value(runtime, "OVERLAY", "Window", nullptr, &value_size)) {
        LogDebug("[ReShadeOverride] OVERLAY.Window: existing value_size=%zu (incl. nulls as reported)", value_size);
        window_config.resize(value_size);
        if ((g_reshade_module != nullptr)
            && reshade::get_config_value(runtime, "OVERLAY", "Window", window_config.data(), &value_size)) {
            if (!window_config.empty() && window_config.back() == '\0') {
                window_config.pop_back();
            }
            LogDebug("[ReShadeOverride] OVERLAY.Window: read ok, stored_len=%zu", window_config.size());
        } else {
            window_config.clear();
            LogWarn("[ReShadeOverride] OVERLAY.Window: get_config_value buffer read failed after size query");
        }
    } else {
        LogDebug("[ReShadeOverride] OVERLAY.Window: no existing key or empty; starting fresh");
    }

    const WindowEntry default_dc_entry = ParseCommaTemplate(kDefaultDcWindowTemplate);
    const WindowEntry default_renodx_entry = ParseCommaTemplate(kDefaultRenodxWindowTemplate);
    std::vector<std::string> tokens = SplitNullDelimitedTokens(window_config);
    WindowEntries entries = ParseWindowEntries(tokens);
    LogDebug("[ReShadeOverride] OVERLAY.Window: parsed %zu entries", entries.size());

    bool changed_window_config = false;
    int dc_entry_index = FindWindowEntryIndex(entries, kDcWindowHeader);
    if (dc_entry_index < 0) {
        entries.push_back(default_dc_entry);
        dc_entry_index = static_cast<int>(entries.size()) - 1;
        changed_window_config = true;
        LogInfo("[ReShadeOverride] OVERLAY.Window: appended default [Window][DC] entry");
    } else if (!EntryHasDockId(entries[dc_entry_index])) {
        entries[dc_entry_index].push_back("DockId=0x00000001");
        entries[dc_entry_index].push_back("");
        entries[dc_entry_index].push_back("999999");
        changed_window_config = true;
        LogInfo("[ReShadeOverride] OVERLAY.Window: injected DockId into existing [Window][DC] entry");
    } else {
        LogDebug("[ReShadeOverride] OVERLAY.Window: unchanged ([Window][DC] with DockId already present)");
    }

    if (dc_entry_index >= 0) {
        LogInfo("[ReShadeOverride] OVERLAY.Window: [Window][DC] entry: %s",
                BuildWindowEntryForLog(entries[dc_entry_index]).c_str());
    }

    if (FindWindowEntryIndex(entries, kRenodxWindowHeader) < 0) {
        entries.push_back(default_renodx_entry);
        changed_window_config = true;
        LogInfo("[ReShadeOverride] OVERLAY.Window: appended default [Window][RenoDX] entry");
    }

    if (changed_window_config) {
        window_config = SerializeWindowEntries(entries);
        reshade::set_config_value(runtime, "OVERLAY", "Window", window_config.c_str(), window_config.size());
        LogInfo("[ReShadeOverride] OVERLAY.Window: wrote config (%zu bytes)", window_config.size());
    }
}

void OverrideReShadeSettings_TutorialAndUpdates(reshade::api::effect_runtime* runtime) {
    reshade::set_config_value(runtime, "OVERLAY", "TutorialProgress", 4);
    reshade::set_config_value(runtime, "GENERAL", "CheckForUpdates", 0);

    if (IsNoReshadeClockMarkerEnabled()) {
        reshade::set_config_value(runtime, "OVERLAY", "ShowClock", 0);
        LogInfo("ReShade settings override - OVERLAY ShowClock -> 0 (.NO_RESHADE_CLOCK marker)");
    }
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
    // Still inside DLL_PROCESS_ATTACH: ReShade config APIs can deadlock under the loader lock (DS2 + MFG).
    if (!g_dc_dllmain_process_attach_returned.load(std::memory_order_acquire)) {
        LogWarn("[ReShadeOverride] skipped during DllMain (loader lock); will run from OnInitEffectRuntime");
        return;
    }
    LogInfo("Overriding ReShade settings - Setting tutorial as viewed and disabling auto updates");

    OverrideReShadeSettings_WindowConfig(runtime);
    LogInfo("[ReShadeOverride] after WindowConfig");
    OverrideReShadeSettings_TutorialAndUpdates(runtime);
    LogInfo("[ReShadeOverride] after TutorialAndUpdates");
    if (IsScreenshotPathConfigEnabled()) {
        OverrideReShadeSettings_ScreenshotPaths(runtime);
        LogInfo("[ReShadeOverride] after ScreenshotPaths");
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
        LogInfo("[ReShadeOverride] after ScreenshotPaths restore-defaults");
    }
    OverrideReShadeSettings_AddDisplayCommanderPaths(runtime, IsGlobalShadersPathsConfigEnabled());
    LogInfo("[ReShadeOverride] after AddDisplayCommanderPaths");

    LogInfo("ReShade settings override completed successfully");
}
