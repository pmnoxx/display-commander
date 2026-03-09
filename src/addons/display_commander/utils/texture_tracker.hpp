#pragma once

#include <cstddef>
#include <cstdint>

namespace utils {

// Per-dimension texture cache stats (1D, 2D, 3D).
struct TextureCacheDimStats {
    uint64_t lookups{0};
    uint64_t hits{0};
    uint64_t lookup_misses{0};
    uint64_t inserts{0};
    uint64_t total_bytes{0};
};

// Thread-safe tracking of loaded texture pointers and their sizes.
// Used when "texture tracking" is enabled in Advanced tab to show texture memory stats.
// AddTexture/RemoveTexture are called from D3D11 CreateTexture* and IUnknown::Release detours.

struct TextureTrackerStats {
    uint64_t current_count{0};
    uint64_t current_bytes{0};
    uint64_t peak_bytes{0};
    /** Texture cache simulator: number of unique (desc + initial data) keys seen so far. Lower bound for cache misses. */
    uint64_t min_cache_misses_possible{0};
    /** Per-dimension cache stats (1D, 2D, 3D). */
    TextureCacheDimStats texture_cache_1d;
    TextureCacheDimStats texture_cache_2d;
    TextureCacheDimStats texture_cache_3d;
    /** CreateTexture2D skipped cache: no initial data (pInitialData null or pSysMem null). */
    uint64_t texture_cache_skip_no_initial_data{0};
    /** CreateTexture2D skipped cache: texture tracking disabled. */
    uint64_t texture_cache_skip_tracking_off{0};
    /** CreateTexture2D skipped cache: D3D11 texture caching disabled. */
    uint64_t texture_cache_skip_caching_off{0};
    /** CreateTexture2D skipped cache: ppTexture2D null. */
    uint64_t texture_cache_skip_ppTexture2D_null{0};
    /** CreateTexture2D skipped cache: hash key was 0 (e.g. zero rows). */
    uint64_t texture_cache_skip_key_zero{0};
    /** CreateTexture2D skipped cache: computed size was 0 (unsupported format). */
    uint64_t texture_cache_skip_size_zero{0};
};

// Record that a cache lookup was attempted (before TextureCacheGet). Dimension-specific:
void TextureCacheLookupRecord1D();
void TextureCacheLookupRecord3D();
void TextureCacheLookupRecord();  // same as 2D (backward compat)

// Record that a lookup did not find a cached texture (TextureCacheGet returned nullptr).
void TextureCacheLookupMissRecord1D();
void TextureCacheLookupMissRecord3D();
void TextureCacheLookupMissRecord();  // same as 2D

// Record that a new texture was inserted into the cache (TextureCachePut actually stored).
void TextureCacheInsertRecord1D();
void TextureCacheInsertRecord3D();
void TextureCacheInsertRecord();  // same as 2D

// Add size_bytes to the total bytes of all cached textures for this dimension.
void TextureCacheAddBytes1D(size_t size_bytes);
void TextureCacheAddBytes3D(size_t size_bytes);
void TextureCacheAddBytes(size_t size_bytes);  // same as 2D

// Record a texture cache hit (returned cached texture instead of creating).
void TextureCacheHitRecord1D();
void TextureCacheHitRecord3D();
void TextureCacheHitRecord();  // same as 2D

// Record skip reasons (one per CreateTexture2D that did not attempt cache lookup).
void TextureCacheRecordSkipNoInitialData();
void TextureCacheRecordSkipTrackingOff();
void TextureCacheRecordSkipCachingOff();
void TextureCacheRecordSkipPpTexture2DNull();
void TextureCacheRecordSkipKeyZero();
void TextureCacheRecordSkipSizeZero();

// Add a texture to the tracker (call after successful CreateTexture* when tracking enabled).
void TextureTrackerAdd(void* texture_ptr, size_t size_bytes);

// Record a cache key in the texture cache simulator (call when a cacheable CreateTexture* succeeds).
// Only counts as a new "min miss" if this key has never been seen. Key should be a hash of (desc + pInitialData).
void TextureCacheSimulatorRecord(uint64_t cache_key);

// Remove a texture from the tracker (call from IUnknown::Release detour before calling original).
// Returns the size that was tracked, or 0 if not found.
size_t TextureTrackerRemove(void* texture_ptr);

// Get current stats (thread-safe; suitable for UI).
TextureTrackerStats TextureTrackerGetStats();

// Reset peak_bytes to current_bytes (e.g. "Reset peak" button in UI).
void TextureTrackerResetPeak();

}  // namespace utils
