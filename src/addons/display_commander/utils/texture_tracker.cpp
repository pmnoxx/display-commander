// Source Code <Display Commander> // follow this order for includes in all files + add this comment at the top
#include "texture_tracker.hpp"
#include "srwlock_wrapper.hpp"
#include "timing.hpp"

// Libraries <ReShade> / <imgui>
// (none)

// Libraries <standard C++>
#include <atomic>
#include <unordered_map>
#include <unordered_set>

// Libraries <Windows.h> — before other Windows headers
#include <Windows.h>

// Libraries <Windows>
// (none)

namespace utils {

namespace {

std::unordered_map<void*, size_t> g_texture_map;
SRWLOCK g_texture_tracker_lock = SRWLOCK_INIT;

std::atomic<uint64_t> g_current_count{0};
std::atomic<uint64_t> g_current_bytes{0};
std::atomic<uint64_t> g_peak_bytes{0};

std::atomic<uint64_t> g_total_misses{0};
std::atomic<uint64_t> g_last_miss_time_ns{0};
std::atomic<double> g_misses_per_sec_ema{0.0};

// Texture cache simulator: set of (desc + initial data) cache keys ever seen. Size = min cache misses possible.
std::unordered_set<uint64_t> g_simulator_keys;
std::atomic<uint64_t> g_min_cache_misses_possible{0};
std::atomic<uint64_t> g_texture_cache_hits{0};
std::atomic<uint64_t> g_texture_cache_lookups{0};
std::atomic<uint64_t> g_texture_cache_lookup_misses{0};
std::atomic<uint64_t> g_texture_cache_inserts{0};
std::atomic<uint64_t> g_texture_cache_total_bytes{0};
std::atomic<uint64_t> g_texture_cache_skip_no_initial_data{0};
std::atomic<uint64_t> g_texture_cache_skip_tracking_off{0};
std::atomic<uint64_t> g_texture_cache_skip_caching_off{0};
std::atomic<uint64_t> g_texture_cache_skip_ppTexture2D_null{0};
std::atomic<uint64_t> g_texture_cache_skip_key_zero{0};
std::atomic<uint64_t> g_texture_cache_skip_size_zero{0};
SRWLOCK g_simulator_lock = SRWLOCK_INIT;

constexpr double kMissRateDecay = 0.9;  // geometric decay: 90% old, 10% new sample

static void RecordMiss() {
    g_total_misses.fetch_add(1, std::memory_order_relaxed);
    const uint64_t now_ns = static_cast<uint64_t>(utils::get_now_ns());
    const uint64_t last_ns = g_last_miss_time_ns.exchange(now_ns, std::memory_order_relaxed);
    if (last_ns != 0) {
        double dt_sec = static_cast<double>(now_ns - last_ns) / static_cast<double>(utils::SEC_TO_NS);
        if (dt_sec < 1e-9) {
            dt_sec = 1e-9;
        }
        const double instantaneous = 1.0 / dt_sec;
        const double cur = g_misses_per_sec_ema.load(std::memory_order_relaxed);
        g_misses_per_sec_ema.store(cur * kMissRateDecay + instantaneous * (1.0 - kMissRateDecay),
                                   std::memory_order_relaxed);
    }
}

}  // namespace

void TextureTrackerAdd(void* texture_ptr, size_t size_bytes) {
    if (texture_ptr == nullptr || size_bytes == 0) {
        return;
    }
    {
        utils::SRWLockExclusive lock(g_texture_tracker_lock);
        auto it = g_texture_map.find(texture_ptr);
        if (it != g_texture_map.end()) {
            return;  // Already tracked (e.g. same ptr returned by different Create overload)
        }
        g_texture_map[texture_ptr] = size_bytes;
    }
    g_current_count.fetch_add(1, std::memory_order_relaxed);
    uint64_t prev = g_current_bytes.fetch_add(static_cast<uint64_t>(size_bytes), std::memory_order_relaxed);
    uint64_t now = prev + size_bytes;
    for (uint64_t peak = g_peak_bytes.load(std::memory_order_relaxed); now > peak;
         peak = g_peak_bytes.load(std::memory_order_relaxed)) {
        if (g_peak_bytes.compare_exchange_weak(peak, now, std::memory_order_relaxed)) {
            break;
        }
    }
}

size_t TextureTrackerRemove(void* texture_ptr) {
    if (texture_ptr == nullptr) {
        return 0;
    }
    size_t size_bytes = 0;
    {
        utils::SRWLockExclusive lock(g_texture_tracker_lock);
        auto it = g_texture_map.find(texture_ptr);
        if (it == g_texture_map.end()) {
            RecordMiss();
            return 0;
        }
        size_bytes = it->second;
        g_texture_map.erase(it);
    }
    g_current_count.fetch_sub(1, std::memory_order_relaxed);
    g_current_bytes.fetch_sub(static_cast<uint64_t>(size_bytes), std::memory_order_relaxed);
    return size_bytes;
}

void TextureCacheSimulatorRecord(uint64_t cache_key) {
    if (cache_key == 0) {
        return;
    }
    utils::SRWLockExclusive lock(g_simulator_lock);
    if (g_simulator_keys.insert(cache_key).second) {
        g_min_cache_misses_possible.fetch_add(1, std::memory_order_relaxed);
    }
}

void TextureCacheHitRecord() {
    g_texture_cache_hits.fetch_add(1, std::memory_order_relaxed);
}

void TextureCacheLookupRecord() {
    g_texture_cache_lookups.fetch_add(1, std::memory_order_relaxed);
}

void TextureCacheLookupMissRecord() {
    g_texture_cache_lookup_misses.fetch_add(1, std::memory_order_relaxed);
}

void TextureCacheInsertRecord() {
    g_texture_cache_inserts.fetch_add(1, std::memory_order_relaxed);
}

void TextureCacheAddBytes(size_t size_bytes) {
    if (size_bytes > 0) {
        g_texture_cache_total_bytes.fetch_add(static_cast<uint64_t>(size_bytes), std::memory_order_relaxed);
    }
}

void TextureCacheRecordSkipNoInitialData() {
    g_texture_cache_skip_no_initial_data.fetch_add(1, std::memory_order_relaxed);
}
void TextureCacheRecordSkipTrackingOff() {
    g_texture_cache_skip_tracking_off.fetch_add(1, std::memory_order_relaxed);
}
void TextureCacheRecordSkipCachingOff() {
    g_texture_cache_skip_caching_off.fetch_add(1, std::memory_order_relaxed);
}
void TextureCacheRecordSkipPpTexture2DNull() {
    g_texture_cache_skip_ppTexture2D_null.fetch_add(1, std::memory_order_relaxed);
}
void TextureCacheRecordSkipKeyZero() {
    g_texture_cache_skip_key_zero.fetch_add(1, std::memory_order_relaxed);
}
void TextureCacheRecordSkipSizeZero() {
    g_texture_cache_skip_size_zero.fetch_add(1, std::memory_order_relaxed);
}

TextureTrackerStats TextureTrackerGetStats() {
    TextureTrackerStats s;
    s.current_count = g_current_count.load(std::memory_order_relaxed);
    s.current_bytes = g_current_bytes.load(std::memory_order_relaxed);
    s.peak_bytes = g_peak_bytes.load(std::memory_order_relaxed);
    s.total_misses = g_total_misses.load(std::memory_order_relaxed);
    s.misses_per_sec_ema = g_misses_per_sec_ema.load(std::memory_order_relaxed);
    s.min_cache_misses_possible = g_min_cache_misses_possible.load(std::memory_order_relaxed);
    s.texture_cache_hits = g_texture_cache_hits.load(std::memory_order_relaxed);
    s.texture_cache_lookups = g_texture_cache_lookups.load(std::memory_order_relaxed);
    s.texture_cache_lookup_misses = g_texture_cache_lookup_misses.load(std::memory_order_relaxed);
    s.texture_cache_inserts = g_texture_cache_inserts.load(std::memory_order_relaxed);
    s.texture_cache_total_bytes = g_texture_cache_total_bytes.load(std::memory_order_relaxed);
    s.texture_cache_skip_no_initial_data = g_texture_cache_skip_no_initial_data.load(std::memory_order_relaxed);
    s.texture_cache_skip_tracking_off = g_texture_cache_skip_tracking_off.load(std::memory_order_relaxed);
    s.texture_cache_skip_caching_off = g_texture_cache_skip_caching_off.load(std::memory_order_relaxed);
    s.texture_cache_skip_ppTexture2D_null = g_texture_cache_skip_ppTexture2D_null.load(std::memory_order_relaxed);
    s.texture_cache_skip_key_zero = g_texture_cache_skip_key_zero.load(std::memory_order_relaxed);
    s.texture_cache_skip_size_zero = g_texture_cache_skip_size_zero.load(std::memory_order_relaxed);
    return s;
}

void TextureTrackerResetPeak() {
    uint64_t cur = g_current_bytes.load(std::memory_order_relaxed);
    g_peak_bytes.store(cur, std::memory_order_relaxed);
}

}  // namespace utils
