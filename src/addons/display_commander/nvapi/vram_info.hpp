#pragma once

#include <cstdint>

namespace display_commander {
namespace nvapi {

// Fills VRAM used and total (bytes) for the first physical GPU via NvAPI_GPU_GetMemoryInfoEx.
// Returns true if both values were obtained; otherwise out_* are unchanged.
// NVIDIA GPUs only; no-op when NVAPI is unavailable or no NVIDIA GPU.
bool GetVramInfoNvapi(uint64_t* out_used_bytes, uint64_t* out_total_bytes);

}  // namespace nvapi
}  // namespace display_commander
