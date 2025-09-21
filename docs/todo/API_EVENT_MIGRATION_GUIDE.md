# API Event Migration Guide

Migration from ReShade addon events to direct API hooks for better performance and independence.

## Current Status

- ✅ **IDXGISwapChain::Present** - Already hooked
- ❌ All other events - Still using ReShade addon events

## Migration Checklist

### Present Events
- [ ] **addon_event::present**
  - DX9: `IDirect3DDevice9::Present` hook
  - DX11: `IDXGISwapChain::Present` hook ✅ (done)
  - DX12: `IDXGISwapChain::Present` hook ✅ (done)
  - OpenGL: `wglSwapBuffers` hook
  - Vulkan: `vkQueuePresentKHR` hook

- [ ] **addon_event::reshade_present**
  - DX9: `IDirect3DDevice9::Present` hook (same as present)
  - DX11: `IDXGISwapChain::Present` hook ✅ (done)
  - DX12: `IDXGISwapChain::Present` hook ✅ (done)
  - OpenGL: `wglSwapBuffers` hook
  - Vulkan: `vkQueuePresentKHR` hook

- [ ] **addon_event::finish_present**
  - DX9: `IDirect3DDevice9::Present` hook (post-call)
  - DX11: `IDXGISwapChain::Present` hook (post-call) ✅ (done)
  - DX12: `IDXGISwapChain::Present` hook (post-call) ✅ (done)
  - OpenGL: `wglSwapBuffers` hook (post-call)
  - Vulkan: `vkQueuePresentKHR` hook (post-call)

### Swapchain Events
- [ ] **addon_event::create_swapchain**
  - DX9: `IDirect3D9::CreateDevice` hook
  - DX11: `IDXGIFactory::CreateSwapChain` hook
  - DX12: `IDXGIFactory::CreateSwapChain` hook
  - OpenGL: `wglCreateContext` + `wglMakeCurrent` hooks
  - Vulkan: `vkCreateSwapchainKHR` hook

- [ ] **addon_event::init_swapchain**
  - DX9: `IDirect3DDevice9::CreateDevice` hook (post-call)
  - DX11: `IDXGISwapChain::Present` hook (first call)
  - DX12: `IDXGISwapChain::Present` hook (first call)
  - OpenGL: `wglMakeCurrent` hook (first call)
  - Vulkan: `vkQueuePresentKHR` hook (first call)

### Device Events
- [ ] **addon_event::destroy_device**
  - DX9: `IDirect3DDevice9::Release` hook
  - DX11: `ID3D11Device::Release` hook
  - DX12: `ID3D12Device::Release` hook
  - OpenGL: `wglDeleteContext` hook
  - Vulkan: `vkDestroyDevice` hook

### Draw Events
- [ ] **addon_event::draw**
  - DX9: `IDirect3DDevice9::DrawPrimitive` hook
  - DX11: `ID3D11DeviceContext::Draw` hook
  - DX12: `ID3D12GraphicsCommandList::DrawInstanced` hook
  - OpenGL: `glDrawArrays` hook
  - Vulkan: `vkCmdDraw` hook

- [ ] **addon_event::draw_indexed**
  - DX9: `IDirect3DDevice9::DrawIndexedPrimitive` hook
  - DX11: `ID3D11DeviceContext::DrawIndexed` hook
  - DX12: `ID3D12GraphicsCommandList::DrawIndexedInstanced` hook
  - OpenGL: `glDrawElements` hook
  - Vulkan: `vkCmdDrawIndexed` hook

- [ ] **addon_event::draw_or_dispatch_indirect**
  - DX11: `ID3D11DeviceContext::DrawInstancedIndirect` hook
  - DX12: `ID3D12GraphicsCommandList::ExecuteIndirect` hook
  - OpenGL: `glDrawArraysIndirect` hook
  - Vulkan: `vkCmdDrawIndirect` hook

### Compute Events
- [ ] **addon_event::dispatch**
  - DX11: `ID3D11DeviceContext::Dispatch` hook
  - DX12: `ID3D12GraphicsCommandList::Dispatch` hook
  - OpenGL: `glDispatchCompute` hook
  - Vulkan: `vkCmdDispatch` hook

- [ ] **addon_event::dispatch_mesh**
  - DX12: `ID3D12GraphicsCommandList::DispatchMesh` hook
  - Vulkan: `vkCmdDrawMeshTasksNV` hook

