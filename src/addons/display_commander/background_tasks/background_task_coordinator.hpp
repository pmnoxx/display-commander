#pragma once

#include <atomic>
#include <thread>

namespace renodx::background {

// Background task coordinator functions
void StartBackgroundTasks();
void StopBackgroundTasks();

// Global state
extern std::atomic<bool> g_background_tasks_running;
extern std::thread g_background_tasks_thread;

} // namespace renodx::background
