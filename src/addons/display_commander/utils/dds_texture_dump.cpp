// Source Code <Display Commander> // follow this order for includes in all files + add this comment at the top
#include "dds_texture_dump.hpp"
#include "logging.hpp"

// Libraries <ReShade> / <imgui>
// (none)

// Libraries <standard C++>
#include <cstdint>
#include <fstream>

// Libraries <Windows.h> — before other Windows headers
#include <Windows.h>

// Libraries <Windows>
#include <d3d11.h>

namespace utils {

namespace {

constexpr uint32_t DDS_MAGIC = 0x20534444;  // "DDS "
constexpr uint32_t DDS_HEADER_SIZE = 124;
constexpr uint32_t DDSD_CAPS = 0x1;
constexpr uint32_t DDSD_HEIGHT = 0x2;
constexpr uint32_t DDSD_WIDTH = 0x4;
constexpr uint32_t DDSD_PITCH = 0x8;
constexpr uint32_t DDSD_PIXELFORMAT = 0x1000;
constexpr uint32_t DDSD_MIPMAPCOUNT = 0x20000;
constexpr uint32_t DDSD_LINEARSIZE = 0x80000;
constexpr uint32_t DDSD_DEPTH = 0x800000;
constexpr uint32_t DDPF_FOURCC = 0x4;
constexpr uint32_t DDSCAPS_TEXTURE = 0x1000;
constexpr uint32_t DDSCAPS2_VOLUME = 0x200000;

#pragma pack(push, 4)
struct DDS_PIXELFORMAT {
    uint32_t dwSize;
    uint32_t dwFlags;
    uint32_t dwFourCC;
    uint32_t dwRGBBitCount;
    uint32_t dwRBitMask;
    uint32_t dwGBitMask;
    uint32_t dwBBitMask;
    uint32_t dwABitMask;
};
struct DDS_HEADER {
    uint32_t dwSize;
    uint32_t dwFlags;
    uint32_t dwHeight;
    uint32_t dwWidth;
    uint32_t dwPitchOrLinearSize;
    uint32_t dwDepth;
    uint32_t dwMipMapCount;
    uint32_t dwReserved1[11];
    DDS_PIXELFORMAT ddspf;
    uint32_t dwCaps;
    uint32_t dwCaps2;
    uint32_t dwCaps3;
    uint32_t dwCaps4;
    uint32_t dwReserved2;
};
// DX10 extended header (after DDS_HEADER when FourCC == "DX10")
enum D3D10_RESOURCE_DIMENSION : uint32_t {
    D3D10_RESOURCE_DIMENSION_UNKNOWN = 0,
    D3D10_RESOURCE_DIMENSION_BUFFER = 1,
    D3D10_RESOURCE_DIMENSION_TEXTURE1D = 2,
    D3D10_RESOURCE_DIMENSION_TEXTURE2D = 3,
    D3D10_RESOURCE_DIMENSION_TEXTURE3D = 4
};
struct DDS_HEADER_DXT10 {
    uint32_t dxgiFormat;
    uint32_t resourceDimension;
    uint32_t miscFlag;
    uint32_t arraySize;
    uint32_t miscFlags2;
};
#pragma pack(pop)

static constexpr uint32_t MakeFourCC(char a, char b, char c, char d) {
    return (uint32_t)(uint8_t)a | ((uint32_t)(uint8_t)b << 8) | ((uint32_t)(uint8_t)c << 16)
           | ((uint32_t)(uint8_t)d << 24);
}
constexpr uint32_t DX10_FOURCC = MakeFourCC('D', 'X', '1', '0');

static bool WriteDDSFile(const void* pData, size_t dataBytes, const DDS_HEADER& header,
                        const DDS_HEADER_DXT10* pHeader10, const std::filesystem::path& path) {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) {
        LogWarn("DDS dump: failed to create directory %s: %s", path.string().c_str(), ec.message().c_str());
        return false;
    }
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        LogWarn("DDS dump: failed to open %s for writing", path.string().c_str());
        return false;
    }
    out.write(reinterpret_cast<const char*>(&DDS_MAGIC), sizeof(DDS_MAGIC));
    out.write(reinterpret_cast<const char*>(&header), sizeof(header));
    if (pHeader10 != nullptr) {
        out.write(reinterpret_cast<const char*>(pHeader10), sizeof(DDS_HEADER_DXT10));
    }
    if (pData != nullptr && dataBytes > 0) {
        out.write(static_cast<const char*>(pData), static_cast<std::streamsize>(dataBytes));
    }
    if (!out) {
        LogWarn("DDS dump: write failed for %s", path.string().c_str());
        return false;
    }
    return true;
}

}  // namespace

