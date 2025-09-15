#pragma once

#include <windows.h>
#include <functional>

namespace adhd_multi_monitor {

// Focus detection callback type
using FocusChangeCallback = std::function<void(bool hasFocus)>;

// Focus detector class for monitoring window focus changes
class FocusDetector {
public:
    FocusDetector();
    ~FocusDetector();

    // Initialize the focus detector
    bool Initialize();

    // Shutdown the focus detector
    void Shutdown();

    // Set the callback for focus changes
    void SetFocusChangeCallback(FocusChangeCallback callback);

    // Check if the target window currently has focus
    bool HasFocus() const;

    // Set the target window to monitor
    void SetTargetWindow(HWND hwnd);

private:
    // Window procedure hook
    static LRESULT CALLBACK WindowProcHook(int nCode, WPARAM wParam, LPARAM lParam);

    // Member variables
    HHOOK hook_handle_;
    HWND target_window_;
    FocusChangeCallback focus_callback_;
    bool initialized_;
    bool current_focus_state_;

    // Static instance for the hook procedure
    static FocusDetector* instance_;
};

} // namespace adhd_multi_monitor
