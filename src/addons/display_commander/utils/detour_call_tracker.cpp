#include "detour_call_tracker.hpp"
#include <string>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cstdio>
#include <cstring>

namespace detour_call_tracker {
namespace {

// Circular buffer configuration
// Use power-of-2 size for efficient modulo operation
constexpr size_t BUFFER_SIZE = 1024;  // Track last 64 detour calls
constexpr size_t BUFFER_MASK = BUFFER_SIZE - 1;  // For fast modulo (BUFFER_SIZE must be power of 2)

// Circular buffer storage with atomic entries
// Each entry has atomic fields for lock-free concurrent access
DetourCallEntry g_detour_buffer[BUFFER_SIZE];

// String storage buffers - one per entry
// Format: "FunctionName:123"
// Size: function name (up to 256 chars) + ":123456" (line number) + null terminator
char g_string_buffers[BUFFER_SIZE][512];

// Atomic index for lock-free insertion
// Incremented on each write, wraps around automatically
std::atomic<size_t> g_write_index{0};

} // anonymous namespace

void RecordDetourCall(const char* function_name, int line_number, uint64_t timestamp_ns) {
    // Get next write position (increment atomically)
    // Using relaxed ordering for maximum performance
    size_t index = g_write_index.fetch_add(1, std::memory_order_relaxed) & BUFFER_MASK;

    // Format function name and line number into the string buffer for this entry
    // Format: "FunctionName:123"
    snprintf(g_string_buffers[index], sizeof(g_string_buffers[index]), "%s:%d", function_name, line_number);

    // Write entry fields with relaxed ordering
    // Each field is atomic, so writes are independent
    // Slight race condition with readers is acceptable for crash reporting
    // Note: Each entry has its own string buffer, so concurrent writes to different indices are safe
    g_detour_buffer[index].function_name.store(g_string_buffers[index], std::memory_order_relaxed);
    g_detour_buffer[index].timestamp_ns.store(timestamp_ns, std::memory_order_relaxed);
}

size_t GetRecentDetourCalls(DetourCallInfo* out_entries, size_t max_count) {
    if (out_entries == nullptr || max_count == 0) {
        return 0;
    }

    // Snapshot the write index (acquire barrier to see latest writes)
    size_t current_index = g_write_index.load(std::memory_order_acquire);

    // If no entries written yet, return empty
    if (current_index == 0) {
        return 0;
    }

    // Determine how many entries to read
    // If buffer hasn't wrapped, only read up to current_index
    size_t available_entries = (current_index < BUFFER_SIZE) ? current_index : BUFFER_SIZE;
    size_t count = std::min(max_count, available_entries);

    // Read entries in reverse chronological order (most recent first)
    // Most recent entry is at (current_index - 1) & BUFFER_MASK
    size_t entries_filled = 0;
    for (size_t i = 0; i < count; ++i) {
        // Calculate index (wrapping backwards from most recent position)
        // Most recent is at (current_index - 1), then go backwards
        size_t read_index = (current_index - 1 - i) & BUFFER_MASK;

        DetourCallEntry& entry = g_detour_buffer[read_index];

        // Read atomic fields with acquire ordering to see latest writes
        const char* func_name = entry.function_name.load(std::memory_order_acquire);
        uint64_t timestamp = entry.timestamp_ns.load(std::memory_order_acquire);

        // Skip empty entries (timestamp of 0 indicates never written)
        if (timestamp == 0) {
            continue;
        }

        // Copy to simple output structure
        out_entries[entries_filled].function_name = func_name;
        out_entries[entries_filled].timestamp_ns = timestamp;
        entries_filled++;
    }

    return entries_filled;
}

std::string FormatRecentDetourCalls(uint64_t crash_timestamp_ns, size_t max_count) {
    DetourCallInfo entries[BUFFER_SIZE];
    size_t count = GetRecentDetourCalls(entries, std::min(max_count, static_cast<size_t>(BUFFER_SIZE)));

    std::ostringstream oss;

    if (count == 0) {
        oss << "Recent Detour Calls: <none recorded>";
        return oss.str();
    }

    oss << "Recent Detour Calls (most recent first, " << count << " entries):\n";

    for (size_t i = 0; i < count; ++i) {
        const DetourCallInfo& entry = entries[i];

        if (entry.function_name == nullptr || entry.timestamp_ns == 0) {
            continue;
        }

        // Calculate time difference
        int64_t time_diff_ns = static_cast<int64_t>(crash_timestamp_ns) - static_cast<int64_t>(entry.timestamp_ns);
        double time_diff_ms = static_cast<double>(time_diff_ns) / 1000000.0;
        double time_diff_us = static_cast<double>(time_diff_ns) / 1000.0;

        oss << "  [" << (i + 1) << "] " << entry.function_name;

        if (time_diff_ns >= 0) {
            oss << " - " << std::fixed << std::setprecision(3) << time_diff_ms << " ms before crash";
            if (time_diff_ms < 1.0) {
                oss << " (" << std::setprecision(1) << time_diff_us << " us)";
            }
        } else {
            oss << " - <invalid timestamp>";
        }

        if (i < count - 1) {
            oss << "\n";
        }
    }

    return oss.str();
}

} // namespace detour_call_tracker
