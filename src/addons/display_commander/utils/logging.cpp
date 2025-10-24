#include "logging.hpp"

#include <cstdio>
#include <reshade.hpp>

// Logging function implementations
void LogInfo(const char *msg, ...) {
    va_list args;
    va_start(args, msg);
    char buffer[1024];
    vsnprintf(buffer, sizeof(buffer), msg, args);
    va_end(args);
    reshade::log::message(reshade::log::level::info, buffer);
}

void LogWarn(const char *msg, ...) {
    va_list args;
    va_start(args, msg);
    char buffer[1024];
    vsnprintf(buffer, sizeof(buffer), msg, args);
    va_end(args);
    reshade::log::message(reshade::log::level::warning, buffer);
}

void LogError(const char *msg, ...) {
    va_list args;
    va_start(args, msg);
    char buffer[1024];
    vsnprintf(buffer, sizeof(buffer), msg, args);
    va_end(args);
    reshade::log::message(reshade::log::level::error, buffer);
}

void LogDebug(const char *msg, ...) {
    va_list args;
    va_start(args, msg);
    char buffer[1024];
    vsnprintf(buffer, sizeof(buffer), msg, args);
    va_end(args);
    reshade::log::message(reshade::log::level::debug, buffer);
}
