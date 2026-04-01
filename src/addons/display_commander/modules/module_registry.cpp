// Source Code <Display Commander> // follow this order for includes in all files + add this comment at the top
#include "module_registry.hpp"

// Source Code <Display Commander>
#include "../config/display_commander_config.hpp"
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
    void (*draw_tab_fn)(display_commander::ui::IImGuiWrapper&, reshade::api::effect_runtime*) = nullptr;
    void (*draw_overlay_fn)(display_commander::ui::IImGuiWrapper&) = nullptr;
    ModuleLifecycleCallback on_enabled_fn = nullptr;
    ModuleLifecycleCallback on_disabled_fn = nullptr;
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
    // Public modules are registered here (always compiled in public builds).
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
        entry.draw_tab_fn = spec.draw_tab_fn;
        entry.draw_overlay_fn = spec.draw_overlay_fn;
        entry.on_enabled_fn = spec.on_enabled_fn;
        entry.on_disabled_fn = spec.on_disabled_fn;
        AddModuleEntry(std::move(entry));
    }
#endif
}

}  // namespace

void InitializeModuleRegistry() {
    bool expected = false;
    if (!g_registry_initialized.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
        return;
    }

    utils::SRWLockExclusive lock(g_modules_lock);
    g_modules.clear();

    RegisterPublicModules();
    RegisterPrivateModules();

    for (ModuleEntry& entry : g_modules) {
        if (entry.initialize_fn != nullptr) {
            entry.initialize_fn(entry.config_api.get());
        }
        if (entry.descriptor.enabled && entry.on_enabled_fn != nullptr) {
            entry.on_enabled_fn();
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
    if (enabled) {
        if (entry->on_enabled_fn != nullptr) {
            entry->on_enabled_fn();
        }
    } else {
        if (entry->on_disabled_fn != nullptr) {
            entry->on_disabled_fn();
        }
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

}  // namespace modules
