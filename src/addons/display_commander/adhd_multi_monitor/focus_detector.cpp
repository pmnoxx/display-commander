#include "focus_detector.hpp"
#include "../utils.hpp"

namespace adhd_multi_monitor {

// Static instance for the hook procedure
FocusDetector* FocusDetector::instance_ = nullptr;

FocusDetector::FocusDetector()
    : hook_handle_(nullptr)
    , target_window_(nullptr)
    , initialized_(false)
    , current_focus_state_(false)
{
}

FocusDetector::~FocusDetector()
{
    Shutdown();
}

bool FocusDetector::Initialize()
{
    if (initialized_)
        return true;

    // Set the static instance
    instance_ = this;

    // Install a CBT hook to monitor window activation
    hook_handle_ = SetWindowsHookExW(
        WH_CBT,
        WindowProcHook,
        GetModuleHandle(nullptr),
        0
    );

    if (!hook_handle_)
    {
        LogError("Failed to install focus detection hook");
        return false;
    }

    initialized_ = true;
    return true;
}

void FocusDetector::Shutdown()
{
    if (!initialized_)
        return;

    if (hook_handle_)
    {
        UnhookWindowsHookEx(hook_handle_);
        hook_handle_ = nullptr;
    }

    instance_ = nullptr;
    initialized_ = false;
}

void FocusDetector::SetFocusChangeCallback(FocusChangeCallback callback)
{
    focus_callback_ = callback;
}

bool FocusDetector::HasFocus() const
{
    return current_focus_state_;
}

void FocusDetector::SetTargetWindow(HWND hwnd)
{
    target_window_ = hwnd;

    // Update focus state immediately
    if (hwnd)
    {
        bool hasFocus = (GetForegroundWindow() == hwnd);
        if (current_focus_state_ != hasFocus)
        {
            current_focus_state_ = hasFocus;
            if (focus_callback_)
            {
                focus_callback_(hasFocus);
            }
        }
    }
}

LRESULT CALLBACK FocusDetector::WindowProcHook(int nCode, WPARAM wParam, LPARAM lParam)
{
    if (nCode >= 0 && instance_)
    {
        switch (nCode)
        {
        case HCBT_ACTIVATE:
        case HCBT_SETFOCUS:
            {
                HWND activatedWindow = reinterpret_cast<HWND>(wParam);
                if (instance_->target_window_ && activatedWindow == instance_->target_window_)
                {
                    if (!instance_->current_focus_state_)
                    {
                        instance_->current_focus_state_ = true;
                        if (instance_->focus_callback_)
                        {
                            instance_->focus_callback_(true);
                        }
                    }
                }
                else if (instance_->target_window_ && instance_->current_focus_state_)
                {
                    // Check if our target window is still the foreground window
                    HWND foregroundWindow = GetForegroundWindow();
                    if (foregroundWindow != instance_->target_window_)
                    {
                        instance_->current_focus_state_ = false;
                        if (instance_->focus_callback_)
                        {
                            instance_->focus_callback_(false);
                        }
                    }
                }
            }
            break;

        case HCBT_DESTROYWND:
            {
                HWND destroyedWindow = reinterpret_cast<HWND>(wParam);
                if (instance_->target_window_ && destroyedWindow == instance_->target_window_)
                {
                    instance_->target_window_ = nullptr;
                    if (instance_->current_focus_state_)
                    {
                        instance_->current_focus_state_ = false;
                        if (instance_->focus_callback_)
                        {
                            instance_->focus_callback_(false);
                        }
                    }
                }
            }
            break;
        }
    }

    return CallNextHookEx(instance_ ? instance_->hook_handle_ : nullptr, nCode, wParam, lParam);
}

} // namespace adhd_multi_monitor
