#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace utils {

// Lock-free ring buffer for performance samples
// Template parameters:
//   T: Sample type (must be trivially copyable)
//   Capacity: Ring buffer capacity (must be power of 2)
template <typename T, size_t Capacity>
class LockFreeRingBuffer {
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of 2");
    static_assert(std::is_trivially_copyable_v<T>, "T must be trivially copyable");

  public:
    // Record a sample (thread-safe, lock-free)
    void Record(const T& sample) {
        uint32_t idx = head_.fetch_add(1, std::memory_order_acq_rel);
        buffer_[idx & (Capacity - 1)] = sample;
    }

    // Get the current head index (for reading)
    uint32_t GetHead() const {
        return head_.load(std::memory_order_acquire);
    }

    // Get the number of samples available (capped at Capacity)
    uint32_t GetCount() const {
        uint32_t head = GetHead();
        return (head > static_cast<uint32_t>(Capacity)) ? static_cast<uint32_t>(Capacity) : head;
    }

    // Access a sample by index (relative to head)
    // idx: 0 = most recent, 1 = second most recent, etc.
    const T& GetSample(uint32_t idx) const {
        uint32_t head = GetHead();
        return buffer_[(head - 1 - idx) & (Capacity - 1)];
    }

    // Access a sample by absolute index
    const T& GetSampleByAbsoluteIndex(uint32_t absolute_idx) const {
        return buffer_[absolute_idx & (Capacity - 1)];
    }

    // Reset the ring buffer
    void Reset() {
        head_.store(0, std::memory_order_release);
    }

    // Get the capacity
    static constexpr size_t GetCapacity() {
        return Capacity;
    }

  private:
    std::atomic<uint32_t> head_{0};
    T buffer_[Capacity] = {};
};

} // namespace utils

