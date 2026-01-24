#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>

namespace detour_call_tracker {

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
class DetourCallGuard {
   public:
    DetourCallGuard(const char* function_name, int line_number, uint64_t timestamp_ns);
    ~DetourCallGuard();

    // Non-copyable, non-movable
    DetourCallGuard(const DetourCallGuard&) = delete;
    DetourCallGuard& operator=(const DetourCallGuard&) = delete;
    DetourCallGuard(DetourCallGuard&&) = delete;
    DetourCallGuard& operator=(DetourCallGuard&&) = delete;

   private:
    const char* function_name_;
    int line_number_;
    uint64_t timestamp_ns_;
    std::atomic<bool>* destroyed_flag_;
};

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
// Two-level macro expansion needed to properly expand __LINE__ before concatenation
#define CONCAT_IMPL(a, b) a##b
#define CONCAT(a, b) CONCAT_IMPL(a, b)
#define RECORD_DETOUR_CALL(timestamp_ns) \
    detour_call_tracker::DetourCallGuard CONCAT(_detour_guard_, __LINE__)(__FUNCTION__, __LINE__, (timestamp_ns))
