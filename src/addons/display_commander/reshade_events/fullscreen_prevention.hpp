#pragma once

#include <reshade.hpp>

namespace display_commander::events {

bool OnSetFullscreenState(reshade::api::swapchain* swapchain, bool fullscreen, void* hmonitor);

}
