#pragma once

#include <cstdint>
#include <string>

namespace latency::pclstats_logger {

// Initialize the logger (call once at startup)
void Initialize();

// Shutdown the logger (call on cleanup)
void Shutdown();

// Enable/disable logging
void SetLoggingEnabled(bool enabled);

// Check if logging is enabled
bool IsPCLLoggingEnabled();

// Log a PCLStats marker event
// This is called from pclstats_etw::EmitMarker() when a marker is emitted
void LogMarker(uint32_t marker, uint64_t frame_id, uint64_t timestamp_ns);

// Get the current log file path
std::string GetLogFilePath();

// Get statistics about logged events
struct LoggerStats {
    uint64_t total_events_logged;
    uint64_t file_write_errors;
    bool is_logging_enabled;
    bool is_file_open;
};

LoggerStats GetStats();

}  // namespace latency::pclstats_logger
