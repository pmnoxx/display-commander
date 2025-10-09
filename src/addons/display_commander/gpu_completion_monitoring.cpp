#include "gpu_completion_monitoring.hpp"
#include "globals.hpp"
#include "utils.hpp"
#include "utils/timing.hpp"

#include <atomic>
#include <chrono>
#include <thread>

namespace {
// GPU completion monitoring thread state
std::atomic<bool> g_gpu_monitoring_thread_running{false};
std::thread g_gpu_monitoring_thread;

// GPU completion monitoring thread function
void GPUCompletionMonitoringThread() {
    LogInfo("GPU completion monitoring thread started");

    while (g_gpu_monitoring_thread_running.load()) {
        // Check if GPU measurement is enabled
        if (!g_gpu_measurement_enabled.load()) {
            // Sleep briefly when disabled to avoid busy-wait
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
            continue;
        }

        // Get the GPU completion event handle
        HANDLE event = g_gpu_completion_event.load();
        if (event == nullptr) {
            // No event available yet, sleep briefly
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
            continue;
        }

        // Blocking wait for GPU completion (with timeout to allow thread shutdown)
        // Use 100ms timeout to allow responsive shutdown when thread is stopped
        DWORD result = WaitForSingleObject(event, 100);

        if (result == WAIT_OBJECT_0) {
            // GPU work completed - capture the exact completion time
            LONGLONG gpu_completion_time = utils::get_now_ns();
            LONGLONG present_start_time = g_present_start_time_ns.load();

            // Calculate GPU duration
            if (present_start_time > 0) {
                LONGLONG gpu_duration_new_ns = gpu_completion_time - present_start_time;

                // Smooth the GPU duration with exponential moving average
                // Alpha = 64 means we average over ~64 frames
                LONGLONG old_duration = g_gpu_duration_ns.load();
                LONGLONG smoothed_duration = UpdateRollingAverage(gpu_duration_new_ns, old_duration);

                // Store the smoothed duration and exact completion time
                g_gpu_duration_ns.store(smoothed_duration);
                g_gpu_completion_time_ns.store(gpu_completion_time);
            }
        } else if (result == WAIT_TIMEOUT) {
            // Timeout - GPU hasn't finished yet, loop again
            // This is normal and allows us to check the running flag periodically
            continue;
        } else {
            // Error or abandoned - log and continue
            LogDebug("GPU completion wait failed with result: %lu", result);
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
        }
    }

    LogInfo("GPU completion monitoring thread stopped");
}
} // anonymous namespace

// Start GPU completion monitoring thread
void StartGPUCompletionMonitoring() {
    if (g_gpu_monitoring_thread_running.load()) {
        LogDebug("GPU completion monitoring thread already running");
        return;
    }

    g_gpu_monitoring_thread_running.store(true);

    // Join existing thread if it's still joinable
    if (g_gpu_monitoring_thread.joinable()) {
        g_gpu_monitoring_thread.join();
    }

    // Start new monitoring thread
    g_gpu_monitoring_thread = std::thread(GPUCompletionMonitoringThread);

    LogInfo("GPU completion monitoring thread started");
}

// Stop GPU completion monitoring thread
void StopGPUCompletionMonitoring() {
    if (!g_gpu_monitoring_thread_running.load()) {
        LogDebug("GPU completion monitoring thread not running");
        return;
    }

    g_gpu_monitoring_thread_running.store(false);

    // Wait for thread to finish
    if (g_gpu_monitoring_thread.joinable()) {
        g_gpu_monitoring_thread.join();
    }

    LogInfo("GPU completion monitoring thread stopped");
}

