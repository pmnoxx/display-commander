
// Source Code <Display Commander> // follow this order for includes in all files + add this comment at the top
#include "display_commander_config.hpp"
#include "chords_file.hpp"
#include "default_overrides.hpp"
#include "default_settings_file.hpp"
#include "global_overrides_file.hpp"
#include "hotkeys_file.hpp"
#include "../globals.hpp"
#include "../utils.hpp"
#include "../utils/display_commander_logger.hpp"
#include "../utils/general_utils.hpp"
#include "../utils/logging.hpp"
#include "../utils/string_utils.hpp"
#include "../utils/srwlock_wrapper.hpp"
#include "../utils/symlink_safe_file.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string_view>


#undef what

namespace display_commander::config {

namespace {

// TODO(remove after 2027-02-06): snake_case -> PascalCase key migration (v1.0.6). Delete kRenamedKeys,
// IniFile::RenameKeys and its call in Initialize() once users have had time to upgrade (3 months).
struct RenamedKey {
    const char* old_key;
    const char* new_key;
};
constexpr RenamedKey kRenamedKeys[] = {
    {"aspect_index", "AspectRatio"},
    {"aspect_width", "WindowAspectWidth"},
    {"alignment", "Alignment"},
    {"fps_limiter_enabled", "FpsLimiter.Enabled"},
    {"fps_limiter_mode", "FpsLimiter.Mode"},
    {"fps_limit", "FpsLimiter.TargetFps"},
    {"fps_limit_background", "FpsLimiter.BackgroundTargetFps"},
    {"background_fps_enabled", "FpsLimiter.BackgroundLimitEnabled"},
    {"fps_limiter_fg2_enabled", "FpsLimiter.Fg2Enabled"},
    {"fps_limiter_fg2_target_boost_percent", "FpsLimiter.Fg2TargetBoostPercent"},
    {"suppress_reflex_sleep", "Reflex.SuppressSleep"},
    {"inject_reflex", "Reflex.InjectionEnabled"},
    {"onpresent_sync_low_latency_ratio", "FpsLimiter.OnPresentSyncLowLatencyRatio"},
    {"onpresent_reflex_mode", "Reflex.OnPresentSyncMode"},
    {"reflex_limiter_reflex_mode", "Reflex.ReflexLimiterMode"},
    {"reflex_disabled_limiter_mode", "Reflex.LimiterOffMode"},
    {"pcl_stats_enabled", "Reflex.PclStatsEnabled"},
    {"use_reflex_markers_as_fps_limiter", "FpsLimiter.ReflexMarkersEnabled"},
    {"reflex_fps_limiter_max_queued_frames", "FpsLimiter.ReflexMaxQueuedFrames"},
    {"native_reflex_fps_preset", "FpsLimiter.NativeReflexPreset"},
    {"use_streamline_proxy_fps_limiter", "FpsLimiter.StreamlineProxyEnabled"},
    {"native_pacing_sim_start_only_doff", "FpsLimiter.NativePacingSimStartOnly"},
    {"delay_present_start_after_sim_enabled_doff", "FpsLimiter.DelayPresentStartAfterSimEnabled"},
    {"delay_present_start_frames", "FpsLimiter.DelayPresentStartFrames"},
    {"safe_mode_fps_limiter", "FpsLimiter.SafeModeEnabled"},
    {"selected_reshade_runtime_index", "ReShade.SelectedRuntimeIndex"},
    {"prevent_tearing", "PreventTearing"},
    {"audio_mute", "Mute"},
    {"mute_in_background", "MuteInBackground"},
    {"mute_in_background_if_other_audio", "MuteInBackgroundIfOtherAudio"},
    {"enable_default_chords", "DefaultChordsEnabled"},
    {"guide_button_solo_ui_toggle_only", "GuideButtonSoloUiToggleOnly"},
    {"keyboard_input_blocking", "KeyboardBlockingMode"},
    {"mouse_input_blocking", "MouseBlockingMode"},
    {"gamepad_input_blocking", "GamepadBlockingMode"},
    {"clip_cursor_enabled", "ClipCursorEnabled"},
    {"unclip_cursor_enabled", "UnclipCursorEnabled"},
    {"show_performance_overlay", "ShowPerformanceOverlay"},
    {"show_fps_counter", "Overlay.ShowFpsCounter"},
    {"show_native_fps", "Overlay.ShowNativeFps"},
    {"show_refresh_rate", "Overlay.ShowRefreshRate"},
    {"show_flip_status", "Overlay.ShowFlipStatus"},
    {"show_vrr_status_def_off", "Overlay.ShowVrrStatus"},
    {"vrr_debug_mode_def_off", "Overlay.ShowVrrDebug"},
    {"show_display_commander_ui", "ShowDisplayCommanderUi"},
    {"display_commander_ui_window_x", "Window.PositionXPx"},
    {"display_commander_ui_window_y", "Window.PositionYPx"},
    {"overlay_label_mode", "Overlay.LabelMode"},
    {"show_clock", "Overlay.ShowClock"},
    {"show_frame_time_graph", "Overlay.ShowFrameTimeGraph"},
    {"show_frame_time_stats", "Overlay.ShowFrameTimeStats"},
    {"show_native_frame_time_graph", "Overlay.ShowNativeFrameTimeGraph"},
    {"show_cpu_usage", "Overlay.ShowCpuUsage"},
    {"show_overlay_cpu_process_load", "Overlay.ShowCpuProcessLoad"},
    {"show_overlay_cpu_system_load", "Overlay.ShowCpuSystemLoad"},
    {"show_nvapi_latency_stats", "ShowNvapiLatencyStats"},
    {"show_overlay_nvapi_sim_duration", "Overlay.ShowNvapiSimDuration"},
    {"show_overlay_nvapi_sim_end_to_rs_start", "Overlay.ShowNvapiSimEndToRenderSubmitStart"},
    {"show_overlay_nvapi_rs_submit_duration", "Overlay.ShowNvapiRenderSubmitDuration"},
    {"show_overlay_nvapi_rs_start_to_present_start", "Overlay.ShowNvapiRenderSubmitStartToPresentStart"},
    {"show_overlay_nvapi_rs_end_to_present_start", "Overlay.ShowNvapiRenderSubmitEndToPresentStart"},
    {"show_overlay_nvapi_present_phase_duration", "Overlay.ShowNvapiPresentPhaseDuration"},
    {"show_overlay_nvapi_present_end_to_rs_end", "Overlay.ShowNvapiPresentEndToRenderSubmitEnd"},
    {"show_overlay_nvapi_gpu_active_ms", "Overlay.ShowNvapiGpuActiveTime"},
    {"show_overlay_nvapi_latency_jitter_abs", "Overlay.ShowNvapiAbsoluteLatencyJitter"},
    {"show_overlay_nvapi_setlatencymarker_threads", "Overlay.ShowNvapiSetLatencyMarkerThreads"},
    {"show_fg_mode", "Overlay.ShowFgMode"},
    {"show_dlss_internal_resolution", "Overlay.ShowDlssInternalResolution"},
    {"show_dlss_status", "Overlay.ShowDlssStatus"},
    {"show_dlss_quality_preset", "Overlay.ShowDlssQualityPreset"},
    {"show_driver_dlss_sr_preset", "Overlay.ShowDriverDlssSuperResolutionPreset"},
    {"show_driver_dlss_rr_preset", "Overlay.ShowDriverDlssRayReconstructionPreset"},
    {"show_fps_limiter_src", "Overlay.ShowFpsLimiterSource"},
    {"show_fps_limiter_late_frames_pct", "Overlay.ShowFpsLimiterLateFramesPercent"},
    {"show_playtime", "Overlay.ShowPlaytime"},
    {"show_overlay_vu_bars", "Overlay.ShowVuBars"},
    {"show_overlay_vram", "Overlay.ShowVram"},
    {"show_overlay_ram", "Overlay.ShowRam"},
    {"overlay_background_alpha", "Overlay.BackgroundAlpha"},
    {"overlay_chart_alpha", "Overlay.ChartAlpha"},
    {"overlay_graph_scale", "Overlay.GraphScale"},
    {"overlay_graph_max_scale", "Overlay.GraphMaxScale"},
    {"overlay_vertical_spacing", "Overlay.VerticalSpacingPx"},
    {"overlay_horizontal_spacing", "Overlay.HorizontalSpacingPx"},
    {"gpu_measurement_enabled", "GpuMeasurementEnabled"},
    {"target_extended_display_device_id", "TargetExtendedDisplayDeviceId"},
    {"game_window_display_device_id", "GameWindowExtendedDisplayDeviceId"},
    {"selected_extended_display_device_id", "SelectedExtendedDisplayDeviceId"},
    {"adhd_single_monitor_enabled_for_game_display", "AdhdSingleMonitorEnabledForGameDisplay"},
    {"adhd_multi_monitor_enabled", "AdhdMultiMonitorEnabled"},
    {"screensaver_mode", "ScreensaverMode"},
    {"frame_time_mode", "FrameTimeMode"},
    {"advanced_settings_enabled", "AdvancedSettingsEnabled"},
    {"log_level", "LogLevel"},
    {"show_hotkeys_tab", "ShowHotkeysTab"},
    {"show_important_info_tab", "ShowImportantInfoTab"},
    {"show_controller_tab", "ShowControllerTab"},
    {"show_reshade_tab", "ShowReshadeTab"},
    {"vulkan_nvll_hooks_enabled_don", "Vulkan.NvLowLatencyHooksEnabled"},
    {"vulkan_vk_loader_hooks_enabled_don", "Vulkan.LoaderHooksEnabled"},
    {"auto_reshade_config_backup", "ReShade.ConfigBackupEnabled"},
    {"dlss_override_enabled", "DlssOverride.Enabled"},
    {"dlss_override_subfolder", "DlssOverride.DlssSubfolder"},
    {"dlss_override_subfolder_dlssd", "DlssOverride.DlssdSubfolder"},
    {"dlss_override_subfolder_dlssg", "DlssOverride.DlssgSubfolder"},
    {"dlss_override_dlss", "DlssOverride.DlssEnabled"},
    {"dlss_override_dlss_fg", "DlssOverride.DlssgEnabled"},
    {"dlss_override_dlss_rr", "DlssOverride.DlssdEnabled"},
};
// Module registry keys in every [Modules_<id>] section.
constexpr RenamedKey kRenamedModuleKeys[] = {
    {"enabled", "Enabled"},
    {"show_tab", "ShowTab"},
    {"show_in_overlay", "ShowInOverlay"},
};

// Keys like Remapping2.GamepadButton: sort by index numerically so Remapping10 follows Remapping9.
bool TryParseRemappingIniKey(std::string_view key, unsigned long& out_index, std::string_view& out_suffix) {
    constexpr std::string_view k_prefix = "Remapping";
    if (key.size() < k_prefix.size() + 1) return false;
    if (key.compare(0, k_prefix.size(), k_prefix) != 0) return false;
    size_t i = k_prefix.size();
    if (i >= key.size() || std::isdigit(static_cast<unsigned char>(key[i])) == 0) return false;
    out_index = 0;
    while (i < key.size() && std::isdigit(static_cast<unsigned char>(key[i])) != 0) {
        constexpr unsigned long k_radix = 10;
        out_index = out_index * k_radix + static_cast<unsigned long>(key[i] - '0');
        ++i;
    }
    if (i < key.size()) {
        if (key[i] != '.') return false;
        out_suffix = key.substr(i + 1);
    } else {
        out_suffix = {};
    }
    return true;
}

bool CompareIniKeysForSave(std::string_view a, std::string_view b) {
    unsigned long ia = 0;
    unsigned long ib = 0;
    std::string_view sa;
    std::string_view sb;
    const bool pa = TryParseRemappingIniKey(a, ia, sa);
    const bool pb = TryParseRemappingIniKey(b, ib, sb);
    if (pa && pb) {
        if (ia != ib) return ia < ib;
        return sa < sb;
    }
    return a < b;
}

bool WriteIniTextToPath(const std::filesystem::path& filepath, const std::string& text) {
    return display_commander::utils::WriteTextFileReplace(filepath, text, "Config");
}

}  // namespace

// Shared section representation for INI
struct ConfigSection {
    std::string name;
    std::vector<std::pair<std::string, std::string>> key_values;
};

// Simple INI-style parser used for reading/writing DisplayCommander.ini.
// Also tolerant enough to read the old DisplayCommander.toml (since it was stored as simple
// [section] + key = "value" lines).
class IniFile {
   public:
    bool LoadFromFile(const std::filesystem::path& filepath) {
        std::ifstream file(filepath);
        if (!file.is_open()) return false;

        sections_.clear();
        std::string line;
        ConfigSection* current_section = nullptr;

        while (std::getline(file, line)) {
            // Remove leading/trailing whitespace
            line.erase(0, line.find_first_not_of(" \t\r\n"));
            line.erase(line.find_last_not_of(" \t\r\n") + 1);

            if (line.empty() || line[0] == ';' || line[0] == '#') continue;

            if (line[0] == '[' && line.back() == ']') {
                std::string section_name = line.substr(1, line.length() - 2);
                sections_.push_back({section_name, {}});
                current_section = &sections_.back();
                continue;
            }

            if (current_section == nullptr) continue;

            size_t equal_pos = line.find('=');
            if (equal_pos == std::string::npos) continue;

            std::string key = line.substr(0, equal_pos);
            std::string value = line.substr(equal_pos + 1);

            // Remove leading/trailing whitespace from key/value
            key.erase(0, key.find_first_not_of(" \t"));
            key.erase(key.find_last_not_of(" \t") + 1);
            value.erase(0, value.find_first_not_of(" \t"));
            value.erase(value.find_last_not_of(" \t") + 1);

            // DisplayCommander.toml stored everything as strings, so the values were often quoted.
            if (value.size() >= 2
                && ((value.front() == '"' && value.back() == '"') || (value.front() == '\'' && value.back() == '\''))) {
                value = value.substr(1, value.size() - 2);
            }

            current_section->key_values.push_back({key, value});
        }

        return true;
    }

