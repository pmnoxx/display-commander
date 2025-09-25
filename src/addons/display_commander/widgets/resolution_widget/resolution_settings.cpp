#include "resolution_settings.hpp"
#include "../../utils.hpp"
#include <imgui.h>
#include <reshade.hpp>


namespace display_commander::widgets::resolution_widget {

// Global instance
std::unique_ptr<ResolutionSettingsManager> g_resolution_settings = nullptr;

// DisplayResolutionSettings implementation
DisplayResolutionSettings::DisplayResolutionSettings(const std::string &display_key, int display_index)
    : display_key_(display_key), display_index_(display_index) {
    // Initialize with current resolution as default
    current_state_.is_current = true;
    last_saved_state_.is_current = true;
}

void DisplayResolutionSettings::Load() {
    LogInfo("DisplayResolutionSettings::Load() - Loading settings for %s (display %d)", display_key_.c_str(),
            display_index_);

    // Load width
    std::string width_key = display_key_ + "_width";
    int loaded_width;
    if (reshade::get_config_value(nullptr, "DisplayCommander.ResolutionWidget", width_key.c_str(), loaded_width)) {
        last_saved_state_.width = loaded_width;
        LogInfo("DisplayResolutionSettings::Load() - Loaded %s: %d", width_key.c_str(), loaded_width);
    } else {
        LogInfo("DisplayResolutionSettings::Load() - %s not found, using default: 0", width_key.c_str());
    }

    // Load height
    std::string height_key = display_key_ + "_height";
    int loaded_height;
    if (reshade::get_config_value(nullptr, "DisplayCommander.ResolutionWidget", height_key.c_str(), loaded_height)) {
        last_saved_state_.height = loaded_height;
        LogInfo("DisplayResolutionSettings::Load() - Loaded %s: %d", height_key.c_str(), loaded_height);
    } else {
        LogInfo("DisplayResolutionSettings::Load() - %s not found, using default: 0", height_key.c_str());
    }

    // Load refresh numerator
    std::string refresh_num_key = display_key_ + "_refresh_num";
    int loaded_refresh_num;
    if (reshade::get_config_value(nullptr, "DisplayCommander.ResolutionWidget", refresh_num_key.c_str(),
                                  loaded_refresh_num)) {
        last_saved_state_.refresh_numerator = loaded_refresh_num;
        LogInfo("DisplayResolutionSettings::Load() - Loaded %s: %d", refresh_num_key.c_str(), loaded_refresh_num);
    } else {
        LogInfo("DisplayResolutionSettings::Load() - %s not found, using default: 0", refresh_num_key.c_str());
    }

    // Load refresh denominator
    std::string refresh_denom_key = display_key_ + "_refresh_denom";
    int loaded_refresh_denom;
    if (reshade::get_config_value(nullptr, "DisplayCommander.ResolutionWidget", refresh_denom_key.c_str(),
                                  loaded_refresh_denom)) {
        last_saved_state_.refresh_denominator = loaded_refresh_denom;
        LogInfo("DisplayResolutionSettings::Load() - Loaded %s: %d", refresh_denom_key.c_str(), loaded_refresh_denom);
    } else {
        LogInfo("DisplayResolutionSettings::Load() - %s not found, using default: 0", refresh_denom_key.c_str());
    }

    // Load is_current flag
    std::string current_key = display_key_ + "_is_current";
    bool loaded_is_current;
    if (reshade::get_config_value(nullptr, "DisplayCommander.ResolutionWidget", current_key.c_str(),
                                  loaded_is_current)) {
        last_saved_state_.is_current = loaded_is_current;
        LogInfo("DisplayResolutionSettings::Load() - Loaded %s: %s", current_key.c_str(),
                loaded_is_current ? "true" : "false");
    } else {
        LogInfo("DisplayResolutionSettings::Load() - %s not found, using default: true", current_key.c_str());
    }

    // Set current state to match last saved
    current_state_ = last_saved_state_;
    ClearDirty();

    LogInfo("DisplayResolutionSettings::Load() - Final loaded state: %dx%d @ %d/%d, is_current=%s",
            last_saved_state_.width, last_saved_state_.height, last_saved_state_.refresh_numerator,
            last_saved_state_.refresh_denominator, last_saved_state_.is_current ? "true" : "false");
}

void DisplayResolutionSettings::Save() {
    // Save width
    std::string width_key = display_key_ + "_width";
    reshade::set_config_value(nullptr, "DisplayCommander.ResolutionWidget", width_key.c_str(), last_saved_state_.width);

    // Save height
    std::string height_key = display_key_ + "_height";
    reshade::set_config_value(nullptr, "DisplayCommander.ResolutionWidget", height_key.c_str(),
                              last_saved_state_.height);

    // Save refresh numerator
    std::string refresh_num_key = display_key_ + "_refresh_num";
    reshade::set_config_value(nullptr, "DisplayCommander.ResolutionWidget", refresh_num_key.c_str(),
                              last_saved_state_.refresh_numerator);

    // Save refresh denominator
    std::string refresh_denom_key = display_key_ + "_refresh_denom";
    reshade::set_config_value(nullptr, "DisplayCommander.ResolutionWidget", refresh_denom_key.c_str(),
                              last_saved_state_.refresh_denominator);

    // Save is_current flag
    std::string current_key = display_key_ + "_is_current";
    reshade::set_config_value(nullptr, "DisplayCommander.ResolutionWidget", current_key.c_str(),
                              last_saved_state_.is_current);
}

void DisplayResolutionSettings::SetCurrentState(const ResolutionData &data) {
    if (current_state_ != data) {
        current_state_ = data;
        MarkDirty();
    }
}

void DisplayResolutionSettings::SaveCurrentState() {
    last_saved_state_ = current_state_;
    ClearDirty();
}

void DisplayResolutionSettings::ResetToLastSaved() {
    current_state_ = last_saved_state_;
    ClearDirty();
}

void DisplayResolutionSettings::SetToCurrentResolution() {
    ResolutionData current_res;
    current_res.is_current = true;
    SetCurrentState(current_res);
}

// ResolutionSettingsManager implementation
ResolutionSettingsManager::ResolutionSettingsManager() { InitializeDisplaySettings(); }

void ResolutionSettingsManager::InitializeDisplaySettings() {
    for (int i = 0; i < MAX_DISPLAYS; ++i) {
        std::string display_key = "Display_" + std::to_string(i);
        display_settings_[i] = std::make_unique<DisplayResolutionSettings>(display_key, i);
    }
}

void ResolutionSettingsManager::LoadAll() {
    LogInfo("ResolutionSettingsManager::LoadAll() - Starting to load settings from Reshade");

    // Load auto-apply setting
    bool loaded_auto_apply;
    if (reshade::get_config_value(nullptr, "DisplayCommander", "AutoApplyResolution", loaded_auto_apply)) {
        auto_apply_.store(loaded_auto_apply);
        LogInfo("ResolutionSettingsManager::LoadAll() - Loaded AutoApplyResolution: %s",
                loaded_auto_apply ? "true" : "false");
    } else {
        LogInfo("ResolutionSettingsManager::LoadAll() - AutoApplyResolution not found, using default: false");
    }

    // Load all display settings
    for (int i = 0; i < MAX_DISPLAYS; ++i) {
        if (display_settings_[i]) {
            LogInfo("ResolutionSettingsManager::LoadAll() - Loading settings for display %d", i);
            display_settings_[i]->Load();
        } else {
            LogError("ResolutionSettingsManager::LoadAll() - Failed to load display settings for display %d", i);
        }
    }

    LogInfo("ResolutionSettingsManager::LoadAll() - Finished loading settings from Reshade");
}

void ResolutionSettingsManager::SaveAll() {
    // Save auto-apply setting
    reshade::set_config_value(nullptr, "DisplayCommander", "AutoApplyResolution", auto_apply_.load());

    // Save all display settings
    for (auto &settings : display_settings_) {
        if (settings) {
            settings->Save();
        } else {
            LogError("Failed to save display settings for display ");
        }
    }
}

DisplayResolutionSettings &ResolutionSettingsManager::GetDisplaySettings(int display_index) {
    if (display_index < 0 || display_index >= MAX_DISPLAYS) {
        display_index = 0;
    }
    return *display_settings_[display_index];
}

const DisplayResolutionSettings &ResolutionSettingsManager::GetDisplaySettings(int display_index) const {
    if (display_index < 0 || display_index >= MAX_DISPLAYS) {
        display_index = 0;
    }
    return *display_settings_[display_index];
}

bool ResolutionSettingsManager::HasAnyDirty() const {
    for (const auto &settings : display_settings_) {
        if (settings && settings->IsDirty()) {
            return true;
        }
    }
    return false;
}

void ResolutionSettingsManager::SaveAllDirty() {
    for (auto &settings : display_settings_) {
        if (settings && settings->IsDirty()) {
            settings->SaveCurrentState();
            settings->Save();
        }
    }
}

void ResolutionSettingsManager::ResetAllDirty() {
    for (auto &settings : display_settings_) {
        if (settings && settings->IsDirty()) {
            settings->ResetToLastSaved();
        }
    }
}

void ResolutionSettingsManager::SetAutoApply(bool enabled) {
    auto_apply_.store(enabled);
    // Save to Reshade settings immediately
    reshade::set_config_value(nullptr, "DisplayCommander", "AutoApplyResolution", enabled);
    LogInfo("ResolutionSettingsManager::SetAutoApply() - Saved AutoApplyResolution=%s to Reshade settings",
            enabled ? "true" : "false");
}

// Global functions
void InitializeResolutionSettings() {
    if (!g_resolution_settings) {
        g_resolution_settings = std::make_unique<ResolutionSettingsManager>();
        g_resolution_settings->LoadAll();
    }
}

void CleanupResolutionSettings() {
    if (g_resolution_settings) {
        g_resolution_settings->SaveAll();
        g_resolution_settings.reset();
        g_resolution_settings = nullptr;
    }
}

} // namespace display_commander::widgets::resolution_widget
