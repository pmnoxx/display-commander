#pragma once

#include <atomic>

namespace display_commanderhooks {

// Hook call statistics (one per hook). Shared by windows_message_hooks and hook_statistics_manager.
// Copyable so std::vector<HookCallStats>::resize() can value-initialize (e.g. on realloc).
struct HookCallStats {
    std::atomic<uint64_t> total_calls{0};
    std::atomic<uint64_t> unsuppressed_calls{0};

    HookCallStats() = default;
    HookCallStats(const HookCallStats& other)
        : total_calls(other.total_calls.load()), unsuppressed_calls(other.unsuppressed_calls.load()) {}
    HookCallStats& operator=(const HookCallStats& other) {
        total_calls.store(other.total_calls.load());
        unsuppressed_calls.store(other.unsuppressed_calls.load());
        return *this;
    }

    void increment_total() { total_calls.fetch_add(1); }
    void increment_unsuppressed() { unsuppressed_calls.fetch_add(1); }
    void reset() {
        total_calls.store(0);
        unsuppressed_calls.store(0);
    }
};

}  // namespace display_commanderhooks
