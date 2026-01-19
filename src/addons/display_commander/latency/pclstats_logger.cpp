#include "pclstats_logger.hpp"

#include "../globals.hpp"
#include "../utils/logging.hpp"
#include "../utils/timing.hpp"

#include <atomic>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <sstream>

namespace latency::pclstats_logger {

static std::atomic<bool> g_logging_enabled{false};
static std::atomic<uint64_t> g_total_events_logged{0};
static std::atomic<uint64_t> g_file_write_errors{0};
static std::atomic<bool> g_file_open{false};

static std::mutex g_file_mutex;
static std::ofstream g_log_file;
static std::string g_log_file_path;

// Marker type names for readable log output
static const char* GetMarkerTypeName(uint32_t marker) {
    switch (marker) {
        case 0:  return "SIMULATION_START";
        case 1:  return "SIMULATION_END";
        case 2:  return "RENDERSUBMIT_START";
        case 3:  return "RENDERSUBMIT_END";
        case 4:  return "PRESENT_START";
        case 5:  return "PRESENT_END";
        case 7:  return "TRIGGER_FLASH";
        case 8:  return "PC_LATENCY_PING";
        case 9:  return "OUT_OF_BAND_RENDERSUBMIT_START";
        case 10: return "OUT_OF_BAND_RENDERSUBMIT_END";
        case 11: return "OUT_OF_BAND_PRESENT_START";
        case 12: return "OUT_OF_BAND_PRESENT_END";
        case 13: return "CONTROLLER_INPUT_SAMPLE";
        default: return "UNKNOWN";
    }
}

static std::string GetDefaultLogFilePath() {
    // Get the addon directory (where DisplayCommander.log is located)
    // We'll use the same directory for the PCLStats log
    std::string log_path = "DisplayCommander_PCLStats.log";

    // Try to get the actual log directory from the logging system if available
    // For now, use the current directory or addon directory
    return log_path;
}

void Initialize() {
    std::lock_guard<std::mutex> lock(g_file_mutex);

    if (g_log_file.is_open()) {
        return;  // Already initialized
    }

    g_log_file_path = GetDefaultLogFilePath();

    // Open file in append mode (don't truncate existing logs)
    g_log_file.open(g_log_file_path, std::ios::out | std::ios::app);

    if (g_log_file.is_open()) {
        g_file_open.store(true, std::memory_order_release);

        // Write header if file is new (check if we're at the beginning)
        std::ifstream check_file(g_log_file_path);
        bool is_new_file = check_file.peek() == std::ifstream::traits_type::eof();
        check_file.close();

        if (is_new_file) {
            g_log_file << "# PCLStats Event Log\n";
            g_log_file << "# Format: Timestamp(ms), MarkerID, MarkerName, FrameID, TimestampNS\n";
            g_log_file << "# This log contains all PCLStats events matching the marker ID used for NVIDIA overlay\n";
            g_log_file << "#\n";
            g_log_file.flush();
        }

        LogInfo("[PCLStats Logger] Log file opened: %s", g_log_file_path.c_str());
    } else {
        g_file_open.store(false, std::memory_order_release);
        g_file_write_errors.fetch_add(1, std::memory_order_relaxed);
        LogWarn("[PCLStats Logger] Failed to open log file: %s", g_log_file_path.c_str());
    }
}

void Shutdown() {
    std::lock_guard<std::mutex> lock(g_file_mutex);

    if (g_log_file.is_open()) {
        g_log_file << "# Logging stopped\n";
        g_log_file.flush();
        g_log_file.close();
        g_file_open.store(false, std::memory_order_release);
        LogInfo("[PCLStats Logger] Log file closed");
    }
}

void SetLoggingEnabled(bool enabled) {
    const bool prev = g_logging_enabled.exchange(enabled, std::memory_order_acq_rel);

    if (prev == enabled) {
        return;  // No change
    }

    if (enabled) {
        Initialize();
        LogInfo("[PCLStats Logger] Logging enabled");
    } else {
        // Don't close file, just stop writing (allows re-enabling without file recreation)
        LogInfo("[PCLStats Logger] Logging disabled");
    }
}

bool IsPCLLoggingEnabled() { return g_logging_enabled.load(std::memory_order_acquire); }

void LogMarker(uint32_t marker, uint64_t frame_id, uint64_t timestamp_ns) {
    // Fast path: check if logging is enabled before acquiring lock
    if (!g_logging_enabled.load(std::memory_order_acquire)) {
        return;
    }

    std::lock_guard<std::mutex> lock(g_file_mutex);

    if (!g_file_open.load(std::memory_order_acquire) || !g_log_file.is_open()) {
        return;
    }

    try {
        // Format: Timestamp(ms), MarkerID, MarkerName, FrameID, TimestampNS
        // Using milliseconds for readability, nanoseconds for precision
        uint64_t timestamp_ms = timestamp_ns / 1000000ULL;
        uint64_t timestamp_ns_remainder = timestamp_ns % 1000000ULL;

        g_log_file << timestamp_ms << "." << std::setfill('0') << std::setw(6) << timestamp_ns_remainder << ", "
                   << marker << ", " << GetMarkerTypeName(marker) << ", " << frame_id << ", " << timestamp_ns << "\n";

        // Flush periodically (every 10 events) to ensure data is written
        static uint64_t flush_counter = 0;
        if (++flush_counter % 10 == 0) {
            g_log_file.flush();
        }

        g_total_events_logged.fetch_add(1, std::memory_order_relaxed);
    } catch (const std::exception& e) {
        g_file_write_errors.fetch_add(1, std::memory_order_relaxed);
        // Don't spam logs on every error
        static uint64_t error_log_count = 0;
        if (error_log_count++ < 5) {
            LogWarn("[PCLStats Logger] Error writing to log file: %s", e.what());
        }
    }
}

std::string GetLogFilePath() {
    std::lock_guard<std::mutex> lock(g_file_mutex);
    return g_log_file_path;
}

LoggerStats GetStats() {
    LoggerStats stats{};
    stats.total_events_logged = g_total_events_logged.load(std::memory_order_acquire);
    stats.file_write_errors = g_file_write_errors.load(std::memory_order_acquire);
    stats.is_logging_enabled = g_logging_enabled.load(std::memory_order_acquire);
    stats.is_file_open = g_file_open.load(std::memory_order_acquire);
    return stats;
}

}  // namespace latency::pclstats_logger
