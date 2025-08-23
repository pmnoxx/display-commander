#pragma once

#include <include/reshade.hpp>

namespace renodx::display_commander::events {

bool OnSetFullscreenState(reshade::api::swapchain* swapchain, bool fullscreen, void* hmonitor);

}
