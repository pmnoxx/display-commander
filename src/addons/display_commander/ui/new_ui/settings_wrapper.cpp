#include "settings_wrapper.hpp"
#include <algorithm>
#include <cmath>
#include "../../renodx/settings.hpp"

// Windows defines min/max as macros, so we need to undefine them
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

namespace ui::new_ui {

// SettingBase implementation
SettingBase::SettingBase(const std::string& key, const std::string& section)
    : key_(key), section_(section) {
}

// FloatSetting implementation
FloatSetting::FloatSetting(const std::string& key, float default_value, float min, float max, const std::string& section)
    : SettingBase(key, section), value_(default_value), default_value_(default_value), min_(min), max_(max) {
}

void FloatSetting::Load() {
    float loaded_value;
    if (reshade::get_config_value(nullptr, section_.c_str(), key_.c_str(), loaded_value)) {
        // If loaded value is invalid (NaN/Inf or out of range), fall back to default
        if (!std::isfinite(loaded_value) || loaded_value < min_ || loaded_value > max_) {
            const float safe_default = std::max(min_, std::min(max_, default_value_));
            value_ = safe_default;
            Save();
        } else {
            value_ = loaded_value;
        }
    } else {
        // Use default value if not found
        const float safe_default = std::max(min_, std::min(max_, default_value_));
        value_ = safe_default;
    }
}

void FloatSetting::Save() {
    reshade::set_config_value(nullptr, section_.c_str(), key_.c_str(), value_);
}

void FloatSetting::SetValue(float value) {
    value_ = std::max(min_, std::min(max_, value));
    Save(); // Auto-save when value changes
}

// IntSetting implementation
IntSetting::IntSetting(const std::string& key, int default_value, int min, int max, const std::string& section)
    : SettingBase(key, section), value_(default_value), default_value_(default_value), min_(min), max_(max) {
}

void IntSetting::Load() {
    int loaded_value;
    if (reshade::get_config_value(nullptr, section_.c_str(), key_.c_str(), loaded_value)) {
        // If loaded value is out of range, fall back to default
        if (loaded_value < min_ || loaded_value > max_) {
            const int safe_default = std::max(min_, std::min(max_, default_value_));
            value_ = safe_default;
            Save();
        } else {
            value_ = loaded_value;
        }
    } else {
        // Use default value if not found
        const int safe_default = std::max(min_, std::min(max_, default_value_));
        value_ = safe_default;
    }
}

void IntSetting::Save() {
    reshade::set_config_value(nullptr, section_.c_str(), key_.c_str(), value_);
}

void IntSetting::SetValue(int value) {
    value_ = std::max(min_, std::min(max_, value));
    Save(); // Auto-save when value changes
}

// BoolSetting implementation
BoolSetting::BoolSetting(const std::string& key, bool default_value, const std::string& section)
    : SettingBase(key, section), value_(default_value), default_value_(default_value) {
}

void BoolSetting::Load() {
    int loaded_value;
    if (reshade::get_config_value(nullptr, section_.c_str(), key_.c_str(), loaded_value)) {
        // Only accept strict 0/1, otherwise fall back to default
        if (loaded_value == 0 || loaded_value == 1) {
            value_.store(loaded_value != 0);
        } else {
            value_.store(default_value_);
            Save();
        }
    } else {
        // Use default value if not found
        value_.store(default_value_);
    }
}

void BoolSetting::Save() {
    reshade::set_config_value(nullptr, section_.c_str(), key_.c_str(), value_.load() ? 1 : 0);
}

void BoolSetting::SetValue(bool value) {
    value_.store(value);
    Save(); // Auto-save when value changes
}

// ComboSetting implementation
ComboSetting::ComboSetting(const std::string& key, int default_value, const std::vector<const char*>& labels, const std::string& section)
    : SettingBase(key, section), value_(default_value), default_value_(default_value), labels_(labels) {
}

void ComboSetting::Load() {
    int loaded_value;
    if (reshade::get_config_value(nullptr, section_.c_str(), key_.c_str(), loaded_value)) {
        // If loaded index is out of range (e.g., labels changed), fall back to default
        const int max_index = static_cast<int>(labels_.size()) - 1;
        if (loaded_value < 0 || loaded_value > max_index) {
            int safe_default = default_value_;
            if (safe_default < 0) safe_default = 0;
            if (safe_default > max_index) safe_default = std::max(0, max_index);
            value_ = safe_default;
            Save();
        } else {
            value_ = loaded_value;
        }
    } else {
        // Use default value if not found
        value_ = default_value_;
    }
}

void ComboSetting::Save() {
    reshade::set_config_value(nullptr, section_.c_str(), key_.c_str(), value_);
}

void ComboSetting::SetValue(int value) {
    value_ = std::max(0, std::min(static_cast<int>(labels_.size()) - 1, value));
    Save(); // Auto-save when value changes
}

// Wrapper function implementations

bool SliderFloatSetting(FloatSetting& setting, const char* label, const char* format) {
    float value = setting.GetValue();
    bool changed = ImGui::SliderFloat(label, &value, setting.GetMin(), setting.GetMax(), format);
    if (changed) {
        setting.SetValue(value);
    }
    // Show reset-to-default button if value differs from default
    {
        float current = setting.GetValue();
        float def = setting.GetDefaultValue();
        if (fabsf(current - def) > 1e-6f) {
            ImGui::SameLine();
            ImGui::PushID(&setting);
            if (ImGui::SmallButton(reinterpret_cast<const char*>(ICON_FK_UNDO))) {
                setting.SetValue(def);
                changed = true;
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Reset to default (%.3f)", def);
            }
            ImGui::PopID();
        }
    }
    return changed;
}

bool SliderIntSetting(IntSetting& setting, const char* label, const char* format) {
    int value = setting.GetValue();
    bool changed = ImGui::SliderInt(label, &value, setting.GetMin(), setting.GetMax(), format);
    if (changed) {
        setting.SetValue(value);
    }
    // Show reset-to-default button if value differs from default
    {
        int current = setting.GetValue();
        int def = setting.GetDefaultValue();
        if (current != def) {
            ImGui::SameLine();
            ImGui::PushID(&setting);
            if (ImGui::SmallButton(reinterpret_cast<const char*>(ICON_FK_UNDO))) {
                setting.SetValue(def);
                changed = true;
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Reset to default (%d)", def);
            }
            ImGui::PopID();
        }
    }
    return changed;
}

bool CheckboxSetting(BoolSetting& setting, const char* label) {
    bool value = setting.GetValue();
    bool changed = ImGui::Checkbox(label, &value);
    if (changed) {
        setting.SetValue(value);
    }
    // Show reset-to-default button if value differs from default
    {
        bool current = setting.GetValue();
        bool def = setting.GetDefaultValue();
        if (current != def) {
            ImGui::SameLine();
            ImGui::PushID(&setting);
            if (ImGui::SmallButton(reinterpret_cast<const char*>(ICON_FK_UNDO))) {
                setting.SetValue(def);
                changed = true;
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Reset to default (%s)", def ? "On" : "Off");
            }
            ImGui::PopID();
        }
    }
    return changed;
}

bool ComboSettingWrapper(ComboSetting& setting, const char* label) {
    int value = setting.GetValue();
    bool changed = ImGui::Combo(label, &value, setting.GetLabels().data(), static_cast<int>(setting.GetLabels().size()));
    if (changed) {
        setting.SetValue(value);
    }
    // Show reset-to-default button if value differs from default
    {
        int current = setting.GetValue();
        int def = setting.GetDefaultValue();
        if (current != def) {
            ImGui::SameLine();
            ImGui::PushID(&setting);
            if (ImGui::SmallButton(reinterpret_cast<const char*>(ICON_FK_UNDO))) {
                setting.SetValue(def);
                changed = true;
            }
            if (ImGui::IsItemHovered()) {
                const auto &labels = setting.GetLabels();
                const char* def_label = (def >= 0 && def < static_cast<int>(labels.size())) ? labels[def] : "Default";
                ImGui::SetTooltip("Reset to default (%s)", def_label);
            }
            ImGui::PopID();
        }
    }
    return changed;
}

bool ButtonSetting(const char* label, const ImVec2& size) {
    return ImGui::Button(label, size);
}

void TextSetting(const char* text) {
    ImGui::Text("%s", text);
}

void SeparatorSetting() {
    ImGui::Separator();
}

void SpacingSetting() {
    ImGui::Spacing();
}

void LoadTabSettings(const std::vector<SettingBase*>& settings) {
    for (auto* setting : settings) {
        if (setting != nullptr) {
            setting->Load();
        }
    }
}

} // namespace ui::new_ui
