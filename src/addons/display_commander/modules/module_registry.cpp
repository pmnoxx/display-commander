// Source Code <Display Commander> // follow this order for includes in all files + add this comment at the top
#include "module_registry.hpp"

// Source Code <Display Commander>
#include "../config/display_commander_config.hpp"
#if DC_INTERNAL_MODULES
#include "audio/audio_module.hpp"
#include "controller/controller_module.hpp"
#include "example_dummy/example_dummy_module.hpp"
#endif
#include "../utils/srwlock_wrapper.hpp"
#if defined(DC_EXTERNAL_MODULES)
#include "private_modules_registration.hpp"
#endif

// Libraries <standard C++>
#include <algorithm>
#include <atomic>
#include <memory>

// Libraries <Windows.h>
#include <Windows.h>

namespace modules {
namespace {

struct ModuleEntry {
    ModuleDescriptor descriptor;
    void (*initialize_fn)(ModuleConfigApi* config_api) = nullptr;
    void (*tick_fn)() = nullptr;
    ModuleReshadePresentBeforeCallback reshade_present_before_fn = nullptr;
    void (*draw_tab_fn)(display_commander::ui::IImGuiWrapper&, reshade::api::effect_runtime*) = nullptr;
    void (*draw_overlay_fn)(display_commander::ui::IImGuiWrapper&) = nullptr;
    ModuleDrawMainTabInlineCallback draw_main_tab_inline_fn = nullptr;
    ModuleLifecycleCallback on_enabled_fn = nullptr;
    ModuleLifecycleCallback on_disabled_fn = nullptr;
    ModuleLifecycleCallback on_uninstall_api_hooks_fn = nullptr;
    ModuleOnLibraryLoadedCallback on_library_loaded_fn = nullptr;
    std::vector<ModuleHotkeySpec> hotkeys;
    std::vector<ModuleActionSpec> actions;
    std::unique_ptr<ModuleConfigApi> config_api;
};

class ModuleConfigApiImpl : public ModuleConfigApi {
   public:
    explicit ModuleConfigApiImpl(std::string module_id) : section_("Modules_" + module_id) {}

    bool GetBool(std::string_view key, bool default_value) override {
        bool value = default_value;
        display_commander::config::get_config_value_or_default(section_.c_str(), std::string(key).c_str(),
                                                               default_value, &value);
        return value;
    }

    void SetBool(std::string_view key, bool value) override {
        display_commander::config::set_config_value(section_.c_str(), std::string(key).c_str(), value);
    }

