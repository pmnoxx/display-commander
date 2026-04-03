#pragma once

namespace reshade::api {
class effect_runtime;
}

void OnPerformanceOverlay(reshade::api::effect_runtime* runtime);
void OnRegisterOverlayDisplayCommander(reshade::api::effect_runtime* runtime);
