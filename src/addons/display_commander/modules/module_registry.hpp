// Source Code <Display Commander> // follow this order for includes in all files + add this comment at the top
#pragma once

// Source Code <Display Commander>
#include "../ui/imgui_wrapper_base.hpp"

// Libraries <ReShade> / <imgui>
#include <reshade.hpp>

// Libraries <standard C++>
#include <string>
#include <string_view>
#include <vector>

namespace modules {

class ModuleConfigApi {
   public:
    virtual ~ModuleConfigApi() = default;
    virtual bool GetBool(std::string_view key, bool default_value) = 0;
    virtual void SetBool(std::string_view key, bool value) = 0;
};

struct ModuleDescriptor {
    std::string id;
    std::string display_name;
    std::string description;
    bool enabled = false;
    bool show_in_overlay = false;
    bool has_tab = false;
    std::string tab_name;
    std::string tab_id;
    bool is_advanced_tab = true;
};

using ModuleLifecycleCallback = void (*)();
using ModuleInitializeCallback = void (*)(ModuleConfigApi* config_api);
using ModuleTickCallback = void (*)();
using ModuleDrawTabCallback = void (*)(display_commander::ui::IImGuiWrapper&, reshade::api::effect_runtime*);
using ModuleDrawOverlayCallback = void (*)(display_commander::ui::IImGuiWrapper&);
using ModuleDrawMainTabInlineCallback = void (*)(display_commander::ui::IImGuiWrapper&,
                                                 reshade::api::effect_runtime*);
using ModuleHotkeyCallback = void (*)();

struct ModuleHotkeySpec {
    std::string id;
    std::string display_name;
    std::string default_shortcut;
    std::string description;
    ModuleHotkeyCallback on_trigger_fn = nullptr;
};

struct RegisteredModuleHotkey {
    std::string module_id;
    std::string module_display_name;
    ModuleHotkeySpec spec;
};

struct ModuleRegistrationSpec {
    ModuleDescriptor descriptor;
    bool default_enabled = false;
    bool default_show_in_overlay = false;
    ModuleInitializeCallback initialize_fn = nullptr;
    ModuleTickCallback tick_fn = nullptr;
    ModuleDrawTabCallback draw_tab_fn = nullptr;
    ModuleDrawOverlayCallback draw_overlay_fn = nullptr;
    ModuleDrawMainTabInlineCallback draw_main_tab_inline_fn = nullptr;
    ModuleLifecycleCallback on_enabled_fn = nullptr;
    ModuleLifecycleCallback on_disabled_fn = nullptr;
    std::vector<ModuleHotkeySpec> hotkeys;
};

void InitializeModuleRegistry();
std::vector<ModuleDescriptor> GetModules();
bool IsModuleEnabled(std::string_view module_id);
bool SetModuleEnabled(std::string_view module_id, bool enabled);
bool IsModuleOverlayEnabled(std::string_view module_id);
bool SetModuleOverlayEnabled(std::string_view module_id, bool enabled);
bool IsModuleTabVisible(std::string_view tab_id);
void TickEnabledModules();
void DrawModuleTabById(std::string_view module_id, display_commander::ui::IImGuiWrapper& imgui,
                       reshade::api::effect_runtime* runtime);
void DrawEnabledModulesInOverlay(display_commander::ui::IImGuiWrapper& imgui);
void DrawModuleMainTabInlineById(std::string_view module_id, display_commander::ui::IImGuiWrapper& imgui,
                                 reshade::api::effect_runtime* runtime);
void DrawEnabledModulesMainTabInline(display_commander::ui::IImGuiWrapper& imgui,
                                     reshade::api::effect_runtime* runtime);
std::vector<RegisteredModuleHotkey> GetEnabledModuleHotkeys();

}  // namespace modules
