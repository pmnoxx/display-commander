# Version (single source of truth for addon; used by compile definitions and version.rc)
set(DISPLAY_COMMANDER_VERSION_MAJOR 0)
set(DISPLAY_COMMANDER_VERSION_MINOR 13)
set(DISPLAY_COMMANDER_VERSION_PATCH 43)

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
  $<BUILD_INTERFACE:${_dc_repo_root}/external/tomlplusplus/include> # 100kb
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
  set_property(TARGET zzz_display_commander PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
  set_target_properties(zzz_display_commander PROPERTIES
    PDB_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    PDB_NAME "zzz_display_commander"
  )
endif()

# System libs: xinput/dinput/winmm/dbghelp are dynamic-load only. setupapi/tdh/advapi32/wininet/bcrypt/user32: version info,
# ETW (PresentMon-style), HTTP, crypto, Win32. No opengl32.lib (proxy exports; standalone UI loads GL at runtime).
target_link_libraries(zzz_display_commander PRIVATE setupapi tdh advapi32 wininet bcrypt user32)

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
  if(MSVC)
    set_property(TARGET minhook PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
  endif()
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

# Optional: copy NVPI Reference.xml next to the addon for runtime NVPI reference data.
set(NVPI_REFERENCE_XML "${_dc_repo_root}/external/NvidiaProfileInspectorRevamped/nspector/Reference.xml")
if(EXISTS "${NVPI_REFERENCE_XML}")
  add_custom_command(TARGET zzz_display_commander POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${NVPI_REFERENCE_XML}" "$<TARGET_FILE_DIR:zzz_display_commander>/Reference.xml"
    COMMENT "Copy Reference.xml (NVPI) next to addon DLL"
  )
  message(STATUS "NVPI Reference.xml will be copied from NvidiaProfileInspectorRevamped submodule")
else()
  message(STATUS "NVPI Reference.xml not found (optional). Run: git submodule update --init external/NvidiaProfileInspectorRevamped")
endif()

# Embed Display Commander shader files and game_default_overrides.toml in the DLL (RCDATA).
set(DC_CONTROL_FX "${CMAKE_CURRENT_LIST_DIR}/res/effects/DisplayCommander_Control.fx")
set(DC_PERCEPTUALBOOST_FX "${CMAKE_CURRENT_LIST_DIR}/res/effects/DisplayCommander_PerceptualBoost.fx")
set(DC_COLOR_FXH "${CMAKE_CURRENT_LIST_DIR}/res/effects/color.fxh")
set(DC_RESHADE_FXH "${CMAKE_CURRENT_LIST_DIR}/res/effects/ReShade.fxh")
set(DC_GAME_DEFAULT_OVERRIDES_TOML "${CMAKE_CURRENT_LIST_DIR}/res/game_default_overrides.toml")
set(DC_LICENSE_SRC "${_dc_repo_root}/LICENSE")
set(DC_THIRD_PARTY_NOTICES_SRC "${_dc_repo_root}/THIRD_PARTY_NOTICES.txt")
set(DC_SHADER_RC "${CMAKE_CURRENT_BINARY_DIR}/shader_effect.rc")
if(EXISTS "${DC_CONTROL_FX}")
  if(NOT EXISTS "${DC_LICENSE_SRC}")
    message(WARNING "LICENSE not found at ${DC_LICENSE_SRC} - embedded license resource (305) will be missing")
  endif()
  if(NOT EXISTS "${DC_THIRD_PARTY_NOTICES_SRC}")
    message(WARNING "THIRD_PARTY_NOTICES.txt not found at ${DC_THIRD_PARTY_NOTICES_SRC} - embedded notices resource (306) will be missing")
  endif()
  if(EXISTS "${DC_PERCEPTUALBOOST_FX}")
    set(DC_SHADER_PERCEPTUAL_DEFINE "#define IDR_PERCEPTUALBOOST_FX 303")
    set(DC_SHADER_PERCEPTUAL_RCDATA "IDR_PERCEPTUALBOOST_FX RCDATA \"DisplayCommander_PerceptualBoost.fx\"")
  else()
    set(DC_SHADER_PERCEPTUAL_DEFINE "")
    set(DC_SHADER_PERCEPTUAL_RCDATA "")
  endif()
  configure_file(
    ${CMAKE_CURRENT_LIST_DIR}/res/shader_effect.rc.in
    ${DC_SHADER_RC}
    @ONLY
  )
  set(DC_SHADER_RC_COPIED_BYPRODUCTS
    "${CMAKE_CURRENT_BINARY_DIR}/DisplayCommander_Control.fx"
    "${CMAKE_CURRENT_BINARY_DIR}/color.fxh"
    "${CMAKE_CURRENT_BINARY_DIR}/ReShade.fxh"
    "${CMAKE_CURRENT_BINARY_DIR}/game_default_overrides.toml"
    "${CMAKE_CURRENT_BINARY_DIR}/DC_LICENSE.txt"
    "${CMAKE_CURRENT_BINARY_DIR}/THIRD_PARTY_NOTICES.txt"
  )
  if(EXISTS "${DC_PERCEPTUALBOOST_FX}")
    list(APPEND DC_SHADER_RC_COPIED_BYPRODUCTS "${CMAKE_CURRENT_BINARY_DIR}/DisplayCommander_PerceptualBoost.fx")
  endif()
  add_custom_target(copy_shader_files_for_rc ALL
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${DC_CONTROL_FX}" "${CMAKE_CURRENT_BINARY_DIR}/DisplayCommander_Control.fx"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${DC_COLOR_FXH}" "${CMAKE_CURRENT_BINARY_DIR}/color.fxh"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${DC_RESHADE_FXH}" "${CMAKE_CURRENT_BINARY_DIR}/ReShade.fxh"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${DC_GAME_DEFAULT_OVERRIDES_TOML}" "${CMAKE_CURRENT_BINARY_DIR}/game_default_overrides.toml"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${DC_LICENSE_SRC}" "${CMAKE_CURRENT_BINARY_DIR}/DC_LICENSE.txt"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${DC_THIRD_PARTY_NOTICES_SRC}" "${CMAKE_CURRENT_BINARY_DIR}/THIRD_PARTY_NOTICES.txt"
    COMMENT "Copy DisplayCommander shader files, game_default_overrides.toml, LICENSE, and THIRD_PARTY_NOTICES to build dir for resource embedding"
    BYPRODUCTS ${DC_SHADER_RC_COPIED_BYPRODUCTS}
  )
  if(EXISTS "${DC_PERCEPTUALBOOST_FX}")
    add_custom_command(TARGET copy_shader_files_for_rc POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different "${DC_PERCEPTUALBOOST_FX}" "${CMAKE_CURRENT_BINARY_DIR}/DisplayCommander_PerceptualBoost.fx"
    )
  endif()
  if(NOT EXISTS "${DC_GAME_DEFAULT_OVERRIDES_TOML}")
    message(WARNING "game_default_overrides.toml not found at ${DC_GAME_DEFAULT_OVERRIDES_TOML} - resource 304 will be missing")
  endif()
  target_sources(zzz_display_commander PRIVATE ${DC_SHADER_RC})
  add_dependencies(zzz_display_commander copy_shader_files_for_rc)
  if(NOT EXISTS "${DC_COLOR_FXH}")
    message(WARNING "color.fxh not found at ${DC_COLOR_FXH} - resource 301 will be missing")
  endif()
  if(NOT EXISTS "${DC_RESHADE_FXH}")
    message(WARNING "ReShade.fxh not found at ${DC_RESHADE_FXH} - resource 302 will be missing")
  endif()
  if(NOT EXISTS "${DC_PERCEPTUALBOOST_FX}")
    message(WARNING "DisplayCommander_PerceptualBoost.fx not found at ${DC_PERCEPTUALBOOST_FX} - resource 303 will be missing")
  endif()
endif()
