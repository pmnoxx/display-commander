#pragma once

#include <cstdarg>

// Logging function declarations
void LogInfo(const char *msg, ...);
void LogWarn(const char *msg, ...);
void LogError(const char *msg, ...);
void LogDebug(const char *msg, ...);
