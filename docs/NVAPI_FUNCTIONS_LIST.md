# NVAPI Functions Reference

This document lists NVAPI functions organized by category. Functions marked with ✅ are used in this codebase.

## Core/Initialization
- ✅ `NvAPI_Initialize()` - Initialize NVAPI
- ✅ `NvAPI_Unload()` - Unload NVAPI
- `NvAPI_GetErrorMessage()` - Get error message string
- `NvAPI_GetInterfaceVersionString()` - Get NVAPI version string
- `NvAPI_GetInterfaceVersionStringEx()` - Get extended NVAPI version string
- `NvAPI_QueryInterface()` - Query interface by ordinal (internal)

## GPU Enumeration & Information
- `NvAPI_EnumTCCPhysicalGPUs()` - Enumerate TCC physical GPUs
- `NvAPI_EnumLogicalGPUs()` - Enumerate logical GPUs
- `NvAPI_EnumPhysicalGPUs()` - Enumerate physical GPUs
- `NvAPI_GetPhysicalGPUsFromDisplay()` - Get physical GPUs from display
- `NvAPI_GetPhysicalGPUFromUnAttachedDisplay()` - Get physical GPU from unattached display
- `NvAPI_GetLogicalGPUFromDisplay()` - Get logical GPU from display
- `NvAPI_GetLogicalGPUFromPhysicalGPU()` - Get logical GPU from physical GPU
- `NvAPI_GetPhysicalGPUsFromLogicalGPU()` - Get physical GPUs from logical GPU
- `NvAPI_GetPhysicalGPUFromGPUID()` - Get physical GPU from GPU ID
- `NvAPI_GetGPUIDfromPhysicalGPU()` - Get GPU ID from physical GPU

## GPU Information Queries
- `NvAPI_GPU_GetFullName()` - Get GPU full name
- `NvAPI_GPU_GetPCIIdentifiers()` - Get PCI identifiers
- `NvAPI_GPU_GetGPUType()` - Get GPU type
- `NvAPI_GPU_GetBusType()` - Get bus type
- `NvAPI_GPU_GetBusId()` - Get bus ID
- `NvAPI_GPU_GetBusSlotId()` - Get bus slot ID
- `NvAPI_GPU_GetVbiosRevision()` - Get VBIOS revision
- `NvAPI_GPU_GetVbiosVersionString()` - Get VBIOS version string
- `NvAPI_GPU_GetPhysicalFrameBufferSize()` - Get physical framebuffer size
- `NvAPI_GPU_GetVirtualFrameBufferSize()` - Get virtual framebuffer size
- `NvAPI_GPU_GetGpuCoreCount()` - Get GPU core count
- `NvAPI_GPU_GetShaderSubPipeCount()` - Get shader sub-pipe count
- `NvAPI_GPU_GetRamBusWidth()` - Get RAM bus width
- `NvAPI_GPU_GetArchInfo()` - Get GPU architecture info
- `NvAPI_GPU_GetBoardInfo()` - Get board info
- `NvAPI_GPU_GetTachReading()` - Get tachometer reading (fan speed)
- `NvAPI_GPU_GetCurrentPCIEDownstreamWidth()` - Get current PCIe downstream width
- `NvAPI_GPU_GetCurrentAGPRate()` - Get current AGP rate
- `NvAPI_GPU_GetAGPAperture()` - Get AGP aperture
- `NvAPI_GPU_GetIRQ()` - Get IRQ
- `NvAPI_GPU_GetQuadroStatus()` - Get Quadro status
- `NvAPI_GPU_GetSystemType()` - Get system type
- `NvAPI_GPU_GetGPUInfo()` - Get comprehensive GPU info
- `NvAPI_GPU_GetVRReadyData()` - Get VR ready data

## GPU Memory & Performance
- `NvAPI_GPU_GetMemoryInfoEx()` - Get memory info (extended)
- `NvAPI_GPU_GetRamType()` - Get RAM type
- `NvAPI_GPU_GetFBWidthAndLocation()` - Get framebuffer width and location
- `NvAPI_GPU_GetAllClockFrequencies()` - Get all clock frequencies
- `NvAPI_GPU_GetDynamicPstatesInfoEx()` - Get dynamic P-states info
- `NvAPI_GPU_GetCurrentPstate()` - Get current P-state
- `NvAPI_GPU_GetPerfDecreaseInfo()` - Get performance decrease info
- `NvAPI_GPU_GetThermalSettings()` - Get thermal settings
- `NvAPI_GPU_GetPCIEInfo()` - Get PCIe info

