#include "adhd_simple_api.hpp"
#include "adhd_multi_monitor.hpp"

namespace adhd_multi_monitor {

namespace api {

bool Initialize() { return g_adhdManager.Initialize(); }

void Shutdown() { g_adhdManager.Shutdown(); }

void Update() { g_adhdManager.Update(); }

void SetEnabled(bool enabled_for_game_display, bool enabled_for_other_displays) {
    g_adhdManager.SetEnabled(enabled_for_game_display, enabled_for_other_displays);
}

bool IsEnabledForGameDisplay() { return g_adhdManager.IsEnabledForGameDisplay(); }

bool IsEnabledForOtherDisplays() { return g_adhdManager.IsEnabledForOtherDisplays(); }

bool IsFocusDisengage() { return g_adhdManager.IsFocusDisengage(); }

bool HasMultipleMonitors() { return g_adhdManager.HasMultipleMonitors(); }

}  // namespace api

}  // namespace adhd_multi_monitor
