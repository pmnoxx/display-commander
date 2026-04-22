// Source Code <Display Commander> // follow this order for includes in all files + add this comment at the top
#include "../modules/audio/audio_module.hpp"

// Unused in normal builds (CMake excludes this TU); satisfies Main tab optional panel symbol when audio module is omitted.

namespace modules::audio {

void DrawMainTabInline(display_commander::ui::IImGuiWrapper& imgui, reshade::api::effect_runtime* runtime) {
    (void)imgui;
    (void)runtime;
}

}  // namespace modules::audio
