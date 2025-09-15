#include "adhd_multi_monitor_module.hpp"

namespace adhd_multi_monitor {

namespace api {

bool Initialize()
{
    return g_adhdIntegration.Initialize();
}

void Shutdown()
{
    g_adhdIntegration.Shutdown();
}

void Update()
{
    g_adhdIntegration.Update();
}

void SetGameWindow(HWND hwnd)
{
    g_adhdIntegration.SetGameWindow(hwnd);
}

void SetEnabled(bool enabled)
{
    g_adhdIntegration.SetEnabled(enabled);
}

void SetFocusDisengage(bool disengage)
{
    g_adhdIntegration.SetFocusDisengage(disengage);
}

bool IsEnabled()
{
    return g_adhdIntegration.IsEnabled();
}

bool IsFocusDisengage()
{
    return g_adhdIntegration.IsFocusDisengage();
}

bool HasMultipleMonitors()
{
    return g_adhdIntegration.HasMultipleMonitors();
}

} // namespace api

} // namespace adhd_multi_monitor
