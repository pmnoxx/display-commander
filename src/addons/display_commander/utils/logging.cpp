#include "logging.hpp"
#include "../globals.hpp"
#include "display_commander_logger.hpp"

#include <cstdio>

// Logging function implementations
void LogInfo(const char *msg, ...) {
    // Check if info level logging is enabled (log if message level <= min level)
    if (static_cast<int>(LogLevel::Info) > static_cast<int>(g_min_log_level.load())) {
        return;
    }

    va_list args;
    va_start(args, msg);
    char buffer[1024];
    vsnprintf(buffer, sizeof(buffer), msg, args);
    va_end(args);
    display_commander::logger::LogInfo(buffer);
}

void LogWarn(const char *msg, ...) {
    // Check if warning level logging is enabled (log if message level <= min level)
    if (static_cast<int>(LogLevel::Warning) > static_cast<int>(g_min_log_level.load())) {
        return;
    }

    va_list args;
    va_start(args, msg);
    char buffer[1024];
    vsnprintf(buffer, sizeof(buffer), msg, args);
    va_end(args);
    display_commander::logger::LogWarning(buffer);
}

void LogError(const char *msg, ...) {
    // Errors are always logged (lowest level = 1)
    va_list args;
    va_start(args, msg);
    char buffer[1024];
    vsnprintf(buffer, sizeof(buffer), msg, args);
    va_end(args);
    display_commander::logger::LogError(buffer);
}

void LogDebug(const char *msg, ...) {
    // Check if debug level logging is enabled (log if message level <= min level)
    if (static_cast<int>(LogLevel::Debug) > static_cast<int>(g_min_log_level.load())) {
        return;
    }

    va_list args;
    va_start(args, msg);
    char buffer[1024];
    vsnprintf(buffer, sizeof(buffer), msg, args);
    va_end(args);
    display_commander::logger::LogDebug(buffer);
}

// Helper function to convert LogLevel to string
static const char* LogLevelToString(LogLevel level) {
    switch (level) {
        case LogLevel::Error:   return "Error";
        case LogLevel::Warning: return "Warning";
        case LogLevel::Info:    return "Info";
        case LogLevel::Debug:   return "Debug";
        default:                return "Unknown";
    }
}

void LogCurrentLogLevel() {
    // Always log the current logging level using LogError so it's always visible
    // This helps diagnose if users have disabled logging
    LogLevel current_level = g_min_log_level.load();
    LogError("Current logging level: %s (value: %d)", LogLevelToString(current_level), static_cast<int>(current_level));
}
