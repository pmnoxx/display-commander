#pragma once

#include <windows.h>

namespace display_commander::utils {

// Status of all three MPO-related registry values (read from HKLM).
struct MpoRegistryStatus {
    bool overlay_test_mode_5;   // Dwm -> OverlayTestMode == 5
    bool disable_mpo;           // GraphicsDrivers -> DisableMPO == 1
    bool disable_overlays;     // GraphicsDrivers -> DisableOverlays == 1 (Windows 11 25H2 solution)
};

bool MpoRegistryGetStatus(MpoRegistryStatus* out);

// Set or clear each value. disabled=true -> set to disable MPO (5 or 1); false -> 0. Requires admin.
bool MpoRegistrySetOverlayTestMode(bool disabled);
bool MpoRegistrySetDisableMPO(bool disabled);
bool MpoRegistrySetDisableOverlays(bool disabled);

}  // namespace display_commander::utils
