#include "pclstats_etw.hpp"
#include "pclstats_logger.hpp"

#include "../globals.hpp"
#include "../utils/logging.hpp"
#include "../utils/timing.hpp"

#include <evntrace.h>
#include <TraceLoggingProvider.h>
#include <windows.h>

#include <atomic>
#include <mutex>
#include <random>
#include <thread>
#include <vector>

// Namespace alias for timing
namespace timing = utils;

// This implements a minimal Special K-style PCLStats TraceLogging provider:
// - Provider name: "PCLStatsTraceLoggingProvider"
// - Event: "PCLStatsEvent" with fields { Marker: UInt32, FrameID: UInt64 }
//
// NVIDIA tooling/overlays that listen for PCLStats typically expect this provider/event schema.

namespace latency::pclstats_etw {

TRACELOGGING_DEFINE_PROVIDER(g_hPCLStatsComponentProvider, "PCLStatsTraceLoggingProvider",
                             (0x0d216f06, 0x82a6, 0x4d49, 0xbc, 0x4f, 0x8f, 0x38, 0xae, 0x56, 0xef, 0xab));

static std::atomic<bool> g_user_enabled{false};
static std::atomic<bool> g_registered{false};
static std::atomic<bool> g_etw_enabled{false};

static std::atomic<uint32_t> g_ping_signal{0};  // 1 => signaled
static std::atomic<bool> g_stop_thread{false};
static std::thread g_ping_thread;

// Statistics
static std::atomic<uint64_t> g_events_emitted{0};
static std::atomic<uint64_t> g_ping_signals_generated{0};
static std::atomic<uint64_t> g_ping_signals_consumed{0};
static std::atomic<uint32_t> g_last_marker_type{0};
static std::atomic<uint64_t> g_last_frame_id{0};
// Per-marker-type counters (indexed by marker type, max 15)
static std::atomic<uint64_t> g_marker_counts[16] = {};
// Marker history (first 100 markers)
static constexpr size_t MAX_MARKER_HISTORY = 100;
static std::vector<MarkerHistoryEntry> g_marker_history;
static std::mutex g_marker_history_mutex;
static std::atomic<bool> g_history_full{false};  // True when we've collected 100 markers
// Lifecycle event tracking
static std::atomic<uint64_t> g_init_events_sent{0};
static std::atomic<uint64_t> g_shutdown_events_sent{0};
static std::atomic<uint64_t> g_flags_events_sent{0};
static std::atomic<uint64_t> g_last_init_event_time_ns{0};
static std::atomic<uint32_t> g_registration_status{0xFFFFFFFF};  // Invalid status until registered

static bool IsAllowed() {
    // Mirror Special-K behavior: don't compete with a Reflex-native game.
    if (g_native_reflex_detected.load(std::memory_order_acquire)) {
        return false;
    }
    return g_user_enabled.load(std::memory_order_acquire);
}

static void WINAPI ProviderCb(LPCGUID, ULONG control_code, UCHAR, ULONGLONG, ULONGLONG, PEVENT_FILTER_DESCRIPTOR,
                              PVOID) {
    switch (control_code) {
        case EVENT_CONTROL_CODE_ENABLE_PROVIDER:
            g_etw_enabled.store(true, std::memory_order_release);
            // Re-emit PCLStatsInit when consumer enables provider (helps with discovery)
            if (g_registered.load(std::memory_order_acquire)) {
                TraceLoggingWrite(g_hPCLStatsComponentProvider, "PCLStatsInit");
                g_init_events_sent.fetch_add(1, std::memory_order_relaxed);
                g_last_init_event_time_ns.store(timing::get_now_ns(), std::memory_order_relaxed);
                LogInfo("[PCLStats] PCLStatsInit event re-emitted on ETW enable (count: %llu)",
                        g_init_events_sent.load(std::memory_order_relaxed));
            }
            break;
        case EVENT_CONTROL_CODE_DISABLE_PROVIDER: g_etw_enabled.store(false, std::memory_order_release); break;
        case EVENT_CONTROL_CODE_CAPTURE_STATE:
            // Emit PCLStatsFlags event (Special K style) - helps with state capture
            TraceLoggingWrite(g_hPCLStatsComponentProvider, "PCLStatsFlags", TraceLoggingUInt32(0, "Flags"));
            g_flags_events_sent.fetch_add(1, std::memory_order_relaxed);
            break;
        default: break;
    }
}

static void PingThreadMain() {
    // 100-300ms random interval (Special K style).
    std::minstd_rand rng(static_cast<uint32_t>(GetTickCount()));
    std::uniform_int_distribution<int> dist(100, 300);

    while (!g_stop_thread.load(std::memory_order_acquire)) {
        const int ms = dist(rng);
        // Use Win32 Sleep to avoid std::condition_variable (mutex).
        ::Sleep(static_cast<DWORD>(ms));

        if (g_stop_thread.load(std::memory_order_acquire)) break;

        if (!IsAllowed()) continue;

        // Only signal pings when a consumer actually enabled the provider (min overhead).
        if (!g_etw_enabled.load(std::memory_order_acquire)) continue;

        g_ping_signal.store(1u, std::memory_order_release);
        g_ping_signals_generated.fetch_add(1, std::memory_order_relaxed);
    }
}

static void EnsureStarted() {
    if (g_registered.load(std::memory_order_acquire)) return;

    // Register provider with enable/disable callback.
    ULONG status = TraceLoggingRegisterEx(g_hPCLStatsComponentProvider, ProviderCb, nullptr);
    g_registration_status.store(status, std::memory_order_release);
    if (status == ERROR_SUCCESS) {
        g_registered.store(true, std::memory_order_release);
        LogInfo("[PCLStats] Provider registered successfully (status: %lu)", status);
    } else {
        LogWarn("[PCLStats] Provider registration failed (status: %lu)", status);
        return;  // Don't proceed if registration failed
    }

    // Emit PCLStatsInit event (Special K style) - this helps NVIDIA overlay discover the provider
    TraceLoggingWrite(g_hPCLStatsComponentProvider, "PCLStatsInit");
    g_init_events_sent.fetch_add(1, std::memory_order_relaxed);
    g_last_init_event_time_ns.store(timing::get_now_ns(), std::memory_order_relaxed);
    LogInfo("[PCLStats] PCLStatsInit event emitted (count: %llu)", g_init_events_sent.load(std::memory_order_relaxed));

    // Start ping thread once.
    if (!g_ping_thread.joinable()) {
        g_stop_thread.store(false, std::memory_order_release);
        g_ping_thread = std::thread(PingThreadMain);
    }
}

static void StopAndUnregister() {
    // Stop ping thread
    g_stop_thread.store(true, std::memory_order_release);
    if (g_ping_thread.joinable()) {
        g_ping_thread.join();
    }

    // Reset ping state
    g_ping_signal.store(0u, std::memory_order_release);

    if (g_registered.exchange(false, std::memory_order_acq_rel)) {
        // Emit PCLStatsShutdown event (Special K style)
        TraceLoggingWrite(g_hPCLStatsComponentProvider, "PCLStatsShutdown");
        g_shutdown_events_sent.fetch_add(1, std::memory_order_relaxed);
        TraceLoggingUnregister(g_hPCLStatsComponentProvider);
    }
    g_etw_enabled.store(false, std::memory_order_release);
}

void SetUserEnabled(bool enabled) {
    const bool prev = g_user_enabled.exchange(enabled, std::memory_order_acq_rel);
    if (prev == enabled) return;

    if (enabled) {
        EnsureStarted();
        LogInfo("[PCLStats] ETW marker generation enabled (user)");
    } else {
        StopAndUnregister();
        LogInfo("[PCLStats] ETW marker generation disabled (user)");
    }
}

void Shutdown() { SetUserEnabled(false); }

void EmitMarker(uint32_t marker, uint64_t frame_id) {
    // Get timestamp before any early returns
    const uint64_t timestamp_ns = timing::get_now_ns();

    // Fast path: avoid TraceLoggingWrite unless enabled by both user and ETW consumer.
    if (!IsAllowed()) return;
    if (!g_etw_enabled.load(std::memory_order_acquire)) return;

    TraceLoggingWrite(g_hPCLStatsComponentProvider, "PCLStatsEvent", TraceLoggingUInt32(marker, "Marker"),
                      TraceLoggingUInt64(frame_id, "FrameID"));

    // Update statistics
    g_events_emitted.fetch_add(1, std::memory_order_relaxed);
    g_last_marker_type.store(marker, std::memory_order_relaxed);
    g_last_frame_id.store(frame_id, std::memory_order_relaxed);

    // Update per-marker-type counter
    if (marker < 16) {
        g_marker_counts[marker].fetch_add(1, std::memory_order_relaxed);
    }

    // Record in history (first 100 markers only)
    if (!g_history_full.load(std::memory_order_acquire)) {
        std::lock_guard<std::mutex> lock(g_marker_history_mutex);
        if (g_marker_history.size() < MAX_MARKER_HISTORY) {
            MarkerHistoryEntry entry;
            entry.marker_type = marker;
            entry.frame_id = frame_id;
            entry.timestamp_ns = timestamp_ns;
            g_marker_history.push_back(entry);

            if (g_marker_history.size() >= MAX_MARKER_HISTORY) {
                g_history_full.store(true, std::memory_order_release);
            }
        }
    }

    // Log to file if enabled (matches the marker ID used for NVIDIA overlay)
    // This acts as a "listener" that captures all PCLStats events
    if (latency::pclstats_logger::IsPCLLoggingEnabled()) {
        latency::pclstats_logger::LogMarker(marker, frame_id, timestamp_ns);
    }
}

DebugStats GetDebugStats() {
    DebugStats stats{};
    stats.user_enabled = g_user_enabled.load(std::memory_order_acquire);
    stats.provider_registered = g_registered.load(std::memory_order_acquire);
    stats.etw_enabled = g_etw_enabled.load(std::memory_order_acquire);
    stats.ping_thread_running = g_ping_thread.joinable() && !g_stop_thread.load(std::memory_order_acquire);
    stats.native_reflex_detected = g_native_reflex_detected.load(std::memory_order_acquire);
    stats.events_emitted = g_events_emitted.load(std::memory_order_acquire);
    stats.ping_signals_generated = g_ping_signals_generated.load(std::memory_order_acquire);
    stats.ping_signals_consumed = g_ping_signals_consumed.load(std::memory_order_acquire);
    stats.last_marker_type = g_last_marker_type.load(std::memory_order_acquire);
    stats.last_frame_id = g_last_frame_id.load(std::memory_order_acquire);

    // Copy per-marker-type counts
    for (int i = 0; i < 16; i++) {
        stats.marker_counts[i] = g_marker_counts[i].load(std::memory_order_acquire);
    }

    // Copy lifecycle event stats
    stats.init_events_sent = g_init_events_sent.load(std::memory_order_acquire);
    stats.shutdown_events_sent = g_shutdown_events_sent.load(std::memory_order_acquire);
    stats.flags_events_sent = g_flags_events_sent.load(std::memory_order_acquire);
    stats.last_init_event_time_ns = g_last_init_event_time_ns.load(std::memory_order_acquire);
    stats.registration_status = g_registration_status.load(std::memory_order_acquire);

    return stats;
}

void EmitTestMarker() {
    static uint64_t test_frame_id = 0;
    test_frame_id++;
    EmitMarker(0, test_frame_id);  // SIMULATION_START = 0
    LogInfo("[PCLStats] Test marker emitted: Marker=0 (SIMULATION_START), FrameID=%llu", test_frame_id);
}

void ReEmitInitEvent() {
    if (g_registered.load(std::memory_order_acquire)) {
        TraceLoggingWrite(g_hPCLStatsComponentProvider, "PCLStatsInit");
        g_init_events_sent.fetch_add(1, std::memory_order_relaxed);
        g_last_init_event_time_ns.store(timing::get_now_ns(), std::memory_order_relaxed);
        LogInfo("[PCLStats] PCLStatsInit event manually re-emitted (count: %llu)",
                g_init_events_sent.load(std::memory_order_relaxed));
    } else {
        LogWarn("[PCLStats] Cannot re-emit PCLStatsInit: provider not registered");
    }
}

bool ConsumePingSignal() {
    if (!IsAllowed()) return false;
    if (!g_etw_enabled.load(std::memory_order_acquire)) return false;

    uint32_t expected = 1u;
    bool consumed = g_ping_signal.compare_exchange_strong(expected, 0u, std::memory_order_acq_rel);
    if (consumed) {
        g_ping_signals_consumed.fetch_add(1, std::memory_order_relaxed);
    }
    return consumed;
}

std::vector<MarkerHistoryEntry> GetMarkerHistory() {
    std::lock_guard<std::mutex> lock(g_marker_history_mutex);
    return g_marker_history;  // Return copy
}

}  // namespace latency::pclstats_etw
