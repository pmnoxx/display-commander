#include "adhd_integration.hpp"
#include "../globals.hpp"
#include "../utils.hpp"

namespace adhd_multi_monitor {

// Global integration instance
AdhdIntegration g_adhdIntegration;

AdhdIntegration::AdhdIntegration()
    : manager_(&g_adhdManager)
    , focus_detector_(nullptr)
    , game_window_(nullptr)
    , initialized_(false)
{
}

AdhdIntegration::~AdhdIntegration()
{
    Shutdown();
}

bool AdhdIntegration::Initialize()
{
    if (initialized_)
        return true;

    // Initialize the manager
    if (!manager_->Initialize())
    {
        LogError("Failed to initialize ADHD multi-monitor manager");
        return false;
    }

    // Create and initialize the focus detector
    focus_detector_ = new FocusDetector();
    if (!focus_detector_->Initialize())
    {
        LogError("Failed to initialize ADHD focus detector");
        delete focus_detector_;
        focus_detector_ = nullptr;
        return false;
    }

    // Set up the focus change callback
    focus_detector_->SetFocusChangeCallback([this](bool hasFocus) {
        OnFocusChanged(hasFocus);
    });

    initialized_ = true;
    return true;
}

void AdhdIntegration::Shutdown()
{
    if (!initialized_)
        return;

    // Shutdown focus detector
    if (focus_detector_)
    {
        focus_detector_->Shutdown();
        delete focus_detector_;
        focus_detector_ = nullptr;
    }

    // Shutdown manager
    if (manager_)
    {
        manager_->Shutdown();
    }

    initialized_ = false;
}

void AdhdIntegration::Update()
{
    if (!initialized_)
        return;

    // Update the game window handle from the global swapchain HWND
    HWND current_hwnd = g_last_swapchain_hwnd.load();
    if (current_hwnd && current_hwnd != game_window_)
    {
        SetGameWindow(current_hwnd);
    }

    // Update the manager
    manager_->UpdateBackgroundWindow();
}

void AdhdIntegration::SetGameWindow(HWND hwnd)
{
    game_window_ = hwnd;

    if (manager_)
    {
        manager_->SetGameWindow(hwnd);
    }

    if (focus_detector_)
    {
        focus_detector_->SetTargetWindow(hwnd);
    }
}

void AdhdIntegration::SetEnabled(bool enabled)
{
    if (manager_)
    {
        manager_->SetEnabled(enabled);
    }
}

void AdhdIntegration::SetFocusDisengage(bool disengage)
{
    if (manager_)
    {
        manager_->SetFocusDisengage(disengage);
    }
}

bool AdhdIntegration::IsEnabled() const
{
    return manager_ ? manager_->IsEnabled() : false;
}

bool AdhdIntegration::IsFocusDisengage() const
{
    return manager_ ? manager_->IsFocusDisengage() : false;
}

bool AdhdIntegration::HasMultipleMonitors() const
{
    return manager_ ? manager_->HasMultipleMonitors() : false;
}

void AdhdIntegration::OnFocusChanged(bool hasFocus)
{
    if (manager_)
    {
        manager_->OnWindowFocusChanged(hasFocus);
    }
}

} // namespace adhd_multi_monitor
