#pragma once

#define ImTextureID ImU64

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include <deps/imgui/imgui.h>
#include <include/reshade.hpp>

// #include "./mutex.hpp"

#define ICON_FK_UNDO u8"\uf0e2"

namespace renodx::utils::settings2 {

// Keep minimal types used by legacy UI wrappers

enum class SettingValueType : uint8_t {
  FLOAT = 0,
  INTEGER = 1,
  BOOLEAN = 2,
  BUTTON = 3,
  LABEL = 4,
  BULLET = 5,
  TEXT = 6,
  TEXT_NOWRAP = 7,
  CUSTOM = 8,
};

struct Setting {
  std::string key;
  float* binding = nullptr;
  SettingValueType value_type = SettingValueType::FLOAT;
  float default_value = 0.f;
  bool can_reset = true;
  std::string label = key;
  std::string section;
  std::string group;
  std::string tooltip;
  std::vector<std::string> labels;
  std::optional<uint32_t> tint;  // HEX notation
  float min = 0.f;
  float max = 100.f;
  std::string format = "%.0f";

  std::function<bool()> is_enabled = [] { return true; };
  std::function<float(float value)> parse = [](float value) { return value; };
  std::function<void()> on_change = [] {};
  std::function<void(float previous, float current)> on_change_value = [](float, float) {};
  std::function<bool()> on_click = [] { return true; };
  std::function<bool()> on_draw = [] { return false; };

  bool is_global = false;
  std::function<bool()> is_visible = [] { return true; };
  bool is_sticky = false;

  float value = default_value;
  int value_as_int = static_cast<int>(default_value);

  [[nodiscard]] float GetMax() const {
    switch (this->value_type) {
      case SettingValueType::BOOLEAN:
        return 1.f;
      case SettingValueType::INTEGER:
        return this->labels.empty() ? this->max : (this->labels.size() - 1);
      case SettingValueType::FLOAT:
      default:
        return this->max;
    }
  }

  [[nodiscard]] float GetValue() const {
    switch (this->value_type) {
      default:
      case SettingValueType::FLOAT:
        return this->value;
      case SettingValueType::INTEGER:
        return static_cast<float>(this->value_as_int);
      case SettingValueType::BOOLEAN:
        return ((this->value_as_int == 0) ? 0.f : 1.f);
    }
  }

  Setting* Set(float v) {
    this->value = v;
    this->value_as_int = static_cast<int>(v);
    return this;
  }

  Setting* Write() {
    if (this->binding != nullptr) {
      *this->binding = this->parse(this->GetValue());
    }
    return this;
  }
};

using Settings = std::vector<Setting*>;

}  // namespace renodx::utils::settings2
