cmake_minimum_required(VERSION 3.20)

project(reshade_display_commander LANGUAGES CXX)

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

# Get git commit count for build number (skip if already set e.g. by CI -DGIT_COMMIT_COUNT=)
if(NOT DEFINED GIT_COMMIT_COUNT OR GIT_COMMIT_COUNT STREQUAL "")
  get_git_commit_count(GIT_COMMIT_COUNT)
endif()

# Build options
option(EXPERIMENTAL_TAB "Enable experimental tab functionality" OFF)
option(EXPERIMENTAL_FEATURES "Enable experimental features (e.g., autofire)" OFF)
option(DEBUG_TABS "Enable debug-only UI tabs" OFF)
option(DC_EXTERNAL_MODULES "Enable private external modules from external/display-commander2-modules" OFF)
option(DC_INTERNAL_MODULES "Register built-in modules (audio, controller, example_dummy)" ON)
option(DC_NO_MODULES "Disable all modules: empty registry, no external modules (forces DC_INTERNAL_MODULES and DC_EXTERNAL_MODULES OFF)" OFF)

if(DC_NO_MODULES)
  set(DC_INTERNAL_MODULES OFF CACHE BOOL "Disabled by DC_NO_MODULES" FORCE)
  set(DC_EXTERNAL_MODULES OFF CACHE BOOL "Disabled by DC_NO_MODULES" FORCE)
endif()

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
