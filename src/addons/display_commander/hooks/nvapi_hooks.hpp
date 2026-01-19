#pragma once

#include <windows.h>
#include "../../../external/nvapi/nvapi_lite_common.h"
#include "../../../external/nvapi/nvapi.h"

// NVAPI function pointer types
using NvAPI_Disp_GetHdrCapabilities_pfn = NvAPI_Status (__cdecl *)(NvU32, NV_HDR_CAPABILITIES*);
using NvAPI_D3D_SetSleepMode_pfn = NvAPI_Status (__cdecl *)(__in IUnknown *pDev, __in NV_SET_SLEEP_MODE_PARAMS *pSetSleepModeParams);
using NvAPI_D3D_Sleep_pfn = NvAPI_Status (__cdecl *)(__in IUnknown *pDev);
using NvAPI_D3D_SetLatencyMarker_pfn = NvAPI_Status (__cdecl *)(__in IUnknown *pDev, __in NV_LATENCY_MARKER_PARAMS *pSetLatencyMarkerParams);
using NvAPI_D3D_GetLatency_pfn = NvAPI_Status (__cdecl *)(__in IUnknown *pDev, __in NV_LATENCY_RESULT_PARAMS *pGetLatencyParams);
using NvAPI_D3D_GetSleepStatus_pfn = NvAPI_Status (__cdecl *)(__in IUnknown *pDev, __in NV_GET_SLEEP_STATUS_PARAMS *pGetSleepStatusParams);

// NVAPI QueryInterface function type (returns void*, takes only ordinal)
using NvAPI_QueryInterface_pfn = void* (__cdecl *)(NvU32);

// Original function pointers
extern NvAPI_Disp_GetHdrCapabilities_pfn NvAPI_Disp_GetHdrCapabilities_Original;
extern NvAPI_D3D_SetSleepMode_pfn NvAPI_D3D_SetSleepMode_Original;
extern NvAPI_D3D_Sleep_pfn NvAPI_D3D_Sleep_Original;
extern NvAPI_D3D_SetLatencyMarker_pfn NvAPI_D3D_SetLatencyMarker_Original;
extern NvAPI_D3D_GetLatency_pfn NvAPI_D3D_GetLatency_Original;

// Hook functions
NvAPI_Status __cdecl NvAPI_Disp_GetHdrCapabilities_Detour(NvU32 displayId, NV_HDR_CAPABILITIES *pHdrCapabilities);

// Direct call functions (bypass stats tracking for internal use)
NvAPI_Status NvAPI_D3D_SetSleepMode_Direct(IUnknown *pDev, NV_SET_SLEEP_MODE_PARAMS *pSetSleepModeParams);
NvAPI_Status NvAPI_D3D_Sleep_Direct(IUnknown *pDev);
NvAPI_Status NvAPI_D3D_SetLatencyMarker_Direct(IUnknown *pDev, NV_LATENCY_MARKER_PARAMS *pSetLatencyMarkerParams);
NvAPI_Status NvAPI_D3D_GetLatency_Direct(IUnknown *pDev, NV_LATENCY_RESULT_PARAMS *pGetLatencyParams);
NvAPI_Status NvAPI_D3D_GetSleepStatus_Direct(IUnknown *pDev, NV_GET_SLEEP_STATUS_PARAMS *pGetSleepStatusParams);

// Hook management
bool InstallNVAPIHooks(HMODULE nvapi_module = nullptr);
void UninstallNVAPIHooks();
