#include "detour_call_tracker.hpp"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <iomanip>
#include <map>
#include <sstream>
#include <string>
#include <vector>
#include "srwlock_wrapper.hpp"

namespace detour_call_tracker {
namespace {

// Comparator for const char* keys (compare by content, not pointer)
struct CStrCmp {
    bool operator()(const char* a, const char* b) const { return std::strcmp(a, b) < 0; }
};

// Map from call-site key (function:line from macro) to most recent call value, under SRWLOCK
static SRWLOCK g_latest_call_map_lock = SRWLOCK_INIT;
static std::map<const char*, LatestCallValue, CStrCmp> g_latest_call_map;

// Circular buffer configuration
// Use power-of-2 size for efficient modulo operation
constexpr size_t BUFFER_SIZE = 1024;             // Track last 64 detour calls
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

// Crash detection: Track active guards
// Each guard gets a slot in this array with an atomic destroyed flag
constexpr size_t MAX_ACTIVE_GUARDS = 512;
constexpr size_t GUARD_MASK = MAX_ACTIVE_GUARDS - 1;  // For fast modulo (must be power of 2)

struct GuardSlot {
    std::atomic<const char*> function_name{nullptr};
    std::atomic<int> line_number{0};
    std::atomic<uint64_t> timestamp_ns{0};
    std::atomic<bool> destroyed{false};
    std::atomic<bool> in_use{false};
};

GuardSlot g_guard_slots[MAX_ACTIVE_GUARDS];
std::atomic<size_t> g_guard_slot_index{0};

// String storage for guard function names
char g_guard_string_buffers[MAX_ACTIVE_GUARDS][512];

}  // anonymous namespace

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
    size_t count = (std::min)(max_count, available_entries);

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
    size_t count = GetRecentDetourCalls(entries, (std::min)(max_count, static_cast<size_t>(BUFFER_SIZE)));

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

DetourCallGuard::DetourCallGuard(const char* call_site_key, const char* function_name, int line_number,
                                 uint64_t timestamp_ns)
    : call_site_key_(call_site_key),
      function_name_(function_name),
      line_number_(line_number),
      timestamp_ns_(timestamp_ns),
      destroyed_flag_(nullptr) {
    // Record the detour call
    RecordDetourCall(function_name, line_number, timestamp_ns);

    // Update latest-call map (most recent call of this type); increment in-progress count
    {
        utils::SRWLockExclusive lock(g_latest_call_map_lock);
        LatestCallValue& val = g_latest_call_map[call_site_key];
        val.timestamp_ns = timestamp_ns;
        val.destroyed = false;
        val.in_progress_count++;
    }

    // Allocate a guard slot for crash detection
    size_t slot_index = g_guard_slot_index.fetch_add(1, std::memory_order_relaxed) & GUARD_MASK;
    GuardSlot& slot = g_guard_slots[slot_index];

    // Format function name and line number
    snprintf(g_guard_string_buffers[slot_index], sizeof(g_guard_string_buffers[slot_index]), "%s:%d", function_name,
             line_number);

    // Initialize slot (using relaxed ordering for performance)
    slot.function_name.store(g_guard_string_buffers[slot_index], std::memory_order_relaxed);
    slot.line_number.store(line_number, std::memory_order_relaxed);
    slot.timestamp_ns.store(timestamp_ns, std::memory_order_relaxed);
    slot.destroyed.store(false, std::memory_order_relaxed);
    slot.in_use.store(true, std::memory_order_release);  // Release to ensure visibility

    // Store pointer to destroyed flag for destructor
    destroyed_flag_ = &slot.destroyed;
}

DetourCallGuard::~DetourCallGuard() {
    // Mark as destroyed if we have a flag
    if (destroyed_flag_ != nullptr) {
        destroyed_flag_->store(true, std::memory_order_release);
    }
    // Update latest-call map: decrement in-progress count and mark destroyed
    if (call_site_key_ != nullptr) {
        utils::SRWLockExclusive lock(g_latest_call_map_lock);
        auto it = g_latest_call_map.find(call_site_key_);
        if (it != g_latest_call_map.end()) {
            if (it->second.in_progress_count > 0) {
                it->second.in_progress_count--;
            }
            it->second.destroyed = true;
        }
    }
}

size_t GetUndestroyedGuardCount() {
    size_t count = 0;
    // Scan all guard slots to find undestroyed ones
    for (size_t i = 0; i < MAX_ACTIVE_GUARDS; ++i) {
        GuardSlot& slot = g_guard_slots[i];
        // Check if slot is in use and not destroyed
        bool in_use = slot.in_use.load(std::memory_order_acquire);
        if (in_use) {
            bool destroyed = slot.destroyed.load(std::memory_order_acquire);
            if (!destroyed) {
                count++;
            }
        }
    }
    return count;
}

std::string FormatUndestroyedGuards(uint64_t crash_timestamp_ns) {
    std::ostringstream oss;

    // Collect undestroyed guards
    UndestroyedGuardInfo undestroyed_guards[MAX_ACTIVE_GUARDS];
    size_t count = 0;

    for (size_t i = 0; i < MAX_ACTIVE_GUARDS && count < MAX_ACTIVE_GUARDS; ++i) {
        GuardSlot& slot = g_guard_slots[i];
        bool in_use = slot.in_use.load(std::memory_order_acquire);
        if (in_use) {
            bool destroyed = slot.destroyed.load(std::memory_order_acquire);
            if (!destroyed) {
                const char* func_name = slot.function_name.load(std::memory_order_acquire);
                int line_number = slot.line_number.load(std::memory_order_acquire);
                uint64_t timestamp = slot.timestamp_ns.load(std::memory_order_acquire);

                if (func_name != nullptr && timestamp != 0) {
                    undestroyed_guards[count].function_name = func_name;
                    undestroyed_guards[count].line_number = line_number;
                    undestroyed_guards[count].timestamp_ns = timestamp;
                    count++;
                }
            }
        }
    }

    oss << "Undestroyed Detour Guards (crashes detected): " << count;

    if (count > 0) {
        oss << "\n";
        for (size_t i = 0; i < count; ++i) {
            const UndestroyedGuardInfo& guard = undestroyed_guards[i];

            // Calculate time difference
            int64_t time_diff_ns = static_cast<int64_t>(crash_timestamp_ns) - static_cast<int64_t>(guard.timestamp_ns);
            double time_diff_ms = static_cast<double>(time_diff_ns) / 1000000.0;
            double time_diff_us = static_cast<double>(time_diff_ns) / 1000.0;

            oss << "  [" << (i + 1) << "] " << guard.function_name;

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
    }

    return oss.str();
}

bool GetLatestCallValue(const char* call_site_key, LatestCallValue* out) {
    if (call_site_key == nullptr || out == nullptr) {
        return false;
    }
    utils::SRWLockShared lock(g_latest_call_map_lock);
    auto it = g_latest_call_map.find(call_site_key);
    if (it == g_latest_call_map.end()) {
        return false;
    }
    *out = it->second;
    return true;
}

std::string FormatAllLatestCalls(uint64_t now_ns) {
    using Entry = std::pair<const char*, LatestCallValue>;
    std::vector<Entry> entries;
    {
        utils::SRWLockShared lock(g_latest_call_map_lock);
        entries.reserve(g_latest_call_map.size());
        for (const auto& kv : g_latest_call_map) {
            entries.push_back(kv);
        }
    }
    // Sort by last call time descending (most recent first); bottom of list = stale longest
    std::sort(entries.begin(), entries.end(),
              [](const Entry& a, const Entry& b) { return a.second.timestamp_ns > b.second.timestamp_ns; });

    std::ostringstream oss;
    oss << "All Detour Call Sites (by last call, most recent first; bottom = stopped longest ago): " << entries.size()
        << " sites\n";

    for (size_t i = 0; i < entries.size(); ++i) {
        const char* key = entries[i].first;
        const LatestCallValue& val = entries[i].second;
        if (key == nullptr) {
            continue;
        }

        int64_t ago_ns = static_cast<int64_t>(now_ns) - static_cast<int64_t>(val.timestamp_ns);
        double ago_ms = static_cast<double>(ago_ns) / 1000000.0;
        double ago_s = static_cast<double>(ago_ns) / 1000000000.0;

        oss << "  [" << (i + 1) << "] " << key;
        if (ago_ns >= 0) {
            if (ago_s >= 1.0) {
                oss << " - " << std::fixed << std::setprecision(2) << ago_s << " s ago";
            } else {
                oss << " - " << std::fixed << std::setprecision(3) << ago_ms << " ms ago";
            }
        } else {
            oss << " - <invalid timestamp>";
        }
        oss << (val.destroyed ? " [destroyed]" : " [active]");
        oss << " in_progress=" << val.in_progress_count;
        if (val.in_progress_count > 0) {
            oss << " (possible crash without cleanup)";
        }
        if (i < entries.size() - 1) {
            oss << "\n";
        }
    }

    return oss.str();
}

}  // namespace detour_call_tracker
