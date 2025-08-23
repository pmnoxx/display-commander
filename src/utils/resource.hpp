/* Minimal subset to provide renodx::utils::resource::Use() and GetResourceInfo() stubs */
#pragma once

#include <include/reshade.hpp>

namespace renodx::utils::resource {

struct ResourceInfo {
  bool is_swap_chain = false;
};

inline ResourceInfo* GetResourceInfo(reshade::api::resource) {
  static ResourceInfo dummy; return &dummy;
}

inline void Use(DWORD) {}

}  // namespace renodx::utils::resource


