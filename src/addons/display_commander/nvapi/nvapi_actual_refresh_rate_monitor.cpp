#include "nvapi_actual_refresh_rate_monitor.hpp"
#include "../globals.hpp"
#include "vrr_status.hpp"

#include <nvapi.h>

#include <atomic>
#include <chrono>
#include <thread>

namespace display_commander::nvapi {

namespace {

constexpr unsigned POLL_MS = 8;
// lastFlipTimeStamp is in 100ns units (Windows FILETIME style). 1e7 units = 1 second.
constexpr double TIMESTAMP_UNITS_PER_SEC = 1e7;

std::atomic<bool> g_active{false};
std::atomic<bool> g_stop_monitor{false};
std::atomic<double> g_actual_refresh_rate_hz{0.0};
std::thread g_monitor_thread;

uint32_t g_prev_count = 0;
uint64_t g_prev_timestamp = 0;
bool g_has_prev = false;

void MonitorThreadFunc() {
    while (!g_stop_monitor.load(std::memory_order_relaxed)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(POLL_MS));

        NvU32 display_id = vrr_status::cached_nvapi_vrr.display_id;
        bool resolved = vrr_status::cached_nvapi_vrr.display_id_resolved;
        if (!resolved || display_id == 0) {
            g_has_prev = false;
            g_actual_refresh_rate_hz.store(0.0, std::memory_order_relaxed);
            continue;
        }

        NV_GET_ADAPTIVE_SYNC_DATA data = {};
        data.version = NV_GET_ADAPTIVE_SYNC_DATA_VER;
        NvAPI_Status st = NvAPI_DISP_GetAdaptiveSyncData(display_id, &data);
        if (st != NVAPI_OK) {
            g_has_prev = false;
            g_actual_refresh_rate_hz.store(0.0, std::memory_order_relaxed);
            continue;
        }

        const uint32_t count = data.lastFlipRefreshCount;
        const uint64_t timestamp = data.lastFlipTimeStamp;

        if (g_has_prev && timestamp > g_prev_timestamp) {
            const uint64_t delta_time = timestamp - g_prev_timestamp;
            const uint32_t delta_count = count - g_prev_count;
            if (delta_time > 0) {
                double window_sec = static_cast<double>(delta_time) / TIMESTAMP_UNITS_PER_SEC;
                if (window_sec > 0.0) {
                    double rate_hz = static_cast<double>(delta_count) / window_sec;
                    // Sanity: typical range 24–240 Hz
                    if (rate_hz >= 1.0 && rate_hz <= 500.0) {
                        g_actual_refresh_rate_hz.store(rate_hz, std::memory_order_relaxed);
                    }
                }
            }
        }

        g_prev_count = count;
        g_prev_timestamp = timestamp;
        g_has_prev = true;
    }
}

}  // namespace

void StartNvapiActualRefreshRateMonitoring() {
    if (g_active.exchange(true)) {
        return;
    }
    g_stop_monitor.store(false);
    g_has_prev = false;
    g_actual_refresh_rate_hz.store(0.0, std::memory_order_relaxed);
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
}

bool IsNvapiActualRefreshRateMonitoringActive() {
    return g_active.load(std::memory_order_relaxed);
}

double GetNvapiActualRefreshRateHz() {
    return g_actual_refresh_rate_hz.load(std::memory_order_relaxed);
}

}  // namespace display_commander::nvapi
