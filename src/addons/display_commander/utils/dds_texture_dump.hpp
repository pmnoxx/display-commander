#pragma once

// Minimal DDS write for D3D11 texture dump.
// Uses DX10 extended header so any DXGI_FORMAT can be stored.
// No dependency on dds.h.

#include <cstdint>
#include <filesystem>

struct D3D11_TEXTURE1D_DESC;
struct D3D11_TEXTURE2D_DESC;
struct D3D11_TEXTURE3D_DESC;
struct D3D11_SUBRESOURCE_DATA;

namespace utils {

// Dump first subresource of a CreateTexture2D call to a .dds file. Returns true on success.
// path = e.g. folder / "tex2d_000001.dds". Creates parent directories if needed.
bool DumpTexture2DToDDS(const D3D11_TEXTURE2D_DESC* pDesc, const D3D11_SUBRESOURCE_DATA* pInitialData,
                        const std::filesystem::path& path);

// Dump first subresource of CreateTexture1D (stored as 1xWidth 2D in DDS). Returns true on success.
bool DumpTexture1DToDDS(const D3D11_TEXTURE1D_DESC* pDesc, const D3D11_SUBRESOURCE_DATA* pInitialData,
                        const std::filesystem::path& path);

// Dump first subresource of CreateTexture3D (volume). Returns true on success.
bool DumpTexture3DToDDS(const D3D11_TEXTURE3D_DESC* pDesc, const D3D11_SUBRESOURCE_DATA* pInitialData,
                        const std::filesystem::path& path);

}  // namespace utils
