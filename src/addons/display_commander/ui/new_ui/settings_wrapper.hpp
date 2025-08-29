#pragma once

#include <deps/imgui/imgui.h>
#include <include/reshade.hpp>
#include <string>
#include <functional>
#include <atomic> // Added for std::atomic

namespace ui::new_ui {

// Constants
static constexpr auto DEFAULT_SECTION = "DisplayCommander";

// Base class for settings that automatically handle loading/saving
class SettingBase {
public:
    SettingBase(const std::string& key, const std::string& section = DEFAULT_SECTION);
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
                 const std::string& section = DEFAULT_SECTION);
    
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
               const std::string& section = DEFAULT_SECTION);
    
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
                const std::string& section = DEFAULT_SECTION);
    
    void Load() override;
    void Save() override;
    
    bool GetValue() const { return value_.load(); }
    void SetValue(bool value);
    bool GetDefaultValue() const { return default_value_; }
    
    // Direct access to the atomic value for performance-critical code
    std::atomic<bool>& GetAtomic() { return value_; }
    const std::atomic<bool>& GetAtomic() const { return value_; }

private:
    std::atomic<bool> value_;
    bool default_value_;
};

// Boolean setting wrapper that references an external atomic variable
class BoolSettingRef : public SettingBase {
public:
    BoolSettingRef(const std::string& key, std::atomic<bool>& external_ref, bool default_value, 
                   const std::string& section = DEFAULT_SECTION);
    
    void Load() override;
    void Save() override;
    
    bool GetValue() const { return external_ref_.get().load(); }
    void SetValue(bool value);
    bool GetDefaultValue() const { return default_value_; }
    
    // Direct access to the referenced atomic value for performance-critical code
    std::atomic<bool>& GetAtomic() { return external_ref_.get(); }
    const std::atomic<bool>& GetAtomic() const { return external_ref_.get(); }

private:
    std::reference_wrapper<std::atomic<bool>> external_ref_;
    bool default_value_;
};

// Float setting wrapper that references an external atomic variable
class FloatSettingRef : public SettingBase {
public:
    FloatSettingRef(const std::string& key, std::atomic<float>& external_ref, float default_value, 
                    float min = 0.0f, float max = 100.0f,
                    const std::string& section = DEFAULT_SECTION);
    
    void Load() override;
    void Save() override;
    
    float GetValue() const { return external_ref_.get().load(); }
    void SetValue(float value);
    float GetDefaultValue() const { return default_value_; }
    float GetMin() const { return min_; }
    float GetMax() const { return max_; }
    
    // Direct access to the referenced atomic value for performance-critical code
    std::atomic<float>& GetAtomic() { return external_ref_.get(); }
    const std::atomic<float>& GetAtomic() const { return external_ref_.get(); }

private:
    std::reference_wrapper<std::atomic<float>> external_ref_;
    float default_value_;
    float min_;
    float max_;
};

// Integer setting wrapper that references an external atomic variable
class IntSettingRef : public SettingBase {
public:
    IntSettingRef(const std::string& key, std::atomic<int>& external_ref, int default_value, 
                  int min = 0, int max = 100,
                  const std::string& section = DEFAULT_SECTION);
    
    void Load() override;
    void Save() override;
    
    int GetValue() const { return external_ref_.get().load(); }
    void SetValue(int value);
    int GetDefaultValue() const { return default_value_; }
    int GetMin() const { return min_; }
    int GetMax() const { return max_; }
    
    // Direct access to the referenced atomic value for performance-critical code
    std::atomic<int>& GetAtomic() { return external_ref_.get(); }
    const std::atomic<int>& GetAtomic() const { return external_ref_.get(); }

private:
    std::reference_wrapper<std::atomic<int>> external_ref_;
    int default_value_;
    int min_;
    int max_;
};

// Combo setting wrapper
class ComboSetting : public SettingBase {
public:
    ComboSetting(const std::string& key, int default_value, 
                 const std::vector<const char*>& labels,
                 const std::string& section = DEFAULT_SECTION);
    
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

// SliderFloat wrapper for FloatSettingRef
bool SliderFloatSetting(FloatSettingRef& setting, const char* label, const char* format = "%.3f");

// SliderInt wrapper
bool SliderIntSetting(IntSetting& setting, const char* label, const char* format = "%d");

// SliderInt wrapper for IntSettingRef
bool SliderIntSetting(IntSettingRef& setting, const char* label, const char* format = "%d");

// Checkbox wrapper
bool CheckboxSetting(BoolSetting& setting, const char* label);

// Checkbox wrapper for BoolSettingRef
bool CheckboxSetting(BoolSettingRef& setting, const char* label);

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

} // namespace ui::new_ui
