#include "vblank_monitor.hpp"
#include <iostream>
#include <thread>
#include <chrono>
#include <iomanip> // Required for std::fixed and std::setprecision
#include <windows.h> // Required for GetForegroundWindow

// Example usage of the VBlankMonitor class
void ExampleVBlankMonitoring() {
    using namespace dxgi::fps_limiter;
    
    // Create a VBlank monitor instance
    VBlankMonitor monitor;
    
    // Start monitoring (will automatically bind to foreground window)
    monitor.StartMonitoring();
    
    std::cout << "VBlank monitoring started. Press Enter to stop..." << std::endl;
    
    // Let it run for a while
    std::this_thread::sleep_for(std::chrono::seconds(10));
    
    // Print some statistics
    std::cout << "\nVBlank Monitor Statistics:" << std::endl;
    std::cout << "  VBlank count: " << monitor.GetVBlankCount() << std::endl;
    std::cout << "  State changes: " << monitor.GetStateChangeCount() << std::endl;
    std::cout << "  VBlank percentage: " << std::fixed << std::setprecision(2) << monitor.GetVBlankPercentage() << "%" << std::endl;
    std::cout << "  Avg VBlank duration: " << monitor.GetAverageVBlankDuration().count() << " ms" << std::endl;
    std::cout << "  Avg Active duration: " << monitor.GetAverageActiveDuration().count() << " ms" << std::endl;
    
    // Get detailed stats
    std::cout << "\nDetailed Statistics:" << std::endl;
    std::cout << monitor.GetDetailedStatsString() << std::endl;
    
    // Get last transition info
    std::cout << "\nLast Transition Info:" << std::endl;
    std::cout << monitor.GetLastTransitionInfo() << std::endl;
    
    // Stop monitoring
    monitor.StopMonitoring();
    
    std::cout << "VBlank monitoring stopped." << std::endl;
}

// Example of binding to a specific window
void ExampleBindToSpecificWindow() {
    using namespace dxgi::fps_limiter;
    
    VBlankMonitor monitor;
    
    // Bind to a specific window (e.g., foreground window)
    HWND hwnd = GetForegroundWindow();
    if (monitor.BindToDisplay(hwnd)) {
        std::cout << "Successfully bound to window" << std::endl;
        
        // Start monitoring
        monitor.StartMonitoring();
        
        // Let it run for a bit
        std::this_thread::sleep_for(std::chrono::seconds(5));
        
        // Stop monitoring
        monitor.StopMonitoring();
    } else {
        std::cout << "Failed to bind to window" << std::endl;
    }
}

// Main example function
int main() {
    std::cout << "VBlank Monitor Examples" << std::endl;
    std::cout << "=======================" << std::endl;
    
    // Example 1: Basic monitoring
    std::cout << "\nExample 1: Basic VBlank Monitoring" << std::endl;
    ExampleVBlankMonitoring();
    
    // Example 2: Bind to specific window
    std::cout << "\nExample 2: Bind to Specific Window" << std::endl;
    ExampleBindToSpecificWindow();
    
    std::cout << "\nExamples completed." << std::endl;
    return 0;
}
