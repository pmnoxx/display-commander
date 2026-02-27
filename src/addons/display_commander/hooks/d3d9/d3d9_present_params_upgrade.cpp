#include "d3d9_present_params_upgrade.hpp"
#include "../../globals.hpp"
#include "../../settings/advanced_tab_settings.hpp"
#include "../../settings/experimental_tab_settings.hpp"
#include "../../settings/main_tab_settings.hpp"
#include "../../utils/logging.hpp"

#include <d3d9.h>

namespace display_commanderhooks::d3d9 {

bool ApplyD3D9PresentParameterUpgrades(D3DPRESENT_PARAMETERS* pp, bool is_create_device_ex) {
    LogInfo("ApplyD3D9PresentParameterUpgrades: pp=%p, is_create_device_ex=%d", pp, is_create_device_ex);
    if (pp == nullptr) {
        return false;
    }

    bool modified = false;

    // Prevent fullscreen (force windowed) if enabled
    if (pp->Windowed == FALSE && settings::g_advancedTabSettings.prevent_fullscreen.GetValue()) {
        LogInfo("D3D9 (no-ReShade): Forcing windowed mode (prevent fullscreen)");
        pp->Windowed = TRUE;
        modified = true;
    }

    // Increase back buffer count to 3 if enabled
    if (settings::g_mainTabSettings.increase_backbuffer_count_to_3.GetValue() && pp->BackBufferCount < 3) {
        LogInfo("D3D9 (no-ReShade): Increasing back buffer count from %u to 3", pp->BackBufferCount);
        pp->BackBufferCount = 3;
        modified = true;
    }

    // FLIPEX and VSync upgrades only for CreateDeviceEx (D3D9Ex)
    if (is_create_device_ex && settings::g_experimentalTabSettings.d3d9_flipex_enabled.GetValue()
        && pp->SwapEffect != D3DSWAPEFFECT_FLIPEX) {
        if (pp->BackBufferCount < 3) {
            LogInfo("D3D9 FLIPEX (no-ReShade): Increasing back buffer count from %u to 3 (required for FLIPEX)",
                    pp->BackBufferCount);
            pp->BackBufferCount = 3;
            modified = true;
        }
        LogInfo("D3D9 FLIPEX (no-ReShade): Upgrading swap effect from %u to FLIPEX (5)", pp->SwapEffect);
        pp->SwapEffect = D3DSWAPEFFECT_FLIPEX;

        if (pp->PresentationInterval != D3DPRESENT_INTERVAL_IMMEDIATE) {
            LogInfo("D3D9 FLIPEX (no-ReShade): Setting sync interval to immediate");
            pp->PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
            modified = true;
        }
        if ((pp->Flags & D3DPRESENT_DONOTFLIP) != 0) {
            LogInfo("D3D9 FLIPEX (no-ReShade): Stripping D3DPRESENT_DONOTFLIP flag");
            pp->Flags &= ~D3DPRESENT_DONOTFLIP;
            modified = true;
        }
        if ((pp->Flags & D3DPRESENTFLAG_LOCKABLE_BACKBUFFER) != 0) {
            LogInfo("D3D9 FLIPEX (no-ReShade): Stripping D3DPRESENTFLAG_LOCKABLE_BACKBUFFER flag");
            pp->Flags &= ~D3DPRESENTFLAG_LOCKABLE_BACKBUFFER;
            modified = true;
        }
        if ((pp->Flags & D3DPRESENTFLAG_DEVICECLIP) != 0) {
            LogInfo("D3D9 FLIPEX (no-ReShade): Stripping D3DPRESENTFLAG_DEVICECLIP flag");
            pp->Flags &= ~D3DPRESENTFLAG_DEVICECLIP;
            modified = true;
        }
        if (pp->MultiSampleType != D3DMULTISAMPLE_NONE) {
            LogInfo("D3D9 FLIPEX (no-ReShade): Setting multisample to D3DMULTISAMPLE_NONE");
            pp->MultiSampleType = D3DMULTISAMPLE_NONE;
            pp->MultiSampleQuality = 0;
            modified = true;
        }
        g_used_flipex.store(true);
        modified = true;
    } else if (is_create_device_ex) {
        g_used_flipex.store(false);
    }

    return modified;
}

}  // namespace display_commanderhooks::d3d9
