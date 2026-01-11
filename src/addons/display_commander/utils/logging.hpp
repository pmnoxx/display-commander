#pragma once

#include <cstdarg>

// Logging function declarations
void LogInfo(const char *msg, ...);
void LogWarn(const char *msg, ...);
void LogError(const char *msg, ...);
void LogDebug(const char *msg, ...);

// Log current logging level (always logs, even if logging is disabled)
void LogCurrentLogLevel();

// Direct logging functions that use LogInfo/LogWarn/LogError
// These are safe during DLLMain as they use the buffered ostream logger
void LogInfoDirect(const char *msg, ...);
void LogWarnDirect(const char *msg, ...);
void LogErrorDirect(const char *msg, ...);

// Throttled error logging macro
// Usage: LogErrorThrottled(10, "Error message %d", value);
// This will only log the error up to 10 times per call site
// On the final (xth) attempt, it will also log a suppression message
#define LogErrorThrottled(throttle_count, ...) \
    do { \
        static int _throttle_counter = 0; \
        if (_throttle_counter < (throttle_count)) { \
            _throttle_counter++; \
            LogError(__VA_ARGS__); \
            if (_throttle_counter == (throttle_count)) { \
                LogError("(Suppressing further occurrences of this error)"); \
            } \
        } \
    } while(0)