   private:
    std::string section_;
};

SRWLOCK g_modules_lock = SRWLOCK_INIT;
std::vector<ModuleEntry> g_modules;
std::atomic<bool> g_registry_initialized{false};

ModuleEntry* FindModuleEntry(std::string_view module_id) {
    auto it = std::find_if(g_modules.begin(), g_modules.end(),
                           [module_id](const ModuleEntry& entry) { return entry.descriptor.id == module_id; });
    if (it == g_modules.end()) {
        return nullptr;
    }
    return &(*it);
}

const ModuleEntry* FindModuleEntryConst(std::string_view module_id) {
    auto it = std::find_if(g_modules.begin(), g_modules.end(),
                           [module_id](const ModuleEntry& entry) { return entry.descriptor.id == module_id; });
    if (it == g_modules.end()) {
        return nullptr;
    }
    return &(*it);
}

void AddModuleEntry(ModuleEntry&& entry) {
    if (entry.descriptor.id.empty()) {
        return;
    }
    if (FindModuleEntryConst(entry.descriptor.id) != nullptr) {
        return;
    }
    g_modules.push_back(std::move(entry));
}

void RegisterPublicModules() {
#if !DC_INTERNAL_MODULES
    return;
#else
    {
        ModuleRegistrationSpec spec{};
        spec.descriptor.id = "audio";
        spec.descriptor.display_name = "Audio";
        spec.descriptor.description = "Audio controls, VU overlay, and audio hotkeys.";
        spec.descriptor.has_tab = true;
        spec.descriptor.tab_name = "Audio";
        spec.descriptor.tab_id = "audio";
        spec.descriptor.is_advanced_tab = false;
        spec.default_enabled = true;
        spec.default_show_in_overlay = false;
        spec.initialize_fn = &audio::Initialize;
        spec.on_enabled_fn = &audio::OnEnabled;
        spec.draw_tab_fn = &audio::DrawTab;
        spec.draw_overlay_fn = &audio::DrawOverlay;
        spec.draw_main_tab_inline_fn = &audio::DrawMainTabInline;
        audio::FillHotkeys(&spec.hotkeys);
        audio::FillActions(&spec.actions);

        ModuleEntry entry{};
        entry.descriptor = spec.descriptor;
        entry.config_api = std::make_unique<ModuleConfigApiImpl>(entry.descriptor.id);
        entry.descriptor.enabled = entry.config_api->GetBool("enabled", spec.default_enabled);
        entry.descriptor.show_in_overlay = entry.config_api->GetBool("show_in_overlay", spec.default_show_in_overlay);
        entry.initialize_fn = spec.initialize_fn;
        entry.tick_fn = spec.tick_fn;
        entry.reshade_present_before_fn = spec.reshade_present_before_fn;
        entry.draw_tab_fn = spec.draw_tab_fn;
        entry.draw_overlay_fn = spec.draw_overlay_fn;
        entry.draw_main_tab_inline_fn = spec.draw_main_tab_inline_fn;
        entry.on_enabled_fn = spec.on_enabled_fn;
        entry.on_disabled_fn = spec.on_disabled_fn;
        entry.on_uninstall_api_hooks_fn = spec.on_uninstall_api_hooks_fn;
        entry.on_library_loaded_fn = spec.on_library_loaded_fn;
        entry.hotkeys = spec.hotkeys;
        entry.actions = spec.actions;
        AddModuleEntry(std::move(entry));
    }

    {
        ModuleRegistrationSpec spec{};
        spec.descriptor.id = "controller";
        spec.descriptor.display_name = "Controller";
        spec.descriptor.description = "XInput / Windows.Gaming.Input hooks, controller UI, and remapping support.";
        spec.descriptor.has_tab = true;
        spec.descriptor.tab_name = "Controller";
        spec.descriptor.tab_id = "controller";
        spec.descriptor.is_advanced_tab = true;
        spec.default_enabled = true;
        spec.default_show_in_overlay = false;
        spec.initialize_fn = &controller::Initialize;
        spec.on_enabled_fn = &controller::OnEnabled;
        spec.on_disabled_fn = &controller::OnDisabled;
        spec.on_uninstall_api_hooks_fn = &controller::OnUninstallApiHooks;
        spec.draw_tab_fn = &controller::DrawTab;
        spec.on_library_loaded_fn = &controller::OnLibraryLoaded;
        spec.reshade_present_before_fn = &controller::OnReshadePresentBefore;

        ModuleEntry entry{};
        entry.descriptor = spec.descriptor;
        entry.config_api = std::make_unique<ModuleConfigApiImpl>(entry.descriptor.id);
        entry.descriptor.enabled = entry.config_api->GetBool("enabled", spec.default_enabled);
        entry.descriptor.show_in_overlay = entry.config_api->GetBool("show_in_overlay", spec.default_show_in_overlay);
        entry.initialize_fn = spec.initialize_fn;
        entry.tick_fn = spec.tick_fn;
        entry.reshade_present_before_fn = spec.reshade_present_before_fn;
        entry.draw_tab_fn = spec.draw_tab_fn;
        entry.draw_overlay_fn = spec.draw_overlay_fn;
        entry.draw_main_tab_inline_fn = spec.draw_main_tab_inline_fn;
        entry.on_enabled_fn = spec.on_enabled_fn;
        entry.on_disabled_fn = spec.on_disabled_fn;
        entry.on_uninstall_api_hooks_fn = spec.on_uninstall_api_hooks_fn;
        entry.on_library_loaded_fn = spec.on_library_loaded_fn;
        entry.hotkeys = spec.hotkeys;
        entry.actions = spec.actions;
        AddModuleEntry(std::move(entry));
    }

    // Public modules are registered here (always compiled in public builds).
    ModuleRegistrationSpec spec{};
    spec.descriptor.id = "example_dummy";
    spec.descriptor.display_name = "Example Dummy";
    spec.descriptor.description = "Minimal in-repo example module with tick, tab, and overlay callbacks.";
    spec.descriptor.has_tab = true;
    spec.descriptor.tab_name = "Example Dummy";
    spec.descriptor.tab_id = "example_dummy";
    spec.descriptor.is_advanced_tab = true;
    spec.default_enabled = false;
    spec.default_show_in_overlay = false;
    spec.initialize_fn = &example_dummy::Initialize;
    spec.tick_fn = &example_dummy::Tick;
    spec.draw_tab_fn = &example_dummy::DrawTab;
    spec.draw_overlay_fn = &example_dummy::DrawOverlay;

    ModuleEntry entry{};
    entry.descriptor = spec.descriptor;
    entry.config_api = std::make_unique<ModuleConfigApiImpl>(entry.descriptor.id);
    entry.descriptor.enabled = entry.config_api->GetBool("enabled", spec.default_enabled);
    entry.descriptor.show_in_overlay = entry.config_api->GetBool("show_in_overlay", spec.default_show_in_overlay);
    entry.initialize_fn = spec.initialize_fn;
    entry.tick_fn = spec.tick_fn;
    entry.reshade_present_before_fn = spec.reshade_present_before_fn;
    entry.draw_tab_fn = spec.draw_tab_fn;
    entry.draw_overlay_fn = spec.draw_overlay_fn;
    entry.draw_main_tab_inline_fn = spec.draw_main_tab_inline_fn;
    entry.on_enabled_fn = spec.on_enabled_fn;
    entry.on_disabled_fn = spec.on_disabled_fn;
    entry.on_uninstall_api_hooks_fn = spec.on_uninstall_api_hooks_fn;
    entry.on_library_loaded_fn = spec.on_library_loaded_fn;
    entry.hotkeys = spec.hotkeys;
    entry.actions = spec.actions;
    AddModuleEntry(std::move(entry));
#endif  // DC_INTERNAL_MODULES
}

void RegisterPrivateModules() {
#if defined(DC_EXTERNAL_MODULES)
    std::vector<ModuleRegistrationSpec> specs;
    GetExternalModuleRegistrations(specs);
    for (const ModuleRegistrationSpec& spec : specs) {
        ModuleEntry entry{};
        entry.descriptor = spec.descriptor;
        if (entry.descriptor.id.empty()) {
            continue;
        }
        entry.config_api = std::make_unique<ModuleConfigApiImpl>(entry.descriptor.id);
        entry.descriptor.enabled = entry.config_api->GetBool("enabled", spec.default_enabled);
        entry.descriptor.show_in_overlay =
            entry.config_api->GetBool("show_in_overlay", spec.default_show_in_overlay);
        entry.initialize_fn = spec.initialize_fn;
        entry.tick_fn = spec.tick_fn;
        entry.reshade_present_before_fn = spec.reshade_present_before_fn;
        entry.draw_tab_fn = spec.draw_tab_fn;
        entry.draw_overlay_fn = spec.draw_overlay_fn;
        entry.draw_main_tab_inline_fn = spec.draw_main_tab_inline_fn;
        entry.on_enabled_fn = spec.on_enabled_fn;
        entry.on_disabled_fn = spec.on_disabled_fn;
        entry.on_uninstall_api_hooks_fn = spec.on_uninstall_api_hooks_fn;
        entry.on_library_loaded_fn = spec.on_library_loaded_fn;
        entry.hotkeys = spec.hotkeys;
        entry.actions = spec.actions;
        AddModuleEntry(std::move(entry));
    }
#endif
}

}  // namespace

void NotifyModulesUninstallApiHooks() {
    InitializeModuleRegistry();
    std::vector<ModuleLifecycleCallback> callbacks;
    {
        utils::SRWLockShared lock(g_modules_lock);
        callbacks.reserve(g_modules.size());
        for (const ModuleEntry& entry : g_modules) {
            if (entry.on_uninstall_api_hooks_fn != nullptr) {
                callbacks.push_back(entry.on_uninstall_api_hooks_fn);
            }
        }
    }
    for (ModuleLifecycleCallback fn : callbacks) {
        if (fn != nullptr) {
            fn();
        }
    }
}

void InitializeModuleRegistry() {
    bool expected = false;
    if (!g_registry_initialized.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
        return;
    }

    std::vector<ModuleLifecycleCallback> on_enabled_after_unlock;
    {
        utils::SRWLockExclusive lock(g_modules_lock);
        g_modules.clear();

        RegisterPublicModules();
        RegisterPrivateModules();

        for (ModuleEntry& entry : g_modules) {
            if (entry.initialize_fn != nullptr) {
                entry.initialize_fn(entry.config_api.get());
            }
        }
        on_enabled_after_unlock.reserve(g_modules.size());
        for (ModuleEntry& entry : g_modules) {
            if (entry.descriptor.enabled && entry.on_enabled_fn != nullptr) {
                on_enabled_after_unlock.push_back(entry.on_enabled_fn);
            }
        }
    }
    for (ModuleLifecycleCallback fn : on_enabled_after_unlock) {
        if (fn != nullptr) {
            fn();
        }
    }
}

void NotifyEnabledModulesOnLibraryLoaded(HMODULE h_module, const wchar_t* module_path_lower) {
    if (h_module == nullptr || module_path_lower == nullptr) {
        return;
    }
    InitializeModuleRegistry();
    std::vector<ModuleOnLibraryLoadedCallback> callbacks;
    {
        utils::SRWLockShared lock(g_modules_lock);
        for (const ModuleEntry& entry : g_modules) {
            if (!entry.descriptor.enabled || entry.on_library_loaded_fn == nullptr) {
                continue;
            }
            callbacks.push_back(entry.on_library_loaded_fn);
        }
    }
    for (ModuleOnLibraryLoadedCallback fn : callbacks) {
        if (fn != nullptr) {
            fn(h_module, module_path_lower);
        }
    }
}

std::vector<ModuleDescriptor> GetModules() {
    InitializeModuleRegistry();
    utils::SRWLockShared lock(g_modules_lock);
    std::vector<ModuleDescriptor> modules;
    modules.reserve(g_modules.size());
    for (const ModuleEntry& entry : g_modules) {
        modules.push_back(entry.descriptor);
    }
    return modules;
}

bool IsModuleEnabled(std::string_view module_id) {
    InitializeModuleRegistry();
    utils::SRWLockShared lock(g_modules_lock);
    const ModuleEntry* entry = FindModuleEntryConst(module_id);
    return (entry != nullptr) ? entry->descriptor.enabled : false;
}

bool SetModuleEnabled(std::string_view module_id, bool enabled) {
    InitializeModuleRegistry();
    ModuleLifecycleCallback on_enabled_fn_after_unlock = nullptr;
    ModuleLifecycleCallback on_disabled_fn_after_unlock = nullptr;
    {
        utils::SRWLockExclusive lock(g_modules_lock);
        ModuleEntry* entry = FindModuleEntry(module_id);
        if (entry == nullptr) {
            return false;
        }
        const bool was_enabled = entry->descriptor.enabled;
        if (was_enabled == enabled) {
            return true;
        }
        entry->descriptor.enabled = enabled;
        if (entry->config_api) {
            entry->config_api->SetBool("enabled", enabled);
        }
        if (enabled && entry->on_enabled_fn != nullptr) {
            on_enabled_fn_after_unlock = entry->on_enabled_fn;
        }
        if (!enabled && entry->on_disabled_fn != nullptr) {
            on_disabled_fn_after_unlock = entry->on_disabled_fn;
        }
        // Soft-disable when !enabled: keep module resident in memory to avoid unload-related crashes; UI skips disabled
        // modules via descriptor.enabled.
    }
    if (on_enabled_fn_after_unlock != nullptr) {
        on_enabled_fn_after_unlock();
    }
    if (on_disabled_fn_after_unlock != nullptr) {
        on_disabled_fn_after_unlock();
    }
    return true;
}

bool IsModuleOverlayEnabled(std::string_view module_id) {
    InitializeModuleRegistry();
    utils::SRWLockShared lock(g_modules_lock);
    const ModuleEntry* entry = FindModuleEntryConst(module_id);
    return (entry != nullptr) ? entry->descriptor.show_in_overlay : false;
}

bool SetModuleOverlayEnabled(std::string_view module_id, bool enabled) {
    InitializeModuleRegistry();
    utils::SRWLockExclusive lock(g_modules_lock);
    ModuleEntry* entry = FindModuleEntry(module_id);
    if (entry == nullptr) {
        return false;
    }
    entry->descriptor.show_in_overlay = enabled;
    if (entry->config_api) {
        entry->config_api->SetBool("show_in_overlay", enabled);
    }
    return true;
}

bool IsModuleTabVisible(std::string_view tab_id) {
    InitializeModuleRegistry();
    utils::SRWLockShared lock(g_modules_lock);
    for (const ModuleEntry& entry : g_modules) {
        if (!entry.descriptor.has_tab) {
            continue;
        }
        if (entry.descriptor.tab_id == tab_id) {
            return entry.descriptor.enabled;
        }
    }
    return true;
}

void TickEnabledModules() {
    InitializeModuleRegistry();
    utils::SRWLockShared lock(g_modules_lock);
    for (const ModuleEntry& entry : g_modules) {
        if (!entry.descriptor.enabled || entry.tick_fn == nullptr) {
            continue;
        }
        entry.tick_fn();
    }
}

void NotifyEnabledModulesReshadePresentBefore() {
    InitializeModuleRegistry();
    utils::SRWLockShared lock(g_modules_lock);
    for (const ModuleEntry& entry : g_modules) {
        if (!entry.descriptor.enabled || entry.reshade_present_before_fn == nullptr) {
            continue;
        }
        entry.reshade_present_before_fn();
    }
}

void DrawModuleTabById(std::string_view module_id, display_commander::ui::IImGuiWrapper& imgui,
                       reshade::api::effect_runtime* runtime) {
    InitializeModuleRegistry();
    utils::SRWLockShared lock(g_modules_lock);
    const ModuleEntry* entry = FindModuleEntryConst(module_id);
    if (entry == nullptr) {
        imgui.Text("Unknown module: %.*s", static_cast<int>(module_id.size()), module_id.data());
        return;
    }
    if (!entry->descriptor.enabled) {
        imgui.TextDisabled("%s is disabled in Main tab > Features.", entry->descriptor.display_name.c_str());
        return;
    }
    if (entry->draw_tab_fn != nullptr) {
        entry->draw_tab_fn(imgui, runtime);
    }
}

void DrawEnabledModulesInOverlay(display_commander::ui::IImGuiWrapper& imgui) {
    InitializeModuleRegistry();
    utils::SRWLockShared lock(g_modules_lock);
    for (const ModuleEntry& entry : g_modules) {
        if (!entry.descriptor.enabled || !entry.descriptor.show_in_overlay || entry.draw_overlay_fn == nullptr) {
            continue;
        }
        entry.draw_overlay_fn(imgui);
    }
}

void DrawModuleMainTabInlineById(std::string_view module_id, display_commander::ui::IImGuiWrapper& imgui,
                                 reshade::api::effect_runtime* runtime) {
    InitializeModuleRegistry();
    utils::SRWLockShared lock(g_modules_lock);
    const ModuleEntry* entry = FindModuleEntryConst(module_id);
    if (entry == nullptr || !entry->descriptor.enabled || entry->draw_main_tab_inline_fn == nullptr) {
        return;
    }
    entry->draw_main_tab_inline_fn(imgui, runtime);
}

void DrawEnabledModulesMainTabInline(display_commander::ui::IImGuiWrapper& imgui,
                                     reshade::api::effect_runtime* runtime) {
    InitializeModuleRegistry();
    utils::SRWLockShared lock(g_modules_lock);
    for (const ModuleEntry& entry : g_modules) {
        if (!entry.descriptor.enabled || entry.draw_main_tab_inline_fn == nullptr) {
            continue;
        }
        entry.draw_main_tab_inline_fn(imgui, runtime);
    }
}

std::vector<RegisteredModuleHotkey> GetEnabledModuleHotkeys() {
    InitializeModuleRegistry();
    utils::SRWLockShared lock(g_modules_lock);

    std::vector<RegisteredModuleHotkey> hotkeys;
    for (const ModuleEntry& entry : g_modules) {
        if (!entry.descriptor.enabled || entry.hotkeys.empty()) {
            continue;
        }

        hotkeys.reserve(hotkeys.size() + entry.hotkeys.size());
        for (const ModuleHotkeySpec& spec : entry.hotkeys) {
            if (spec.id.empty() || spec.on_trigger_fn == nullptr) {
                continue;
            }

            RegisteredModuleHotkey hotkey{};
            hotkey.module_id = entry.descriptor.id;
            hotkey.module_display_name = entry.descriptor.display_name;
            hotkey.spec = spec;
            hotkeys.push_back(std::move(hotkey));
        }
    }

    return hotkeys;
}

std::vector<RegisteredModuleAction> GetEnabledModuleActions() {
    InitializeModuleRegistry();
    utils::SRWLockShared lock(g_modules_lock);

    std::vector<RegisteredModuleAction> actions;
    for (const ModuleEntry& entry : g_modules) {
        if (!entry.descriptor.enabled || entry.actions.empty()) {
            continue;
        }

        actions.reserve(actions.size() + entry.actions.size());
        for (const ModuleActionSpec& spec : entry.actions) {
            if (spec.id.empty() || spec.on_trigger_fn == nullptr) {
                continue;
            }

            RegisteredModuleAction action{};
            action.module_id = entry.descriptor.id;
            action.module_display_name = entry.descriptor.display_name;
            action.spec = spec;
            actions.push_back(std::move(action));
        }
    }

    return actions;
}

bool TriggerEnabledModuleActionById(std::string_view action_id) {
    if (action_id.empty()) {
        return false;
    }

    InitializeModuleRegistry();
    utils::SRWLockShared lock(g_modules_lock);

    for (const ModuleEntry& entry : g_modules) {
        if (!entry.descriptor.enabled || entry.actions.empty()) {
            continue;
        }
        for (const ModuleActionSpec& spec : entry.actions) {
            if (spec.id == action_id && spec.on_trigger_fn != nullptr) {
                spec.on_trigger_fn();
                return true;
            }
        }
    }

    return false;
}

}  // namespace modules
