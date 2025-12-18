#pragma once

#include <atomic>
#include <functional>
#include <reshade_imgui.hpp>
#include <string>


namespace ui::new_ui {

// Constants
static constexpr auto DEFAULT_SECTION = "DisplayCommander";

// Base class for settings that automatically handle loading/saving
class SettingBase {
  public:
    SettingBase(const std::string &key, const std::string &section = DEFAULT_SECTION);
    virtual ~SettingBase() = default;

    // Load the setting value from Reshade config
    virtual void Load() = 0;

    // Save the setting value to Reshade config
    virtual void Save() = 0;

    // Get the setting key
    const std::string &GetKey() const { return key_; }

    // Get the setting section
    const std::string &GetSection() const { return section_; }

    // Dirty state tracking
    bool IsDirty() const { return is_dirty_; }
    void MarkClean() { is_dirty_ = false; }
    void MarkDirty() { is_dirty_ = true; }

    // Get the current value as a string for comparison
    virtual std::string GetValueAsString() const = 0;

  protected:
    std::string key_;
    std::string section_;
    bool is_dirty_ = false;
};

// Float setting wrapper
class FloatSetting : public SettingBase {
  public:
    FloatSetting(const std::string &key, float default_value, float min = 0.0f, float max = 100.0f,
                 const std::string &section = DEFAULT_SECTION);

    void Load() override;
    void Save() override;
    std::string GetValueAsString() const override;

    float GetValue() const { return value_.load(); }
    void SetValue(float value);
    float GetDefaultValue() const { return default_value_; }
    float GetMin() const { return min_; }
    float GetMax() const { return max_; }
    void SetMax(float new_max) { max_ = new_max; }

    // Direct access to the atomic value for performance-critical code
    std::atomic<float> &GetAtomic() { return value_; }
    const std::atomic<float> &GetAtomic() const { return value_; }

  private:
    std::atomic<float> value_;
    float default_value_;
    float min_;
    float max_;
};

// Integer setting wrapper
class IntSetting : public SettingBase {
  public:
    IntSetting(const std::string &key, int default_value, int min = 0, int max = 100,
               const std::string &section = DEFAULT_SECTION);

    void Load() override;
    void Save() override;
    std::string GetValueAsString() const override;

    int GetValue() const { return value_.load(); }
    void SetValue(int value);
    int GetDefaultValue() const { return default_value_; }
    int GetMin() const { return min_; }
    int GetMax() const { return max_; }

    // Direct access to the atomic value for performance-critical code
    std::atomic<int> &GetAtomic() { return value_; }
    const std::atomic<int> &GetAtomic() const { return value_; }

  private:
    std::atomic<int> value_;
    int default_value_;
    int min_;
    int max_;
};

// Boolean setting wrapper
class BoolSetting : public SettingBase {
  public:
    BoolSetting(const std::string &key, bool default_value, const std::string &section = DEFAULT_SECTION);

    void Load() override;
    void Save() override;
    std::string GetValueAsString() const override;

    bool GetValue() const { return value_.load(); }
    void SetValue(bool value);
    bool GetDefaultValue() const { return default_value_; }

    // Direct access to the atomic value for performance-critical code
    std::atomic<bool> &GetAtomic() { return value_; }
    const std::atomic<bool> &GetAtomic() const { return value_; }

  private:
    std::atomic<bool> value_;
    bool default_value_;
};

// Boolean setting wrapper that references an external atomic variable
class BoolSettingRef : public SettingBase {
  public:
    BoolSettingRef(const std::string &key, std::atomic<bool> &external_ref, bool default_value,
                   const std::string &section = DEFAULT_SECTION);

    void Load() override;
    void Save() override;
    std::string GetValueAsString() const override;

    bool GetValue() const { return external_ref_.get().load(); }
    void SetValue(bool value);
    bool GetDefaultValue() const { return default_value_; }

    // Direct access to the referenced atomic value for performance-critical code
    std::atomic<bool> &GetAtomic() { return external_ref_.get(); }
    const std::atomic<bool> &GetAtomic() const { return external_ref_.get(); }

  private:
    std::reference_wrapper<std::atomic<bool>> external_ref_;
    bool default_value_;
};

// Float setting wrapper that references an external atomic variable
class FloatSettingRef : public SettingBase {
  public:
    FloatSettingRef(const std::string &key, std::atomic<float> &external_ref, float default_value, float min = 0.0f,
                    float max = 100.0f, const std::string &section = DEFAULT_SECTION);

    void Load() override;
    void Save() override;
    std::string GetValueAsString() const override;

    float GetValue() const { return external_ref_.get().load(); }
    void SetValue(float value);
    float GetDefaultValue() const { return default_value_; }
    float GetMin() const { return min_; }
    float GetMax() const { return max_; }
    void SetMax(float new_max) { max_ = new_max; }

