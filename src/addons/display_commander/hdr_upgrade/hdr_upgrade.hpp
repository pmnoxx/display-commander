#pragma once

#include <reshade.hpp>

namespace display_commander::hdr_upgrade {

/** use_hdr10: false = scRGB (R16G16B16A16_FLOAT), true = HDR10 (R10G10B10A2_UNORM).
 *  Called from create_swapchain event when Swapchain HDR Upgrade is enabled. Modifies desc to request
 *  HDR back buffer, FLIP_DISCARD, ALLOW_TEARING, and back_buffer_count >= 2.
 *  Only applies to DXGI (D3D10/11/12). Returns true if desc was modified. */
bool ModifyCreateSwapchainDesc(reshade::api::device_api api, reshade::api::swapchain_desc& desc, bool use_hdr10);

/** use_hdr10: false = scRGB, true = HDR10. Called from init_swapchain event when Swapchain HDR Upgrade
 *  is enabled. Tracks back buffer handles, checks display HDR support, ResizeBuffers, SetColorSpace1,
 *  and effect_runtime::set_color_space. DXGI only. */
void OnInitSwapchain(reshade::api::swapchain* swapchain, bool resize, bool use_hdr10);

/** Called from destroy_swapchain. Removes this swap chain's back buffers from the tracked set. */
void OnDestroySwapchain(reshade::api::swapchain* swapchain, bool resize);

/** Called from create_resource_view. If the resource is a tracked HDR back buffer, overrides desc.format
 *  to match (R16G16B16A16_FLOAT or R10G10B10A2_UNORM). Returns true if desc was modified. */
bool OnCreateResourceView(reshade::api::device* device, reshade::api::resource resource,
                          reshade::api::resource_usage usage_type, reshade::api::resource_view_desc& desc);

}  // namespace display_commander::hdr_upgrade