- [ ] **addon_event::dispatch_rays**
  - DX12: `ID3D12GraphicsCommandList::DispatchRays` hook
  - Vulkan: `vkCmdTraceRaysKHR` hook

### Resource Events
- [ ] **addon_event::create_resource**
  - DX9: `IDirect3DDevice9::CreateTexture` hook
  - DX11: `ID3D11Device::CreateTexture2D` hook
  - DX12: `ID3D12Device::CreateCommittedResource` hook
  - OpenGL: `glGenTextures` hook
  - Vulkan: `vkCreateImage` hook

- [ ] **addon_event::create_resource_view**
  - DX9: `IDirect3DDevice9::CreateRenderTargetView` hook
  - DX11: `ID3D11Device::CreateRenderTargetView` hook
  - DX12: `ID3D12Device::CreateRenderTargetView` hook
  - OpenGL: `glFramebufferTexture2D` hook
  - Vulkan: `vkCreateImageView` hook

- [ ] **addon_event::copy_resource**
  - DX9: `IDirect3DDevice9::StretchRect` hook
  - DX11: `ID3D11DeviceContext::CopyResource` hook
  - DX12: `ID3D12GraphicsCommandList::CopyResource` hook
  - OpenGL: `glCopyTexSubImage2D` hook
  - Vulkan: `vkCmdCopyImage` hook

### Buffer Events
- [ ] **addon_event::update_buffer_region**
  - DX11: `ID3D11DeviceContext::UpdateSubresource` hook
  - DX12: `ID3D12GraphicsCommandList::UpdateBufferResource` hook
  - OpenGL: `glBufferSubData` hook
  - Vulkan: `vkCmdUpdateBuffer` hook

- [ ] **addon_event::update_buffer_region_command**
  - DX12: `ID3D12GraphicsCommandList::UpdateBufferResource` hook
  - Vulkan: `vkCmdUpdateBuffer` hook

### Viewport Events
- [ ] **addon_event::bind_viewports**
  - DX9: `IDirect3DDevice9::SetViewport` hook
  - DX11: `ID3D11DeviceContext::RSSetViewports` hook
  - DX12: `ID3D12GraphicsCommandList::RSSetViewports` hook
  - OpenGL: `glViewport` hook
  - Vulkan: `vkCmdSetViewport` hook

- [ ] **addon_event::bind_scissor_rects**
  - DX9: `IDirect3DDevice9::SetScissorRect` hook
  - DX11: `ID3D11DeviceContext::RSSetScissorRects` hook
  - DX12: `ID3D12GraphicsCommandList::RSSetScissorRects` hook
  - OpenGL: `glScissor` hook
  - Vulkan: `vkCmdSetScissor` hook

### Fullscreen Events
- [ ] **addon_event::set_fullscreen_state**
  - DX9: `IDirect3DDevice9::Reset` hook
  - DX11: `IDXGISwapChain::SetFullscreenState` hook
  - DX12: `IDXGISwapChain::SetFullscreenState` hook
  - OpenGL: `ChangeDisplaySettings` hook
  - Vulkan: `vkQueuePresentKHR` hook (with fullscreen detection)

### ReShade-Specific Events
- [ ] **addon_event::reshade_overlay**
  - All APIs: Custom overlay detection logic
  - Requires monitoring ImGui state or overlay flags

- [ ] **addon_event::init_effect_runtime**
  - All APIs: ReShade runtime initialization detection
  - Monitor for ReShade DLL loading

- [ ] **addon_event::reshade_open_overlay**
  - All APIs: Overlay state monitoring
  - Track overlay open/close state

## Implementation Priority

1. **High Priority** (Core functionality)
   - Present events (already done)
   - Swapchain creation/destruction
   - Device creation/destruction

2. **Medium Priority** (Feature completeness)
   - Draw events
   - Resource events
   - Viewport events

3. **Low Priority** (Advanced features)
   - Compute events
   - Buffer events
   - ReShade-specific events

## Notes

- DX9/DX11/DX12 hooks should be implemented first as they're most commonly used
- OpenGL hooks require `wgl` function interception
- Vulkan hooks require `vk` function interception
- All hooks should maintain the same callback signature as current ReShade events
- Consider using MinHook for Windows APIs and manual vtable patching for graphics APIs
