#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>

namespace detour_call_tracker {

// Value for the "latest call" map: most recent call of a given (function_name, line) site
struct LatestCallValue {
    uint64_t timestamp_ns{0};
    bool destroyed{true};       // true once guard destructor ran
    size_t in_progress_count{0};  // incremented in guard ctor, decremented in guard dtor; >0 => possible crash without cleanup
};

// Simple output structure for reading detour calls
struct DetourCallInfo {
    const char* function_name;
    uint64_t timestamp_ns;
};

// Circular buffer entry structure with atomic fields
// Each field is atomic to allow lock-free concurrent access
struct DetourCallEntry {
    std::atomic<const char*> function_name{nullptr};
    std::atomic<uint64_t> timestamp_ns{0};
};

// Record a detour function call (thread-safe, lock-free)
// Uses relaxed memory ordering for maximum performance
// Function name and line number are combined into a formatted string
void RecordDetourCall(const char* function_name, int line_number, uint64_t timestamp_ns);

// Get recent detour calls (up to max_count, most recent first)
// Returns number of entries filled
// Thread-safe but may miss entries being written concurrently (acceptable for crash reporting)
size_t GetRecentDetourCalls(DetourCallInfo* out_entries, size_t max_count);

// Format recent detour calls with time differences from crash
// Returns formatted string for logging
std::string FormatRecentDetourCalls(uint64_t crash_timestamp_ns, size_t max_count = 16);

// Structure for undestroyed guard information
struct UndestroyedGuardInfo {
    const char* function_name;
    int line_number;
    uint64_t timestamp_ns;
};

// RAII guard class for crash detection
// Tracks if a detour call completes cleanly (destructor called)
// If destructor is not called, indicates a crash occurred
// Also updates the latest-call map (call_site_key -> LatestCallValue) under SRWLOCK
class DetourCallGuard {
   public:
    // call_site_key: unique per (function, line), use DETOUR_CALL_SITE_KEY macro
    DetourCallGuard(const char* call_site_key, const char* function_name, int line_number, uint64_t timestamp_ns);
    ~DetourCallGuard();

    // Non-copyable, non-movable
    DetourCallGuard(const DetourCallGuard&) = delete;
    DetourCallGuard& operator=(const DetourCallGuard&) = delete;
    DetourCallGuard(DetourCallGuard&&) = delete;
    DetourCallGuard& operator=(DetourCallGuard&&) = delete;

   private:
    const char* call_site_key_;
    const char* function_name_;
    int line_number_;
    uint64_t timestamp_ns_;
    std::atomic<bool>* destroyed_flag_;
};

// Get latest call value for a call-site key (under shared lock). Returns false if no entry.
bool GetLatestCallValue(const char* call_site_key, LatestCallValue* out);

// Format all call sites from the latest-call map, sorted by last call time (most recent first).
// Each line: call_site_key, time since now_ns (e.g. "5.2 s ago"), destroyed flag.
// Use when thread is stuck: now_ns = time of snapshot; entries at the bottom are the ones
// that stopped being made (stale longest).
std::string FormatAllLatestCalls(uint64_t now_ns);

// Get count of undestroyed guards (crashes detected)
size_t GetUndestroyedGuardCount();

// Format undestroyed guards with time differences from crash
// Returns formatted string for logging
// Always includes count, even if 0
std::string FormatUndestroyedGuards(uint64_t crash_timestamp_ns);

}  // namespace detour_call_tracker

// Macro to record detour call with crash detection - pass timestamp in nanoseconds
// Creates a local guard object that tracks if the function completes cleanly
// If the destructor doesn't get called (crash), it will be detected in exit hooks
// Usage: RECORD_DETOUR_CALL(utils::get_now_ns())
// The guard object lives for the duration of the function scope
// DETOUR_CALL_SITE_KEY: string literal unique per (function, line), used as map key for latest-call
#define STRINGIFY_IMPL(x)    #x
#define TOSTRING(x)          STRINGIFY_IMPL(x)
#define DETOUR_CALL_SITE_KEY (__FUNCTION__ ":" TOSTRING(__LINE__))
#define CONCAT_IMPL(a, b)    a##b
#define CONCAT(a, b)         CONCAT_IMPL(a, b)
#define RECORD_DETOUR_CALL(timestamp_ns)                                                                      \
    detour_call_tracker::DetourCallGuard CONCAT(_detour_guard_, __LINE__)(DETOUR_CALL_SITE_KEY, __FUNCTION__, \
                                                                          __LINE__, (timestamp_ns))