bool DumpTexture2DToDDS(const D3D11_TEXTURE2D_DESC* pDesc, const D3D11_SUBRESOURCE_DATA* pInitialData,
                        const std::filesystem::path& path) {
    if (pDesc == nullptr || pInitialData == nullptr || pInitialData->pSysMem == nullptr) {
        return false;
    }
    const UINT width = pDesc->Width;
    const UINT height = pDesc->Height;
    if (width == 0 || height == 0) return false;
    const UINT pitch = pInitialData->SysMemPitch;
    // For 2D textures SysMemSlicePitch is 0 per D3D11; compute size as pitch * height.
    const size_t dataSize = (pInitialData->SysMemSlicePitch != 0)
                                ? static_cast<size_t>(pInitialData->SysMemSlicePitch)
                                : (static_cast<size_t>(pitch) * static_cast<size_t>(height));
    if (dataSize == 0 || pitch == 0) return false;

    DDS_HEADER h = {};
    h.dwSize = DDS_HEADER_SIZE;
    h.dwFlags = DDSD_CAPS | DDSD_HEIGHT | DDSD_WIDTH | DDSD_PIXELFORMAT | DDSD_PITCH | DDSD_MIPMAPCOUNT;
    h.dwHeight = height;
    h.dwWidth = width;
    h.dwPitchOrLinearSize = pitch;
    h.dwDepth = 0;
    h.dwMipMapCount = (pDesc->MipLevels != 0) ? pDesc->MipLevels : 1;
    h.ddspf.dwSize = sizeof(DDS_PIXELFORMAT);
    h.ddspf.dwFlags = DDPF_FOURCC;
    h.ddspf.dwFourCC = DX10_FOURCC;
    h.ddspf.dwRGBBitCount = 0;
    h.ddspf.dwRBitMask = h.ddspf.dwGBitMask = h.ddspf.dwBBitMask = h.ddspf.dwABitMask = 0;
    h.dwCaps = DDSCAPS_TEXTURE;
    h.dwCaps2 = 0;
    h.dwCaps3 = h.dwCaps4 = h.dwReserved2 = 0;

    DDS_HEADER_DXT10 h10 = {};
    h10.dxgiFormat = static_cast<uint32_t>(pDesc->Format);
    h10.resourceDimension = D3D10_RESOURCE_DIMENSION_TEXTURE2D;
    h10.miscFlag = 0;
    h10.arraySize = (pDesc->ArraySize != 0) ? pDesc->ArraySize : 1;
    h10.miscFlags2 = 0;

    return WriteDDSFile(pInitialData->pSysMem, dataSize, h, &h10, path);
}

