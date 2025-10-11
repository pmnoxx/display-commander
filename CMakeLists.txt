cmake_minimum_required(VERSION 3.20)

project(reshade_display_commander VERSION 0.2.4 LANGUAGES CXX)

# Build options
option(EXPERIMENTAL_TAB "Enable experimental tab functionality" OFF)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Enable C for third-party C sources (MinHook)
enable_language(C)

# Use direct externals (drop RENODX_SUBMODULE_DIR)
if(NOT EXISTS "${CMAKE_CURRENT_LIST_DIR}/external/reshade")
  message(FATAL_ERROR "Missing submodule: external/reshade. Run: git submodule update --init --recursive")
endif()

# Include addon subdirectories
add_subdirectory(src/addons/display_commander)

# Include tools
add_subdirectory(tools/driver_restart)