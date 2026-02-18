#pragma once

#include <cstdint>

namespace display_commander {
namespace dxgi {

// Fills VRAM used and budget (bytes) for the first DXGI adapter via IDXGIAdapter3::QueryVideoMemoryInfo.
// Uses DXGI_MEMORY_SEGMENT_GROUP_LOCAL; used = CurrentUsage, total = Budget.
// Returns true if both values were obtained; otherwise out_* are unchanged.
// Works with any DXGI adapter (NVIDIA, AMD, Intel). Requires Windows 10+ (IDXGIAdapter3).
bool GetVramInfo(uint64_t* out_used_bytes, uint64_t* out_total_bytes);

}  // namespace dxgi
}  // namespace display_commander
