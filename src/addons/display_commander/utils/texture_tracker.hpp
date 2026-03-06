#pragma once

#include <cstddef>
#include <cstdint>

namespace utils {

// Thread-safe tracking of loaded texture pointers and their sizes.
// Used when "texture tracking" is enabled in Advanced tab to show texture memory stats.
// AddTexture/RemoveTexture are called from D3D11 CreateTexture* and IUnknown::Release detours.

struct TextureTrackerStats {
    uint64_t current_count{0};
    uint64_t current_bytes{0};
    uint64_t peak_bytes{0};
    /** Total Release calls where the texture was not in the map (e.g. created before tracking). */
    uint64_t total_misses{0};
    /** Exponential moving average of misses per second (geometric decay). */
    double misses_per_sec_ema{0.0};
};

// Add a texture to the tracker (call after successful CreateTexture* when tracking enabled).
void TextureTrackerAdd(void* texture_ptr, size_t size_bytes);

// Remove a texture from the tracker (call from IUnknown::Release detour before calling original).
// Returns the size that was tracked, or 0 if not found.
size_t TextureTrackerRemove(void* texture_ptr);

// Get current stats (thread-safe; suitable for UI).
TextureTrackerStats TextureTrackerGetStats();

// Reset peak_bytes to current_bytes (e.g. "Reset peak" button in UI).
void TextureTrackerResetPeak();

}  // namespace utils
