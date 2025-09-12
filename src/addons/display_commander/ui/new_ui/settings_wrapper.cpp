#include "settings_wrapper.hpp"
#include <algorithm>
#include <cmath>
#include "../../renodx/settings.hpp"
#include "../../utils.hpp"

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
            value_.store(safe_default);
            Save();
        } else {
            value_.store(loaded_value);
        }
    } else {
        // Use default value if not found
        const float safe_default = std::max(min_, std::min(max_, default_value_));
        value_.store(safe_default);
    }
}

void FloatSetting::Save() {
    reshade::set_config_value(nullptr, section_.c_str(), key_.c_str(), value_.load());
}

void FloatSetting::SetValue(float value) {
    const float clamped_value = std::max(min_, std::min(max_, value));
    value_.store(clamped_value);
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
            value_.store(safe_default);
            Save();
        } else {
            value_.store(loaded_value);
        }
    } else {
        // Use default value if not found
        const int safe_default = std::max(min_, std::min(max_, default_value_));
        value_.store(safe_default);
    }
}

void IntSetting::Save() {
    reshade::set_config_value(nullptr, section_.c_str(), key_.c_str(), value_.load());
}

void IntSetting::SetValue(int value) {
    const int clamped_value = std::max(min_, std::min(max_, value));
    value_.store(clamped_value);
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


// BoolSettingRef implementation
BoolSettingRef::BoolSettingRef(const std::string& key, std::atomic<bool>& external_ref, bool default_value, const std::string& section)
    : SettingBase(key, section), external_ref_(external_ref), default_value_(default_value) {
}

void BoolSettingRef::Load() {
    int loaded_value;
    if (reshade::get_config_value(nullptr, section_.c_str(), key_.c_str(), loaded_value)) {
        // Only accept strict 0/1, otherwise fall back to default
        if (loaded_value == 0 || loaded_value == 1) {
            external_ref_.get().store(loaded_value != 0);
        } else {
            external_ref_.get().store(default_value_);
            Save();
        }
    } else {
        // Use default value if not found
        external_ref_.get().store(default_value_);
    }
}

void BoolSettingRef::Save() {
    reshade::set_config_value(nullptr, section_.c_str(), key_.c_str(), external_ref_.get().load() ? 1 : 0);
}

void BoolSettingRef::SetValue(bool value) {
    external_ref_.get().store(value);
    Save(); // Auto-save when value changes
}

// FloatSettingRef implementation
FloatSettingRef::FloatSettingRef(const std::string& key, std::atomic<float>& external_ref, float default_value, float min, float max, const std::string& section)
    : SettingBase(key, section), external_ref_(external_ref), default_value_(default_value), min_(min), max_(max) {
}

void FloatSettingRef::Load() {
    float loaded_value;
    if (reshade::get_config_value(nullptr, section_.c_str(), key_.c_str(), loaded_value)) {
        // If loaded value is invalid (NaN/Inf or out of range), fall back to default
        if (!std::isfinite(loaded_value) || loaded_value < min_ || loaded_value > max_) {
            const float safe_default = std::max(min_, std::min(max_, default_value_));
            external_ref_.get().store(safe_default);
            Save();
        } else {
            external_ref_.get().store(loaded_value);
        }
    } else {
        // Use default value if not found
        const float safe_default = std::max(min_, std::min(max_, default_value_));
        external_ref_.get().store(safe_default);
    }
}

void FloatSettingRef::Save() {
    reshade::set_config_value(nullptr, section_.c_str(), key_.c_str(), external_ref_.get().load());
}

void FloatSettingRef::SetValue(float value) {
    const float clamped_value = std::max(min_, std::min(max_, value));
    external_ref_.get().store(clamped_value);
    Save(); // Auto-save when value changes
}

// IntSettingRef implementation
IntSettingRef::IntSettingRef(const std::string& key, std::atomic<int>& external_ref, int default_value, int min, int max, const std::string& section)
    : SettingBase(key, section), external_ref_(external_ref), default_value_(default_value), min_(min), max_(max) {
}

void IntSettingRef::Load() {
    int loaded_value;
    if (reshade::get_config_value(nullptr, section_.c_str(), key_.c_str(), loaded_value)) {
        // If loaded value is out of range, fall back to default
        if (loaded_value < min_ || loaded_value > max_) {
            const int safe_default = std::max(min_, std::min(max_, default_value_));
            external_ref_.get().store(safe_default);
            Save();
        } else {
            external_ref_.get().store(loaded_value);
        }
    } else {
        // Use default value if not found
        const int safe_default = std::max(min_, std::min(max_, default_value_));
        external_ref_.get().store(safe_default);
    }
}

void IntSettingRef::Save() {
    reshade::set_config_value(nullptr, section_.c_str(), key_.c_str(), external_ref_.get().load());
}

void IntSettingRef::SetValue(int value) {
    const int clamped_value = std::max(min_, std::min(max_, value));
    external_ref_.get().store(clamped_value);
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

// ResolutionPairSetting implementation
ResolutionPairSetting::ResolutionPairSetting(const std::string& key, int default_width, int default_height, const std::string& section)
    : SettingBase(key, section), width_(default_width), height_(default_height),
      default_width_(default_width), default_height_(default_height) {
}

void ResolutionPairSetting::Load() {
    // Load width
    std::string width_key = key_ + "_width";
    int loaded_width;
    if (reshade::get_config_value(nullptr, section_.c_str(), width_key.c_str(), loaded_width)) {
        width_ = loaded_width;
    } else {
        width_ = default_width_;
    }

    // Load height
    std::string height_key = key_ + "_height";
    int loaded_height;
    if (reshade::get_config_value(nullptr, section_.c_str(), height_key.c_str(), loaded_height)) {
        height_ = loaded_height;
    } else {
        height_ = default_height_;
    }
}

void ResolutionPairSetting::Save() {
    // Save width
    std::string width_key = key_ + "_width";
    reshade::set_config_value(nullptr, section_.c_str(), width_key.c_str(), width_);

    // Save height
    std::string height_key = key_ + "_height";
    reshade::set_config_value(nullptr, section_.c_str(), height_key.c_str(), height_);
}

void ResolutionPairSetting::SetResolution(int width, int height) {
    width_ = width;
    height_ = height;
    Save(); // Auto-save when value changes
}

void ResolutionPairSetting::SetCurrentResolution() {
    // Set to 0,0 to indicate "current resolution"
    width_ = 0;
    height_ = 0;
    Save();
}

// RefreshRatePairSetting implementation
RefreshRatePairSetting::RefreshRatePairSetting(const std::string& key, int default_numerator, int default_denominator, const std::string& section)
    : SettingBase(key, section), numerator_(default_numerator), denominator_(default_denominator),
      default_numerator_(default_numerator), default_denominator_(default_denominator) {
}

void RefreshRatePairSetting::Load() {
    // Load numerator
    std::string num_key = key_ + "_num";
    int loaded_numerator;
    if (reshade::get_config_value(nullptr, section_.c_str(), num_key.c_str(), loaded_numerator)) {
        numerator_ = loaded_numerator;
    } else {
        numerator_ = default_numerator_;
    }

    // Load denominator
    std::string denom_key = key_ + "_denum";
    int loaded_denominator;
    if (reshade::get_config_value(nullptr, section_.c_str(), denom_key.c_str(), loaded_denominator)) {
        denominator_ = loaded_denominator;
    } else {
        denominator_ = default_denominator_;
    }
}

void RefreshRatePairSetting::Save() {
    // Save numerator
    std::string num_key = key_ + "_num";
    reshade::set_config_value(nullptr, section_.c_str(), num_key.c_str(), numerator_);

    // Save denominator
    std::string denom_key = key_ + "_denum";
    reshade::set_config_value(nullptr, section_.c_str(), denom_key.c_str(), denominator_);
}

void RefreshRatePairSetting::SetRefreshRate(int numerator, int denominator) {
    numerator_ = numerator;
    denominator_ = denominator;
    Save(); // Auto-save when value changes
}

void RefreshRatePairSetting::SetCurrentRefreshRate() {
    // Set to 0,0 to indicate "current refresh rate"
    numerator_ = 0;
    denominator_ = 0;
    Save();
}

double RefreshRatePairSetting::GetHz() const {
    if (denominator_ == 0) {
        return 0.0; // Current refresh rate
    }
    return static_cast<double>(numerator_) / static_cast<double>(denominator_);
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

bool SliderFloatSetting(FloatSettingRef& setting, const char* label, const char* format) {
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

bool SliderIntSetting(IntSettingRef& setting, const char* label, const char* format) {
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

bool CheckboxSetting(BoolSettingRef& setting, const char* label) {
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

// FixedIntArraySetting implementation
FixedIntArraySetting::FixedIntArraySetting(const std::string& key, size_t array_size, int default_value,
                                         int min, int max, const std::string& section)
    : SettingBase(key, section)
    , array_size_(array_size)
    , default_value_(default_value)
    , min_(min)
    , max_(max)
{
    // Initialize the atomic array with default values
    values_.resize(array_size_);
    for (size_t i = 0; i < array_size_; ++i) {
        values_[i] = new std::atomic<int>(default_value_);
    }
}

FixedIntArraySetting::~FixedIntArraySetting() {
    // Clean up allocated atomic values
    for (auto* ptr : values_) {
        delete ptr;
    }
}

void FixedIntArraySetting::Load() {
    // Load each array element from ReShade config
    for (size_t i = 0; i < array_size_; ++i) {
        std::string element_key = key_ + "_" + std::to_string(i);
        int value = default_value_;

        if (reshade::get_config_value(nullptr, section_.c_str(), element_key.c_str(), value)) {
            // Clamp value to min/max range
            value = std::max(min_, std::min(max_, value));
            values_[i]->store(value);
            LogInfo("FixedIntArraySetting::Load() - Loaded %s[%zu] = %d from config", key_.c_str(), i, value);
        } else {
            values_[i]->store(default_value_);
            LogInfo("FixedIntArraySetting::Load() - No config found for %s[%zu], using default %d", key_.c_str(), i, default_value_);
        }
    }
    is_dirty_ = false;
}

void FixedIntArraySetting::Save() {
    if (!is_dirty_) return;

    // Save each array element to ReShade config
    for (size_t i = 0; i < array_size_; ++i) {
        std::string element_key = key_ + "_" + std::to_string(i);
        int value = values_[i]->load();
        reshade::set_config_value(nullptr, section_.c_str(), element_key.c_str(), value);
        LogInfo("FixedIntArraySetting::Save() - Saved %s[%zu] = %d to config", key_.c_str(), i, value);
    }
    is_dirty_ = false;
}

int FixedIntArraySetting::GetValue(size_t index) const {
    if (index >= array_size_) {
        return default_value_;
    }
    return values_[index]->load();
}

void FixedIntArraySetting::SetValue(size_t index, int value) {
    if (index >= array_size_) return;

    // Clamp value to min/max range
    value = std::max(min_, std::min(max_, value));
    values_[index]->store(value);
    is_dirty_ = true;
    Save(); // Auto-save when value changes
}

std::vector<int> FixedIntArraySetting::GetAllValues() const {
    std::vector<int> result;
    result.reserve(array_size_);
    for (size_t i = 0; i < array_size_; ++i) {
        result.push_back(values_[i]->load());
    }
    return result;
}

void FixedIntArraySetting::SetAllValues(const std::vector<int>& values) {
    size_t copy_size = std::min(values.size(), array_size_);
    for (size_t i = 0; i < copy_size; ++i) {
        int value = std::max(min_, std::min(max_, values[i]));
        values_[i]->store(value);
    }
    is_dirty_ = true;
}

// StringSetting implementation
StringSetting::StringSetting(const std::string& key, const std::string& default_value,
                             const std::string& section)
    : SettingBase(key, section), value_(default_value), default_value_(default_value) {
}

void StringSetting::Load() {
    char buffer[256] = {0};
    size_t buffer_size = sizeof(buffer);
    if (reshade::get_config_value(nullptr, section_.c_str(), key_.c_str(), buffer, &buffer_size)) {
        value_ = std::string(buffer);
    } else {
        // Use default value
        value_ = default_value_;
    }
    is_dirty_ = false;
}

void StringSetting::Save() {
    if (is_dirty_) {
        reshade::set_config_value(nullptr, section_.c_str(), key_.c_str(), value_.c_str());
        is_dirty_ = false;
    }
}

void StringSetting::SetValue(const std::string& value) {
    if (value_ != value) {
        value_ = value;
        is_dirty_ = true;
    }
}

} // namespace ui::new_ui
