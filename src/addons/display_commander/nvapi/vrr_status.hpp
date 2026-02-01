#pragma once

#include <atomic>
#include <string>

#include <windows.h>

// NVAPI header lives in external/nvapi and is exposed via include paths in the project.
#include <nvapi.h>

namespace nvapi {

struct VrrStatus {
    bool nvapi_initialized = false;
    bool display_id_resolved = false;
    bool vrr_info_queried = false;

    NvAPI_Status resolve_status = NVAPI_ERROR;
    NvAPI_Status query_status = NVAPI_ERROR;

    NvU32 display_id = 0;
    std::string nvapi_display_name;

    // Fields from NV_GET_VRR_INFO (only valid if vrr_info_queried == true and query_status == NVAPI_OK)
    bool is_vrr_enabled = false;
    bool is_vrr_possible = false;
    bool is_vrr_requested = false;
    bool is_vrr_indicator_enabled = false;
    bool is_display_in_vrr_mode = false;
};

// DXGI output DeviceName is typically "\\\\.\\DISPLAY1". NVAPI expects "\\DISPLAY1".
// This helper queries VRR state via NvAPI_Disp_GetVRRInfo.
// NOTE: Function implementation moved to continuous_monitoring.cpp
// Forward declaration for external use
bool TryQueryVrrStatusFromDxgiOutputDeviceName(const wchar_t* dxgi_output_device_name, VrrStatus& out_status);

}  // namespace nvapi
