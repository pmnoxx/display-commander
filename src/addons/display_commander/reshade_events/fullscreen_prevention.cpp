#include "../globals.hpp"
#include "fullscreen_prevention.hpp"
#include <windows.h>

namespace display_commander::events {

bool OnSetFullscreenState(reshade::api::swapchain *swapchain, bool fullscreen, void *hmonitor) {
    return fullscreen && s_prevent_fullscreen.load();
}

} // namespace display_commander::events