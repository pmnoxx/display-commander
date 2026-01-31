#pragma once

#include "../globals.hpp"

#include <atomic>
#include <thread>
#include <windows.h>
#include <evntrace.h>
#include <string>
#include <cstdint>
#include <vector>

namespace presentmon {

// ETW event-type summary (cached schema for exploration/debug)
struct PresentMonEventTypeSummary {
    std::string provider_guid;
    std::string provider_name;
    uint16_t event_id;
    uint16_t task;
    uint8_t opcode;
    uint8_t level;
    uint64_t keyword;
    std::string event_name;
    std::string props; // comma-separated property names (or name=? markers)
    uint64_t count;
};

// DWM "flip compatibility" snapshot (e.g. event types exposing IsDirectFlipCompatible)
struct PresentMonFlipCompatibility {
    bool is_valid = false;
    uint64_t last_update_time_ns = 0;

    uint64_t surface_luid = 0;
    uint32_t surface_width = 0;
    uint32_t surface_height = 0;
    uint32_t pixel_format = 0;
    uint32_t flags = 0;
    uint32_t color_space = 0;

    bool is_direct_flip_compatible = false;
    bool is_advanced_direct_flip_compatible = false;
    bool is_overlay_compatible = false;
    bool is_overlay_required = false;
    bool no_overlapping_content = false;
};

// Per-surface compatibility summary (for "recent surfaces" UI)
struct PresentMonSurfaceCompatibilitySummary {
    uint64_t surface_luid = 0;
    uint64_t last_update_time_ns = 0;
    uint64_t count = 0;
    uint64_t hwnd = 0; // 0 if unknown

    uint32_t surface_width = 0;
    uint32_t surface_height = 0;
    uint32_t pixel_format = 0;
    uint32_t flags = 0;
    uint32_t color_space = 0;

    bool is_direct_flip_compatible = false;
    bool is_advanced_direct_flip_compatible = false;
    bool is_overlay_compatible = false;
    bool is_overlay_required = false;
    bool no_overlapping_content = false;
};

// PresentMon flip state information
struct PresentMonFlipState {
    DxgiBypassMode flip_mode;
    bool is_valid;
    uint64_t last_update_time;  // QPC timestamp
    std::string present_mode_str;  // e.g., "Hardware Independent Flip", "Composed Flip"
    std::string debug_info;  // Additional debug information
};

// PresentMon debug information
struct PresentMonDebugInfo {
    bool is_running;
    bool thread_started;
    bool etw_session_active;
    std::string thread_status;
    std::string etw_session_status;
    std::string etw_session_name;
    std::string last_error;
    uint64_t events_processed;
    uint64_t events_processed_for_current_pid;
    uint64_t events_lost;
    uint64_t last_event_time;
    uint32_t last_event_pid;
    std::string last_provider;
    uint16_t last_event_id;
    std::string last_present_mode_value;
    std::string last_provider_name;
    std::string last_event_name;
    std::string last_interesting_props;

    // Per-provider counters (graphics-relevant)
    uint64_t events_dxgkrnl;
    uint64_t events_dxgi;
    uint64_t events_dwm;

    // Last graphics-relevant event info (DxgKrnl/DXGI/DWM)
    std::string last_graphics_provider;
    uint16_t last_graphics_event_id;
    uint32_t last_graphics_event_pid;
    std::string last_graphics_provider_name;
    std::string last_graphics_event_name;
    std::string last_graphics_props;

    // List of ETW sessions starting with "DC_" prefix
    std::vector<std::string> dc_etw_sessions;
};

// PresentMon manager for ETW-based presentation tracking
// Similar to Special-K's PresentMon integration
class PresentMonManager {
public:
    PresentMonManager();
    ~PresentMonManager();

    // Start PresentMon worker thread
    void StartWorker();

    // Stop PresentMon worker thread
    void StopWorker();

    // Check if PresentMon is running
    bool IsRunning() const;

    // Check if PresentMon is needed (based on system/game state)
    bool IsNeeded() const;

    // Get flip state from PresentMon (returns true if valid data available)
    bool GetFlipState(PresentMonFlipState& flip_state) const;

    // Get debug information
    void GetDebugInfo(PresentMonDebugInfo& debug_info) const;

