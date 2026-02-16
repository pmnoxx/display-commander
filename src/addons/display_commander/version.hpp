#pragma once

// Version information for Display Commander addon

#ifndef DISPLAY_COMMANDER_VERSION_HPP
#define DISPLAY_COMMANDER_VERSION_HPP

// Version numbers (major.minor.patch)
#define DISPLAY_COMMANDER_VERSION_MAJOR 0
#define DISPLAY_COMMANDER_VERSION_MINOR 12
#define DISPLAY_COMMANDER_VERSION_PATCH 0

// Build number from git commit count (set by CMake)
#ifndef GIT_COMMIT_COUNT
#define DISPLAY_COMMANDER_VERSION_BUILD        0
#define DISPLAY_COMMANDER_VERSION_BUILD_STRING "0"
#else
#define DISPLAY_COMMANDER_VERSION_BUILD        GIT_COMMIT_COUNT
#define DISPLAY_COMMANDER_VERSION_BUILD_STRING STRINGIFY(GIT_COMMIT_COUNT)
#endif

// String conversion macro
#define STRINGIFY(x)  STRINGIFY_(x)
#define STRINGIFY_(x) #x

// Version string (includes build number)
#define DISPLAY_COMMANDER_VERSION_STRING_MAJOR_MINOR_PATCH "0.12.0"
#define DISPLAY_COMMANDER_VERSION_STRING \
    DISPLAY_COMMANDER_VERSION_STRING_MAJOR_MINOR_PATCH "." DISPLAY_COMMANDER_VERSION_BUILD_STRING

// Build date and time (automatically set by CMake)
#ifndef BUILD_DATE
#define DISPLAY_COMMANDER_BUILD_DATE "unknown"
#else
#define DISPLAY_COMMANDER_BUILD_DATE BUILD_DATE
#endif

#ifndef BUILD_TIME
#define DISPLAY_COMMANDER_BUILD_TIME "unknown"
#else
#define DISPLAY_COMMANDER_BUILD_TIME BUILD_TIME
#endif

// Full version info string
#define DISPLAY_COMMANDER_FULL_VERSION                                                              \
    "Display Commander v" DISPLAY_COMMANDER_VERSION_STRING " (Build: " DISPLAY_COMMANDER_BUILD_DATE \
    " " DISPLAY_COMMANDER_BUILD_TIME ")"

#endif  // DISPLAY_COMMANDER_VERSION_HPP
