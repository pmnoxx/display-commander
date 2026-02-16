cmake_minimum_required(VERSION 3.20)

project(reshade_display_commander VERSION 0.2.4 LANGUAGES CXX)

# Function to get git commit count for build number
function(get_git_commit_count OUTPUT_VAR)
    find_package(Git QUIET)
    if(NOT GIT_FOUND)
        set(${OUTPUT_VAR} "0" PARENT_SCOPE)
        return()
    endif()

    execute_process(
        COMMAND "${GIT_EXECUTABLE}" rev-list --count HEAD
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        RESULT_VARIABLE GIT_RESULT
        OUTPUT_VARIABLE GIT_OUTPUT
        ERROR_QUIET
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )

    if(NOT GIT_RESULT EQUAL 0)
        set(${OUTPUT_VAR} "0" PARENT_SCOPE)
        return()
    endif()

    set(${OUTPUT_VAR} "${GIT_OUTPUT}" PARENT_SCOPE)
endfunction()

# Get git commit count for build number
get_git_commit_count(GIT_COMMIT_COUNT)

# Get current build date and time
string(TIMESTAMP BUILD_DATE "%Y-%m-%d")
string(TIMESTAMP BUILD_TIME "%H:%M:%S")

# Set build date and time as compile definitions
set(BUILD_DATE_DEFINE "BUILD_DATE=\"${BUILD_DATE}\"")
set(BUILD_TIME_DEFINE "BUILD_TIME=\"${BUILD_TIME}\"")

message(STATUS "Build date: ${BUILD_DATE}")
message(STATUS "Build time: ${BUILD_TIME}")

# Build options
option(EXPERIMENTAL_TAB "Enable experimental tab functionality" OFF)
option(EXPERIMENTAL_FEATURES "Enable experimental features (e.g., autofire)" OFF)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Enable C for third-party C sources (MinHook)
enable_language(C)

# Use direct externals
if(NOT EXISTS "${CMAKE_CURRENT_LIST_DIR}/external/reshade")
  message(FATAL_ERROR "Missing submodule: external/reshade. Run: git submodule update --init --recursive")
endif()

# Include addon subdirectories
add_subdirectory(src/addons/display_commander)

# Include tools
add_subdirectory(tools)
