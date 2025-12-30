#include "perf_measurement.hpp"

namespace perf_measurement {
namespace {

constexpr std::size_t kMetricCount = static_cast<std::size_t>(Metric::Count);

std::array<std::atomic<std::uint64_t>, kMetricCount> g_total_ns = {};
std::array<std::atomic<std::uint64_t>, kMetricCount> g_samples = {};
std::array<std::atomic<std::uint64_t>, kMetricCount> g_last_ns = {};
std::array<std::atomic<std::uint64_t>, kMetricCount> g_max_ns = {};

inline std::size_t ToIndex(Metric metric) {
    return static_cast<std::size_t>(metric);
}

} // namespace

void ScopedTimer::Record(Metric metric, std::uint64_t dt_ns) {
    const std::size_t idx = ToIndex(metric);
    if (idx >= kMetricCount) {
        return;
    }

    g_last_ns[idx].store(dt_ns, std::memory_order_relaxed);
    g_total_ns[idx].fetch_add(dt_ns, std::memory_order_relaxed);
    g_samples[idx].fetch_add(1, std::memory_order_relaxed);

    // Update max with CAS loop (relaxed is fine for stats)
    std::uint64_t current_max = g_max_ns[idx].load(std::memory_order_relaxed);
    while (dt_ns > current_max &&
           !g_max_ns[idx].compare_exchange_weak(current_max, dt_ns, std::memory_order_relaxed, std::memory_order_relaxed)) {
        // current_max updated by compare_exchange_weak
    }
}

Snapshot GetSnapshot(Metric metric) {
    const std::size_t idx = ToIndex(metric);
    if (idx >= kMetricCount) {
        return {};
    }

    Snapshot s;
    s.samples = g_samples[idx].load(std::memory_order_relaxed);
    s.total_ns = g_total_ns[idx].load(std::memory_order_relaxed);
    s.last_ns = g_last_ns[idx].load(std::memory_order_relaxed);
    s.max_ns = g_max_ns[idx].load(std::memory_order_relaxed);
    return s;
}

void ResetAll() {
    for (std::size_t i = 0; i < kMetricCount; ++i) {
        g_total_ns[i].store(0, std::memory_order_relaxed);
        g_samples[i].store(0, std::memory_order_relaxed);
        g_last_ns[i].store(0, std::memory_order_relaxed);
        g_max_ns[i].store(0, std::memory_order_relaxed);
    }
}

} // namespace perf_measurement