## Display Management
- `NvAPI_EnumNvidiaDisplayHandle()` - Enumerate NVIDIA display handles
- `NvAPI_GetDisplayDriverVersion()` - Get display driver version
- `NvAPI_GPU_GetAllOutputs()` - Get all outputs
- `NvAPI_GPU_GetConnectedOutputs()` - Get connected outputs
- `NvAPI_GPU_GetActiveOutputs()` - Get active outputs
- `NvAPI_GPU_GetOutputType()` - Get output type
- `NvAPI_GPU_GetConnectedDisplayIds()` - Get connected display IDs
- `NvAPI_GPU_GetAllDisplayIds()` - Get all display IDs
- `NvAPI_GPU_ValidateOutputCombination()` - Validate output combination
- `NvAPI_GPU_SetEDID()` - Set EDID
- ✅ `NvAPI_GPU_GetEDID()` - Get EDID
- `NvAPI_SetView()` - Set view
- `NvAPI_SetViewEx()` - Set view (extended)

## Display Configuration
- `NvAPI_DISP_GetDisplayIdByDisplayName()` - Get display ID by display name
- ✅ `NvAPI_DISP_GetDisplayIdByDisplayName()` - Get display ID by display name (used in VRR status)
- `NvAPI_Disp_GetDisplayIdInfo()` - Get display ID info
- `NvAPI_Disp_GetDisplayIdsFromTarget()` - Get display IDs from target
- `NvAPI_SYS_GetDisplayIdFromGpuAndOutputId()` - Get display ID from GPU and output ID

## VRR (Variable Refresh Rate) / G-SYNC
- ✅ `NvAPI_Disp_GetVRRInfo()` - Get VRR info (used in VRR status)
- `NvAPI_DISP_GetAdaptiveSyncData()` - Get adaptive sync data
- `NvAPI_DISP_SetAdaptiveSyncData()` - Set adaptive sync data
- `NvAPI_DISP_GetMonitorCapabilities()` - Get monitor capabilities

## HDR (High Dynamic Range)
- ✅ `NvAPI_Disp_GetHdrCapabilities()` - Get HDR capabilities (hooked in codebase)
- `NvAPI_Disp_HdrColorControl()` - HDR color control
- `NvAPI_Disp_ColorControl()` - Color control

## D3D11/D3D12 Integration
- ✅ `NvAPI_D3D_SetSleepMode_Direct()` - Set sleep mode (Reflex)
- ✅ `NvAPI_D3D_Sleep_Direct()` - Sleep (Reflex)
- ✅ `NvAPI_D3D_SetLatencyMarker_Direct()` - Set latency marker (Reflex)
- ✅ `NvAPI_D3D_GetLatency_Direct()` - Get latency (Reflex)
- `NvAPI_D3D_IsGSyncCapable()` - Check if G-SYNC capable
- `NvAPI_D3D_IsGSyncActive()` - Check if G-SYNC active
- `NvAPI_D3D11_CreateFastGeometryShader()` - Create fast geometry shader
- `NvAPI_D3D11_CreateCubinComputeShader()` - Create CUBIN compute shader
- `NvAPI_D3D11_LaunchCubinShader()` - Launch CUBIN shader
- `NvAPI_D3D12_CreateCubinComputeShader()` - Create CUBIN compute shader (D3D12)
- `NvAPI_D3D12_LaunchCubinShader()` - Launch CUBIN shader (D3D12)

## Driver Settings (DRS)
- ✅ `NvAPI_DRS_CreateSession()` - Create DRS session (used for fullscreen prevention)
- ✅ `NvAPI_DRS_LoadSettings()` - Load DRS settings
- ✅ `NvAPI_DRS_SaveSettings()` - Save DRS settings
- ✅ `NvAPI_DRS_FindApplicationByName()` - Find application by name
- ✅ `NvAPI_DRS_CreateProfile()` - Create profile
- ✅ `NvAPI_DRS_CreateApplication()` - Create application
- ✅ `NvAPI_DRS_SetSetting()` - Set setting (used for OGL_DX_PRESENT_DEBUG)
- `NvAPI_DRS_GetSetting()` - Get setting
- `NvAPI_DRS_DeleteProfile()` - Delete profile
- `NvAPI_DRS_DeleteApplication()` - Delete application

## System Information
- ✅ `NvAPI_SYS_GetDriverAndBranchVersion()` - Get driver and branch version
- `NvAPI_SYS_GetDisplayIdFromGpuAndOutputId()` - Get display ID from GPU and output ID

