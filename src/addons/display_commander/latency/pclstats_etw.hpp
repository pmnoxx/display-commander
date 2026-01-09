#pragma once

#include <cstdint>
#include <vector>

namespace latency::pclstats_etw {

// User toggle (UI / settings). Default should be OFF (handled by settings default).
void SetUserEnabled(bool enabled);

// Cleanup (safe to call multiple times).
void Shutdown();

// Emit a PCLStats marker event (ETW) for a given NV marker type and frame id.
// Marker values follow NV_LATENCY_MARKER_TYPE numbering (e.g. SIMULATION_START=0, PC_LATENCY_PING=8).
void EmitMarker(uint32_t marker, uint64_t frame_id);

// Ping signal consumption helper.
// Returns true at most once per ping interval when enabled; intended to be checked on SIMULATION_START.
bool ConsumePingSignal();

// Marker history entry
struct MarkerHistoryEntry {
    uint32_t marker_type;
    uint64_t frame_id;
    uint64_t timestamp_ns; // Time when marker was emitted
};

// Debug/statistics interface
struct DebugStats {
    bool user_enabled;
    bool provider_registered;
    bool etw_enabled;
    bool ping_thread_running;
    bool native_reflex_detected;
    uint64_t events_emitted;
    uint64_t ping_signals_generated;
    uint64_t ping_signals_consumed;
    uint32_t last_marker_type;
    uint64_t last_frame_id;
    // Per-marker-type counts (indexed by marker type value)
    uint64_t marker_counts[16]; // Support up to marker type 15
    // Lifecycle events
    uint64_t init_events_sent;
    uint64_t shutdown_events_sent;
    uint64_t flags_events_sent;
    uint64_t last_init_event_time_ns; // 0 if never sent
    // Registration status
    uint32_t registration_status; // ERROR_SUCCESS (0) if registered successfully
};

DebugStats GetDebugStats();

// Get marker history (first 100 markers)
// Returns vector of marker history entries, ordered chronologically
std::vector<MarkerHistoryEntry> GetMarkerHistory();

// Test function: manually emit a test marker
void EmitTestMarker();

// Re-emit PCLStatsInit event (for debugging - helps NVIDIA overlay discover provider)
void ReEmitInitEvent();

} // namespace latency::pclstats_etw


