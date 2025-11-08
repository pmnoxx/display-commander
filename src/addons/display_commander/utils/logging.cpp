#include "logging.hpp"
#include "../globals.hpp"

#include <cstdio>
#include <reshade.hpp>

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
    reshade::log::message(reshade::log::level::info, buffer);
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
    reshade::log::message(reshade::log::level::warning, buffer);
}

void LogError(const char *msg, ...) {
    // Errors are always logged (lowest level = 1)
    va_list args;
    va_start(args, msg);
    char buffer[1024];
    vsnprintf(buffer, sizeof(buffer), msg, args);
    va_end(args);
    reshade::log::message(reshade::log::level::error, buffer);
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
    reshade::log::message(reshade::log::level::debug, buffer);
}