    std::string SerializeToString() const {
        std::ostringstream out;

        std::vector<size_t> section_order(sections_.size());
        for (size_t i = 0; i < section_order.size(); ++i) {
            section_order[i] = i;
        }
        std::sort(section_order.begin(), section_order.end(), [this](size_t a, size_t b) {
            return sections_[a].name < sections_[b].name;
        });

        for (size_t si : section_order) {
            const auto& section = sections_[si];
            std::vector<std::pair<std::string, std::string>> kvs = section.key_values;
            std::sort(kvs.begin(), kvs.end(), [](const auto& x, const auto& y) {
                return CompareIniKeysForSave(x.first, y.first);
            });

            out << "[" << section.name << "]\n";
            for (const auto& kv : kvs) {
                out << kv.first << "=" << kv.second << "\n";
            }
            out << "\n";
        }

        return out.str();
    }

    bool SaveToFile(const std::filesystem::path& filepath) {
        return WriteIniTextToPath(filepath, SerializeToString());
    }

    bool GetValue(const std::string& section, const std::string& key, std::string& value) const {
        for (const auto& s : sections_) {
            if (s.name != section) continue;
            for (const auto& kv : s.key_values) {
                if (kv.first != key) continue;
                value = kv.second;

                // Back-compat: older INI stored integer-ish device IDs, but current code expects the extended string IDs.
                if ((key.find("DeviceId") != std::string::npos
                     || key == "target_display")
                    && !value.empty()
                    && std::all_of(value.begin(), value.end(), ::isdigit)) {
                    value.clear();
                    // Remove the invalid value from the in-memory representation so it's not re-saved.
                    const_cast<IniFile*>(this)->SetValue(section, key, "");
                }

                return true;
            }
        }
        return false;
    }

