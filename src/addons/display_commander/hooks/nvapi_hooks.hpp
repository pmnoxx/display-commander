#pragma once

#include <windows.h>
#include "../../../external/nvapi/nvapi_lite_common.h"
#include "../../../external/nvapi/nvapi.h"

// NVAPI function pointer types
using NvAPI_Disp_GetHdrCapabilities_pfn = NvAPI_Status (__cdecl *)(NvU32, NV_HDR_CAPABILITIES*);

// NVAPI QueryInterface function type (returns void*, takes only ordinal)
using NvAPI_QueryInterface_pfn = void* (__cdecl *)(NvU32);

// Original function pointers
extern NvAPI_Disp_GetHdrCapabilities_pfn NvAPI_Disp_GetHdrCapabilities_Original;

// Hook functions
NvAPI_Status __cdecl NvAPI_Disp_GetHdrCapabilities_Detour(NvU32 displayId, NV_HDR_CAPABILITIES *pHdrCapabilities);

// Hook management
bool InstallNVAPIHooks();
void UninstallNVAPIHooks();