    // Snapshot of cached ETW event types (for UI exploration).
    // Best-effort / lock-free snapshot: may be slightly inconsistent while ETW thread updates.
    void GetEventTypeSummaries(std::vector<PresentMonEventTypeSummary>& out, bool graphics_only) const;

    // Latest DWM flip-compatibility snapshot (best-effort)
    bool GetFlipCompatibility(PresentMonFlipCompatibility& out) const;

    // Recent DWM flip-compatibility surfaces (best-effort)
    void GetRecentFlipCompatibilitySurfaces(std::vector<PresentMonSurfaceCompatibilitySummary>& out, uint64_t within_ms) const;

    // Get list of ETW sessions starting with specified prefix (e.g., "DC_")
    static void GetEtwSessionsWithPrefix(const wchar_t* prefix, std::vector<std::string>& out_session_names);

    // Stop ETW session by name (public for UI cleanup)
    static void StopEtwSessionByName(const wchar_t* session_name);

    // Stop all ETW sessions starting with DC_ (used at start to clear the deck before creating ours)
    static void StopAllDcEtwSessions();

    // Stop all DC_ ETW sessions except the one with the given name (used when no events for 10s to clear conflicting sessions)
    static void StopOtherDcEtwSessions(const wchar_t* our_session_name);

    // Update flip state (called from ETW consumer thread when implemented)
    void UpdateFlipState(DxgiBypassMode mode, const std::string& present_mode_str, const std::string& debug_info = "");

    // Update debug information
    void UpdateDebugInfo(const std::string& thread_status, const std::string& etw_status,
                        const std::string& error = "", uint64_t events_processed = 0,
                        uint64_t events_lost = 0);

private:
    // Worker thread function
    static void WorkerThread(PresentMonManager* manager);

    // Cleanup thread: every 10s, if no events seen for 10s, stop all other DC_ ETW sessions
    static void CleanupThread(PresentMonManager* manager);

    // PresentMon ETW main loop (our own implementation; no Special-K code)
    int PresentMonMain();

    // Stop ETW session if running
    void RequestStopEtw();

    // Query existing ETW session by name and get its handle
    // Returns true if session exists and handle was retrieved, false otherwise
    static bool QueryEtwSessionByName(const wchar_t* session_name, TRACEHANDLE& out_handle);

    // ETW event callback (static trampoline)
    static void WINAPI EtwEventRecordCallback(PEVENT_RECORD event_record);

    // ETW event handler
    void OnEtwEvent(PEVENT_RECORD event_record);

    // Event type cache (fixed-size, lock-free-ish; updated from ETW thread)
    struct EventTypeEntry {
        std::atomic<uint64_t> key_hash{0}; // 0 = empty
        std::atomic<uint64_t> count{0};
        uint16_t event_id{0};
        uint16_t task{0};
        uint8_t opcode{0};
        uint8_t level{0};
        uint64_t keyword{0};

        std::atomic<std::string*> provider_guid{nullptr};
        std::atomic<std::string*> provider_name{nullptr};
        std::atomic<std::string*> event_name{nullptr};
        std::atomic<std::string*> props{nullptr};
        std::atomic<uint64_t> last_schema_update_ns{0};
    };

    static constexpr size_t k_event_type_cache_size = 256;
    EventTypeEntry m_event_types[k_event_type_cache_size];

    void TrackEventType(PEVENT_RECORD event_record, bool is_graphics_provider);

    void UpdateFlipCompatibilityFromDwmEvent(PEVENT_RECORD event_record);
    void UpdateSurfaceWindowMappingFromEvent(PEVENT_RECORD event_record);

    // Thread handles
    std::thread m_worker_thread;
    std::thread m_cleanup_thread;
    std::atomic<bool> m_running;
    std::atomic<bool> m_should_stop;

    // Flip state tracking (thread-safe)
    mutable std::atomic<DxgiBypassMode> m_flip_mode;
    mutable std::atomic<bool> m_flip_state_valid;
    mutable std::atomic<uint64_t> m_flip_state_update_time;
    mutable std::atomic<std::string*> m_present_mode_str;
    mutable std::atomic<std::string*> m_debug_info_str;

    // Debug info tracking
    mutable std::atomic<bool> m_thread_started;
    mutable std::atomic<bool> m_etw_session_active;
    mutable std::atomic<std::string*> m_thread_status;
    mutable std::atomic<std::string*> m_etw_session_status;
    mutable std::atomic<std::string*> m_last_error;
    mutable std::atomic<uint64_t> m_events_processed;
    mutable std::atomic<uint64_t> m_events_processed_for_current_pid;
    mutable std::atomic<uint64_t> m_events_lost;
    mutable std::atomic<uint64_t> m_last_event_time;
    mutable std::atomic<uint32_t> m_last_event_pid;

