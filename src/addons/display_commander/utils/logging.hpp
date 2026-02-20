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

// Throttled info logging macro
// Usage: LogInfoThrottled(10, "Info message %d", value);
// Logs only the first throttle_count times per call site, then suppresses
#define LogInfoThrottled(throttle_count, ...) \
    do { \
        static int _info_throttle_counter = 0; \
        if (_info_throttle_counter < (throttle_count)) { \
            _info_throttle_counter++; \
            LogInfo(__VA_ARGS__); \
            if (_info_throttle_counter == (throttle_count)) { \
                LogInfo("(Suppressing further occurrences of this info log)"); \
            } \
        } \
    } while(0)

// Throttled warn logging macro
// Usage: LogWarnThrottled(10, "Warn message %d", value);
// Logs only the first throttle_count times per call site, then suppresses
#define LogWarnThrottled(throttle_count, ...) \
    do { \
        static int _warn_throttle_counter = 0; \
        if (_warn_throttle_counter < (throttle_count)) { \
            _warn_throttle_counter++; \
            LogWarn(__VA_ARGS__); \
            if (_warn_throttle_counter == (throttle_count)) { \
                LogWarn("(Suppressing further occurrences of this warning)"); \
            } \
        } \
    } while(0)

// Throttled debug logging macro
// Usage: LogDebugThrottled(5, "Debug message %p", ptr);
// Logs only the first throttle_count times per call site, then suppresses
#define LogDebugThrottled(throttle_count, ...) \
    do { \
        static int _dbg_throttle_counter = 0; \
        if (_dbg_throttle_counter < (throttle_count)) { \
            _dbg_throttle_counter++; \
            LogDebug(__VA_ARGS__); \
            if (_dbg_throttle_counter == (throttle_count)) { \
                LogDebug("(Suppressing further occurrences of this debug log)"); \
            } \
        } \
    } while(0)
