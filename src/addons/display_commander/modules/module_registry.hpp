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

// Libraries <Windows.h>
#include <Windows.h>

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
    /** When false, the module's top-level overlay tab is hidden (module may still run if enabled). */
    bool show_tab = true;
    bool has_tab = false;
    /** True when the module registers overlay UI (`draw_overlay_fn`). */
    bool has_overlay = false;
    std::string tab_name;
    std::string tab_id;
    bool is_advanced_tab = true;
};

using ModuleLifecycleCallback = void (*)();
using ModuleInitializeCallback = void (*)(ModuleConfigApi* config_api);
using ModuleTickCallback = void (*)();
/** Invoked from Display Commander's ReShade present-before handler (addon `present` timing). Not a nested ReShade event dispatch. */
using ModuleReshadePresentBeforeCallback = void (*)();
using ModuleDrawTabCallback = void (*)(display_commander::ui::IImGuiWrapper&, reshade::api::effect_runtime*);
using ModuleDrawOverlayCallback = void (*)(display_commander::ui::IImGuiWrapper&);
using ModuleDrawMainTabInlineCallback = void (*)(display_commander::ui::IImGuiWrapper&,
                                                 reshade::api::effect_runtime*);
/** Called when any DLL is loaded (LoadLibrary path). `module_path_lower` is the lowercased path/name DC uses for matching. */
using ModuleOnLibraryLoadedCallback = void (*)(HMODULE h_module, const wchar_t* module_path_lower);
using ModuleHotkeyCallback = void (*)();
using ModuleActionCallback = void (*)();

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

struct ModuleActionSpec {
    std::string id;
    std::string display_name;
    std::string description;
    ModuleActionCallback on_trigger_fn = nullptr;
};

struct RegisteredModuleAction {
    std::string module_id;
    std::string module_display_name;
    ModuleActionSpec spec;
};

struct ModuleRegistrationSpec {
    ModuleDescriptor descriptor;
    bool default_enabled = false;
    bool default_show_in_overlay = false;
    bool default_show_tab = true;
    ModuleInitializeCallback initialize_fn = nullptr;
    ModuleTickCallback tick_fn = nullptr;
    ModuleReshadePresentBeforeCallback reshade_present_before_fn = nullptr;
    ModuleDrawTabCallback draw_tab_fn = nullptr;
    ModuleDrawOverlayCallback draw_overlay_fn = nullptr;
    ModuleDrawMainTabInlineCallback draw_main_tab_inline_fn = nullptr;
    ModuleLifecycleCallback on_enabled_fn = nullptr;
    ModuleLifecycleCallback on_disabled_fn = nullptr;
    /** Optional: run before global API-hook / MinHook teardown (e.g. uninstall module-owned detours). */
    ModuleLifecycleCallback on_uninstall_api_hooks_fn = nullptr;
    ModuleOnLibraryLoadedCallback on_library_loaded_fn = nullptr;
    std::vector<ModuleHotkeySpec> hotkeys;
    std::vector<ModuleActionSpec> actions;
};

void InitializeModuleRegistry();
/** For each enabled module that registered `on_library_loaded_fn`, invoke it so the module can react (e.g. install hooks). */
void NotifyEnabledModulesOnLibraryLoaded(HMODULE h_module, const wchar_t* module_path_lower);
/** For each registered module with `on_uninstall_api_hooks_fn`, invoke it (e.g. before `MH_DisableHook(MH_ALL_HOOKS)`). */
void NotifyModulesUninstallApiHooks();
std::vector<ModuleDescriptor> GetModules();
bool IsModuleEnabled(std::string_view module_id);
bool SetModuleEnabled(std::string_view module_id, bool enabled);
bool IsModuleOverlayEnabled(std::string_view module_id);
bool SetModuleOverlayEnabled(std::string_view module_id, bool enabled);
bool IsModuleTabShown(std::string_view module_id);
bool SetModuleTabShown(std::string_view module_id, bool shown);
bool IsModuleTabVisible(std::string_view tab_id);
/** True if `tab_id` matches a registered module overlay tab (see `has_tab` / `tab_id` on the descriptor). */
bool IsRegisteredModuleTabId(std::string_view tab_id);
void TickEnabledModules();
/** For each enabled module with `reshade_present_before_fn`, invoke it (ReShade present-before path). */
void NotifyEnabledModulesReshadePresentBefore();
void DrawModuleTabById(std::string_view module_id, display_commander::ui::IImGuiWrapper& imgui,
                       reshade::api::effect_runtime* runtime);
void DrawEnabledModulesInOverlay(display_commander::ui::IImGuiWrapper& imgui);
void DrawModuleMainTabInlineById(std::string_view module_id, display_commander::ui::IImGuiWrapper& imgui,
                                 reshade::api::effect_runtime* runtime);
void DrawEnabledModulesMainTabInline(display_commander::ui::IImGuiWrapper& imgui,
                                     reshade::api::effect_runtime* runtime);
std::vector<RegisteredModuleHotkey> GetEnabledModuleHotkeys();
std::vector<RegisteredModuleAction> GetEnabledModuleActions();
bool TriggerEnabledModuleActionById(std::string_view action_id);

}  // namespace modules
