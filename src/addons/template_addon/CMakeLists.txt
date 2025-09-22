# Template Addon CMakeLists.txt

# Sources
file(GLOB_RECURSE TEMPLATE_ADDON_SOURCES
  CONFIGURE_DEPENDS
  ${CMAKE_CURRENT_LIST_DIR}/*.cpp
)

if(TEMPLATE_ADDON_SOURCES STREQUAL "")
  message(FATAL_ERROR "No sources found under src/addons/template_addon")
endif()

add_library(zzz_template_addon MODULE ${TEMPLATE_ADDON_SOURCES})

# MSVC-specific flags
if(MSVC)
  target_compile_options(zzz_template_addon PRIVATE /bigobj /arch:SSE2)
  # Suppress Microsoft extension warnings for function pointer casting (common with MinHook)
  target_compile_options(zzz_template_addon PRIVATE /wd4191)
endif()

# GCC/Clang-specific flags
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(zzz_template_addon PRIVATE -msse4.1 -msse4.2)
  # Suppress Microsoft extension warnings for function pointer casting (common with MinHook)
  target_compile_options(zzz_template_addon PRIVATE -Wno-microsoft-cast)
endif()

# Include directories
target_include_directories(zzz_template_addon
  PRIVATE
  ${CMAKE_CURRENT_LIST_DIR}/../../../src
  ${CMAKE_CURRENT_LIST_DIR}/../../../src/addons/template_addon
  ${CMAKE_CURRENT_LIST_DIR}/../../../include
  SYSTEM PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}/../../../external/reshade/include  # provides ReShade/reshade.hpp
    ${CMAKE_CURRENT_LIST_DIR}/../../../external/reshade/deps/imgui # provides imgui.h
    ${CMAKE_CURRENT_LIST_DIR}/../../../external/minhook/include    # MinHook headers
)

# Link system libs (minimal set for template)
target_link_libraries(zzz_template_addon PRIVATE)

# MinHook library (optional for template)
if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/../../../external/minhook")
  # MinHook source files
  set(MINHOOK_SOURCES
    ${CMAKE_CURRENT_LIST_DIR}/../../../external/minhook/src/buffer.c
    ${CMAKE_CURRENT_LIST_DIR}/../../../external/minhook/src/hook.c
    ${CMAKE_CURRENT_LIST_DIR}/../../../external/minhook/src/trampoline.c
    ${CMAKE_CURRENT_LIST_DIR}/../../../external/minhook/src/hde/hde32.c
    ${CMAKE_CURRENT_LIST_DIR}/../../../external/minhook/src/hde/hde64.c
  )

  # Create MinHook static library
  add_library(minhook_template STATIC ${MINHOOK_SOURCES})

  # MinHook compile definitions
  target_compile_definitions(minhook_template PRIVATE MH_STATIC=1)

  # MinHook include directories
  target_include_directories(minhook_template PRIVATE ${CMAKE_CURRENT_LIST_DIR}/../../../external/minhook/include)

  # When using clang-cl, undefine _MSC_VER for MinHook C sources to force memcpy path instead of __movsb
  if(CMAKE_C_COMPILER_ID STREQUAL "Clang")
    set_source_files_properties(${MINHOOK_SOURCES} PROPERTIES COMPILE_OPTIONS "-U_MSC_VER")
  endif()

  # Link MinHook to template addon (optional)
  target_link_libraries(zzz_template_addon PRIVATE minhook_template)

  message(STATUS "MinHook library configured for template addon")
else()
  message(WARNING "MinHook not found - template addon will be built without hooking capabilities")
endif()

# Output naming
if(CMAKE_GENERATOR_PLATFORM STREQUAL "Win32" OR CMAKE_EXE_LINKER_FLAGS MATCHES "/machine:X86" OR CMAKE_SIZEOF_VOID_P EQUAL 4)
  set(TARGET_SUFFIX ".addon32")
else()
  set(TARGET_SUFFIX ".addon64")
endif()

set_target_properties(zzz_template_addon PROPERTIES
  SUFFIX "${TARGET_SUFFIX}"
)
