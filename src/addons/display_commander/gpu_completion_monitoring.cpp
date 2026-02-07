#include "gpu_completion_monitoring.hpp"
#include "globals.hpp"
#include "settings/main_tab_settings.hpp"
#include "swapchain_events.hpp"
#include "utils.hpp"
#include "utils/logging.hpp"
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
        if (settings::g_mainTabSettings.gpu_measurement_enabled.GetValue() == 0) {
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

        static int count = 0;
        count++;
        if (count < 3) {
            LogDebug("[GPU Completion Monitoring] GPU completion monitoring thread: %d", count);
        }

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

                // Write GPU completion time into frame data for the frame this GPU work belongs to
                // (render thread may have already written the rest of the slot; GPU often completes after Present)
                const uint64_t frame_id = g_global_frame_id.load();
                if (frame_id > 0) {
                    const size_t slot = static_cast<size_t>((frame_id - 1) % kFrameDataBufferSize);
                    g_frame_data[slot].gpu_completion_time_ns.store(gpu_completion_time);
                }
            }

            // Sim-to-display latency measurement
            // Check if this callback finishes second (after OnPresentUpdateAfter2)
            LONGLONG sim_start_for_measurement = g_sim_start_ns_for_measurement.load();
            if (sim_start_for_measurement > 0) {
                g_gpu_completion_callback_finished.store(true);
                g_gpu_completion_callback_time_ns.store(gpu_completion_time);

                // If OnPresentUpdateAfter2 was already called, we're finishing second
                if (g_present_update_after2_called.load()) {
                    // Calculate sim-to-display latency
                    LONGLONG latency_new_ns = gpu_completion_time - sim_start_for_measurement;

                    // Smooth the latency with exponential moving average
                    LONGLONG old_latency = g_sim_to_display_latency_ns.load();
                    LONGLONG smoothed_latency = UpdateRollingAverage(latency_new_ns, old_latency);

                    g_sim_to_display_latency_ns.store(smoothed_latency);

                    // Record frame time for Display Timing mode (GPU finished second, this is actual display time)
                    RecordFrameTime(FrameTimeMode::kDisplayTiming);

                    // Calculate GPU late time (GPU finished after Present)
                    LONGLONG present_time = g_present_update_after2_time_ns.load();
                    if (present_time > 0) {
                        LONGLONG gpu_late_new_ns = gpu_completion_time - present_time;
                        LONGLONG old_gpu_late = g_gpu_late_time_ns.load();
                        LONGLONG smoothed_gpu_late = UpdateRollingAverage(gpu_late_new_ns, old_gpu_late);
                        g_gpu_late_time_ns.store(smoothed_gpu_late);
                    }
                } else {
                    // GPU finished first, so late time is 0
                    g_gpu_late_time_ns.store(0);
                }
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
}  // anonymous namespace

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

// GPU completion callback for OpenGL (assumes immediate completion)
void HandleOpenGLGPUCompletion() {
    // For OpenGL, we assume GPU processing finished immediately after wglSwapBuffers
    // This is a reasonable assumption since OpenGL doesn't have the same GPU completion
    // tracking mechanisms as DirectX

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

        // Write GPU completion time into frame data for the frame this GPU work belongs to
        const uint64_t frame_id = g_global_frame_id.load();
        if (frame_id > 0) {
            const size_t slot = static_cast<size_t>((frame_id - 1) % kFrameDataBufferSize);
            g_frame_data[slot].gpu_completion_time_ns.store(gpu_completion_time);
        }
    }

    // Sim-to-display latency measurement
    // Check if this callback finishes second (after OnPresentUpdateAfter2)
    LONGLONG sim_start_for_measurement = g_sim_start_ns_for_measurement.load();
    if (sim_start_for_measurement > 0) {
        g_gpu_completion_callback_finished.store(true);
        g_gpu_completion_callback_time_ns.store(gpu_completion_time);

        // If OnPresentUpdateAfter2 was already called, we're finishing second
        if (g_present_update_after2_called.load()) {
            // Calculate sim-to-display latency
            LONGLONG latency_new_ns = gpu_completion_time - sim_start_for_measurement;

            // Smooth the latency with exponential moving average
            LONGLONG old_latency = g_sim_to_display_latency_ns.load();
            LONGLONG smoothed_latency = UpdateRollingAverage(latency_new_ns, old_latency);

            g_sim_to_display_latency_ns.store(smoothed_latency);

            // Record frame time for Display Timing mode (GPU finished second, this is actual display time)
            RecordFrameTime(FrameTimeMode::kDisplayTiming);

            // Calculate GPU late time (GPU finished after Present)
            LONGLONG present_time = g_present_update_after2_time_ns.load();
            if (present_time > 0) {
                LONGLONG gpu_late_new_ns = gpu_completion_time - present_time;
                LONGLONG old_gpu_late = g_gpu_late_time_ns.load();
                LONGLONG smoothed_gpu_late = UpdateRollingAverage(gpu_late_new_ns, old_gpu_late);
                g_gpu_late_time_ns.store(smoothed_gpu_late);
            }
        } else {
            // GPU finished first, so late time is 0
            g_gpu_late_time_ns.store(0);
        }
    }
}
