// Source Code <Display Commander>
#pragma once

// Libraries <standard C++>
#include <cstdint>
#include <filesystem>

namespace display_commander::dc_service {

enum class ServiceArchitecture { X86, X64 };

struct ServiceStatus {
    bool running = false;
    std::uint32_t pid = 0;  // Creator process ID of the DC service for this architecture
};

// Query DC service status for the given architecture (32-bit or 64-bit).
// Returns {running=false, pid=0} if the service is not running or status cannot be determined.
ServiceStatus QueryServiceStatus(ServiceArchitecture arch);

// Internal helper called from RunDLL Start entry point to enforce a single service instance
// per architecture and to record the creator PID in shared state.
// Returns true if this process should continue starting the service, or false if another
// instance is already running or initialization failed.
bool InitializeServiceForCurrentProcess();

// Path to addon for given architecture, or empty if not found. Searches DC load path, AppData, stable/Debug.
std::filesystem::path GetAddonPathForArch(ServiceArchitecture arch);

// Returns true if start was attempted successfully (process created).
bool StartService(ServiceArchitecture arch);

// Returns true if stop succeeded.
bool StopService(ServiceArchitecture arch);

}  // namespace display_commander::dc_service