    // Last-seen event info (for debugging)
    mutable std::atomic<std::string*> m_last_provider;
    mutable std::atomic<uint16_t> m_last_event_id;
    mutable std::atomic<std::string*> m_last_present_mode_value;
    mutable std::atomic<std::string*> m_last_provider_name;
    mutable std::atomic<std::string*> m_last_event_name;
    mutable std::atomic<std::string*> m_last_interesting_props;
    mutable std::atomic<uint64_t> m_last_schema_update_time_ns;

    // Per-provider counters (graphics-relevant)
    mutable std::atomic<uint64_t> m_events_dxgkrnl;
    mutable std::atomic<uint64_t> m_events_dxgi;
    mutable std::atomic<uint64_t> m_events_dwm;

    // Last graphics-relevant event info
    mutable std::atomic<std::string*> m_last_graphics_provider;
    mutable std::atomic<uint16_t> m_last_graphics_event_id;
    mutable std::atomic<uint32_t> m_last_graphics_event_pid;
    mutable std::atomic<std::string*> m_last_graphics_provider_name;
    mutable std::atomic<std::string*> m_last_graphics_event_name;
    mutable std::atomic<std::string*> m_last_graphics_props;
    mutable std::atomic<uint64_t> m_last_graphics_schema_update_time_ns;

    // DWM flip-compatibility state (thread-safe)
    mutable std::atomic<bool> m_flip_compat_valid;
    mutable std::atomic<uint64_t> m_flip_compat_last_update_ns;
    mutable std::atomic<uint64_t> m_flip_compat_surface_luid;
    mutable std::atomic<uint32_t> m_flip_compat_surface_width;
    mutable std::atomic<uint32_t> m_flip_compat_surface_height;
    mutable std::atomic<uint32_t> m_flip_compat_pixel_format;
    mutable std::atomic<uint32_t> m_flip_compat_flags;
    mutable std::atomic<uint32_t> m_flip_compat_color_space;
    mutable std::atomic<uint32_t> m_flip_compat_is_direct;
    mutable std::atomic<uint32_t> m_flip_compat_is_adv_direct;
    mutable std::atomic<uint32_t> m_flip_compat_is_overlay;
    mutable std::atomic<uint32_t> m_flip_compat_is_overlay_required;
    mutable std::atomic<uint32_t> m_flip_compat_no_overlapping;

    // Recent surface cache (fixed-size, lock-free-ish)
    struct SurfaceEntry {
        std::atomic<uint64_t> key_hash{0}; // 0 = empty
        std::atomic<uint64_t> surface_luid{0};
        std::atomic<uint64_t> last_update_ns{0};
        std::atomic<uint64_t> count{0};
        std::atomic<uint64_t> hwnd{0};

        std::atomic<uint32_t> surface_width{0};
        std::atomic<uint32_t> surface_height{0};
        std::atomic<uint32_t> pixel_format{0};
        std::atomic<uint32_t> flags{0};
        std::atomic<uint32_t> color_space{0};

        std::atomic<uint32_t> is_direct{0};
        std::atomic<uint32_t> is_adv_direct{0};
        std::atomic<uint32_t> is_overlay{0};
        std::atomic<uint32_t> is_overlay_required{0};
        std::atomic<uint32_t> no_overlapping{0};
    };

    static constexpr size_t k_surface_cache_size = 256;
    SurfaceEntry m_surface_cache[k_surface_cache_size];

    // ETW handles (stored as integers for atomics)
    std::atomic<uint64_t> m_etw_session_handle;
    std::atomic<uint64_t> m_etw_trace_handle;

    // Session name (constant after StartWorker)
    wchar_t m_session_name[64];

    // Provider GUIDs (set once before ProcessTrace begins)
    GUID m_guid_dxgkrnl;
    GUID m_guid_dxgi;
    GUID m_guid_dwm;
    bool m_have_dxgkrnl;
    bool m_have_dxgi;
    bool m_have_dwm;
};

// Global instance
extern PresentMonManager g_presentMonManager;

}  // namespace presentmon

