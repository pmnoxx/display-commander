#include "nvapi_actual_refresh_rate_monitor.hpp"
#include "../globals.hpp"
#include "../settings/main_tab_settings.hpp"
#include "vrr_status.hpp"

#include <nvapi.h>

#include <array>
#include <atomic>
#include <chrono>
#include <thread>

namespace display_commander::nvapi {

namespace {

// 1 ms needed for FG/high fps: lastFlipRefreshCount is per app frame; at 60 fps an 8 ms poll
// often sees delta_count == 0.
constexpr unsigned POLL_MS = 1;
// lastFlipTimeStamp is in 100ns units (Windows FILETIME style). 1e7 units = 1 second.
constexpr double TIMESTAMP_UNITS_PER_SEC = 1e7;
constexpr size_t RECENT_SAMPLES_SIZE = 256;
// After this many consecutive NvAPI_DISP_GetAdaptiveSyncData failures, UI shows a warning.
constexpr uint32_t FAILURE_WARNING_THRESHOLD = 1000;

std::atomic<bool> g_active{false};
std::atomic<uint32_t> g_consecutive_failures{0};
std::atomic<bool> g_stop_monitor{false};
std::atomic<double> g_actual_refresh_rate_hz{0.0};
std::thread g_monitor_thread;

// Ring buffer of recent actual refresh rate samples (Hz) for the time graph
std::array<double, RECENT_SAMPLES_SIZE> g_recent_samples{};
std::atomic<size_t> g_recent_write_index{0};
std::atomic<size_t> g_recent_count{0};

uint32_t g_prev_count = 0;
uint64_t g_prev_timestamp = 0;
bool g_has_prev = false;

void PushSample(double rate_hz) {
    size_t idx = g_recent_write_index.load(std::memory_order_relaxed) % RECENT_SAMPLES_SIZE;
    g_recent_samples[idx] = rate_hz;
    g_recent_write_index.store((idx + 1) % RECENT_SAMPLES_SIZE, std::memory_order_release);
    size_t c = g_recent_count.load(std::memory_order_relaxed);
    if (c < RECENT_SAMPLES_SIZE) {
        g_recent_count.store(c + 1, std::memory_order_release);
    }
}

void MonitorThreadFunc() {
    while (!g_stop_monitor.load(std::memory_order_relaxed)) {
        if (settings::g_mainTabSettings.show_refresh_rate_frame_times.GetValue()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(4));
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(POLL_MS));
        }

        std::shared_ptr<::nvapi::VrrStatus> vrr = vrr_status::cached_nvapi_vrr.load();
        NvU32 display_id = vrr ? vrr->display_id : 0;
        bool resolved = vrr && vrr->display_id_resolved;
        if (!resolved || display_id == 0) {
            g_has_prev = false;
            g_actual_refresh_rate_hz.store(0.0, std::memory_order_relaxed);
            g_consecutive_failures.store(0, std::memory_order_relaxed);
            continue;
        }

        NV_GET_ADAPTIVE_SYNC_DATA data = {};
        data.version = NV_GET_ADAPTIVE_SYNC_DATA_VER;
        NvAPI_Status st = NvAPI_DISP_GetAdaptiveSyncData(display_id, &data);
        if (st != NVAPI_OK) {
            uint32_t prev = g_consecutive_failures.load(std::memory_order_relaxed);
            if (prev < FAILURE_WARNING_THRESHOLD) {
                g_consecutive_failures.store(prev + 1, std::memory_order_release);
            }
            g_has_prev = false;
            g_actual_refresh_rate_hz.store(0.0, std::memory_order_relaxed);
            continue;
        }

        g_consecutive_failures.store(0, std::memory_order_release);

        const uint32_t count = data.lastFlipRefreshCount;
        const uint64_t timestamp = data.lastFlipTimeStamp;

        if (g_has_prev && timestamp > g_prev_timestamp) {
            const uint64_t delta_time = timestamp - g_prev_timestamp;
            const uint32_t delta_count = count - g_prev_count;
            if (delta_time > 0 && delta_count > 0) {
                double window_sec = static_cast<double>(delta_time) / TIMESTAMP_UNITS_PER_SEC;
                if (window_sec > 0.0) {
                    double rate_hz = static_cast<double>(delta_count) / window_sec;
                    // Sanity: typical range 24–240 Hz
                    if (rate_hz >= 1.0 && rate_hz <= 1000.0) {
                        g_actual_refresh_rate_hz.store(rate_hz, std::memory_order_relaxed);
                        for (size_t i = 0; i < (std::min)(2U, delta_count); i++) {
                            PushSample(rate_hz);
                        }
                    }
                }
            }
        }

        g_prev_count = count;
        g_prev_timestamp = timestamp;
        g_has_prev = true;
    }
}

size_t GetRecentCount() { return g_recent_count.load(std::memory_order_acquire); }

double GetRecentSampleAtLogical(size_t logical_index) {
    size_t count = g_recent_count.load(std::memory_order_acquire);
    size_t write_index = g_recent_write_index.load(std::memory_order_acquire);
    if (logical_index >= count) {
        return 0.0;
    }
    size_t physical;
    if (count < RECENT_SAMPLES_SIZE) {
        physical = logical_index;
    } else {
        physical = (write_index + logical_index) % RECENT_SAMPLES_SIZE;
    }
    return g_recent_samples[physical];
}

}  // namespace

void StartNvapiActualRefreshRateMonitoring() {
    if (g_active.exchange(true)) {
        return;
    }
    g_stop_monitor.store(false);
    g_has_prev = false;
    g_actual_refresh_rate_hz.store(0.0, std::memory_order_relaxed);
    g_consecutive_failures.store(0, std::memory_order_release);
    g_monitor_thread = std::thread(MonitorThreadFunc);
}

void StopNvapiActualRefreshRateMonitoring() {
    if (!g_active.exchange(false)) {
        return;
    }
    g_stop_monitor.store(true);
    if (g_monitor_thread.joinable()) {
        g_monitor_thread.join();
    }
    g_actual_refresh_rate_hz.store(0.0, std::memory_order_relaxed);
    g_recent_count.store(0, std::memory_order_release);
    g_consecutive_failures.store(0, std::memory_order_release);
}

bool IsNvapiActualRefreshRateMonitoringActive() { return g_active.load(std::memory_order_relaxed); }

bool IsNvapiGetAdaptiveSyncDataFailingRepeatedly() {
    return g_consecutive_failures.load(std::memory_order_acquire) >= FAILURE_WARNING_THRESHOLD;
}

double GetNvapiActualRefreshRateHz() { return g_actual_refresh_rate_hz.load(std::memory_order_relaxed); }

size_t GetNvapiActualRefreshRateRecentCount() { return GetRecentCount(); }

double GetNvapiActualRefreshRateRecentSampleAt(size_t logical_index) { return GetRecentSampleAtLogical(logical_index); }

}  // namespace display_commander::nvapi