    // Direct access to the referenced atomic value for performance-critical code
    std::atomic<float> &GetAtomic() { return external_ref_.get(); }
    const std::atomic<float> &GetAtomic() const { return external_ref_.get(); }

    // Dirty value management for slider interactions
    void SetDirtyValue(float value) { dirty_value_ = value; has_dirty_value_ = true; }
    float GetDirtyValue() const { return dirty_value_; }
    bool HasDirtyValue() const { return has_dirty_value_; }
    void ClearDirtyValue() { has_dirty_value_ = false; }

  private:
    std::reference_wrapper<std::atomic<float>> external_ref_;
    float default_value_;
    float min_;
    float max_;
    float dirty_value_;  // Stores intermediate value during slider interaction
    bool has_dirty_value_;  // Tracks whether dirty_value_ is valid
};

// Integer setting wrapper that references an external atomic variable
class IntSettingRef : public SettingBase {
  public:
    IntSettingRef(const std::string &key, std::atomic<int> &external_ref, int default_value, int min = 0, int max = 100,
                  const std::string &section = DEFAULT_SECTION);

    void Load() override;
    void Save() override;
    std::string GetValueAsString() const override;

    int GetValue() const { return external_ref_.get().load(); }
    void SetValue(int value);
    int GetDefaultValue() const { return default_value_; }
    int GetMin() const { return min_; }
    int GetMax() const { return max_; }
    void SetMax(int new_max) { max_ = new_max; }

    // Direct access to the referenced atomic value for performance-critical code
    std::atomic<int> &GetAtomic() { return external_ref_.get(); }
    const std::atomic<int> &GetAtomic() const { return external_ref_.get(); }

    // Dirty value management for slider interactions
    void SetDirtyValue(int value) { dirty_value_ = value; has_dirty_value_ = true; }
    int GetDirtyValue() const { return dirty_value_; }
    bool HasDirtyValue() const { return has_dirty_value_; }
    void ClearDirtyValue() { has_dirty_value_ = false; }

  private:
    std::reference_wrapper<std::atomic<int>> external_ref_;
    int default_value_;
    int min_;
    int max_;
    int dirty_value_;  // Stores intermediate value during slider interaction
    bool has_dirty_value_;  // Tracks whether dirty_value_ is valid
};

// Combo setting wrapper
class ComboSetting : public SettingBase {
  public:
    ComboSetting(const std::string &key, int default_value, const std::vector<const char *> &labels,
                 const std::string &section = DEFAULT_SECTION);

    void Load() override;
    void Save() override;
    std::string GetValueAsString() const override;

    int GetValue() const { return value_; }
    void SetValue(int value);
    int GetDefaultValue() const { return default_value_; }
    const std::vector<const char *> &GetLabels() const { return labels_; }

  private:
    int value_;
    int default_value_;
    std::vector<const char *> labels_;
};

// Combo setting wrapper that references an external atomic variable
class ComboSettingRef : public SettingBase {
  public:
    ComboSettingRef(const std::string &key, std::atomic<int> &external_ref, int default_value,
                    const std::vector<const char *> &labels, const std::string &section = DEFAULT_SECTION);

    void Load() override;
    void Save() override;
    std::string GetValueAsString() const override;

    int GetValue() const { return external_ref_.get().load(); }
    void SetValue(int value);
    int GetDefaultValue() const { return default_value_; }
    const std::vector<const char *> &GetLabels() const { return labels_; }

    // Direct access to the referenced atomic value for performance-critical code
    std::atomic<int> &GetAtomic() { return external_ref_.get(); }
    const std::atomic<int> &GetAtomic() const { return external_ref_.get(); }

  private:
    std::reference_wrapper<std::atomic<int>> external_ref_;
    int default_value_;
    std::vector<const char *> labels_;
};

// Combo setting wrapper that references an external atomic enum variable
template <typename EnumType> class ComboSettingEnumRef : public SettingBase {
  public:
    ComboSettingEnumRef(const std::string &key, std::atomic<EnumType> &external_ref, int default_value,
                        const std::vector<const char *> &labels, const std::string &section = DEFAULT_SECTION);

    void Load() override;
    void Save() override;
    std::string GetValueAsString() const override;

    int GetValue() const;
    void SetValue(int value);
    int GetDefaultValue() const { return default_value_; }
    const std::vector<const char *> &GetLabels() const { return labels_; }

    // Direct access to the referenced atomic value for performance-critical code
    std::atomic<EnumType> &GetAtomic() { return external_ref_.get(); }
    const std::atomic<EnumType> &GetAtomic() const { return external_ref_.get(); }

  private:
    std::reference_wrapper<std::atomic<EnumType>> external_ref_;
    int default_value_;
    std::vector<const char *> labels_;
};

// Resolution pair setting (width, height)
class ResolutionPairSetting : public SettingBase {
  public:
    ResolutionPairSetting(const std::string &key, int default_width, int default_height,
                          const std::string &section = DEFAULT_SECTION);