    void SetValue(const std::string& section, const std::string& key, const std::string& value) {
        for (auto& s : sections_) {
            if (s.name != section) continue;
            for (auto& kv : s.key_values) {
                if (kv.first != key) continue;
                kv.second = value;
                return;
            }
            s.key_values.push_back({key, value});
            return;
        }
        sections_.push_back({section, {{key, value}}});
    }

    bool GetValue(const std::string& section, const std::string& key, std::vector<std::string>& values) const {
        values.clear();
        std::string value_str;
        if (!GetValue(section, key, value_str)) return false;

        std::stringstream ss(value_str);
        std::string item;
        while (std::getline(ss, item, '\0')) {
            if (!item.empty()) values.push_back(item);
        }
        return !values.empty();
    }

    void SetValue(const std::string& section, const std::string& key, const std::vector<std::string>& values) {
        std::string value_str;
        for (size_t i = 0; i < values.size(); ++i) {
            if (i > 0) value_str += '\0';
            value_str += values[i];
        }
        SetValue(section, key, value_str);
    }

    // Renames old keys of section_name (or of every section starting with it) to their new names in place (keeps order). When the new key already
    // exists, the old entry is just dropped. Returns true when anything changed.
    bool RenameKeys(std::string_view section_name, bool prefix_match, const RenamedKey* keys, size_t count) {
        bool changed = false;
        for (auto& s : sections_) {
            if (prefix_match ? s.name.compare(0, section_name.size(), section_name) != 0 : s.name != section_name) continue;
            for (size_t i = 0; i < count; ++i) {
                auto old_it = std::find_if(s.key_values.begin(), s.key_values.end(),
                                           [&](const auto& kv) { return kv.first == keys[i].old_key; });
                if (old_it == s.key_values.end()) continue;
                const bool new_exists = std::any_of(s.key_values.begin(), s.key_values.end(),
                                                    [&](const auto& kv) { return kv.first == keys[i].new_key; });
                if (new_exists) {
                    s.key_values.erase(old_it);
                } else {
                    old_it->first = keys[i].new_key;
                }
                changed = true;
            }
        }
        return changed;
    }

