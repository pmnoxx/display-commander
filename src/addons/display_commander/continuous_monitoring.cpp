#include "addon.hpp"
#include "utils.hpp"
#include "background_window.hpp"
#include <thread>
#include <chrono>
#include <sstream>

// Forward declarations
void ComputeDesiredSize(int& out_w, int& out_h);

// Global variables for monitoring
extern float s_continuous_monitoring_enabled;
extern std::atomic<bool> g_monitoring_thread_running;
extern std::thread g_monitoring_thread;
extern std::atomic<HWND> g_last_swapchain_hwnd;

// Additional global variables needed for monitoring
extern float s_remove_top_bar;
extern float s_prevent_always_on_top;

extern float s_background_feature_enabled; // Added this line

// ReShade runtime for input blocking


// Background window manager
extern class BackgroundWindowManager g_backgroundWindowManager;

// Input blocking state

static std::atomic<bool> g_app_in_background = false;

// Main monitoring thread function
void ContinuousMonitoringThread() {
    LogInfo("Continuous monitoring thread started");
    
    while (g_monitoring_thread_running.load()) {
        // Check if monitoring is still enabled
        if (s_continuous_monitoring_enabled < 0.5f) {
            break;
        }
        
        // Get the current swapchain window
        HWND hwnd = g_last_swapchain_hwnd.load();
        if (hwnd != nullptr && IsWindow(hwnd)) {
            // BACKGROUND DETECTION: Check if the app is in background
            bool current_background = (GetForegroundWindow() != hwnd);
            bool background_changed = (current_background != g_app_in_background.load());
            
            if (background_changed) {
                g_app_in_background.store(current_background);
                
                if (current_background) {
                    LogInfo("Continuous monitoring: App moved to BACKGROUND");
                } else {
                    LogInfo("Continuous monitoring: App moved to FOREGROUND");
                }
            }
            
            // Apply window changes - the function will automatically determine what needs to be changed
            ApplyWindowChange(hwnd, "continuous_monitoring_auto_fix");
            
            // BACKGROUND WINDOW MANAGEMENT: Update background window if feature is enabled
            static int background_check_counter = 0;
            if (++background_check_counter % 10 == 0) { // Log every 10 seconds
                std::ostringstream oss;
                oss << "Continuous monitoring: Background feature check - s_background_feature_enabled = " << s_background_feature_enabled 
                    << ", has_background_window = " << g_backgroundWindowManager.HasBackgroundWindow();
                LogInfo(oss.str().c_str());
            }
            
            // FOCUS LOSS DETECTION: Close background window when main window loses focus
            HWND foreground_window = GetForegroundWindow();
            if (foreground_window != hwnd && g_backgroundWindowManager.HasBackgroundWindow()) {
                // Main window lost focus, close background window
                LogInfo("Continuous monitoring: Main window lost focus - closing background window");
         
         //       g_backgroundWindowManager.DestroyBackgroundWindow();
            }
            
            if (s_background_feature_enabled >= 0.5f) {
                // Only create/update background window if main window has focus
                if (foreground_window == hwnd) {
                    LogInfo("Continuous monitoring: Calling UpdateBackgroundWindow for background window management");
                    g_backgroundWindowManager.UpdateBackgroundWindow(hwnd);
                } else {
                    if (background_check_counter % 10 == 0) { // Log occasionally
                        LogInfo("Continuous monitoring: Skipping background window update - main window not focused");
                    }
                }
            } else {
                if (background_check_counter % 10 == 0) { // Log occasionally
                    LogInfo("Continuous monitoring: Background feature disabled (s_background_feature_enabled < 0.5f)");
                }
            }
        }
        
        // Sleep for 1 second
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    
    LogInfo("Continuous monitoring thread stopped");
}

// Start continuous monitoring
void StartContinuousMonitoring() {
    if (g_monitoring_thread_running.load()) {
        LogDebug("Continuous monitoring already running");
        return;
    }
    
    if (s_continuous_monitoring_enabled < 0.5f) {
        LogDebug("Continuous monitoring disabled, not starting");
        return;
    }
    
    g_monitoring_thread_running.store(true);
    
    // Start the monitoring thread
    if (g_monitoring_thread.joinable()) {
        g_monitoring_thread.join();
    }
    
    g_monitoring_thread = std::thread(ContinuousMonitoringThread);
    
    LogInfo("Continuous monitoring started");
}

// Stop continuous monitoring
void StopContinuousMonitoring() {
    if (!g_monitoring_thread_running.load()) {
        LogDebug("Continuous monitoring not running");
        return;
    }
    
    g_monitoring_thread_running.store(false);
    
    // Wait for thread to finish
    if (g_monitoring_thread.joinable()) {
        g_monitoring_thread.join();
    }
    
    LogInfo("Continuous monitoring stopped");
}
