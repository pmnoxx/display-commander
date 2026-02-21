#pragma once

// Version information for Display Commander addon

#ifndef DISPLAY_COMMANDER_VERSION_HPP
#define DISPLAY_COMMANDER_VERSION_HPP

// String conversion macro (used for version string)
#define STRINGIFY(x)  STRINGIFY_(x)
#define STRINGIFY_(x) #x

// Version numbers (major.minor.patch) - set by CMake from CMakeLists.txt; fallbacks for non-CMake use (e.g. IDE)
#ifndef DISPLAY_COMMANDER_VERSION_MAJOR
#define DISPLAY_COMMANDER_VERSION_MAJOR 0
#endif
#ifndef DISPLAY_COMMANDER_VERSION_MINOR
#define DISPLAY_COMMANDER_VERSION_MINOR 12
#endif
#ifndef DISPLAY_COMMANDER_VERSION_PATCH
#define DISPLAY_COMMANDER_VERSION_PATCH 11
#endif

// Version string major.minor.patch (derived from the numeric macros above)
#define DISPLAY_COMMANDER_VERSION_STRING_MAJOR_MINOR_PATCH          \
    STRINGIFY(DISPLAY_COMMANDER_VERSION_MAJOR) "."                  \
    STRINGIFY(DISPLAY_COMMANDER_VERSION_MINOR) "."                  \
    STRINGIFY(DISPLAY_COMMANDER_VERSION_PATCH)

// Build number from git commit count (set by CMake)
#ifndef GIT_COMMIT_COUNT
#define DISPLAY_COMMANDER_VERSION_BUILD        0
#define DISPLAY_COMMANDER_VERSION_BUILD_STRING "0"
#else
#define DISPLAY_COMMANDER_VERSION_BUILD        GIT_COMMIT_COUNT
#define DISPLAY_COMMANDER_VERSION_BUILD_STRING STRINGIFY(GIT_COMMIT_COUNT)
#endif

// Version string (includes build number)
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
