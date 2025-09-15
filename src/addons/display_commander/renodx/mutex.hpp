#pragma once

#include <windows.h>

namespace utils::mutex2 {

static SRWLOCK global_srwlock = SRWLOCK_INIT;

}  // namespace utils::mutex
