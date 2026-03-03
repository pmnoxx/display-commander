#pragma once

// Source Code <Display Commander>
// Init stage limit for binary-search debugging (game fails to start).
// When a file .DCMAX_STAGE.<N> exists in the process (game) directory, initialization
// stops after N stages so the user can binary-search which stage causes the failure.
// 0 = no limit (feature off when no file present).

#include <atomic>

namespace display_commander::utils {

// Current init stage (1-based; incremented at each ENTER_INIT_STAGE).
extern std::atomic<int> g_init_stage;

// Max stage allowed (from .DCMAX_STAGE.<N> file). 0 = no limit.
extern int g_max_init_stage_allowed;

// Load g_max_init_stage_allowed from process exe directory: look for file named
// .DCMAX_STAGE.<N>, set g_max_init_stage_allowed = N. Call once at startup.
void InitStageLimit_LoadFromFile();

// Increment g_init_stage (call at start of each init step).
void NextInitStage();

// True when limit is enabled and current stage exceeds the limit (skip rest of init).
inline bool ReachMaxAllowedStage() {
    const int max_allowed = g_max_init_stage_allowed;
    if (max_allowed <= 0) return false;
    return g_init_stage.load(std::memory_order_acquire) > max_allowed;
}

}  // namespace display_commander::utils

// At each init step: enter stage then check; if over limit, return from current function.
#define ENTER_INIT_STAGE() display_commander::utils::NextInitStage()
#define REACH_MAX_ALLOWED_STAGE() display_commander::utils::ReachMaxAllowedStage()