bool DumpTexture1DToDDS(const D3D11_TEXTURE1D_DESC* pDesc, const D3D11_SUBRESOURCE_DATA* pInitialData,
                        const std::filesystem::path& path) {
    if (pDesc == nullptr || pInitialData == nullptr || pInitialData->pSysMem == nullptr) {
        return false;
    }
    const UINT width = pDesc->Width;
    if (width == 0) return false;
    const size_t dataSize = static_cast<size_t>(pInitialData->SysMemPitch);

    DDS_HEADER h = {};
    h.dwSize = DDS_HEADER_SIZE;
    h.dwFlags = DDSD_CAPS | DDSD_HEIGHT | DDSD_WIDTH | DDSD_PIXELFORMAT | DDSD_PITCH | DDSD_MIPMAPCOUNT;
    h.dwHeight = 1;
    h.dwWidth = width;
    h.dwPitchOrLinearSize = static_cast<uint32_t>(dataSize);
    h.dwDepth = 0;
    h.dwMipMapCount = (pDesc->MipLevels != 0) ? pDesc->MipLevels : 1;
    h.ddspf.dwSize = sizeof(DDS_PIXELFORMAT);
    h.ddspf.dwFlags = DDPF_FOURCC;
    h.ddspf.dwFourCC = DX10_FOURCC;
    h.ddspf.dwRGBBitCount = 0;
    h.ddspf.dwRBitMask = h.ddspf.dwGBitMask = h.ddspf.dwBBitMask = h.ddspf.dwABitMask = 0;
    h.dwCaps = DDSCAPS_TEXTURE;
    h.dwCaps2 = 0;
    h.dwCaps3 = h.dwCaps4 = h.dwReserved2 = 0;

    DDS_HEADER_DXT10 h10 = {};
    h10.dxgiFormat = static_cast<uint32_t>(pDesc->Format);
    h10.resourceDimension = D3D10_RESOURCE_DIMENSION_TEXTURE1D;
    h10.miscFlag = 0;
    h10.arraySize = (pDesc->ArraySize != 0) ? pDesc->ArraySize : 1;
    h10.miscFlags2 = 0;

    return WriteDDSFile(pInitialData->pSysMem, dataSize, h, &h10, path);
}

bool DumpTexture3DToDDS(const D3D11_TEXTURE3D_DESC* pDesc, const D3D11_SUBRESOURCE_DATA* pInitialData,
                        const std::filesystem::path& path) {
    if (pDesc == nullptr || pInitialData == nullptr || pInitialData->pSysMem == nullptr) {
        return false;
    }
    const UINT width = pDesc->Width;
    const UINT height = pDesc->Height;
    const UINT depth = pDesc->Depth;
    if (width == 0 || height == 0 || depth == 0) return false;
    const UINT slicePitch = pInitialData->SysMemSlicePitch;
    const size_t dataSize = static_cast<size_t>(slicePitch) * static_cast<size_t>(depth);

    DDS_HEADER h = {};
    h.dwSize = DDS_HEADER_SIZE;
    h.dwFlags = DDSD_CAPS | DDSD_HEIGHT | DDSD_WIDTH | DDSD_PIXELFORMAT | DDSD_DEPTH | DDSD_MIPMAPCOUNT
                | DDSD_LINEARSIZE;
    h.dwHeight = height;
    h.dwWidth = width;
    h.dwPitchOrLinearSize = static_cast<uint32_t>(dataSize);
    h.dwDepth = depth;
    h.dwMipMapCount = (pDesc->MipLevels != 0) ? pDesc->MipLevels : 1;
    h.ddspf.dwSize = sizeof(DDS_PIXELFORMAT);
    h.ddspf.dwFlags = DDPF_FOURCC;
    h.ddspf.dwFourCC = DX10_FOURCC;
    h.ddspf.dwRGBBitCount = 0;
    h.ddspf.dwRBitMask = h.ddspf.dwGBitMask = h.ddspf.dwBBitMask = h.ddspf.dwABitMask = 0;
    h.dwCaps = DDSCAPS_TEXTURE;
    h.dwCaps2 = DDSCAPS2_VOLUME;
    h.dwCaps3 = h.dwCaps4 = h.dwReserved2 = 0;

    DDS_HEADER_DXT10 h10 = {};
    h10.dxgiFormat = static_cast<uint32_t>(pDesc->Format);
    h10.resourceDimension = D3D10_RESOURCE_DIMENSION_TEXTURE3D;
    h10.miscFlag = 0;
    h10.arraySize = 1;
    h10.miscFlags2 = 0;

    return WriteDDSFile(pInitialData->pSysMem, dataSize, h, &h10, path);
}

}  // namespace utils
