#include "adhd_simple_api.hpp"

namespace adhd_multi_monitor {

namespace api {

bool Initialize() { return g_adhdManager.Initialize(); }

void Shutdown() { g_adhdManager.Shutdown(); }

void Update() { g_adhdManager.Update(); }

void SetEnabled(bool enabled) { g_adhdManager.SetEnabled(enabled); }

bool IsEnabled() { return g_adhdManager.IsEnabled(); }

bool IsFocusDisengage() { return g_adhdManager.IsFocusDisengage(); }

bool HasMultipleMonitors() { return g_adhdManager.HasMultipleMonitors(); }

} // namespace api

} // namespace adhd_multi_monitor
