// Source Code <Display Commander> // follow this order for includes in all files + add this comment at the top
#include "swapchain_hdr_metadata.hpp"
#include "../../config/display_commander_config.hpp"
#include "../../globals.hpp"

// Libraries <ReShade>
#include <reshade.hpp>

// Libraries <standard C++>
#include <cmath>

// Libraries <Windows.h> — before other Windows headers
#include <Windows.h>

// Libraries <Windows> / DXGI
#include <dxgi1_6.h>

namespace ui::new_ui {
namespace {

// CTA-861-G / DXGI HDR10: chromaticity encoded as 0-50000 for 0.00000-0.50000 (0.00001 steps)
constexpr UINT32 HDR10_CHROMATICITY_SCALE = 50000u;

bool has_last_metadata = false;
bool auto_apply_hdr_metadata = false;

[[maybe_unused]] DXGI_HDR_METADATA_HDR10 last_hdr_metadata = {
    .RedPrimary = {32000, 16500},
    .GreenPrimary = {15000, 30000},
    .BluePrimary = {7500, 3000},
    .WhitePoint = {15635, 16450},
    .MaxMasteringLuminance = 1000,
    .MinMasteringLuminance = 0,
    .MaxContentLightLevel = 1000,
    .MaxFrameAverageLightLevel = 400,
};

}  // namespace

void InitSwapchainTab() {
    double prim_red_x = 0.708;
    double prim_red_y = 0.292;
    double prim_green_x = 0.170;
    double prim_green_y = 0.797;
    double prim_blue_x = 0.131;
    double prim_blue_y = 0.046;
    double white_point_x = 0.3127;
    double white_point_y = 0.3290;
    int32_t max_mdl = 1000;
    float min_mdl = 0.0f;
    int32_t max_cll = 1000;
    int32_t max_fall = 100;

    display_commander::config::get_config_value("ReShade_HDR_Metadata", "prim_red_x", prim_red_x);
    display_commander::config::get_config_value("ReShade_HDR_Metadata", "prim_red_y", prim_red_y);
    display_commander::config::get_config_value("ReShade_HDR_Metadata", "prim_green_x", prim_green_x);
    display_commander::config::get_config_value("ReShade_HDR_Metadata", "prim_green_y", prim_green_y);
    display_commander::config::get_config_value("ReShade_HDR_Metadata", "prim_blue_x", prim_blue_x);
    display_commander::config::get_config_value("ReShade_HDR_Metadata", "prim_blue_y", prim_blue_y);
    display_commander::config::get_config_value("ReShade_HDR_Metadata", "white_point_x", white_point_x);
    display_commander::config::get_config_value("ReShade_HDR_Metadata", "white_point_y", white_point_y);
    display_commander::config::get_config_value("ReShade_HDR_Metadata", "max_mdl", max_mdl);
    display_commander::config::get_config_value("ReShade_HDR_Metadata", "min_mdl", min_mdl);
    display_commander::config::get_config_value("ReShade_HDR_Metadata", "max_cll", max_cll);
    display_commander::config::get_config_value("ReShade_HDR_Metadata", "max_fall", max_fall);

    display_commander::config::get_config_value("ReShade_HDR_Metadata", "has_last_metadata", has_last_metadata);
    display_commander::config::get_config_value("ReShade_HDR_Metadata", "auto_apply_hdr_metadata",
                                                auto_apply_hdr_metadata);

    last_hdr_metadata.RedPrimary[0] = static_cast<UINT16>(std::round(prim_red_x * HDR10_CHROMATICITY_SCALE));
    last_hdr_metadata.RedPrimary[1] = static_cast<UINT16>(std::round(prim_red_y * HDR10_CHROMATICITY_SCALE));
    last_hdr_metadata.GreenPrimary[0] = static_cast<UINT16>(std::round(prim_green_x * HDR10_CHROMATICITY_SCALE));
    last_hdr_metadata.GreenPrimary[1] = static_cast<UINT16>(std::round(prim_green_y * HDR10_CHROMATICITY_SCALE));
    last_hdr_metadata.BluePrimary[0] = static_cast<UINT16>(std::round(prim_blue_x * HDR10_CHROMATICITY_SCALE));
    last_hdr_metadata.BluePrimary[1] = static_cast<UINT16>(std::round(prim_blue_y * HDR10_CHROMATICITY_SCALE));
    last_hdr_metadata.WhitePoint[0] = static_cast<UINT16>(std::round(white_point_x * HDR10_CHROMATICITY_SCALE));
    last_hdr_metadata.WhitePoint[1] = static_cast<UINT16>(std::round(white_point_y * HDR10_CHROMATICITY_SCALE));
    last_hdr_metadata.MaxMasteringLuminance = static_cast<UINT>(max_mdl);
    last_hdr_metadata.MinMasteringLuminance = static_cast<UINT>(min_mdl * 10000.0f);
    last_hdr_metadata.MaxContentLightLevel = static_cast<UINT16>(max_cll);
    last_hdr_metadata.MaxFrameAverageLightLevel = static_cast<UINT16>(max_fall);

}

void AutoApplyTrigger() {
    if (!auto_apply_hdr_metadata) {
        return;
    }
    if (g_last_reshade_device_api.load() != reshade::api::device_api::d3d12
        && g_last_reshade_device_api.load() != reshade::api::device_api::d3d11
        && g_last_reshade_device_api.load() != reshade::api::device_api::d3d10) {
        return;
    }
    HWND hwnd = g_last_swapchain_hwnd.load();
    if (hwnd == nullptr || !IsWindow(hwnd)) {
        return;
    }
    (void)hwnd;
    (void)has_last_metadata;
    // IDXGISwapChain4 is not resolved from HWND here; avoid calling SetHDRMetaData on nullptr.
}

}  // namespace ui::new_ui
