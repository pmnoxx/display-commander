#pragma once

#include <d3d9.h>

namespace display_commanderhooks::d3d9 {

// D3D9Ex does not accept D3DPOOL_MANAGED (1) for CreateTexture/CreateVertexBuffer/etc.
// The runtime uses an internal "managed" pool with value 6. When we have an IDirect3DDevice9Ex
// and the game passes Pool == D3DPOOL_MANAGED, we must pass 6 to the real API instead.
#ifndef D3DPOOL_MANAGED_EX
#define D3DPOOL_MANAGED_EX ((D3DPOOL)6)
#endif

// Returns the pool to pass to the real Create* call. If device is IDirect3DDevice9Ex and
// pool is D3DPOOL_MANAGED, returns D3DPOOL_MANAGED_EX (6); otherwise returns pool unchanged.
D3DPOOL UpgradePoolForDevice9Ex(IDirect3DDevice9* device, D3DPOOL pool);

}  // namespace display_commanderhooks::d3d9
