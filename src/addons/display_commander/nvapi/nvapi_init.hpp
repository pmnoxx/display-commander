#pragma once

// One-time NVAPI initialization. Safe to call from multiple threads; uses internal
// atomics. Returns true if NVAPI is initialized (or was already), false on failure.

namespace nvapi {

bool EnsureNvApiInitialized();

}  // namespace nvapi
