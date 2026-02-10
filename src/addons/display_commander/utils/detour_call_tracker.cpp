#include "detour_call_tracker.hpp"
#include "srwlock_wrapper.hpp"
#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <iomanip>
#include <sstream>
#include <vector>

namespace detour_call_tracker {
namespace {

Entry g_entries[MAX_ENTRIES];
std::atomic<uint64_t> g_used_entries{0};
SRWLOCK g_context_lock = SRWLOCK_INIT;

}  // anonymous namespace

uint32_t AllocateEntryIndex(const char* key) {
    uint64_t idx = g_used_entries.fetch_add(1, std::memory_order_relaxed);
    if (idx >= MAX_ENTRIES) {
        return 0;
    }
    Entry& e = g_entries[static_cast<size_t>(idx)];
    e.key = key;
    e.inprogress_cnt.store(0, std::memory_order_relaxed);
    e.total_cnt.store(0, std::memory_order_relaxed);
    e.last_call_ns.store(0, std::memory_order_relaxed);
    e.prev_call_ns.store(0, std::memory_order_relaxed);
    e.context[0] = '\0';
    return static_cast<uint32_t>(idx);
}

void SetCallSiteContextByKey(const char* key, const char* fmt, ...) {
    if (key == nullptr || fmt == nullptr) {
        return;
    }
    va_list args;
    va_start(args, fmt);
    utils::SRWLockExclusive lock(g_context_lock);
    uint64_t used = g_used_entries.load(std::memory_order_acquire);
    size_t limit = (std::min)(static_cast<uint64_t>(MAX_ENTRIES), used);
    for (size_t i = 0; i < limit; ++i) {
        Entry& e = g_entries[i];
        if (e.key != nullptr && std::strcmp(e.key, key) == 0) {
            (void)std::vsnprintf(e.context, CONTEXT_SIZE, fmt, args);
            break;
        }
    }
    va_end(args);
}

void RecordCallNoGuard(uint32_t entry_index, uint64_t timestamp_ns) {
    if (entry_index >= MAX_ENTRIES) {
        return;
    }
    Entry& e = g_entries[entry_index];
    e.total_cnt.fetch_add(1, std::memory_order_relaxed);
    uint64_t prev = e.last_call_ns.exchange(timestamp_ns, std::memory_order_relaxed);
    e.prev_call_ns.store(prev, std::memory_order_relaxed);
}

DetourCallGuard::DetourCallGuard(uint32_t entry_index, uint64_t timestamp_ns)
    : entry_index_(entry_index), entry_(nullptr) {
    if (entry_index >= MAX_ENTRIES) {
        return;
    }
    entry_ = &g_entries[entry_index];
    entry_->inprogress_cnt.fetch_add(1, std::memory_order_relaxed);
    entry_->total_cnt.fetch_add(1, std::memory_order_relaxed);
    uint64_t prev = entry_->last_call_ns.exchange(timestamp_ns, std::memory_order_relaxed);
    entry_->prev_call_ns.store(prev, std::memory_order_relaxed);
}

DetourCallGuard::~DetourCallGuard() {
    if (entry_ != nullptr) {
        entry_->inprogress_cnt.fetch_sub(1, std::memory_order_release);
    }
}

size_t GetUndestroyedGuardCount() {
    uint64_t used = g_used_entries.load(std::memory_order_acquire);
    size_t limit = (std::min)(static_cast<uint64_t>(MAX_ENTRIES), used);
    size_t count = 0;
    for (size_t i = 0; i < limit; ++i) {
        if (g_entries[i].inprogress_cnt.load(std::memory_order_acquire) != 0) {
            count++;
        }
    }
    return count;
}

std::string FormatUndestroyedGuards(uint64_t crash_timestamp_ns) {
    uint64_t used = g_used_entries.load(std::memory_order_acquire);
    size_t limit = (std::min)(static_cast<uint64_t>(MAX_ENTRIES), used);

    struct Undestroyed {
        const char* key;
        const char* context;
        uint64_t last_ns;
        uint64_t prev_ns;
    };
    std::vector<Undestroyed> list;
    list.reserve(64);

    for (size_t i = 0; i < limit; ++i) {
        Entry& e = g_entries[i];
        if (e.inprogress_cnt.load(std::memory_order_acquire) == 0) {
            continue;
        }
        list.push_back({e.key, e.context, e.last_call_ns.load(std::memory_order_acquire),
                        e.prev_call_ns.load(std::memory_order_acquire)});
    }

    std::ostringstream oss;
    oss << "Undestroyed Detour Guards (crashes detected): " << list.size();
    for (size_t i = 0; i < list.size(); ++i) {
        oss << "\n  [" << (i + 1) << "] " << (list[i].key != nullptr ? list[i].key : "<unknown>");
        if (list[i].context != nullptr && list[i].context[0] != '\0') {
            oss << " | " << list[i].context;
        }
        int64_t time_diff_ns = static_cast<int64_t>(crash_timestamp_ns) - static_cast<int64_t>(list[i].last_ns);
        double time_diff_ms = static_cast<double>(time_diff_ns) / 1000000.0;
        double time_diff_us = static_cast<double>(time_diff_ns) / 1000.0;
        if (time_diff_ns >= 0) {
            oss << " - " << std::fixed << std::setprecision(3) << time_diff_ms << " ms before crash";
            if (time_diff_ms < 1.0) {
                oss << " (" << std::setprecision(1) << time_diff_us << " us)";
            }
        } else {
            oss << " - <invalid timestamp>";
        }
        if (list[i].prev_ns != 0 && list[i].last_ns >= list[i].prev_ns) {
            uint64_t interval_ns = list[i].last_ns - list[i].prev_ns;
            double interval_ms = static_cast<double>(interval_ns) / 1000000.0;
            oss << " | prev " << std::fixed << std::setprecision(3) << interval_ms << " ms ago";
            if (interval_ns > 0) {
                double calls_per_sec = 1e9 / static_cast<double>(interval_ns);
                oss << " (~" << (calls_per_sec >= 1.0 ? std::setprecision(1) : std::setprecision(2)) << calls_per_sec
                    << " calls/s)";
            }
        }
    }
    return oss.str();
}

std::string FormatDetourCallsByTime(uint64_t crash_timestamp_ns, size_t max_count) {
    uint64_t used = g_used_entries.load(std::memory_order_acquire);
    size_t limit = (std::min)(static_cast<uint64_t>(MAX_ENTRIES), used);

    struct IndexAndTime {
        size_t index;
        uint64_t last_call_ns;
    };
    std::vector<IndexAndTime> by_time;
    by_time.reserve(limit);

    for (size_t i = 0; i < limit; ++i) {
        uint64_t last_ns = g_entries[i].last_call_ns.load(std::memory_order_acquire);
        if (last_ns == 0) {
            continue;
        }
        by_time.push_back({i, last_ns});
    }

    std::sort(by_time.begin(), by_time.end(),
              [](const IndexAndTime& a, const IndexAndTime& b) { return a.last_call_ns > b.last_call_ns; });

    std::ostringstream oss;
    oss << "Detour Calls by time (newest first, " << by_time.size() << " sites):\n";
    size_t n = (std::min)(max_count, by_time.size());
    for (size_t i = 0; i < n; ++i) {
        const IndexAndTime& it = by_time[i];
        Entry& e = g_entries[it.index];
        const char* key = e.key;
        uint64_t last_ns = it.last_call_ns;
        int64_t time_diff_ns = static_cast<int64_t>(crash_timestamp_ns) - static_cast<int64_t>(last_ns);
        double time_diff_ms = static_cast<double>(time_diff_ns) / 1000000.0;
        double time_diff_us = static_cast<double>(time_diff_ns) / 1000.0;
        oss << "  [" << (i + 1) << "] " << (key != nullptr ? key : "<unknown>");
        if (time_diff_ns >= 0) {
            oss << " - " << std::fixed << std::setprecision(3) << time_diff_ms << " ms before crash";
            if (time_diff_ms < 1.0) {
                oss << " (" << std::setprecision(1) << time_diff_us << " us)";
            }
        } else {
            oss << " - <invalid timestamp>";
        }
        uint64_t inprog = e.inprogress_cnt.load(std::memory_order_acquire);
        if (inprog != 0) {
            oss << " [in_progress=" << inprog << "]";
        }
        uint64_t prev_ns = e.prev_call_ns.load(std::memory_order_acquire);
        if (prev_ns != 0 && last_ns >= prev_ns) {
            uint64_t interval_ns = last_ns - prev_ns;
            double interval_ms = static_cast<double>(interval_ns) / 1000000.0;
            oss << " | prev " << std::fixed << std::setprecision(3) << interval_ms << " ms ago";
            if (interval_ns > 0) {
                double calls_per_sec = 1e9 / static_cast<double>(interval_ns);
                oss << " (~" << (calls_per_sec >= 1.0 ? std::setprecision(1) : std::setprecision(2)) << calls_per_sec
                    << " calls/s)";
            }
        }
        if (i < n - 1) {
            oss << "\n";
        }
    }
    return oss.str();
}

std::string FormatRecentDetourCalls(uint64_t crash_timestamp_ns, size_t max_count) {
    return FormatDetourCallsByTime(crash_timestamp_ns, max_count);
}

std::string FormatAllLatestCalls(uint64_t now_ns) {
    uint64_t used = g_used_entries.load(std::memory_order_acquire);
    size_t limit = (std::min)(static_cast<uint64_t>(MAX_ENTRIES), used);

    struct IndexAndTime {
        size_t index;
        uint64_t last_call_ns;
    };
    std::vector<IndexAndTime> by_time;
    by_time.reserve(limit);

    for (size_t i = 0; i < limit; ++i) {
        uint64_t last_ns = g_entries[i].last_call_ns.load(std::memory_order_acquire);
        if (last_ns == 0) {
            continue;
        }
        by_time.push_back({i, last_ns});
    }

    std::sort(by_time.begin(), by_time.end(),
              [](const IndexAndTime& a, const IndexAndTime& b) { return a.last_call_ns > b.last_call_ns; });

    std::ostringstream oss;
    oss << "All Detour Call Sites (by last call, most recent first): " << by_time.size() << " sites\n";

    for (size_t i = 0; i < by_time.size(); ++i) {
        const IndexAndTime& it = by_time[i];
        Entry& e = g_entries[it.index];
        const char* key = e.key;
        uint64_t last_ns = it.last_call_ns;
        int64_t ago_ns = static_cast<int64_t>(now_ns) - static_cast<int64_t>(last_ns);
        double ago_ms = static_cast<double>(ago_ns) / 1000000.0;
        double ago_s = static_cast<double>(ago_ns) / 1000000000.0;
        oss << "  [" << (i + 1) << "] " << (key != nullptr ? key : "<unknown>");
        if (ago_ns >= 0) {
            if (ago_s >= 1.0) {
                oss << " - " << std::fixed << std::setprecision(2) << ago_s << " s ago";
            } else {
                oss << " - " << std::fixed << std::setprecision(3) << ago_ms << " ms ago";
            }
        } else {
            oss << " - <invalid timestamp>";
        }
        uint64_t inprog = e.inprogress_cnt.load(std::memory_order_acquire);
        oss << " in_progress=" << inprog;
        if (inprog != 0) {
            oss << " (possible crash without cleanup)";
        }
        uint64_t prev_ns = e.prev_call_ns.load(std::memory_order_acquire);
        if (prev_ns != 0 && last_ns >= prev_ns) {
            uint64_t interval_ns = last_ns - prev_ns;
            double interval_ms = static_cast<double>(interval_ns) / 1000000.0;
            oss << " | prev " << std::fixed << std::setprecision(3) << interval_ms << " ms ago";
            if (interval_ns > 0) {
                double calls_per_sec = 1e9 / static_cast<double>(interval_ns);
                oss << " (~" << (calls_per_sec >= 1.0 ? std::setprecision(1) : std::setprecision(2)) << calls_per_sec
                    << " calls/s)";
            }
        }
        if (i < by_time.size() - 1) {
            oss << "\n";
        }
    }
    return oss.str();
}

}  // namespace detour_call_tracker
