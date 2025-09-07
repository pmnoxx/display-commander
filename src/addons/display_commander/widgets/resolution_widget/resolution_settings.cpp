#include "resolution_settings.hpp"
#include <deps/imgui/imgui.h>
#include <include/reshade.hpp>

namespace display_commander::widgets::resolution_widget {

// Global instance
std::unique_ptr<ResolutionSettingsManager> g_resolution_settings = nullptr;

// DisplayResolutionSettings implementation
DisplayResolutionSettings::DisplayResolutionSettings(const std::string& display_key, int display_index)
    : display_key_(display_key), display_index_(display_index) {
    // Initialize with current resolution as default
    current_state_.is_current = true;
    last_saved_state_.is_current = true;
}

void DisplayResolutionSettings::Load() {
    // Load width
    std::string width_key = display_key_ + "_width";
    int loaded_width;
    if (reshade::get_config_value(nullptr, "ResolutionWidget", width_key.c_str(), loaded_width)) {
        last_saved_state_.width = loaded_width;
    }

    // Load height
    std::string height_key = display_key_ + "_height";
    int loaded_height;
    if (reshade::get_config_value(nullptr, "ResolutionWidget", height_key.c_str(), loaded_height)) {
        last_saved_state_.height = loaded_height;
    }

    // Load refresh numerator
    std::string refresh_num_key = display_key_ + "_refresh_num";
    int loaded_refresh_num;
    if (reshade::get_config_value(nullptr, "ResolutionWidget", refresh_num_key.c_str(), loaded_refresh_num)) {
        last_saved_state_.refresh_numerator = loaded_refresh_num;
    }

    // Load refresh denominator
    std::string refresh_denom_key = display_key_ + "_refresh_denom";
    int loaded_refresh_denom;
    if (reshade::get_config_value(nullptr, "ResolutionWidget", refresh_denom_key.c_str(), loaded_refresh_denom)) {
        last_saved_state_.refresh_denominator = loaded_refresh_denom;
    }

    // Load is_current flag
    std::string current_key = display_key_ + "_is_current";
    bool loaded_is_current;
    if (reshade::get_config_value(nullptr, "ResolutionWidget", current_key.c_str(), loaded_is_current)) {
        last_saved_state_.is_current = loaded_is_current;
    }

    // Set current state to match last saved
    current_state_ = last_saved_state_;
    ClearDirty();
}

void DisplayResolutionSettings::Save() {
    // Save width
    std::string width_key = display_key_ + "_width";
    reshade::set_config_value(nullptr, "ResolutionWidget", width_key.c_str(), last_saved_state_.width);

    // Save height
    std::string height_key = display_key_ + "_height";
    reshade::set_config_value(nullptr, "ResolutionWidget", height_key.c_str(), last_saved_state_.height);

    // Save refresh numerator
    std::string refresh_num_key = display_key_ + "_refresh_num";
    reshade::set_config_value(nullptr, "ResolutionWidget", refresh_num_key.c_str(), last_saved_state_.refresh_numerator);

    // Save refresh denominator
    std::string refresh_denom_key = display_key_ + "_refresh_denom";
    reshade::set_config_value(nullptr, "ResolutionWidget", refresh_denom_key.c_str(), last_saved_state_.refresh_denominator);

    // Save is_current flag
    std::string current_key = display_key_ + "_is_current";
    reshade::set_config_value(nullptr, "ResolutionWidget", current_key.c_str(), last_saved_state_.is_current);
}

void DisplayResolutionSettings::SetCurrentState(const ResolutionData& data) {
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
ResolutionSettingsManager::ResolutionSettingsManager() {
    InitializeDisplaySettings();
}

void ResolutionSettingsManager::InitializeDisplaySettings() {
    for (int i = 0; i < MAX_DISPLAYS; ++i) {
        std::string display_key = "Display_" + std::to_string(i);
        display_settings_[i] = std::make_unique<DisplayResolutionSettings>(display_key, i);
    }
}

void ResolutionSettingsManager::LoadAll() {
    // Load auto-apply setting
    bool loaded_auto_apply;
    if (reshade::get_config_value(nullptr, "ResolutionWidget", "AutoApply", loaded_auto_apply)) {
        auto_apply_.store(loaded_auto_apply);
    }

    // Load all display settings
    for (auto& settings : display_settings_) {
        if (settings) {
            settings->Load();
        }
    }
}

void ResolutionSettingsManager::SaveAll() {
    // Save auto-apply setting
    reshade::set_config_value(nullptr, "ResolutionWidget", "AutoApply", auto_apply_.load());

    // Save all display settings
    for (auto& settings : display_settings_) {
        if (settings) {
            settings->Save();
        }
    }
}

DisplayResolutionSettings& ResolutionSettingsManager::GetDisplaySettings(int display_index) {
    if (display_index < 0 || display_index >= MAX_DISPLAYS) {
        display_index = 0;
    }
    return *display_settings_[display_index];
}

const DisplayResolutionSettings& ResolutionSettingsManager::GetDisplaySettings(int display_index) const {
    if (display_index < 0 || display_index >= MAX_DISPLAYS) {
        display_index = 0;
    }
    return *display_settings_[display_index];
}

bool ResolutionSettingsManager::HasAnyDirty() const {
    for (const auto& settings : display_settings_) {
        if (settings && settings->IsDirty()) {
            return true;
        }
    }
    return false;
}

void ResolutionSettingsManager::SaveAllDirty() {
    for (auto& settings : display_settings_) {
        if (settings && settings->IsDirty()) {
            settings->SaveCurrentState();
            settings->Save();
        }
    }
}

void ResolutionSettingsManager::ResetAllDirty() {
    for (auto& settings : display_settings_) {
        if (settings && settings->IsDirty()) {
            settings->ResetToLastSaved();
        }
    }
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
    }
}

} // namespace display_commander::widgets::resolution_widget
