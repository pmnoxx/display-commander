cmake_minimum_required(VERSION 3.20)

project(reshade_display_commander VERSION 0.2.4 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Use direct externals (drop RENODX_SUBMODULE_DIR)
if(NOT EXISTS "${CMAKE_CURRENT_LIST_DIR}/external/reshade")
  message(FATAL_ERROR "Missing submodule: external/reshade. Run: git submodule update --init --recursive")
endif()

# Sources
file(GLOB_RECURSE DISPLAY_COMMANDER_SOURCES
  CONFIGURE_DEPENDS
  ${CMAKE_CURRENT_LIST_DIR}/src/addons/display_commander/*.cpp
)

if(DISPLAY_COMMANDER_SOURCES STREQUAL "")
  message(FATAL_ERROR "No sources found under src/addons/display_commander")
endif()

add_library(display_commander MODULE ${DISPLAY_COMMANDER_SOURCES})

# MSVC-specific flags
if(MSVC)
  target_compile_options(display_commander PRIVATE /bigobj)
endif()

# Include directories
target_include_directories(display_commander
  PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/src/addons/display_commander
    ${CMAKE_CURRENT_LIST_DIR}/src
    ${CMAKE_CURRENT_LIST_DIR}/include
  SYSTEM PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}/external/reshade            # provides include/reshade.hpp and deps/imgui
    ${CMAKE_CURRENT_LIST_DIR}/external/Streamline/include # Streamline headers if referenced
    ${CMAKE_CURRENT_LIST_DIR}/external/SpecialK/depends/include/nvapi
)

# Link system libs
target_link_libraries(display_commander PRIVATE dbghelp)

# NVAPI linkage
if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/external/SpecialK/depends/lib/nvapi")
  if(CMAKE_SIZEOF_VOID_P EQUAL 4)
    target_link_libraries(display_commander PRIVATE "${CMAKE_CURRENT_LIST_DIR}/external/SpecialK/depends/lib/nvapi/Win32/nvapi.lib")
  else()
    target_link_libraries(display_commander PRIVATE "${CMAKE_CURRENT_LIST_DIR}/external/SpecialK/depends/lib/nvapi/x64/nvapi64.lib")
  endif()
else()
  message(WARNING "NVAPI libs not found at ${CMAKE_CURRENT_LIST_DIR}/external/SpecialK/depends/lib/nvapi. NVAPI features may fail to link.")
endif()

# Output naming
if(CMAKE_GENERATOR_PLATFORM STREQUAL "Win32" OR CMAKE_EXE_LINKER_FLAGS MATCHES "/machine:X86" OR CMAKE_SIZEOF_VOID_P EQUAL 4)
  set(TARGET_SUFFIX ".addon32")
else()
  set(TARGET_SUFFIX ".addon64")
endif()

set_target_properties(display_commander PROPERTIES
  SUFFIX "${TARGET_SUFFIX}"
)


