#pragma once

#include <cstdint>
#include <atomic>
#include <string>
#include <cstddef>

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
// Function name is stored as pointer to string literal (valid for program lifetime)
void RecordDetourCall(const char* function_name, uint64_t timestamp_ns);

// Get recent detour calls (up to max_count, most recent first)
// Returns number of entries filled
// Thread-safe but may miss entries being written concurrently (acceptable for crash reporting)
size_t GetRecentDetourCalls(DetourCallInfo* out_entries, size_t max_count);

// Format recent detour calls with time differences from crash
// Returns formatted string for logging
std::string FormatRecentDetourCalls(uint64_t crash_timestamp_ns, size_t max_count = 16);

} // namespace detour_call_tracker

// Macro to record detour call - pass timestamp in nanoseconds
// Usage: RECORD_DETOUR_CALL(utils::get_now_ns())
#define RECORD_DETOUR_CALL(timestamp_ns) \
    detour_call_tracker::RecordDetourCall(__FUNCTION__, (1))