    void Load() override;
    void Save() override;
    std::string GetValueAsString() const override;

    int GetWidth() const { return width_; }
    int GetHeight() const { return height_; }
    void SetResolution(int width, int height);
    void SetCurrentResolution(); // Sets to current monitor resolution (0,0 means current)

    int GetDefaultWidth() const { return default_width_; }
    int GetDefaultHeight() const { return default_height_; }

  private:
    int width_;
    int height_;
    int default_width_;
    int default_height_;
};

// Refresh rate pair setting (numerator, denominator)
class RefreshRatePairSetting : public SettingBase {
  public:
    RefreshRatePairSetting(const std::string &key, int default_numerator, int default_denominator,
                           const std::string &section = DEFAULT_SECTION);

    void Load() override;
    void Save() override;
    std::string GetValueAsString() const override;

    int GetNumerator() const { return numerator_; }
    int GetDenominator() const { return denominator_; }
    void SetRefreshRate(int numerator, int denominator);
    void SetCurrentRefreshRate(); // Sets to current monitor refresh rate (0,0 means current)

    int GetDefaultNumerator() const { return default_numerator_; }
    int GetDefaultDenominator() const { return default_denominator_; }

    // Helper to get refresh rate as Hz
    double GetHz() const;

  private:
    int numerator_;
    int denominator_;
    int default_numerator_;
    int default_denominator_;
};

// Fixed-size integer array setting with atomic values
class FixedIntArraySetting : public SettingBase {
  public:
    FixedIntArraySetting(const std::string &key, size_t array_size, int default_value, int min = 0, int max = 100,
                         const std::string &section = DEFAULT_SECTION);
    ~FixedIntArraySetting();

    void Load() override;
    void Save() override;
    std::string GetValueAsString() const override;

    // Get value at index
    int GetValue(size_t index) const;
    void SetValue(size_t index, int value);

    // Get all values as a vector
    std::vector<int> GetAllValues() const;
    void SetAllValues(const std::vector<int> &values);

    // Array access
    int operator[](size_t index) const { return GetValue(index); }

    // Get array size
    size_t GetSize() const { return array_size_; }

    // Get default value
    int GetDefaultValue() const { return default_value_; }

    // Get min/max values
    int GetMin() const { return min_; }
    int GetMax() const { return max_; }

    // Direct access to atomic array for performance-critical code
    std::atomic<int> &GetAtomic(size_t index) { return *values_[index]; }
    const std::atomic<int> &GetAtomic(size_t index) const { return *values_[index]; }

  private:
    std::vector<std::atomic<int> *> values_;
    size_t array_size_;
    int default_value_;
    int min_;
    int max_;
};

// String setting wrapper
class StringSetting : public SettingBase {
  public:
    StringSetting(const std::string &key, const std::string &default_value,
                  const std::string &section = DEFAULT_SECTION);

    void Load() override;
    void Save() override;
    std::string GetValueAsString() const override;

    // Get/set values
    const std::string &GetValue() const { return value_; }
    void SetValue(const std::string &value);

  private:
    std::string value_;
    std::string default_value_;
};

// Wrapper functions for ImGui controls that automatically handle settings

// SliderFloat wrapper
bool SliderFloatSetting(FloatSetting &setting, const char *label, const char *format = "%.3f");

// SliderFloat wrapper for FloatSettingRef
bool SliderFloatSettingRef(FloatSettingRef &setting, const char *label, const char *format = "%.3f");

// SliderInt wrapper
bool SliderIntSetting(IntSetting &setting, const char *label, const char *format = "%d");

// SliderInt wrapper for IntSettingRef
bool SliderIntSetting(IntSettingRef &setting, const char *label, const char *format = "%d");

// Checkbox wrapper
bool CheckboxSetting(BoolSetting &setting, const char *label);

// Checkbox wrapper for BoolSettingRef
bool CheckboxSetting(BoolSettingRef &setting, const char *label);

// Combo wrapper
bool ComboSettingWrapper(ComboSetting &setting, const char *label);
bool ComboSettingRefWrapper(ComboSettingRef &setting, const char *label);
template <typename EnumType> bool ComboSettingEnumRefWrapper(ComboSettingEnumRef<EnumType> &setting, const char *label);

// Button wrapper (for settings that don't store values)
bool ButtonSetting(const char *label, const ImVec2 &size = ImVec2(0, 0));

// Text wrapper
void TextSetting(const char *text);

// Separator wrapper
void SeparatorSetting();

// Spacing wrapper
void SpacingSetting();

// Utility function to load all settings for a tab
void LoadTabSettings(const std::vector<SettingBase *> &settings);

// Smart logging function that only logs settings changed from default values
void LoadTabSettingsWithSmartLogging(const std::vector<SettingBase *> &settings, const std::string& tab_name);

} // namespace ui::new_ui
