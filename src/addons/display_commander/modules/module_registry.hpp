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

}  // namespace modules
