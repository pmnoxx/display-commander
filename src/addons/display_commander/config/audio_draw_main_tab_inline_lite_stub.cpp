// Source Code <Display Commander> // follow this order for includes in all files + add this comment at the top
#include "../modules/audio/audio_module.hpp"

// DC_LITE: modules/audio/*.cpp is omitted; Main tab optional panel still links this symbol.

namespace modules::audio {

void DrawMainTabInline(display_commander::ui::IImGuiWrapper& imgui, reshade::api::effect_runtime* runtime) {
    (void)imgui;
    (void)runtime;
}

}  // namespace modules::audio
