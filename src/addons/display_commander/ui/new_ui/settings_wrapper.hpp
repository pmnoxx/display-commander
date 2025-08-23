#pragma once

#include <deps/imgui/imgui.h>
#include <include/reshade.hpp>
#include <string>
#include <functional>

namespace renodx::ui::new_ui {

// Base class for settings that automatically handle loading/saving
class SettingBase {
public:
    SettingBase(const std::string& key, const std::string& section = "DisplayCommanderNew");
    virtual ~SettingBase() = default;
    
    // Load the setting value from Reshade config
    virtual void Load() = 0;
    
    // Save the setting value to Reshade config
    virtual void Save() = 0;
    
    // Get the setting key
    const std::string& GetKey() const { return key_; }
    
    // Get the setting section
    const std::string& GetSection() const { return section_; }

protected:
    std::string key_;
    std::string section_;
};

// Float setting wrapper
class FloatSetting : public SettingBase {
public:
    FloatSetting(const std::string& key, float default_value, float min = 0.0f, float max = 100.0f, 
                 const std::string& section = "DisplayCommanderNew");
    
    void Load() override;
    void Save() override;
    
    float GetValue() const { return value_; }
    void SetValue(float value);
    float GetDefaultValue() const { return default_value_; }
    float GetMin() const { return min_; }
    float GetMax() const { return max_; }

private:
    float value_;
    float default_value_;
    float min_;
    float max_;
};

// Integer setting wrapper
class IntSetting : public SettingBase {
public:
    IntSetting(const std::string& key, int default_value, int min = 0, int max = 100,
               const std::string& section = "DisplayCommanderNew");
    
    void Load() override;
    void Save() override;
    
    int GetValue() const { return value_; }
    void SetValue(int value);
    int GetDefaultValue() const { return default_value_; }
    int GetMin() const { return min_; }
    int GetMax() const { return max_; }

private:
    int value_;
    int default_value_;
    int min_;
    int max_;
};

// Boolean setting wrapper
class BoolSetting : public SettingBase {
public:
    BoolSetting(const std::string& key, bool default_value, 
                const std::string& section = "DisplayCommanderNew");
    
    void Load() override;
    void Save() override;
    
    bool GetValue() const { return value_; }
    void SetValue(bool value);
    bool GetDefaultValue() const { return default_value_; }

private:
    bool value_;
    bool default_value_;
};

// Combo setting wrapper
class ComboSetting : public SettingBase {
public:
    ComboSetting(const std::string& key, int default_value, 
                 const std::vector<const char*>& labels,
                 const std::string& section = "DisplayCommanderNew");
    
    void Load() override;
    void Save() override;
    
    int GetValue() const { return value_; }
    void SetValue(int value);
    int GetDefaultValue() const { return default_value_; }
    const std::vector<const char*>& GetLabels() const { return labels_; }

private:
    int value_;
    int default_value_;
    std::vector<const char*> labels_;
};

// Wrapper functions for ImGui controls that automatically handle settings

// SliderFloat wrapper
bool SliderFloatSetting(FloatSetting& setting, const char* label, const char* format = "%.3f");

// SliderInt wrapper
bool SliderIntSetting(IntSetting& setting, const char* label, const char* format = "%d");

// Checkbox wrapper
bool CheckboxSetting(BoolSetting& setting, const char* label);

// Combo wrapper
bool ComboSettingWrapper(ComboSetting& setting, const char* label);

// Button wrapper (for settings that don't store values)
bool ButtonSetting(const char* label, const ImVec2& size = ImVec2(0, 0));

// Text wrapper
void TextSetting(const char* text);

// Separator wrapper
void SeparatorSetting();

// Spacing wrapper
void SpacingSetting();

// Utility function to load all settings for a tab
void LoadTabSettings(const std::vector<SettingBase*>& settings);

} // namespace renodx::ui::new_ui
