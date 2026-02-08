#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>

namespace detour_call_tracker {

// Maximum number of distinct detour call sites (one entry per RECORD_DETOUR_CALL site)
constexpr size_t MAX_ENTRIES = 65536;

// One entry per call site. Index is assigned once via AllocateEntryIndex (static in macro).
struct Entry {
    const char* key{nullptr};
    std::atomic<uint64_t> inprogress_cnt{0};
    std::atomic<uint64_t> total_cnt{0};
    std::atomic<uint64_t> last_call_ns{0};
    std::atomic<uint64_t> prev_call_ns{0};  // second-to-last call; interval = last_call_ns - prev_call_ns
};

// Allocate a new entry index for the given call-site key. Called once per macro expansion (static).
// Returns index in [0, MAX_ENTRIES). Thread-safe. Entry at index is initialized with key.
uint32_t AllocateEntryIndex(const char* key);

// Record a call without creating a guard (e.g. FreeLibraryAndExitThread which never returns).
// Updates total_cnt and last_call_ns only; does not touch inprogress_cnt.
void RecordCallNoGuard(uint32_t entry_index, uint64_t timestamp_ns);

// RAII guard: on construction increments entry's inprogress_cnt, total_cnt, sets last_call_ns;
// on destruction decrements inprogress_cnt. If destructor never runs (crash), inprogress_cnt stays > 0.
class DetourCallGuard {
   public:
    DetourCallGuard(uint32_t entry_index, uint64_t timestamp_ns);
    ~DetourCallGuard();

    DetourCallGuard(const DetourCallGuard&) = delete;
    DetourCallGuard& operator=(const DetourCallGuard&) = delete;
    DetourCallGuard(DetourCallGuard&&) = delete;
    DetourCallGuard& operator=(DetourCallGuard&&) = delete;

   private:
    uint32_t entry_index_;
    Entry* entry_{nullptr};
};

// --- Crash reporting: iterate entries 0 .. used_entries-1 ---

// Count of entries with inprogress_cnt != 0 (guards that didn't finish).
size_t GetUndestroyedGuardCount();

// Format entries with inprogress_cnt != 0 (one line per entry, time since crash_timestamp_ns).
std::string FormatUndestroyedGuards(uint64_t crash_timestamp_ns);

// Format all entries sorted by last_call_ns descending (newest first). Shows call order before crash.
// max_count limits how many lines are included.
std::string FormatDetourCallsByTime(uint64_t crash_timestamp_ns, size_t max_count = 256);

// Backward compatibility: same as FormatDetourCallsByTime (recent calls, most recent first).
std::string FormatRecentDetourCalls(uint64_t crash_timestamp_ns, size_t max_count = 16);

// All call sites by last call time (most recent first). Used by advanced tab.
std::string FormatAllLatestCalls(uint64_t now_ns);

}  // namespace detour_call_tracker

// Macro: each expansion gets a static entry index (via AllocateEntryIndex) and a guard.
#define STRINGIFY_IMPL(x)  #x
#define TOSTRING(x)       STRINGIFY_IMPL(x)
#define DETOUR_CALL_SITE_KEY (__FUNCTION__ ":" TOSTRING(__LINE__))
#define CONCAT_IMPL(a, b) a##b
#define CONCAT(a, b)      CONCAT_IMPL(a, b)
#define RECORD_DETOUR_CALL(timestamp_ns)                                                                    \
    static const uint32_t CONCAT(_detour_idx_, __LINE__) = detour_call_tracker::AllocateEntryIndex(        \
        DETOUR_CALL_SITE_KEY);                                                                              \
    detour_call_tracker::DetourCallGuard CONCAT(_detour_guard_, __LINE__)(CONCAT(_detour_idx_, __LINE__),  \
                                                                          (timestamp_ns))
