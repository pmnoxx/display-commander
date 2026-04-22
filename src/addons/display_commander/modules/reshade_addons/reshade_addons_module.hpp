// Source Code <Display Commander> // follow this order for includes in all files + add this comment at the top
#pragma once

// Source Code <Display Commander>
#include "../module_registry.hpp"

namespace modules::reshade_addons {

/**
 * ReShade add-ons / paths UI. Disabling this module (Main > Modules) hides the ReShade tab only;
 * Display Commander still applies ReShade.ini overrides from main_entry and other settings when configured.
 */
void Initialize(ModuleConfigApi* config_api);
void DrawTab(display_commander::ui::IImGuiWrapper& imgui, reshade::api::effect_runtime* runtime);

}  // namespace modules::reshade_addons
