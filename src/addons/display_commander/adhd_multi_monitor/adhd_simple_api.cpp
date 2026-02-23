#include "adhd_simple_api.hpp"
#include "adhd_multi_monitor.hpp"

namespace adhd_multi_monitor {

namespace api {

bool Initialize() { return g_adhdManager.Initialize(); }

void Shutdown() { g_adhdManager.Shutdown(); }

void SetEnabled(bool enabled_for_game_display, bool enabled_for_other_displays) {
    g_adhdManager.SetEnabled(enabled_for_game_display, enabled_for_other_displays);
}

bool IsEnabledForGameDisplay() { return g_adhdManager.IsEnabledForGameDisplay(); }

bool IsEnabledForOtherDisplays() { return g_adhdManager.IsEnabledForOtherDisplays(); }

bool IsFocusDisengage() { return g_adhdManager.IsFocusDisengage(); }

bool HasMultipleMonitors() { return g_adhdManager.HasMultipleMonitors(); }

void GetBackgroundWindowDebugInfo(BackgroundWindowDebugInfo* out) {
    if (out == nullptr) return;
    HWND hwnd = nullptr;
    RECT rect = {0, 0, 0, 0};
    bool is_visible = false;
    g_adhdManager.GetBackgroundWindowDebugInfo(&hwnd, &rect, &is_visible);
    out->hwnd = reinterpret_cast<void*>(hwnd);
    out->not_null = (hwnd != nullptr);
    out->left = rect.left;
    out->top = rect.top;
    out->width = rect.right - rect.left;
    out->height = rect.bottom - rect.top;
    out->is_visible = is_visible;
}

}  // namespace api

}  // namespace adhd_multi_monitor
