/**
 * DX11 Proxy UI
 *
 * UI components for controlling the DX11 proxy device
 */

#pragma once

namespace dx11_proxy {

/**
 * Initialize DX11 proxy UI (load settings)
 */
void InitUI();

/**
 * Draw the DX11 proxy controls section
 * Should be called from experimental tab
 */
void DrawDX11ProxyControls();

} // namespace dx11_proxy