## SLI / Multi-GPU
- `NvAPI_GPU_GetConnectedSLIOutputs()` - Get connected SLI outputs
- `NvAPI_GPU_ValidateOutputCombination()` - Validate output combination
- `NvAPI_D3D11_MultiGPU_Init()` - Initialize multi-GPU
- `NvAPI_D3D11_MultiGPU_GetCaps()` - Get multi-GPU capabilities

## I2C Communication
- `NvAPI_I2CRead()` - I2C read
- `NvAPI_I2CWrite()` - I2C write

## ECC (Error Correcting Code)
- `NvAPI_GPU_GetECCStatusInfo()` - Get ECC status info
- `NvAPI_GPU_GetECCErrorInfo()` - Get ECC error info
- `NvAPI_GPU_ResetECCErrorInfo()` - Reset ECC error info
- `NvAPI_GPU_GetECCConfigurationInfo()` - Get ECC configuration info
- `NvAPI_GPU_SetECCConfiguration()` - Set ECC configuration

## Workstation Features
- `NvAPI_GPU_WorkstationFeatureSetup()` - Setup workstation features
- `NvAPI_GPU_WorkstationFeatureQuery()` - Query workstation features
- `NvAPI_GPU_QueryWorkstationFeatureSupport()` - Query workstation feature support

## Scanout / Display Output
- `NvAPI_GPU_SetScanoutIntensity()` - Set scanout intensity
- `NvAPI_GPU_GetScanoutIntensityState()` - Get scanout intensity state
- `NvAPI_GPU_SetScanoutWarping()` - Set scanout warping
- `NvAPI_GPU_GetScanoutWarpingState()` - Get scanout warping state
- `NvAPI_GPU_SetScanoutCompositionParameter()` - Set scanout composition parameter
- `NvAPI_GPU_GetScanoutCompositionParameter()` - Get scanout composition parameter
- `NvAPI_GPU_GetScanoutConfiguration()` - Get scanout configuration
- `NvAPI_GPU_GetScanoutConfigurationEx()` - Get scanout configuration (extended)

## CUDA / Compute
- `NvAPI_GPU_CudaEnumComputeCapableGpus()` - Enumerate CUDA compute capable GPUs
- `NvAPI_D3D11_GetCudaTextureObject()` - Get CUDA texture object
- `NvAPI_D3D11_GetCudaMergedTextureSamplerObject()` - Get CUDA merged texture sampler object
- `NvAPI_D3D12_GetCudaTextureObject()` - Get CUDA texture object (D3D12)
- `NvAPI_D3D12_GetCudaSurfaceObject()` - Get CUDA surface object (D3D12)

## NVLINK
- `NvAPI_GPU_NVLINK_GetCaps()` - Get NVLINK capabilities
- `NvAPI_GPU_NVLINK_GetStatus()` - Get NVLINK status

## Encoding / Video
- `NvAPI_GPU_GetEncoderStatistics()` - Get encoder statistics
- `NvAPI_GPU_GetEncoderSessionsInfo()` - Get encoder sessions info

## Ray Tracing
- `NvAPI_D3D12_BuildRaytracingAccelerationStructureEx()` - Build RT acceleration structure (extended)
- `NvAPI_D3D12_BuildRaytracingDisplacementMicromapArray()` - Build RT displacement micromap array
- `NvAPI_D3D12_BuildRaytracingOpacityMicromapArray()` - Build RT opacity micromap array
- `NvAPI_D3D12_EnableRaytracingValidation()` - Enable RT validation
- `NvAPI_D3D12_FlushRaytracingValidationMessages()` - Flush RT validation messages

## OpenGL
- `NvAPI_OGL_ExpertModeSet()` - Set OpenGL expert mode
- `NvAPI_OGL_ExpertModeGet()` - Get OpenGL expert mode
- `NvAPI_OGL_ExpertModeDefaultsSet()` - Set OpenGL expert mode defaults
- `NvAPI_OGL_ExpertModeDefaultsGet()` - Get OpenGL expert mode defaults

## HDCP
- `NvAPI_GPU_GetHDCPSupportStatus()` - Get HDCP support status

## Virtualization
- `NvAPI_GPU_GetVirtualizationInfo()` - Get virtualization info
- `NvAPI_GPU_GetLogicalGpuInfo()` - Get logical GPU info

## Licensing
- `NvAPI_GPU_GetLicensableFeatures()` - Get licensable features

## Notes

- Functions marked with ✅ are actively used in the Display Commander codebase
- Most functions require `NvAPI_Initialize()` to be called first
- Many functions require specific driver versions or GPU capabilities
- DRS (Driver Settings) functions are used for per-application profile management
- VRR/HDR functions are used for display capability queries
- Reflex functions (D3D_SetSleepMode, D3D_Sleep, etc.) are used for low-latency frame pacing

