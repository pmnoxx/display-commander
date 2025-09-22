/* Minimal subset to provide utils::resource::Use() and GetResourceInfo() stubs */
#pragma once

#include <reshade.hpp>

namespace utils::resource {

struct ResourceInfo {
    bool is_swap_chain = false;
};

inline ResourceInfo* GetResourceInfo(reshade::api::resource) {
    static ResourceInfo dummy; return &dummy;
}

inline void Use(DWORD) {}

}  // namespace utils::resource


