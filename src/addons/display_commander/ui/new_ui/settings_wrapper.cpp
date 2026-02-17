#include "settings_wrapper.hpp"
#include "../../config/display_commander_config.hpp"
#include "../../globals.hpp"
#include "../../performance_types.hpp"

#include <reshade_imgui.hpp>
#include "../../res/forkawesome.h"

#include <algorithm>
#include <cmath>
#include "../../utils/logging.hpp"

// Windows defines min/max as macros, so we need to undefine them
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

namespace ui::new_ui {

// SettingBase implementation
SettingBase::SettingBase(const std::string& key, const std::string& section) : key_(key), section_(section) {}

// FloatSetting implementation
FloatSetting::FloatSetting(const std::string& key, float default_value, float min, float max,
                           const std::string& section)
    : SettingBase(key, section), value_(default_value), default_value_(default_value), min_(min), max_(max) {}

void FloatSetting::Load() {
    float loaded_value;
    if (display_commander::config::get_config_value(section_.c_str(), key_.c_str(), loaded_value)) {
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
    display_commander::config::set_config_value(section_.c_str(), key_.c_str(), value_.load());
}

std::string FloatSetting::GetValueAsString() const { return std::to_string(value_.load()); }

void FloatSetting::SetValue(float value) {
    const float clamped_value = std::max(min_, std::min(max_, value));
    value_.store(clamped_value);
    Save();  // Auto-save when value changes
    std::string reason = "setting changed: " + (section_ != DEFAULT_SECTION ? section_ + "." : "") + key_;
    display_commander::config::save_config(reason.c_str());  // Write to disk
}

// IntSetting implementation
IntSetting::IntSetting(const std::string& key, int default_value, int min, int max, const std::string& section)
    : SettingBase(key, section), value_(default_value), default_value_(default_value), min_(min), max_(max) {}

void IntSetting::Load() {
    int loaded_value;
    if (display_commander::config::get_config_value(section_.c_str(), key_.c_str(), loaded_value)) {
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

void IntSetting::Save() { display_commander::config::set_config_value(section_.c_str(), key_.c_str(), value_.load()); }

std::string IntSetting::GetValueAsString() const { return std::to_string(value_.load()); }

void IntSetting::SetValue(int value) {
    const int clamped_value = std::max(min_, std::min(max_, value));
    value_.store(clamped_value);
    Save();  // Auto-save when value changes
    std::string reason = "setting changed: " + (section_ != DEFAULT_SECTION ? section_ + "." : "") + key_;
    display_commander::config::save_config(reason.c_str());  // Write to disk
}

// BoolSetting implementation
BoolSetting::BoolSetting(const std::string& key, bool default_value, const std::string& section)
    : SettingBase(key, section), value_(default_value), default_value_(default_value) {}

void BoolSetting::Load() {
    int loaded_value;
    if (display_commander::config::get_config_value(section_.c_str(), key_.c_str(), loaded_value)) {
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
    display_commander::config::set_config_value(section_.c_str(), key_.c_str(), value_.load() ? 1 : 0);
}

std::string BoolSetting::GetValueAsString() const { return value_.load() ? "1" : "0"; }

void BoolSetting::SetValue(bool value) {
    value_.store(value);
    Save();  // Auto-save when value changes
    std::string reason = "setting changed: " + (section_ != DEFAULT_SECTION ? section_ + "." : "") + key_;
    display_commander::config::save_config(reason.c_str());  // Write to disk
}

// BoolSettingRef implementation
BoolSettingRef::BoolSettingRef(const std::string& key, std::atomic<bool>& external_ref, bool default_value,
                               const std::string& section)
    : SettingBase(key, section), external_ref_(external_ref), default_value_(default_value) {}

void BoolSettingRef::Load() {
    int loaded_value;
    if (display_commander::config::get_config_value(section_.c_str(), key_.c_str(), loaded_value)) {
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
    display_commander::config::set_config_value(section_.c_str(), key_.c_str(), external_ref_.get().load() ? 1 : 0);
}

std::string BoolSettingRef::GetValueAsString() const { return external_ref_.get().load() ? "1" : "0"; }

void BoolSettingRef::SetValue(bool value) {
    external_ref_.get().store(value);
    Save();  // Auto-save when value changes
    std::string reason = "setting changed: " + (section_ != DEFAULT_SECTION ? section_ + "." : "") + key_;
    display_commander::config::save_config(reason.c_str());  // Write to disk
}

// FloatSettingRef implementation
FloatSettingRef::FloatSettingRef(const std::string& key, std::atomic<float>& external_ref, float default_value,
                                 float min, float max, const std::string& section)
    : SettingBase(key, section),
      external_ref_(external_ref),
      default_value_(default_value),
      min_(min),
      max_(max),
      dirty_value_(0.0f),
      has_dirty_value_(false) {}

void FloatSettingRef::Load() {
    float loaded_value;
    if (display_commander::config::get_config_value(section_.c_str(), key_.c_str(), loaded_value)) {
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
    display_commander::config::set_config_value(section_.c_str(), key_.c_str(), external_ref_.get().load());
}

std::string FloatSettingRef::GetValueAsString() const { return std::to_string(external_ref_.get().load()); }

void FloatSettingRef::SetValue(float value) {
    const float clamped_value = std::max(min_, std::min(max_, value));
    external_ref_.get().store(clamped_value);
    Save();  // Auto-save when value changes
    std::string reason = "setting changed: " + (section_ != DEFAULT_SECTION ? section_ + "." : "") + key_;
    display_commander::config::save_config(reason.c_str());  // Write to disk
}

// IntSettingRef implementation
IntSettingRef::IntSettingRef(const std::string& key, std::atomic<int>& external_ref, int default_value, int min,
                             int max, const std::string& section)
    : SettingBase(key, section),
      external_ref_(external_ref),
      default_value_(default_value),
      min_(min),
      max_(max),
      dirty_value_(0),
      has_dirty_value_(false) {}

void IntSettingRef::Load() {
    int loaded_value;
    if (display_commander::config::get_config_value(section_.c_str(), key_.c_str(), loaded_value)) {
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
    display_commander::config::set_config_value(section_.c_str(), key_.c_str(), external_ref_.get().load());
}

std::string IntSettingRef::GetValueAsString() const { return std::to_string(external_ref_.get().load()); }

void IntSettingRef::SetValue(int value) {
    const int clamped_value = std::max(min_, std::min(max_, value));
    external_ref_.get().store(clamped_value);
    Save();  // Auto-save when value changes
    std::string reason = "setting changed: " + (section_ != DEFAULT_SECTION ? section_ + "." : "") + key_;
    display_commander::config::save_config(reason.c_str());  // Write to disk
}

// ComboSetting implementation
ComboSetting::ComboSetting(const std::string& key, int default_value, const std::vector<const char*>& labels,
                           const std::string& section)
    : SettingBase(key, section), value_(default_value), default_value_(default_value), labels_(labels) {}

void ComboSetting::Load() {
    int loaded_value;
    if (display_commander::config::get_config_value(section_.c_str(), key_.c_str(), loaded_value)) {
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

void ComboSetting::Save() { display_commander::config::set_config_value(section_.c_str(), key_.c_str(), value_); }

std::string ComboSetting::GetValueAsString() const { return std::to_string(value_); }

void ComboSetting::SetValue(int value) {
    value_ = std::max(0, std::min(static_cast<int>(labels_.size()) - 1, value));
    Save();  // Auto-save when value changes
    std::string reason = "setting changed: " + (section_ != DEFAULT_SECTION ? section_ + "." : "") + key_;
    display_commander::config::save_config(reason.c_str());  // Write to disk
}

// ComboSettingRef implementation
ComboSettingRef::ComboSettingRef(const std::string& key, std::atomic<int>& external_ref, int default_value,
                                 const std::vector<const char*>& labels, const std::string& section)
    : SettingBase(key, section), external_ref_(external_ref), default_value_(default_value), labels_(labels) {}

void ComboSettingRef::Load() {
    int loaded_value;
    if (display_commander::config::get_config_value(section_.c_str(), key_.c_str(), loaded_value)) {
        // If loaded index is out of range (e.g., labels changed), fall back to default
        const int max_index = static_cast<int>(labels_.size()) - 1;
        if (loaded_value < 0 || loaded_value > max_index) {
            int safe_default = default_value_;
            if (safe_default < 0) safe_default = 0;
            if (safe_default > max_index) safe_default = std::max(0, max_index);
            external_ref_.get().store(safe_default);
            Save();
        } else {
            external_ref_.get().store(loaded_value);
        }
    } else {
        // Use default value if not found
        external_ref_.get().store(default_value_);
    }
}

void ComboSettingRef::Save() {
    display_commander::config::set_config_value(section_.c_str(), key_.c_str(), external_ref_.get().load());
}

std::string ComboSettingRef::GetValueAsString() const { return std::to_string(external_ref_.get().load()); }

void ComboSettingRef::SetValue(int value) {
    int clamped_value = std::max(0, std::min(static_cast<int>(labels_.size()) - 1, value));
    external_ref_.get().store(clamped_value);
    Save();  // Auto-save when value changes
    std::string reason = "setting changed: " + (section_ != DEFAULT_SECTION ? section_ + "." : "") + key_;
    display_commander::config::save_config(reason.c_str());  // Write to disk
}

// Helper functions for LogLevel index <-> enum mapping (declared early for specializations)
// Mapping: Index 0 -> Debug (4), Index 1 -> Info (3), Index 2 -> Warning (2), Index 3 -> Error (1)
namespace {
inline LogLevel LogLevelIndexToEnum(int index) {
    switch (index) {
        case 0:  return LogLevel::Debug;
        case 1:  return LogLevel::Info;
        case 2:  return LogLevel::Warning;
        case 3:  return LogLevel::Error;
        default: return LogLevel::Debug;  // Default to Debug for out-of-range
    }
}

inline int LogLevelEnumToIndex(LogLevel level) {
    switch (level) {
        case LogLevel::Debug:   return 0;
        case LogLevel::Info:    return 1;
        case LogLevel::Warning: return 2;
        case LogLevel::Error:   return 3;
        default:                return 0;  // Default to Debug index
    }
}
}  // anonymous namespace

// Forward declarations for LogLevel specializations
template <>
void ComboSettingEnumRef<LogLevel>::Load();
template <>
void ComboSettingEnumRef<LogLevel>::Save();
template <>
std::string ComboSettingEnumRef<LogLevel>::GetValueAsString() const;
template <>
int ComboSettingEnumRef<LogLevel>::GetValue() const;
template <>
void ComboSettingEnumRef<LogLevel>::SetValue(int value);

// ComboSettingEnumRef implementation
template <typename EnumType>
ComboSettingEnumRef<EnumType>::ComboSettingEnumRef(const std::string& key, std::atomic<EnumType>& external_ref,
                                                   int default_value, const std::vector<const char*>& labels,
                                                   const std::string& section)
    : SettingBase(key, section), external_ref_(external_ref), default_value_(default_value), labels_(labels) {}

template <typename EnumType>
void ComboSettingEnumRef<EnumType>::Load() {
    int loaded_value;
    if (display_commander::config::get_config_value(section_.c_str(), key_.c_str(), loaded_value)) {
        // If loaded index is out of range (e.g., labels changed), fall back to default
        const int max_index = static_cast<int>(labels_.size()) - 1;
        if (loaded_value < 0 || loaded_value > max_index) {
            int safe_default = default_value_;
            if (safe_default < 0) safe_default = 0;
            if (safe_default > max_index) safe_default = std::max(0, max_index);
            external_ref_.get().store(static_cast<EnumType>(safe_default));
            Save();
        } else {
            external_ref_.get().store(static_cast<EnumType>(loaded_value));
        }
    } else {
        // Use default value if not found
        external_ref_.get().store(static_cast<EnumType>(default_value_));
    }
}

template <typename EnumType>
void ComboSettingEnumRef<EnumType>::Save() {
    display_commander::config::set_config_value(section_.c_str(), key_.c_str(),
                                                static_cast<int>(external_ref_.get().load()));
}

template <typename EnumType>
std::string ComboSettingEnumRef<EnumType>::GetValueAsString() const {
    return std::to_string(static_cast<int>(external_ref_.get().load()));
}

template <typename EnumType>
int ComboSettingEnumRef<EnumType>::GetValue() const {
    return static_cast<int>(external_ref_.get().load());
}

template <typename EnumType>
void ComboSettingEnumRef<EnumType>::SetValue(int value) {
    int clamped_value = std::max(0, std::min(static_cast<int>(labels_.size()) - 1, value));
    external_ref_.get().store(static_cast<EnumType>(clamped_value));
    Save();  // Auto-save when value changes
    std::string reason = "setting changed: " + (section_ != DEFAULT_SECTION ? section_ + "." : "") + key_;
    display_commander::config::save_config(reason.c_str());  // Write to disk
}

// Explicit template specializations for LogLevel (must be before template instantiation)
template <>
void ComboSettingEnumRef<LogLevel>::Load() {
    int loaded_index;
    if (display_commander::config::get_config_value(section_.c_str(), key_.c_str(), loaded_index)) {
        // If loaded index is out of range, fall back to default
        const int max_index = static_cast<int>(labels_.size()) - 1;
        if (loaded_index < 0 || loaded_index > max_index) {
            int safe_default = default_value_;
            if (safe_default < 0) {
                safe_default = 0;
            }
            if (safe_default > max_index) {
                safe_default = std::max(0, max_index);
            }
            external_ref_.get().store(LogLevelIndexToEnum(safe_default));
            Save();
        } else {
            external_ref_.get().store(LogLevelIndexToEnum(loaded_index));
        }
    } else {
        // Use default value if not found
        external_ref_.get().store(LogLevelIndexToEnum(default_value_));
    }
}

template <>
void ComboSettingEnumRef<LogLevel>::Save() {
    int index = LogLevelEnumToIndex(external_ref_.get().load());
    display_commander::config::set_config_value(section_.c_str(), key_.c_str(), index);
}

template <>
std::string ComboSettingEnumRef<LogLevel>::GetValueAsString() const {
    return std::to_string(LogLevelEnumToIndex(external_ref_.get().load()));
}

template <>
int ComboSettingEnumRef<LogLevel>::GetValue() const {
    return LogLevelEnumToIndex(external_ref_.get().load());
}

template <>
void ComboSettingEnumRef<LogLevel>::SetValue(int value) {
    int clamped_value = std::max(0, std::min(static_cast<int>(labels_.size()) - 1, value));
    external_ref_.get().store(LogLevelIndexToEnum(clamped_value));
    Save();  // Auto-save when value changes
    std::string reason = "setting changed: " + (section_ != DEFAULT_SECTION ? section_ + "." : "") + key_;
    display_commander::config::save_config(reason.c_str());  // Write to disk
}

// ResolutionPairSetting implementation
ResolutionPairSetting::ResolutionPairSetting(const std::string& key, int default_width, int default_height,
                                             const std::string& section)
    : SettingBase(key, section),
      width_(default_width),
      height_(default_height),
      default_width_(default_width),
      default_height_(default_height) {}

void ResolutionPairSetting::Load() {
    // Load width
    std::string width_key = key_ + "_width";
    int loaded_width;
    if (display_commander::config::get_config_value(section_.c_str(), width_key.c_str(), loaded_width)) {
        width_ = loaded_width;
    } else {
        width_ = default_width_;
    }

    // Load height
    std::string height_key = key_ + "_height";
    int loaded_height;
    if (display_commander::config::get_config_value(section_.c_str(), height_key.c_str(), loaded_height)) {
        height_ = loaded_height;
    } else {
        height_ = default_height_;
    }
}

void ResolutionPairSetting::Save() {
    // Save width
    std::string width_key = key_ + "_width";
    display_commander::config::set_config_value(section_.c_str(), width_key.c_str(), width_);

    // Save height
    std::string height_key = key_ + "_height";
    display_commander::config::set_config_value(section_.c_str(), height_key.c_str(), height_);
}

std::string ResolutionPairSetting::GetValueAsString() const {
    return std::to_string(width_) + "x" + std::to_string(height_);
}

void ResolutionPairSetting::SetResolution(int width, int height) {
    width_ = width;
    height_ = height;
    Save();  // Auto-save when value changes
}

void ResolutionPairSetting::SetCurrentResolution() {
    // Set to 0,0 to indicate "current resolution"
    width_ = 0;
    height_ = 0;
    Save();
}

// RefreshRatePairSetting implementation
RefreshRatePairSetting::RefreshRatePairSetting(const std::string& key, int default_numerator, int default_denominator,
                                               const std::string& section)
    : SettingBase(key, section),
      numerator_(default_numerator),
      denominator_(default_denominator),
      default_numerator_(default_numerator),
      default_denominator_(default_denominator) {}

void RefreshRatePairSetting::Load() {
    // Load numerator
    std::string num_key = key_ + "_num";
    int loaded_numerator;
    if (display_commander::config::get_config_value(section_.c_str(), num_key.c_str(), loaded_numerator)) {
        numerator_ = loaded_numerator;
    } else {
        numerator_ = default_numerator_;
    }

    // Load denominator
    std::string denom_key = key_ + "_denum";
    int loaded_denominator;
    if (display_commander::config::get_config_value(section_.c_str(), denom_key.c_str(), loaded_denominator)) {
        denominator_ = loaded_denominator;
    } else {
        denominator_ = default_denominator_;
    }
}

void RefreshRatePairSetting::Save() {
    // Save numerator
    std::string num_key = key_ + "_num";
    display_commander::config::set_config_value(section_.c_str(), num_key.c_str(), numerator_);

    // Save denominator
    std::string denom_key = key_ + "_denum";
    display_commander::config::set_config_value(section_.c_str(), denom_key.c_str(), denominator_);
}

std::string RefreshRatePairSetting::GetValueAsString() const {
    return std::to_string(numerator_) + "/" + std::to_string(denominator_);
}

void RefreshRatePairSetting::SetRefreshRate(int numerator, int denominator) {
    numerator_ = numerator;
    denominator_ = denominator;
    Save();  // Auto-save when value changes
}

void RefreshRatePairSetting::SetCurrentRefreshRate() {
    // Set to 0,0 to indicate "current refresh rate"
    numerator_ = 0;
    denominator_ = 0;
    Save();
}

double RefreshRatePairSetting::GetHz() const {
    if (denominator_ == 0) {
        return 0.0;  // Current refresh rate
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

bool SliderFloatSettingRef(FloatSettingRef& setting, const char* label, const char* format) {
    float value = setting.GetValue();
    bool changed = ImGui::SliderFloat(label, &value, setting.GetMin(), setting.GetMax(), format);
    if (changed) {
        // Check if this is keyboard input or mouse input
        ImGuiIO& io = ImGui::GetIO();
        bool is_mouse_input = io.MouseDown[0] || io.MouseDown[1] || io.MouseDown[2];  // Any mouse button held

        if (is_mouse_input) {
            // Mouse input (slider dragging) - apply immediately
            setting.ClearDirtyValue();
            setting.SetValue(value);
        } else {
            // Keyboard input - store as dirty value for later application
            setting.SetDirtyValue(value);
        }
    }

    // Apply dirty value when keyboard editing is finished
    if (ImGui::IsItemDeactivatedAfterEdit() && setting.HasDirtyValue()) {
        setting.SetValue(setting.GetDirtyValue());
        setting.ClearDirtyValue();
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
        // TODO: consider doing it for other slider settings as well
        // Check if this is keyboard input or mouse input
        ImGuiIO& io = ImGui::GetIO();
        bool is_mouse_input = io.MouseDown[0] || io.MouseDown[1] || io.MouseDown[2];  // Any mouse button held

        if (is_mouse_input) {
            // Mouse input (slider dragging) - apply immediately
            setting.ClearDirtyValue();
            setting.SetValue(value);
        } else {
            // Keyboard input - store as dirty value for later application
            setting.SetDirtyValue(value);
        }
    }

    // Apply dirty value when keyboard editing is finished
    if (ImGui::IsItemDeactivatedAfterEdit() && setting.HasDirtyValue()) {
        setting.SetValue(setting.GetDirtyValue());
        setting.ClearDirtyValue();
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
    bool changed =
        ImGui::Combo(label, &value, setting.GetLabels().data(), static_cast<int>(setting.GetLabels().size()));
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
                const auto& labels = setting.GetLabels();
                const char* def_label = (def >= 0 && def < static_cast<int>(labels.size())) ? labels[def] : "Default";
                ImGui::SetTooltip("Reset to default (%s)", def_label);
            }
            ImGui::PopID();
        }
    }
    return changed;
}

bool ComboSettingRefWrapper(ComboSettingRef& setting, const char* label) {
    int value = setting.GetValue();
    bool changed =
        ImGui::Combo(label, &value, setting.GetLabels().data(), static_cast<int>(setting.GetLabels().size()));
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
                const auto& labels = setting.GetLabels();
                const char* def_label = (def >= 0 && def < static_cast<int>(labels.size())) ? labels[def] : "Default";
                ImGui::SetTooltip("Reset to default (%s)", def_label);
            }
            ImGui::PopID();
        }
    }
    return changed;
}

template <typename EnumType>
bool ComboSettingEnumRefWrapper(ComboSettingEnumRef<EnumType>& setting, const char* label) {
    int value = setting.GetValue();
    bool changed =
        ImGui::Combo(label, &value, setting.GetLabels().data(), static_cast<int>(setting.GetLabels().size()));
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
                const auto& labels = setting.GetLabels();
                const char* def_label = (def >= 0 && def < static_cast<int>(labels.size())) ? labels[def] : "Default";
                ImGui::SetTooltip("Reset to default (%s)", def_label);
            }
            ImGui::PopID();
        }
    }
    return changed;
}

bool ButtonSetting(const char* label, const ImVec2& size) { return ImGui::Button(label, size); }

void TextSetting(const char* text) { ImGui::Text("%s", text); }

// Explicit template instantiations for ScreensaverMode
template class ComboSettingEnumRef<ScreensaverMode>;
template bool ComboSettingEnumRefWrapper<ScreensaverMode>(ComboSettingEnumRef<ScreensaverMode>& setting,
                                                          const char* label);

// Explicit template instantiations for OnPresentReflexMode
template class ComboSettingEnumRef<OnPresentReflexMode>;
template bool ComboSettingEnumRefWrapper<OnPresentReflexMode>(ComboSettingEnumRef<OnPresentReflexMode>& setting,
                                                             const char* label);

// Explicit template instantiations for FrameTimeMode
template class ComboSettingEnumRef<FrameTimeMode>;
template bool ComboSettingEnumRefWrapper<FrameTimeMode>(ComboSettingEnumRef<FrameTimeMode>& setting, const char* label);

// Explicit template instantiations for ComboSettingEnumRef
template class ComboSettingEnumRef<WindowMode>;
template bool ComboSettingEnumRefWrapper<WindowMode>(ComboSettingEnumRef<WindowMode>& setting, const char* label);

// Explicit template instantiations for InputBlockingMode
template class ComboSettingEnumRef<InputBlockingMode>;
template bool ComboSettingEnumRefWrapper<InputBlockingMode>(ComboSettingEnumRef<InputBlockingMode>& setting,
                                                            const char* label);

// Note: LogLevel uses explicit specializations for methods, but we still need to instantiate
// the class template itself (for constructor and other non-specialized members)
template class ComboSettingEnumRef<LogLevel>;
template bool ComboSettingEnumRefWrapper<LogLevel>(ComboSettingEnumRef<LogLevel>& setting, const char* label);

void SeparatorSetting() { ImGui::Separator(); }

void SpacingSetting() { ImGui::Spacing(); }

void LoadTabSettings(const std::vector<SettingBase*>& settings) {
    for (auto* setting : settings) {
        if (setting != nullptr) {
            setting->Load();
        }
    }
}

// Smart logging function that only logs settings changed from default values
void LoadTabSettingsWithSmartLogging(const std::vector<SettingBase*>& settings, const std::string& tab_name) {
    std::vector<std::string> changed_settings;

    for (auto* setting : settings) {
        if (setting != nullptr) {
            // Store the original value before loading
            std::string original_value = setting->GetValueAsString();
            setting->Load();
            std::string loaded_value = setting->GetValueAsString();

            // Check if the value changed from default
            if (original_value != loaded_value) {
                changed_settings.push_back(setting->GetKey() + " " + original_value + "->" + loaded_value);
            }
        }
    }

    // Log only if there are changed settings
    if (!changed_settings.empty()) {
        LogInfo("%s settings loaded - %zu non-default values:", tab_name.c_str(), changed_settings.size());
        for (const auto& setting : changed_settings) {
            LogInfo("  %s", setting.c_str());
        }
    } else {
        LogInfo("%s settings loaded - all values at default", tab_name.c_str());
    }
}

// FixedIntArraySetting implementation
FixedIntArraySetting::FixedIntArraySetting(const std::string& key, size_t array_size, int default_value, int min,
                                           int max, const std::string& section)
    : SettingBase(key, section), array_size_(array_size), default_value_(default_value), min_(min), max_(max) {
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

        if (display_commander::config::get_config_value(section_.c_str(), element_key.c_str(), value)) {
            // Clamp value to min/max range
            value = std::max(min_, std::min(max_, value));
            values_[i]->store(value);
            LogInfo("FixedIntArraySetting::Load() - Loaded %s[%zu] = %d from config", key_.c_str(), i, value);
        } else {
            values_[i]->store(default_value_);
            LogInfo("FixedIntArraySetting::Load() - No config found for %s[%zu], using default %d", key_.c_str(), i,
                    default_value_);
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
        display_commander::config::set_config_value(section_.c_str(), element_key.c_str(), value);
        LogInfo("FixedIntArraySetting::Save() - Saved %s[%zu] = %d to config", key_.c_str(), i, value);
    }
    is_dirty_ = false;
}

std::string FixedIntArraySetting::GetValueAsString() const {
    std::string result = "[";
    for (size_t i = 0; i < array_size_; ++i) {
        if (i > 0) result += ",";
        result += std::to_string(values_[i]->load());
    }
    result += "]";
    return result;
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
    Save();  // Auto-save when value changes
    std::string reason = "setting changed: " + (section_ != DEFAULT_SECTION ? section_ + "." : "") + key_ + "["
                         + std::to_string(index) + "]";
    display_commander::config::save_config(reason.c_str());  // Write to disk
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
    Save();  // Auto-save when value changes, consistent with other setting types
}

// StringSetting implementation
StringSetting::StringSetting(const std::string& key, const std::string& default_value, const std::string& section)
    : SettingBase(key, section), value_(default_value), default_value_(default_value) {}

void StringSetting::Load() {
    char buffer[256] = {0};
    size_t buffer_size = sizeof(buffer);
    if (display_commander::config::get_config_value(section_.c_str(), key_.c_str(), buffer, &buffer_size)) {
        value_ = std::string(buffer);
    } else {
        // Use default value
        value_ = default_value_;
    }
    is_dirty_ = false;
}

void StringSetting::Save() {
    if (is_dirty_) {
        display_commander::config::set_config_value(section_.c_str(), key_.c_str(), value_.c_str());
        is_dirty_ = false;
    }
}

std::string StringSetting::GetValueAsString() const { return value_; }

void StringSetting::SetValue(const std::string& value) {
    if (value_ != value) {
        value_ = value;
        is_dirty_ = true;
        Save();  // Auto-save when value changes, consistent with other setting types
        std::string reason = "setting changed: " + (section_ != DEFAULT_SECTION ? section_ + "." : "") + key_;
        display_commander::config::save_config(reason.c_str());  // Write to disk
    }
}

}  // namespace ui::new_ui
