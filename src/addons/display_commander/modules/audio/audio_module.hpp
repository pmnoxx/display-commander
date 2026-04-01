// Source Code <Display Commander> // follow this order for includes in all files + add this comment at the top
#pragma once

// Source Code <Display Commander>
#include "../module_registry.hpp"

namespace modules::audio {

void Initialize(ModuleConfigApi* config_api);
void DrawTab(display_commander::ui::IImGuiWrapper& imgui, reshade::api::effect_runtime* runtime);
void DrawOverlay(display_commander::ui::IImGuiWrapper& imgui);
void DrawMainTabInline(display_commander::ui::IImGuiWrapper& imgui, reshade::api::effect_runtime* runtime);
void FillHotkeys(std::vector<ModuleHotkeySpec>* hotkeys_out);

}  // namespace modules::audio
