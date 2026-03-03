// Source Code <Display Commander> // follow this order for includes in all files
#include "nvapi_init.hpp"
#include "../utils/logging.hpp"

#include <atomic>
#include <cstdio>

#include <windows.h>

// NVAPI header lives in external/nvapi and is exposed via include paths in the project.
#include <nvapi.h>

namespace nvapi {

bool EnsureNvApiInitialized() {
    static std::atomic<bool> g_inited{false};
    static std::atomic<bool> g_failed{false};

    if (g_inited.load(std::memory_order_acquire)) {
        return true;
    }
    if (g_failed.load(std::memory_order_acquire)) {
        return false;
    }

    const NvAPI_Status st = NvAPI_Initialize();
    if (st != NVAPI_OK) {
        LogWarn("[nvapi] NVAPI_Initialize failed: %d", st);
        g_failed.store(true, std::memory_order_release);
        return false;
    }
    LogInfo("[nvapi] NVAPI_Initialize succeeded");

    NvU32 driver_version = 0;
    NvAPI_ShortString branch_string = {};
    if (NvAPI_SYS_GetDriverAndBranchVersion(&driver_version, branch_string) == NVAPI_OK) {
        const unsigned major = driver_version / 100;
        const unsigned minor = driver_version % 100;
        char branch_buf[64] = {};
        if (branch_string[0] != '\0') {
            (void)snprintf(branch_buf, sizeof(branch_buf), " (branch: %s)", branch_string);
        }
        LogInfo("[nvapi] NVIDIA driver version (NVAPI): %u.%02u%s", major, minor, branch_buf);
    } else {
        LogWarn("[nvapi] Failed to get NVIDIA driver version");
    }

    g_inited.store(true, std::memory_order_release);
    return true;
}

}  // namespace nvapi
