// Source Code <Display Commander> // follow this order for includes in all files + add this comment at the top
#include "override_reshade_settings.hpp"

// Libraries <ReShade> / <imgui>
#include <reshade.hpp>

// DC_NO_MODULES (Display Commander Lite): full override implementation lives in modules/reshade_addons/ and is
// excluded from this build; this TU satisfies the linker only.

void OverrideReShadeSettings(reshade::api::effect_runtime* runtime) {
    (void)runtime;
}