   private:
    std::vector<ConfigSection> sections_;
};

// DisplayCommanderConfigManager implementation
DisplayCommanderConfigManager& DisplayCommanderConfigManager::GetInstance() {
    static DisplayCommanderConfigManager instance;
    return instance;
}

void DisplayCommanderConfigManager::Initialize(std::optional<std::wstring_view> config_directory) {
    ::utils::SRWLockExclusive lock(config_mutex_);

    if (initialized_) {
        return;
    }

    std::filesystem::path config_dir;
    if (config_directory.has_value() && !config_directory->empty()) {
        config_dir = std::filesystem::path(std::wstring(config_directory->data(), config_directory->size()));
    }
    if (config_dir.empty()) {
        config_dir = display_commander::utils::Utf8ToFilesystemPath(GetConfigFilePath()).parent_path();
    }

    const std::filesystem::path toml_path = config_dir / L"DisplayCommander.toml";
    const std::filesystem::path ini_path = config_dir / L"DisplayCommander.ini";
    config_path_ = display_commander::utils::FilesystemPathToUtf8(ini_path);
    config_file_ = std::make_unique<IniFile>();

    // Initialize logger with DisplayCommander.log in the config directory
    display_commander::logger::Initialize(config_dir / L"DisplayCommander.log");

    // Test the logger
    display_commander::logger::LogInfo("DisplayCommander config system initializing - logger test successful");

    EnsureConfigFileExists();

    // Prefer .ini; migrate from old .toml if only .toml exists.
    const bool ini_exists = std::filesystem::exists(ini_path);
    const bool toml_exists = std::filesystem::exists(toml_path);

    if (ini_exists) {
        if (config_file_->LoadFromFile(ini_path)) {
            LogInfo("DisplayCommanderConfigManager: Loaded config from %s", config_path_.c_str());
            const bool renamed_host = config_file_->RenameKeys("DisplayCommander", false, kRenamedKeys, std::size(kRenamedKeys));
            const bool renamed_modules = config_file_->RenameKeys("Modules_", true, kRenamedModuleKeys, std::size(kRenamedModuleKeys));
            if (renamed_host || renamed_modules) {
                LogInfo("DisplayCommanderConfigManager: Renamed old snake_case keys to PascalCase");
                config_file_->SaveToFile(ini_path);
            }
        } else {
            LogInfo("DisplayCommanderConfigManager: Opened config file at %s (load failed, using empty)",
                    config_path_.c_str());
        }
    } else if (toml_exists) {
        if (config_file_->LoadFromFile(toml_path) && config_file_->SaveToFile(ini_path)) {
            std::error_code ec;
            std::filesystem::remove(toml_path, ec);
            LogInfo("DisplayCommanderConfigManager: Migrated config from %s to %s and removed .toml",
                    display_commander::utils::FilesystemPathToUtf8(toml_path).c_str(), config_path_.c_str());
        } else {
            LogInfo("DisplayCommanderConfigManager: Migrated config from %s to memory; save to %s failed (will retry "
                    "on next save)",
                    display_commander::utils::FilesystemPathToUtf8(toml_path).c_str(), config_path_.c_str());
        }
    } else {
        LogInfo("DisplayCommanderConfigManager: Created new config file at %s", config_path_.c_str());
    }

    initialized_ = true;
}

bool DisplayCommanderConfigManager::GetConfigValue(const char* section, const char* key, std::string& value) {
    // Hotkeys are stored in hotkeys.toml (Display Commander folder) for sharing across games
    if (section != nullptr && strcmp(section, "DisplayCommander") == 0 && key != nullptr && IsHotkeyConfigKey(key)) {
        return GetHotkeyValue(key, value);
    }
    // Global overrides: values in global_overrides.toml override game config even when it exists
    // (auto_reshade_config_backup is not overridden globally anymore; ignore stale key in global_overrides.toml)
    if (section != nullptr && strcmp(section, "DisplayCommander") == 0 && key != nullptr
        && strcmp(key, "ReShade.ConfigBackupEnabled") != 0 && GetGlobalOverrideValue(key, value)) {
        return true;
    }
    // Chords / gamepad remap settings are stored in chords.toml for sharing across games
    if (section != nullptr && key != nullptr && IsChordConfigKey(section, key)) {
        return GetChordValue(section, key, value);
    }
    ::utils::SRWLockExclusive lock(config_mutex_);
    if (!initialized_) {
        Initialize();
    }
    if (!config_file_->GetValue(section != nullptr ? section : "", key != nullptr ? key : "", value)) {
        return false;
    }
    return true;
}

bool DisplayCommanderConfigManager::GetConfigValue(const char* section, const char* key, int& value) {
    std::string str_value;
    if (GetConfigValue(section, key, str_value)) {
        try {
            value = std::stoi(str_value);
            return true;
        } catch (const std::exception&) {
            return false;
        }
    }
    return false;
}

bool DisplayCommanderConfigManager::GetConfigValue(const char* section, const char* key, uint32_t& value) {
    std::string str_value;
    if (GetConfigValue(section, key, str_value)) {
        try {
            value = static_cast<uint32_t>(std::stoul(str_value));
            return true;
        } catch (const std::exception&) {
            return false;
        }
    }
    return false;
}

bool DisplayCommanderConfigManager::GetConfigValue(const char* section, const char* key, float& value) {
    std::string str_value;
    if (GetConfigValue(section, key, str_value)) {
        try {
            value = std::stof(str_value);
            return true;
        } catch (const std::exception&) {
            return false;
        }
    }
    return false;
}

bool DisplayCommanderConfigManager::GetConfigValue(const char* section, const char* key, double& value) {
    std::string str_value;
    if (GetConfigValue(section, key, str_value)) {
        try {
            value = std::stod(str_value);
            return true;
        } catch (const std::exception&) {
            return false;
        }
    }
    return false;
}

bool DisplayCommanderConfigManager::GetConfigValue(const char* section, const char* key, bool& value) {
    int int_value;
    if (GetConfigValue(section, key, int_value)) {
        value = (int_value != 0);
        return true;
    }
    return false;
}

bool DisplayCommanderConfigManager::GetConfigValue(const char* section, const char* key,
                                                   std::vector<std::string>& values) {
    ::utils::SRWLockExclusive lock(config_mutex_);
    if (!initialized_) {
        Initialize();
    }
    return config_file_->GetValue(section != nullptr ? section : "", key != nullptr ? key : "", values);
}

void DisplayCommanderConfigManager::GetConfigValueEnsureExists(const char* section, const char* key, std::string& value,
                                                               const std::string& default_value) {
    if (!GetConfigValue(section, key, value)) {
        // Value doesn't exist, set default and save
        SetConfigValue(section, key, default_value);
        FlushConfig("get_config_value_ensure_exists");
        value = default_value;
    }
}

void DisplayCommanderConfigManager::GetConfigValueEnsureExists(const char* section, const char* key, int& value,
                                                               int default_value) {
    if (!GetConfigValue(section, key, value)) {
        // Value doesn't exist, set default and save
        SetConfigValue(section, key, default_value);
        FlushConfig("get_config_value_ensure_exists");
        value = default_value;
    }
}

void DisplayCommanderConfigManager::GetConfigValueEnsureExists(const char* section, const char* key, uint32_t& value,
                                                               uint32_t default_value) {
    if (!GetConfigValue(section, key, value)) {
        // Value doesn't exist, set default and save
        SetConfigValue(section, key, default_value);
        FlushConfig("get_config_value_ensure_exists");
        value = default_value;
    }
}

void DisplayCommanderConfigManager::GetConfigValueEnsureExists(const char* section, const char* key, float& value,
                                                               float default_value) {
    if (!GetConfigValue(section, key, value)) {
        // Value doesn't exist, set default and save
        SetConfigValue(section, key, default_value);
        FlushConfig("get_config_value_ensure_exists");
        value = default_value;
    }
}

void DisplayCommanderConfigManager::GetConfigValueEnsureExists(const char* section, const char* key, double& value,
                                                               double default_value) {
    if (!GetConfigValue(section, key, value)) {
        // Value doesn't exist, set default and save
        SetConfigValue(section, key, default_value);
        FlushConfig("get_config_value_ensure_exists");
        value = default_value;
    }
}

void DisplayCommanderConfigManager::GetConfigValueEnsureExists(const char* section, const char* key, bool& value,
                                                               bool default_value) {
    if (!GetConfigValue(section, key, value)) {
        // Value doesn't exist, set default and save
        SetConfigValue(section, key, default_value);
        FlushConfig("get_config_value_ensure_exists");
        value = default_value;
    }
}

void DisplayCommanderConfigManager::SetConfigValue(const char* section, const char* key, const std::string& value) {
    if (section != nullptr && strcmp(section, "DisplayCommander") == 0 && key != nullptr && IsHotkeyConfigKey(key)) {
        SetHotkeyValue(key, value);
        return;
    }
    if (section != nullptr && key != nullptr && IsChordConfigKey(section, key)) {
        SetChordValue(section, key, value);
        return;
    }
    ::utils::SRWLockExclusive lock(config_mutex_);
    if (!initialized_) {
        Initialize();
    }
    config_file_->SetValue(section != nullptr ? section : "", key != nullptr ? key : "", value);
}

void DisplayCommanderConfigManager::SetConfigValue(const char* section, const char* key, const char* value) {
    if (section != nullptr && strcmp(section, "DisplayCommander") == 0 && key != nullptr && IsHotkeyConfigKey(key)) {
        SetHotkeyValue(key, value != nullptr ? value : "");
        return;
    }
    if (section != nullptr && key != nullptr && IsChordConfigKey(section, key)) {
        SetChordValue(section, key, value != nullptr ? value : "");
        return;
    }
    ::utils::SRWLockExclusive lock(config_mutex_);
    if (!initialized_) {
        Initialize();
    }
    config_file_->SetValue(section != nullptr ? section : "", key != nullptr ? key : "", value != nullptr ? value : "");
}

void DisplayCommanderConfigManager::SetConfigValue(const char* section, const char* key, int value) {
    SetConfigValue(section, key, std::to_string(value));
}

void DisplayCommanderConfigManager::SetConfigValue(const char* section, const char* key, uint32_t value) {
    SetConfigValue(section, key, std::to_string(value));
}

void DisplayCommanderConfigManager::SetConfigValue(const char* section, const char* key, float value) {
    SetConfigValue(section, key, std::to_string(value));
}

void DisplayCommanderConfigManager::SetConfigValue(const char* section, const char* key, double value) {
    SetConfigValue(section, key, std::to_string(value));
}

void DisplayCommanderConfigManager::SetConfigValue(const char* section, const char* key, bool value) {
    SetConfigValue(section, key, value ? 1 : 0);
}

void DisplayCommanderConfigManager::SetConfigValue(const char* section, const char* key,
                                                   const std::vector<std::string>& values) {
    ::utils::SRWLockExclusive lock(config_mutex_);
    if (!initialized_) {
        Initialize();
    }
    config_file_->SetValue(section != nullptr ? section : "", key != nullptr ? key : "", values);
}

bool DisplayCommanderConfigManager::SerializeLiveIniLocked(std::string& out) {
    if (!initialized_ || config_file_ == nullptr) {
        return false;
    }
    EnsureConfigFileExists();
    out = config_file_->SerializeToString();
    return true;
}

std::string DisplayCommanderConfigManager::GetConfigPath() const { return config_path_; }

void DisplayCommanderConfigManager::SetAutoFlushLogs(bool enabled) {
    //auto_flush_logs_.store(enabled);
   //if (enabled) {
        // Immediately flush logs when auto-flush is enabled
    //    display_commander::logger::FlushLogs();
   // }
}

bool DisplayCommanderConfigManager::GetAutoFlushLogs() const { return auto_flush_logs_.load(); }

void DisplayCommanderConfigManager::EnsureConfigFileExists() {
    if (config_path_.empty()) {
        config_path_ = GetConfigFilePath();
    }

    // Create directory if it doesn't exist
    const std::filesystem::path config_dir =
        display_commander::utils::Utf8ToFilesystemPath(config_path_).parent_path();
    if (!config_dir.empty()) {
        std::error_code ec;
        if (!std::filesystem::exists(config_dir, ec)) {
            std::filesystem::create_directories(config_dir, ec);
        }
    }
}

std::string DisplayCommanderConfigManager::GetConfigFilePath() {
    const std::wstring exe_path = GetCurrentProcessPathW();
    if (!exe_path.empty()) {
        return display_commander::utils::FilesystemPathToUtf8(
            std::filesystem::path(exe_path).parent_path() / L"DisplayCommander.ini");
    }
    WCHAR cwd[MAX_PATH] = {};
    if (GetCurrentDirectoryW(MAX_PATH, cwd) > 0) {
        return display_commander::utils::FilesystemPathToUtf8(std::filesystem::path(cwd) / L"DisplayCommander.ini");
    }
    return "DisplayCommander.ini";
}

// Global function implementations
bool get_config_value(const char* section, const char* key, std::string& value) {
    return DisplayCommanderConfigManager::GetInstance().GetConfigValue(section, key, value);
}

bool get_config_value(const char* section, const char* key, int& value) {
    return DisplayCommanderConfigManager::GetInstance().GetConfigValue(section, key, value);
}

bool get_config_value(const char* section, const char* key, uint32_t& value) {
    return DisplayCommanderConfigManager::GetInstance().GetConfigValue(section, key, value);
}

bool get_config_value(const char* section, const char* key, float& value) {
    return DisplayCommanderConfigManager::GetInstance().GetConfigValue(section, key, value);
}

bool get_config_value(const char* section, const char* key, double& value) {
    return DisplayCommanderConfigManager::GetInstance().GetConfigValue(section, key, value);
}

bool get_config_value(const char* section, const char* key, bool& value) {
    return DisplayCommanderConfigManager::GetInstance().GetConfigValue(section, key, value);
}

bool get_config_value(const char* section, const char* key, std::vector<std::string>& values) {
    return DisplayCommanderConfigManager::GetInstance().GetConfigValue(section, key, values);
}

bool get_config_value(const char* section, const char* key, char* buffer, size_t* buffer_size) {
    std::string value;
    if (DisplayCommanderConfigManager::GetInstance().GetConfigValue(section, key, value)) {
        if (buffer != nullptr && buffer_size != nullptr) {
            size_t copy_size = (value.length() < (*buffer_size - 1)) ? value.length() : (*buffer_size - 1);
            strncpy_s(buffer, *buffer_size, value.c_str(), copy_size);
            buffer[copy_size] = '\0';
            *buffer_size = copy_size + 1;
            return true;
        }
    }
    return false;
}

void set_config_value(const char* section, const char* key, const std::string& value) {
    DisplayCommanderConfigManager::GetInstance().SetConfigValue(section, key, value);
}

void set_config_value(const char* section, const char* key, const char* value) {
    DisplayCommanderConfigManager::GetInstance().SetConfigValue(section, key, value);
}

void set_config_value(const char* section, const char* key, int value) {
    DisplayCommanderConfigManager::GetInstance().SetConfigValue(section, key, value);
}

void set_config_value(const char* section, const char* key, uint32_t value) {
    DisplayCommanderConfigManager::GetInstance().SetConfigValue(section, key, value);
}

void set_config_value(const char* section, const char* key, float value) {
    DisplayCommanderConfigManager::GetInstance().SetConfigValue(section, key, value);
}

void set_config_value(const char* section, const char* key, double value) {
    DisplayCommanderConfigManager::GetInstance().SetConfigValue(section, key, value);
}

void set_config_value(const char* section, const char* key, bool value) {
    DisplayCommanderConfigManager::GetInstance().SetConfigValue(section, key, value);
}

void set_config_value(const char* section, const char* key, const std::vector<std::string>& values) {
    DisplayCommanderConfigManager::GetInstance().SetConfigValue(section, key, values);
}

void save_config(const char* reason) { DisplayCommanderConfigManager::GetInstance().SaveConfig(reason); }

void save_config_immediate(const char* reason) {
    DisplayCommanderConfigManager::GetInstance().FlushConfig(reason);
}

void flush_config_on_overlay_close() {
    DisplayCommanderConfigManager::GetInstance().FlushConfigOnOverlayClose();
}

void flush_config_on_exit() { DisplayCommanderConfigManager::GetInstance().FlushConfigOnExit(); }

void get_config_value_ensure_exists(const char* section, const char* key, std::string& value,
                                    const std::string& default_value) {
    DisplayCommanderConfigManager::GetInstance().GetConfigValueEnsureExists(section, key, value, default_value);
}

void get_config_value_ensure_exists(const char* section, const char* key, int& value, int default_value) {
    DisplayCommanderConfigManager::GetInstance().GetConfigValueEnsureExists(section, key, value, default_value);
}

void get_config_value_ensure_exists(const char* section, const char* key, uint32_t& value, uint32_t default_value) {
    DisplayCommanderConfigManager::GetInstance().GetConfigValueEnsureExists(section, key, value, default_value);
}

void get_config_value_ensure_exists(const char* section, const char* key, float& value, float default_value) {
    DisplayCommanderConfigManager::GetInstance().GetConfigValueEnsureExists(section, key, value, default_value);
}

void get_config_value_ensure_exists(const char* section, const char* key, double& value, double default_value) {
    DisplayCommanderConfigManager::GetInstance().GetConfigValueEnsureExists(section, key, value, default_value);
}

void get_config_value_ensure_exists(const char* section, const char* key, bool& value, bool default_value) {
    DisplayCommanderConfigManager::GetInstance().GetConfigValueEnsureExists(section, key, value, default_value);
}

void get_config_value_or_default(const char* section, const char* key, bool default_value, bool* out_value) {
    if (out_value == nullptr) return;
    bool from_config = false;
    if (get_config_value(section, key, from_config)) {
        *out_value = from_config;
        return;
    }
    std::string ov;
    if (display_commander::config::GetDefaultOverride(section, key, ov)) {
        if (ov == "1" || ov == "true" || ov == "yes") {
            *out_value = true;
            display_commander::config::MarkUsedOverride(section, key);
            return;
        }
        if (ov == "0" || ov == "false" || ov == "no") {
            *out_value = false;
            display_commander::config::MarkUsedOverride(section, key);
            return;
        }
    }
    if (display_commander::config::GetDefaultSettingValue(section, key, ov)) {
        if (ov == "1" || ov == "true" || ov == "yes") {
            *out_value = true;
            return;
        }
        if (ov == "0" || ov == "false" || ov == "no") {
            *out_value = false;
            return;
        }
    }
    *out_value = default_value;
}

void get_config_value_or_default(const char* section, const char* key, int default_value, int* out_value) {
    if (out_value == nullptr) return;
    int from_config = 0;
    if (get_config_value(section, key, from_config)) {
        *out_value = from_config;
        return;
    }
    std::string ov;
    if (display_commander::config::GetDefaultOverride(section, key, ov)) {
        try {
            *out_value = std::stoi(ov);
            display_commander::config::MarkUsedOverride(section, key);
            return;
        } catch (const std::exception&) {
            /* fall through to default */
        }
    }
    if (display_commander::config::GetDefaultSettingValue(section, key, ov)) {
        try {
            *out_value = std::stoi(ov);
            return;
        } catch (const std::exception&) {
            /* fall through to default */
        }
    }
    *out_value = default_value;
}

void get_config_value_or_default(const char* section, const char* key, float default_value, float* out_value) {
    if (out_value == nullptr) return;
    float from_config = 0.0f;
    if (get_config_value(section, key, from_config)) {
        *out_value = from_config;
        return;
    }
    std::string ov;
    if (display_commander::config::GetDefaultOverride(section, key, ov)) {
        try {
            *out_value = std::stof(ov);
            display_commander::config::MarkUsedOverride(section, key);
            return;
        } catch (const std::exception&) {
            /* fall through to default */
        }
    }
    if (display_commander::config::GetDefaultSettingValue(section, key, ov)) {
        try {
            *out_value = std::stof(ov);
            return;
        } catch (const std::exception&) {
            /* fall through to default */
        }
    }
    *out_value = default_value;
}

}  // namespace display_commander::config
