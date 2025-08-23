#pragma once

#include <windows.h>
#include <atomic>
#include <thread>

// Background window management using dedicated message processing thread
// 
// ARCHITECTURAL PRINCIPLE: Windows must be created in the same thread that processes their messages.
// This prevents cross-thread ownership issues and ensures proper message routing.
// The background window is created inside m_background_thread, not in the main thread.
class BackgroundWindowManager {
public:
    BackgroundWindowManager();
    ~BackgroundWindowManager();
    
    // Create or update background window
    void UpdateBackgroundWindow(HWND game_hwnd);
    
    // Destroy background window
    void DestroyBackgroundWindow();
    
    // Check if background window exists
    bool HasBackgroundWindow() const;
    
    // Get background window handle
    HWND GetBackgroundWindow() const;

private:
    // Create the background window in the background thread
    bool CreateBackgroundWindow(HWND game_hwnd);
    
    // Update background window position and size
    void UpdateBackgroundWindowPosition(HWND game_hwnd);
    
    // Start the dedicated message processing thread
    void StartBackgroundThread(HWND game_hwnd);
    
    // Create the background window inside the message thread
    bool CreateBackgroundWindowInThread(HWND game_hwnd);
    
    // Background window handle
    HWND m_background_hwnd;
    
    // Flag to track if background window exists
    std::atomic<bool> m_has_background_window;
    
    // Dedicated message processing thread
    std::thread m_background_thread;
    
    // Color cycling for visual feedback
    std::atomic<int> m_frame_counter;
    static constexpr int COLOR_COUNT = 2;
    static constexpr COLORREF COLORS[COLOR_COUNT] = {
        RGB(0, 0, 0),         // Black (visible)
        RGB(255, 0, 255)      // Magenta (transparent)
    };
    
    // Window class name for background window
    static constexpr const char* BACKGROUND_WINDOW_CLASS = "RenodxBackgroundWindow";
    
    // Register window class
    static bool RegisterWindowClass();
    
    // Unregister window class
    static void UnregisterWindowClass();
};

// Global instance
extern BackgroundWindowManager g_backgroundWindowManager;
