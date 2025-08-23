/*
 * Minimal vendored subset from renodx2 utils/swapchain.hpp for color space changes and FPS limiter integration
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <dxgi1_6.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <deque>
#include <functional>
#include <ios>
#include <mutex>
#include <shared_mutex>
#include <optional>
#include <unordered_set>
#include <vector>


#include <include/reshade.hpp>

#include "./data.hpp"
#include "./device.hpp"


#include "./resource.hpp"

namespace utils::swapchain {

static float fps_limit = 0.f;

struct __declspec(uuid("4721e307-0cf3-4293-b4a5-40d0a4e62544")) DeviceData {
  std::shared_mutex mutex;
  std::unordered_set<reshade::api::effect_runtime*> effect_runtimes;
  reshade::api::resource_desc back_buffer_desc;
  reshade::api::color_space current_color_space = reshade::api::color_space::unknown;
};

struct __declspec(uuid("3cf9a628-8518-4509-84c3-9fbe9a295212")) CommandListData {
  std::vector<reshade::api::resource_view> current_render_targets;
  reshade::api::resource_view current_depth_stencil = {0};
  bool has_swapchain_render_target_dirty = true;
  bool has_swapchain_render_target = false;
  uint8_t pass_count = 0;
};

static bool IsBackBuffer(reshade::api::resource resource) {
  auto* info = resource::GetResourceInfo(resource);
  if (info == nullptr) return false;
  return info->is_swap_chain;
}

static CommandListData* GetCurrentState(reshade::api::command_list* cmd_list) {
  return utils::data::Get<CommandListData>(cmd_list);
}

static reshade::api::resource_desc GetBackBufferDesc(reshade::api::device* device) {
  reshade::api::resource_desc desc = {};
  {
    auto* device_data = utils::data::Get<DeviceData>(device);
    if (device_data == nullptr) {
      reshade::log::message(reshade::log::level::error, "GetBackBufferDesc(No device data)");
      return desc;
    }
    desc = device_data->back_buffer_desc;
  }
  return desc;
}

static reshade::api::resource_desc GetBackBufferDesc(reshade::api::command_list* cmd_list) {
  auto* device = cmd_list->get_device();
  if (device == nullptr) return {};
  return GetBackBufferDesc(device);
}

static bool IsDirectX(reshade::api::swapchain* swapchain) {
  auto* device = swapchain->get_device();
  return device::IsDirectX(device);
}

static bool IsDXGI(reshade::api::swapchain* swapchain) {
  auto* device = swapchain->get_device();
  return device::IsDXGI(device);
}

static std::optional<DXGI_OUTPUT_DESC1> GetDirectXOutputDesc1(reshade::api::swapchain* swapchain) {
  auto* native_swapchain = reinterpret_cast<IDXGISwapChain*>(swapchain->get_native());

  IDXGISwapChain4* swapchain4;

  if (!SUCCEEDED(native_swapchain->QueryInterface(IID_PPV_ARGS(&swapchain4)))) {
    reshade::log::message(reshade::log::level::error, "GetDirectXOutputDesc1(Failed to get native swap chain)");
    return std::nullopt;
  }

  DXGI_COLOR_SPACE_TYPE dx_color_space;

  IDXGIOutput* output;
  HRESULT hr = swapchain4->GetContainingOutput(&output);

  swapchain4->Release();
  swapchain4 = nullptr;

  if (!SUCCEEDED(hr)) {
    reshade::log::message(reshade::log::level::error, "GetDirectXOutputDesc1(Failed to get containing output)");
    return std::nullopt;
  }

  IDXGIOutput6* output6;
  hr = output->QueryInterface(&output6);

  output->Release();
  output = nullptr;

  if (!SUCCEEDED(hr)) {
    reshade::log::message(reshade::log::level::error, "GetDirectXOutputDesc1(Failed to query output6)");
    return std::nullopt;
  }

  DXGI_OUTPUT_DESC1 output_desc;
  hr = output6->GetDesc1(&output_desc);
  output6->Release();
  output6 = nullptr;
  if (!SUCCEEDED(hr)) {
    reshade::log::message(reshade::log::level::error, "GetDirectXOutputDesc1(Failed to get output desc)");
    return std::nullopt;
  }
  return output_desc;
}

static bool ChangeColorSpace(reshade::api::swapchain* swapchain, reshade::api::color_space color_space) {
  if (IsDXGI(swapchain)) {
    DXGI_COLOR_SPACE_TYPE dx_color_space = DXGI_COLOR_SPACE_CUSTOM;
    switch (color_space) {
      case reshade::api::color_space::srgb_nonlinear:       dx_color_space = DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709; break;
      case reshade::api::color_space::extended_srgb_linear: dx_color_space = DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709; break;
      case reshade::api::color_space::hdr10_st2084:         dx_color_space = DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020; break;
      case reshade::api::color_space::hdr10_hlg:            dx_color_space = DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P2020; break;
      default:                                              return false;
    }

    auto* native_swapchain = reinterpret_cast<IDXGISwapChain*>(swapchain->get_native());

    IDXGISwapChain4* swapchain4;
    if (!SUCCEEDED(native_swapchain->QueryInterface(IID_PPV_ARGS(&swapchain4)))) {
      reshade::log::message(reshade::log::level::error, "changeColorSpace(Failed to get native swap chain)");
      if (swapchain4 != nullptr) { swapchain4->Release(); swapchain4 = nullptr; }
      return false;
    }

    const HRESULT hr = swapchain4->SetColorSpace1(dx_color_space);
    if (FAILED(hr)) {
      if (swapchain4 != nullptr) { swapchain4->Release(); swapchain4 = nullptr; }
      std::stringstream s;
      s << "utils::swapchain::ChangeColorSpace(Failed to set DirectX color space, hr = 0x" << std::hex << hr << std::dec << ")";
      reshade::log::message(reshade::log::level::warning, s.str().c_str());
      return false;
    }
    swapchain4->Release();
    swapchain4 = nullptr;
  }

  auto* device = swapchain->get_device();
  std::unordered_set<reshade::api::effect_runtime*> runtimes;
  auto* data = utils::data::Get<DeviceData>(device);
  if (data != nullptr) {
    std::unique_lock lock(data->mutex);
    data->current_color_space = color_space;
    runtimes = data->effect_runtimes;
  }
  for (auto* runtime : runtimes) {
    runtime->set_color_space(color_space);
    reshade::log::message(reshade::log::level::debug, "utils::swapchain::ChangeColorSpace(Updated runtime)");
  }
  return true;
}

namespace internal {
static bool attached = false;
static void OnInitDevice(reshade::api::device* device) {
  DeviceData* data;
  bool created = utils::data::CreateOrGet<DeviceData>(device, data);
  (void)created;
}
static void OnDestroyDevice(reshade::api::device* device) {
  device->destroy_private_data<DeviceData>();
}
static void OnInitSwapchain(reshade::api::swapchain* swapchain, bool resize) {
  auto* device = swapchain->get_device();
  auto* data = utils::data::Get<DeviceData>(device);
  if (data == nullptr) return;
  const std::unique_lock lock(data->mutex);
  data->back_buffer_desc = device->get_resource_desc(swapchain->get_current_back_buffer());
}
static void OnDestroySwapchain(reshade::api::swapchain* swapchain, bool resize) {
  auto* device = swapchain->get_device();
  auto* data = utils::data::Get<DeviceData>(device);
  if (data == nullptr) return;
  data->back_buffer_desc = {};
}
static void OnInitEffectRuntime(reshade::api::effect_runtime* runtime) {
  auto* device = runtime->get_device();
  auto* data = utils::data::Get<DeviceData>(device);
  if (data == nullptr) return;
  const std::unique_lock lock(data->mutex);
  data->effect_runtimes.emplace(runtime);
  if (data->current_color_space != reshade::api::color_space::unknown) {
    runtime->set_color_space(data->current_color_space);
  }
}
static void OnDestroyEffectRuntime(reshade::api::effect_runtime* runtime) {
  auto* device = runtime->get_device();
  auto* data = utils::data::Get<DeviceData>(device);
  if (data == nullptr) return;
  const std::unique_lock lock(data->mutex);
  data->effect_runtimes.erase(runtime);
}
}  // namespace internal

static void Use(DWORD fdw_reason) {
  utils::resource::Use(fdw_reason);
  switch (fdw_reason) {
    case DLL_PROCESS_ATTACH:
      if (internal::attached) return;
      internal::attached = true;
      reshade::register_event<reshade::addon_event::init_device>(internal::OnInitDevice);
      reshade::register_event<reshade::addon_event::destroy_device>(internal::OnDestroyDevice);
      reshade::register_event<reshade::addon_event::init_swapchain>(internal::OnInitSwapchain);
      reshade::register_event<reshade::addon_event::destroy_swapchain>(internal::OnDestroySwapchain);
      reshade::register_event<reshade::addon_event::init_effect_runtime>(internal::OnInitEffectRuntime);
      reshade::register_event<reshade::addon_event::destroy_effect_runtime>(internal::OnDestroyEffectRuntime);
      break;
    case DLL_PROCESS_DETACH:
      reshade::unregister_event<reshade::addon_event::init_device>(internal::OnInitDevice);
      reshade::unregister_event<reshade::addon_event::destroy_device>(internal::OnDestroyDevice);
      reshade::unregister_event<reshade::addon_event::init_swapchain>(internal::OnInitSwapchain);
      reshade::unregister_event<reshade::addon_event::destroy_swapchain>(internal::OnDestroySwapchain);
      reshade::unregister_event<reshade::addon_event::init_effect_runtime>(internal::OnInitEffectRuntime);
      reshade::unregister_event<reshade::addon_event::destroy_effect_runtime>(internal::OnDestroyEffectRuntime);
      break;
  }
}

}  // namespace utils::swapchain


