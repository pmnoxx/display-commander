// Source Code <Display Commander> // follow this order for includes in all files + add this comment at the top
// Shared DXGI color space string helper (logging, UI).

// Libraries <Windows.h> — before other Windows headers
#include <Windows.h>

// Libraries <Windows>
#include <dxgi1_4.h>

namespace utils {

/** Human-readable string for DXGI_COLOR_SPACE_TYPE (logging and UI). */
inline const char* GetDXGIColorSpaceString(DXGI_COLOR_SPACE_TYPE color_space) {
    switch (color_space) {
        case DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709:
            return "RGB Full G22 None P709";
        case DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709:
            return "RGB Full G10 None P709";
        case DXGI_COLOR_SPACE_RGB_STUDIO_G22_NONE_P709:
            return "RGB Studio G22 None P709";
        case DXGI_COLOR_SPACE_RGB_STUDIO_G22_NONE_P2020:
            return "RGB Studio G22 None P2020";
        case DXGI_COLOR_SPACE_RESERVED:
            return "Reserved";
        case DXGI_COLOR_SPACE_YCBCR_FULL_G22_NONE_P709_X601:
            return "YCbCr Full G22 None P709 X601";
        case DXGI_COLOR_SPACE_YCBCR_STUDIO_G22_LEFT_P601:
            return "YCbCr Studio G22 Left P601";
        case DXGI_COLOR_SPACE_YCBCR_FULL_G22_LEFT_P601:
            return "YCbCr Full G22 Left P601";
        case DXGI_COLOR_SPACE_YCBCR_STUDIO_G22_LEFT_P709:
            return "YCbCr Studio G22 Left P709";
        case DXGI_COLOR_SPACE_YCBCR_FULL_G22_LEFT_P709:
            return "YCbCr Full G22 Left P709";
        case DXGI_COLOR_SPACE_YCBCR_STUDIO_G22_LEFT_P2020:
            return "YCbCr Studio G22 Left P2020";
        case DXGI_COLOR_SPACE_YCBCR_FULL_G22_LEFT_P2020:
            return "YCbCr Full G22 Left P2020";
        case DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020:
            return "RGB Full G2084 None P2020 (HDR10)";
        case DXGI_COLOR_SPACE_YCBCR_STUDIO_G2084_LEFT_P2020:
            return "YCbCr Studio G2084 Left P2020";
        case DXGI_COLOR_SPACE_RGB_STUDIO_G2084_NONE_P2020:
            return "RGB Studio G2084 None P2020";
        case DXGI_COLOR_SPACE_YCBCR_STUDIO_G22_TOPLEFT_P2020:
            return "YCbCr Studio G22 TopLeft P2020";
        case DXGI_COLOR_SPACE_YCBCR_STUDIO_G2084_TOPLEFT_P2020:
            return "YCbCr Studio G2084 TopLeft P2020";
        case DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P2020:
            return "RGB Full G22 None P2020";
        case DXGI_COLOR_SPACE_YCBCR_STUDIO_G24_LEFT_P709:
            return "YCbCr Studio G24 Left P709";
        case DXGI_COLOR_SPACE_YCBCR_STUDIO_G24_LEFT_P2020:
            return "YCbCr Studio G24 Left P2020";
        case DXGI_COLOR_SPACE_YCBCR_STUDIO_G24_TOPLEFT_P2020:
            return "YCbCr Studio G24 TopLeft P2020";
        case DXGI_COLOR_SPACE_CUSTOM:
            return "Custom";
        default:
            return "Unknown";
    }
}

}  // namespace utils
