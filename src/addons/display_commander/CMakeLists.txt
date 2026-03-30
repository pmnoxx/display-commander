# Version (single source of truth for addon; used by compile definitions and version.rc)
set(DISPLAY_COMMANDER_VERSION_MAJOR 0)
set(DISPLAY_COMMANDER_VERSION_MINOR 13)
set(DISPLAY_COMMANDER_VERSION_PATCH 58)

# Repository root (this file: src/addons/display_commander/CMakeLists.txt → ../../..).
set(_dc_repo_root "${CMAKE_CURRENT_LIST_DIR}/../../..")

# All addon .cpp under this directory (recursive).
file(GLOB_RECURSE DISPLAY_COMMANDER_SOURCES
  CONFIGURE_DEPENDS

  ${CMAKE_CURRENT_LIST_DIR}/*.cpp
)


# ReShade ImGui wrapper is header-only (included from experimental_tab.cpp) to avoid undefined ImGui symbols at link
list(FILTER DISPLAY_COMMANDER_SOURCES EXCLUDE REGEX ".*/imgui_wrapper_reshade\\.cpp$")

set(DISPLAY_COMMANDER_VERSION_STRING "${DISPLAY_COMMANDER_VERSION_MAJOR}.${DISPLAY_COMMANDER_VERSION_MINOR}.${DISPLAY_COMMANDER_VERSION_PATCH}.${GIT_COMMIT_COUNT}")
if(CMAKE_SIZEOF_VOID_P EQUAL 4)
  set(DISPLAY_COMMANDER_BITNESS "32-bit")
else()
  set(DISPLAY_COMMANDER_BITNESS "64-bit")
endif()
configure_file(
  ${CMAKE_CURRENT_LIST_DIR}/proxy_dll/version.rc.in
  ${CMAKE_CURRENT_BINARY_DIR}/version.rc
  @ONLY
)

add_library(zzz_display_commander MODULE
  ${DISPLAY_COMMANDER_SOURCES}
  ${CMAKE_CURRENT_BINARY_DIR}/version.rc
)
target_sources(zzz_display_commander PRIVATE ${CMAKE_CURRENT_LIST_DIR}/proxy_dll/exports.def)

target_compile_definitions(zzz_display_commander PRIVATE
  GIT_COMMIT_COUNT=${GIT_COMMIT_COUNT}
  DISPLAY_COMMANDER_VERSION_MAJOR=${DISPLAY_COMMANDER_VERSION_MAJOR}
  DISPLAY_COMMANDER_VERSION_MINOR=${DISPLAY_COMMANDER_VERSION_MINOR}
  DISPLAY_COMMANDER_VERSION_PATCH=${DISPLAY_COMMANDER_VERSION_PATCH}
)
if(EXPERIMENTAL_TAB)
  target_compile_definitions(zzz_display_commander PRIVATE EXPERIMENTAL_TAB=1)
endif()
if(EXPERIMENTAL_FEATURES)
  target_compile_definitions(zzz_display_commander PRIVATE EXPERIMENTAL_FEATURES=1)
endif()
# Includes: repo include/ first; third-party as SYSTEM (never put bare SYSTEM on its own line — MSVC can emit -I.../SYSTEM).
target_include_directories(zzz_display_commander PRIVATE
  "${_dc_repo_root}/include"
)
target_include_directories(zzz_display_commander SYSTEM PRIVATE
  $<BUILD_INTERFACE:${_dc_repo_root}/external/reshade/include>
  $<BUILD_INTERFACE:${_dc_repo_root}/external/reshade/deps/imgui>
  $<BUILD_INTERFACE:${_dc_repo_root}/external/Streamline/include>
  $<BUILD_INTERFACE:${_dc_repo_root}/external/nvapi>
  $<BUILD_INTERFACE:${_dc_repo_root}/external/minhook/include>
  $<BUILD_INTERFACE:${_dc_repo_root}/external/vulkan-headers/include>
)
if(MSVC)
  # Single /arch flag: MSVC does not allow multiple /arch options (x86 build was failing with /arch:SSE2 /arch:AVX /arch:AVX2).
  if(CMAKE_SIZEOF_VOID_P EQUAL 4)
    set(DC_MSVC_ARCH "/arch:SSE2")
  else()
    set(DC_MSVC_ARCH "/arch:AVX2")
  endif()
  # Suppress warnings: 4191 unsafe cast, 4244/4267 conversion loss, 4996 deprecated CRT, 4005 macro redefinition
  target_compile_options(zzz_display_commander PRIVATE
    /bigobj ${DC_MSVC_ARCH} /utf-8 /D_ENABLE_EXTENDED_ALIGNED_STORAGE
    /wd4191 /wd4244 /wd4267 /wd4996 /wd4005
  )
  set_target_properties(zzz_display_commander PROPERTIES
    PDB_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    PDB_NAME "zzz_display_commander"
  )
endif()

# System libs: xinput/dinput/winmm/dbghelp are dynamic-load only. setupapi/tdh/advapi32/user32/winhttp: version info,
# ETW (PresentMon-style), Win32. No opengl32.lib (proxy exports; standalone UI loads GL at runtime).
target_link_libraries(zzz_display_commander PRIVATE setupapi tdh advapi32 user32 winhttp)

# MinHook (static); NVAPI is loaded at runtime (nvapi_loader), headers only from external/nvapi.
if(EXISTS "${_dc_repo_root}/external/minhook")
  set(MINHOOK_SOURCES
    ${_dc_repo_root}/external/minhook/src/buffer.c
    ${_dc_repo_root}/external/minhook/src/hook.c
    ${_dc_repo_root}/external/minhook/src/trampoline.c
  )
  if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    list(APPEND MINHOOK_SOURCES ${_dc_repo_root}/external/minhook/src/hde/hde64.c)
  else()
    list(APPEND MINHOOK_SOURCES ${_dc_repo_root}/external/minhook/src/hde/hde32.c)
  endif()
  add_library(minhook STATIC ${MINHOOK_SOURCES})
  target_compile_definitions(minhook PRIVATE MH_STATIC=1)
  target_include_directories(minhook PRIVATE "${_dc_repo_root}/external/minhook/include")
  target_link_libraries(zzz_display_commander PRIVATE minhook)
  message(STATUS "MinHook library configured successfully")
else()
  message(FATAL_ERROR "MinHook not found at ${_dc_repo_root}/external/minhook. Run: git submodule update --init --recursive")
endif()

# Output naming
if(CMAKE_GENERATOR_PLATFORM STREQUAL "Win32" OR CMAKE_EXE_LINKER_FLAGS MATCHES "/machine:X86" OR CMAKE_SIZEOF_VOID_P EQUAL 4)
  set(TARGET_SUFFIX ".addon32")
else()
  set(TARGET_SUFFIX ".addon64")
endif()

set_target_properties(zzz_display_commander PROPERTIES SUFFIX "${TARGET_SUFFIX}")

