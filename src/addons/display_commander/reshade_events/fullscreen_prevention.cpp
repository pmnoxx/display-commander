#include "fullscreen_prevention.hpp"
#include "../addon.hpp"
#include <windows.h>

namespace renodx::display_commander::events {

bool OnSetFullscreenState(reshade::api::swapchain* swapchain, bool fullscreen, void* hmonitor) {
    return fullscreen && s_prevent_fullscreen >= 0.5f;
}

}