#include "d3d9_pool_upgrade.hpp"

#include <d3d9.h>

#ifndef IID_IDirect3DDevice9Ex
// Some toolchains (e.g. MinGW) may not define this in d3d9.h
extern "C" const GUID IID_IDirect3DDevice9Ex = {
    0xb18b10ce, 0x2649, 0x405a, {0x87, 0x0f, 0x95, 0xf7, 0x77, 0xd4, 0x31, 0x3a}};
#endif

namespace display_commanderhooks::d3d9 {

D3DPOOL UpgradePoolForDevice9Ex(IDirect3DDevice9* device, D3DPOOL pool) {
    if (device == nullptr || pool != D3DPOOL_MANAGED) {
        return pool;
    }
    IDirect3DDevice9Ex* ex = nullptr;
    if (SUCCEEDED(device->QueryInterface(IID_IDirect3DDevice9Ex, reinterpret_cast<void**>(&ex)))) {
        if (ex != nullptr) {
            ex->Release();
        }
        return D3DPOOL_MANAGED_EX;
    }
    return pool;
}

}  // namespace display_commanderhooks::d3d9
