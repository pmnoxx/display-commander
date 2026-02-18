#include "vram_info.hpp"
#include <nvapi.h>

namespace display_commander {
namespace nvapi {

bool GetVramInfoNvapi(uint64_t* out_used_bytes, uint64_t* out_total_bytes) {
    if (out_used_bytes == nullptr || out_total_bytes == nullptr) {
        return false;
    }

    if (NvAPI_Initialize() != NVAPI_OK) {
        return false;
    }

    NvPhysicalGpuHandle gpus[64] = {0};
    NvU32 gpu_count = 0;
    if (NvAPI_EnumPhysicalGPUs(gpus, &gpu_count) != NVAPI_OK || gpu_count == 0) {
        return false;
    }

    NV_GPU_MEMORY_INFO_EX meminfo = {};
    meminfo.version = NV_GPU_MEMORY_INFO_EX_VER;
    if (NvAPI_GPU_GetMemoryInfoEx(gpus[0], &meminfo) != NVAPI_OK) {
        return false;
    }

    // dedicatedVideoMemory is total physical VRAM (bytes); curAvailableDedicatedVideoMemory is current free.
    *out_total_bytes = meminfo.dedicatedVideoMemory;
    *out_used_bytes = (meminfo.dedicatedVideoMemory > meminfo.curAvailableDedicatedVideoMemory)
                          ? (meminfo.dedicatedVideoMemory - meminfo.curAvailableDedicatedVideoMemory)
                          : 0ULL;
    return true;
}

}  // namespace nvapi
}  // namespace display_commander
